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
 * MECHANISM:
 *
 * During the transaction deallocated extents are recorded in atom's delete
 * set. In reiser4, there are two methods to deallocate a block:
 * 1. deferred deallocation, enabled by BA_DEFER flag to reiser4_dealloc_block().
 *    In this mode, blocks are stored to delete set instead of being marked free
 *    immediately. After committing the transaction, the delete set is "applied"
 *    by the block allocator and all these blocks are marked free in memory
 *    (see reiser4_post_write_back_hook()).
 *    Space management plugins also read the delete set to update on-disk
 *    allocation records (see reiser4_pre_commit_hook()).
 * 2. immediate deallocation (the opposite).
 *    In this mode, blocks are marked free immediately. This is used by the
 *    journal subsystem to manage space used by the journal records, so these
 *    allocations are not visible to the space management plugins and never hit
 *    the disk.
 *
 * When discard is enabled, all immediate deallocations become deferred. This
 * is OK because journal's allocations happen after reiser4_pre_commit_hook()
 * where the on-disk space allocation records are updated. So, in this mode
 * the atom's delete set becomes "the discard set" -- list of blocks that have
 * to be considered for discarding.
 *
 * Discarding is performed before completing deferred deallocations, hence all
 * extents in the discard set are still marked as allocated and cannot contain
 * any data. Thus we can avoid any checks for blocks directly present in the
 * discard set.
 *
 * However, we pad each extent from both sides to erase unit boundaries,
 * and these paddings still have to be checked if they fall outside of initial
 * extent.
 *
 * So, at commit time the following actions take place:
 * - delete sets are merged to form the discard set;
 * - elements of the discard set are sorted;
 * - the discard set is iterated, joining any adjacent extents;
 * - for each extent, a single call to blkdev_issue_discard() is done.
 */

#include "forward.h"
#include "discard.h"
#include "context.h"
#include "debug.h"
#include "txnmgr.h"
#include "super.h"

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/lcm.h>

/*
 * For 1-dimension integer lattice (a,b) (a - offset, b - step)
 * find its minimal sub-lattice which can be represented in the
 * more coarse grained (scaled with factor r >= 1) coordinates.
 * If representation is impossible, return 1. Otherwise return 0.
 *
 * @a - offset of the lattice in the initial coordinates;
 * @b - step of the lattice in the initial coordinates;
 * @x - offset of the sub-lattice in the scaled coordinates;
 * @y - step of the sub-lattice in the scaled coordinates;
 * @r - scale factor.
 */
static int convert_lattice_params(int *x, int *y, int a, int b, int r)
{
	assert("edward-1635", b != 0);
	assert("edward-1636", r >= 1);

	if (a % r)
		return 1;
	*x = a / r;
	*y = lcm(b, r) / r;

	/* normalize offset */
	*x = *x % *y;
	return 0;
}

#define MAX_DISCARD_UNIT_BYTES (1 << 20)

/*
 * Verify customer's or kernel's discard params
 * at mount time. Re-calculate their values (to be
 * in blocks) and store them in the superblock.
 *
 * Pre-conditions: superblock contains customer's
 * discard params in bytes (if it was specified at
 * mount time).
 */
void check_discard_params(struct super_block *sb)
{
	int ret;
	reiser4_super_info_data *sbinfo;
	discard_params *sb_discard;
	struct queue_limits *limits;
	int unit;
	int offset;

	if (!reiser4_is_set(sb, REISER4_DISCARD))
		return;

	sbinfo = get_super_private(sb);
	limits = &bdev_get_queue(sb->s_bdev)->limits;
	sb_discard = &sbinfo->discard;

	if (sb_discard->unit) {
		/*
		 * discard params were specified by customer
		 */
		unit = sb_discard->unit;
		offset = sb_discard->offset;
	}
	else {
		/*
		 * grab discard params from the kernel
		 */
		unit = limits->discard_granularity;
		offset = bdev_discard_alignment(sb->s_bdev);
	}
	if (unit == 0)
		goto disable;
	if (unit > MAX_DISCARD_UNIT_BYTES) {
		warning("", "%s: unsupported erase unit (%d)", sb->s_id, unit);
		goto disable;
	}
	ret = convert_lattice_params(&sb_discard->offset,
				     &sb_discard->unit,
				     offset,
				     unit,
				     sb->s_blocksize);
	if (ret) {
		warning("", "%s: unsupported alignment (%d)", sb->s_id, offset);
		goto disable;
	}
	if (sb_discard->unit > MAX_DISCARD_UNIT_BYTES / sb->s_blocksize) {
		warning("", "%s: unsupported erase unit (%d)", sb->s_id, unit);
		goto disable;
	}
	printk("reiser4: %s: enable discard support "
	       "(erase unit %u blocks, alignment %u blocks)\n",
	       sb->s_id, sb_discard->unit, sb_discard->offset);
	return;
disable:
	warning("", "%s: disable discard support", sb->s_id);
	clear_bit(REISER4_DISCARD, &sbinfo->fs_flags);
	return;
}

