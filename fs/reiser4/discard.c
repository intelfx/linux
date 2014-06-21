/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* TRIM/discard interoperation subsystem for reiser4. */

/*
 * This subsystem is responsible for populating an atom's ->discard_set and
 * (later) converting it into a series of discard calls to the kernel.
 *
 * The discard is an in-kernel interface for notifying the storage
 * hardware about blocks that are being logically freed by the filesystem.
 * This is done via calling the blkdev_issue_discard() function. There are
 * restrictions on block ranges: they should constitute at least one erase unit
 * in length and be correspondingly aligned. Otherwise a discard request will
 * be ignored.
 *
 * The erase unit size is kept in struct queue_limits as discard_granularity.
 * The offset from the partition start to the first erase unit is kept in
 * struct queue_limits as discard_alignment.
 *
 * At atom level, we record numbers of all blocks that happen to be deallocated
 * during the transaction. Then we read the generated set, filter out any blocks
 * that have since been allocated again and issue discards for everything still
 * valid. This is what discard.[ch] is here for.
 *
 * However, simply iterating through the recorded extents is not enough:
 * - if a single extent is smaller than the erase unit, then this particular
 *   extent won't be discarded even if it is surrounded by enough free blocks
 *   to constitute a whole erase unit;
 * - we won't be able to merge small adjacent extents forming an extent long
 *   enough to be discarded.
 *
 * MECHANISM:
 *
 * During the transaction deallocated extents are recorded in atom's delete
 * sets. There are two delete sets, because data in one of them (delete_set) is
 * also used by other parts of reiser4. The second delete set (aux_delete_set)
 * complements the first one and is maintained only when discard is enabled.
 *
 * Together these sets constitute "the discard set" -- blocks that have to be
 * considered for discarding. On atom commit we will generate a minimal
 * superset of the discard set, comprised of whole erase units.
 *
 * So, at commit time the following actions take place:
 * - delete sets are merged to form the discard set;
 * - elements of the discard set are sorted;
 * - the discard set is iterated, joining any adjacent extents;
 * - each of resulting extents is "covered" by erase units:
 *   - its start is rounded down to the closest erase unit boundary;
 *   - starting from this block, extents of erase unit length are created
 *     until the original extent is fully covered;
 * - the calculated erase units are checked to be fully deallocated;
 * - remaining (valid) erase units are then passed to blkdev_issue_discard().
 */

#include "discard.h"
#include "context.h"
#include "debug.h"
#include "txnmgr.h"
#include "super.h"

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/blkdev.h>

static int __discard_extent(struct block_device *bdev, sector_t start,
                            sector_t len)
{
	assert("intelfx-21", bdev != NULL);

	return blkdev_issue_discard(bdev, start, len, reiser4_ctx_gfp_mask_get(),
	                            0);
}

static int discard_extent(txn_atom *atom UNUSED_ARG,
                          const reiser4_block_nr* start,
                          const reiser4_block_nr* len,
                          void *data UNUSED_ARG)
{
	struct super_block *sb = reiser4_get_current_sb();
	struct block_device *bdev = sb->s_bdev;
	struct queue_limits *limits = &bdev_get_queue(bdev)->limits;

	sector_t extent_start_sec, extent_end_sec,
	         unit_sec, request_start_sec = 0, request_len_sec = 0;
	reiser4_block_nr unit_start_blk, unit_len_blk;
	int ret, erase_unit_counter = 0;

	const int sec_per_blk = sb->s_blocksize >> 9;

	/* from blkdev_issue_discard():
	 * Zero-sector (unknown) and one-sector granularities are the same.  */
	const int granularity = max(limits->discard_granularity >> 9, 1U);
	const int alignment = (bdev_discard_alignment(bdev) >> 9) % granularity;

	/* we assume block = N * sector */
	assert("intelfx-7", sec_per_blk > 0);

	/* convert extent to sectors */
	extent_start_sec = *start * sec_per_blk;
	extent_end_sec = (*start + *len) * sec_per_blk;

	/* round down extent start sector to an erase unit boundary */
	unit_sec = extent_start_sec;
	if (granularity > 1) {
		sector_t tmp = extent_start_sec - alignment;
		unit_sec -= sector_div(tmp, granularity);
	}

	/* iterate over erase units in the extent */
	do {
		/* considering erase unit:
		 * [unit_sec; unit_sec + granularity) */

		/* calculate block range for erase unit:
		 * [unit_start_blk; unit_start_blk+unit_len_blk) */
		unit_start_blk = unit_sec;
		do_div(unit_start_blk, sec_per_blk);

		if (granularity > 1) {
			unit_len_blk = unit_sec + granularity - 1;
			do_div(unit_len_blk, sec_per_blk);
			++unit_len_blk;

			assert("intelfx-22", unit_len_blk > unit_start_blk);

			unit_len_blk -= unit_start_blk;
		} else {
			unit_len_blk = 1;
		}

		if (reiser4_check_blocks(&unit_start_blk, &unit_len_blk, 0)) {
			/* OK. Add this unit to the accumulator.
			 * We accumulate discard units to call blkdev_issue_discard()
			 * not too frequently. */

			if (request_len_sec > 0) {
				request_len_sec += granularity;
			} else {
				request_start_sec = unit_sec;
				request_len_sec = granularity;
			}
		} else {
			/* This unit can't be discarded. Discard what's been accumulated
			 * so far. */
			if (request_len_sec > 0) {
				ret = __discard_extent(bdev, request_start_sec, request_len_sec);
				if (ret != 0) {
					return ret;
				}
				request_len_sec = 0;
			}
		}

		unit_sec += granularity;
		++erase_unit_counter;
	} while (unit_sec < extent_end_sec);

	/* Discard the last accumulated request. */
	if (request_len_sec > 0) {
		ret = __discard_extent(bdev, request_start_sec, request_len_sec);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int discard_atom(txn_atom *atom)
{
	int ret;
	struct list_head discard_set;

	if (!reiser4_is_set(reiser4_get_current_sb(), REISER4_DISCARD)) {
		spin_unlock_atom(atom);
		return 0;
	}

	assert("intelfx-28", atom != NULL);

	if (list_empty(&atom->discard.delete_set) &&
	    list_empty(&atom->discard.aux_delete_set)) {
		spin_unlock_atom(atom);
		return 0;
	}

	/* Take the delete sets from the atom in order to release atom spinlock. */
	blocknr_list_init(&discard_set);
	blocknr_list_merge(&atom->discard.delete_set, &discard_set);
	blocknr_list_merge(&atom->discard.aux_delete_set, &discard_set);
	spin_unlock_atom(atom);

	/* Sort the discard list, joining adjacent and overlapping extents. */
	blocknr_list_sort_and_join(&discard_set);

	/* Perform actual dirty work. */
	ret = blocknr_list_iterator(NULL, &discard_set, &discard_extent, NULL, 1);
	if (ret != 0) {
		return ret;
	}

	/* Let's do this again for any new extents in the atom's discard set. */
	return -E_REPEAT;
}

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