static int __discard_extent(struct block_device *bdev, sector_t start,
                            sector_t len)
{
	assert("intelfx-21", bdev != NULL);

	return blkdev_issue_discard(bdev, start, len,
				    reiser4_ctx_gfp_mask_get(), 0);
}

/*
 * Return size of head padding of an extent on a lattice
 * with step @ulen and offset @uoff.
 * @start - the start offset of the extent.
 */
static int extent_get_headp(reiser4_block_nr start, int uoff, int ulen)
{
	__u64 headp;
	headp = ulen + start - uoff;
	headp = do_div(headp, ulen);
	return headp;
}

/*
 * Return size of tail padding of an extent on a lattice
 * with step @ulen and offset @uoff.
 * @end - the end offset of the extent.
 */
static int extent_get_tailp(reiser4_block_nr end, int uoff, int ulen)
{
	__u64 tailp;
	tailp = ulen + end - uoff;
	tailp = do_div(tailp, ulen);
	if (tailp)
		tailp = ulen - tailp;
	return tailp;
}

static inline struct list_head *get_next_at(struct list_head *pos,
					    struct list_head *head)
{
	assert("edward-1631", pos != NULL);
	assert("edward-1632", head != NULL);
	assert("edward-1633", pos != head);

	return pos->next == head ? NULL : pos->next;
}

static inline int check_free_blocks(const reiser4_block_nr start,
				    const reiser4_block_nr len)
{
	/*
	 * NOTE: we do not use BA_PERMANENT in out allocations
	 * even though these blocks are later deallocated with BA_DEFER
	 * (via updating the delete set with newly allocated blocks).
	 * The discard code is ran after the pre-commit hook so deallocated block
	 * accounting is already done.
	 */
	return reiser4_alloc_blocks_exact (&start, &len, BLOCK_NOT_COUNTED,
					   BA_FORMATTED) == 0;
}

/* Make sure that extents are sorted and merged */
#if REISER4_DEBUG
static inline void check_blocknr_list_at(struct list_head *pos,
					 struct list_head *head)
{
	struct list_head *next;

	if (pos == NULL)
		return;
	next = get_next_at(pos, head);
	if (next == NULL)
		return;
	if (blocknr_list_entry_start(next) <=
	    blocknr_list_entry_start(pos) + blocknr_list_entry_len(pos))
		warning("edward-1634",
			"discard bad pair of extents: (%llu,%llu), (%llu,%llu)",
			(unsigned long long)blocknr_list_entry_start(pos),
			(unsigned long long)blocknr_list_entry_len(pos),
			(unsigned long long)blocknr_list_entry_start(next),
			(unsigned long long)blocknr_list_entry_len(next));
}
#else
#define check_blocknr_list_at(pos, head) noop
#endif

/*
 * discard_sorted_merged_extents() - scan the list of sorted and
 * merged extents and check head and tail paddings of each
 * extent in the working space map. Try to "glue" the nearby
 * extents. Discard the (glued) extents with padded (or cut)
 * head and tail.
 * The paddings, if any, are allocated before discarding, and the list
 * is updated to contain all new allocations.
 *
 * Pre-conditions: @head points to the list of sorted and
 * merged extents.
 *
 * Local variables:
 *
 * d_uni - discard unit size (in blocks);
 * d_off - discard alignment (in blocks);
 *
 * start - offset of the first block of the extent;
 * len - length of the extent;
 * end - offset of the first block beyond extent;
 *
 * headp - size of head padding of the extent;
 * tailp - size of tail padding of the extent;
 *
 * astart - actual start to discard (offset of the head padding);
 * alen - actual length to discard (length of glued aligned and padded extents).
 *
 * estart - start of extent to be written back to the list
 * eend - end (last block + 1) of extent to be written back to the list
 *
 * Terminology in the comments:
 *
 * head - a part of extent at the beginning;
 * tail - a part of extent at the end.
 */

static int discard_sorted_merged_extents(struct list_head *head)
{
	int ret;
	struct super_block *sb = reiser4_get_current_sb();
	int d_uni;
	int d_off;
	struct list_head *pos;
	int headp_is_known_dirty = 0;

	d_off = get_super_private(sb)->discard.offset;
	d_uni = get_super_private(sb)->discard.unit;

	for (pos = head->next; pos != head; pos = pos->next) {
		int headp;
		int tailp;
		reiser4_block_nr start;
		reiser4_block_nr len;
		reiser4_block_nr end;
		reiser4_block_nr astart; __s64 alen;
		reiser4_block_nr estart, eend;

		check_blocknr_list_at(pos, head);

		start = blocknr_list_entry_start(pos);
		len = blocknr_list_entry_len(pos);
		estart = start;

		/*
		 * Step I. Cut or pad the head of extent
		 *
		 * This extent wasn't glued
		 */
		headp = extent_get_headp(start, d_off, d_uni);

		if (headp == 0) {
			/*
			 * empty head padding
			 */
			assert("edward-1635", headp_is_known_dirty == 0);
			astart = start;
			alen = len;
		} else if (!headp_is_known_dirty &&
			   check_free_blocks(start - headp, headp)) {
			/*
			 * head padding is clean,
			 * pad the head
			 */
			astart = start - headp;
			alen = len + headp;
			estart -= headp;
		} else {
			/*
			 * head padding is dirty,
			 * cut the head
			 */
			headp_is_known_dirty = 0;
			astart = start + (d_uni - headp);
			alen = len - (d_uni - headp);
		}

		/*
		 * Step II. Try to glue all nearby extents to the tail
		 *          Cut or pad the tail of the last extent.
		 */
		end = start + len;
		eend = end;
		tailp = extent_get_tailp(end, d_off, d_uni);

		/*
		 * This "gluing" loop updates @end, @tailp, @alen, @eend
		 */
		while (1) {
			struct list_head *next;

			next = get_next_at(pos, head);
			check_blocknr_list_at(next, head);

			if (next && (end + tailp >= blocknr_list_entry_start(next))) {
				/*
				 * try to glue the next extent
				 */
				reiser4_block_nr next_start;
				reiser4_block_nr next_len;

				next_start = blocknr_list_entry_start(next);
				next_len = blocknr_list_entry_len(next);

				if (check_free_blocks(end, next_start - end)) {
					/*
					 * jump to the glued extent
					 */
					alen += (next_start + next_len - end);
					end = next_start + next_len;
					tailp = extent_get_tailp(end, d_off, d_uni);
					eend = end;
					/*
					 * remove the glued extent from the list
					 *
					 * (don't update pos, current next->next
					 * will become pos->next)
					 */
					blocknr_list_del(next);
					/*
					 * try to glue more extents
					 */
					continue;
				} else {
					/*
					 * gluing failed, cut the tail
					 */
					if (end + tailp > next_start)
						headp_is_known_dirty = 1;

					alen -= (d_uni - tailp);
					break;
				}

			} else {
				/*
				 * nothing to glue:
				 * this is the last extent, or the next
				 * extent is too far. So just check the
				 * rest of tail padding and finish with
				 * the extent
				 */
				if (tailp == 0)
					break;
				else if (check_free_blocks(end, tailp)) {
					/*
					 * tail padding is clean,
					 * pad the tail
					 */
					alen += tailp;
					eend += tailp;
				} else
					/*
					 * tail padding is dirty,
					 * cut the tail
					 */
					alen -= (d_uni - tailp);
				break;
			}
		}

		/*
		 * Step III. Discard the result
		 */
		if (alen > 0) {
			assert("intelfx-74", estart < eend);
			assert("intelfx-75", estart <= start);
			assert("intelfx-76", estart <= astart);
			assert("intelfx-77", start + len <= eend);
			assert("intelfx-78", astart + alen <= eend);

			/* here @eend becomes length */
			eend -= estart;
			assert("intelfx-79",
			       reiser4_check_blocks(&estart, &eend, 1));
			blocknr_list_update_extent(pos, &estart, &eend);

			ret = __discard_extent(sb->s_bdev,
					       astart * (sb->s_blocksize >> 9),
					       alen * (sb->s_blocksize >> 9));
			if (ret)
				return ret;
		}
	}

	return 0;
}

int discard_atom(txn_atom *atom, struct list_head *processed_set)
{
	int ret;
	struct list_head discard_set;

	if (!reiser4_is_set(reiser4_get_current_sb(), REISER4_DISCARD)) {
		spin_unlock_atom(atom);
		return 0;
	}

	assert("intelfx-28", atom != NULL);
	assert("intelfx-59", processed_set != NULL);

	if (list_empty(&atom->discard.delete_set)) {
		/* Nothing left to discard. */
		spin_unlock_atom(atom);
		return 0;
	}

	/* Take the delete sets from the atom in order to release atom spinlock. */
	blocknr_list_init(&discard_set);
	blocknr_list_merge(&atom->discard.delete_set, &discard_set);
	spin_unlock_atom(atom);

	/* Sort the discard list, joining adjacent and overlapping extents. */
	blocknr_list_sort_and_join(&discard_set);

	/* Perform actual dirty work. The discard set may change at this point. */
	ret = discard_sorted_merged_extents(&discard_set);

	/* Add processed extents to the temporary list. */
	blocknr_list_merge(&discard_set, processed_set);

	if (ret != 0) {
		return ret;
	}

	/* Let's do this again for any new extents in the atom's discard set. */
	return -E_REPEAT;
}

void discard_atom_post(txn_atom *atom, struct list_head *processed_set)
{
	assert("intelfx-60", atom != NULL);
	assert("intelfx-61", processed_set != NULL);

	if (!reiser4_is_set(reiser4_get_current_sb(), REISER4_DISCARD)) {
		spin_unlock_atom(atom);
		return;
	}

	blocknr_list_merge(processed_set, &atom->discard.delete_set);
	spin_unlock_atom(atom);
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
