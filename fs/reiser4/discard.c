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
#include "plugin/cluster.h"

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/lcm.h>

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
	sb_discard->offset = offset;
	sb_discard->unit = unit;

	printk("reiser4: %s: enable discard support "
	       "(erase unit %u bytes, alignment %u bytes)\n",
	       sb->s_id, sb_discard->unit, sb_discard->offset);
	return;
disable:
	warning("", "%s: disable discard support", sb->s_id);
	clear_bit(REISER4_DISCARD, &sbinfo->fs_flags);
	return;
}

static int discard_precise_extent(struct block_device *bdev, sector_t start,
				  sector_t len)
{
	assert("intelfx-21", bdev != NULL);

	return blkdev_issue_discard(bdev, start, len,
				    reiser4_ctx_gfp_mask_get(), 0);
}

/*
 * @start - start of precise extent (offset of the first byte);
 * return lenght of the head padding in bytes
 */
static int precise_extent_headp(__u64 start, int uoff, int ulen)
{
	__u64 headp;

	assert("edward-1636", uoff < ulen);

	headp = start + ulen - uoff;
	headp = do_div(headp, ulen);
	return headp;
}

/*
 * @end - end of precise extent (offset of the last byte + 1);
 * return lenght of the tail padding in bytes
 */
static int precise_extent_tailp(__u64 end, int uoff, int ulen)
{
	__u64 tailp;

	assert("edward-1637", uoff < ulen);

	tailp = end + ulen - uoff;
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

/*
 * Check if a given block range is free (clean) and allocate it.
 *
 * NOTE: this operation is not idempotent.
 */
static inline int try_allocate_blocks(const reiser4_block_nr start,
				      const reiser4_block_nr len)
{
	/*
	 * NOTE: we do not use BA_PERMANENT in out allocations
	 * even though these blocks are later deallocated with BA_DEFER
	 * (via updating the delete set with newly allocated blocks).
	 * The discard code is ran after the pre-commit hook so deallocated block
	 * accounting is already done.
	 */
	return reiser4_alloc_blocks_exact(&start,
					  &len,
					  BLOCK_NOT_COUNTED, BA_FORMATTED) == 0;
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
 * discard_precise_extents() - scan the list of sorted and merged extents and
 * check head and tail paddings of each extent in the working space map. Try to
 * "glue" the nearby extents. Discard the resulted (glued, padded, or cut)
 * extents.
 *
 * Head and tail paddings, if any, are allocated before discarding, and the list
 * is updated to contain all new allocations.
 *
 * Pre-conditions: @head points to the list of sorted and merged extents.
 *
 * Local variables:
 *
 * d_uni - discard unit size (in bytes);
 * d_off - discard alignment (in bytes);
 *
 * start - start of extent (in blocks);
 * len - length of extent (in blocks);
 * end - end of extent (last block + 1);
 *
 * headp - size of head padding of extent (in blocks);
 * tailp - size of tail padding of extent (in blocks);
 *
 * a_start - start of resulted (glued, aligned and padded) extent to discard;
 * a_len - length of resulted (glued, aligned and padded) extent to discard;
 *
 * estart - start of extent to be written back to the list (in blocks);
 * eend - end (last block + 1) of extent to be written back to the list;
 *
 * Terminology in the comments:
 *
 * head - a part of extent at the beginning;
 * tail - a part of extent at the end.
 */

static int discard_precise_extents(struct list_head *head)
{
	int ret;
	int d_uni;
	int d_off;
	struct list_head *pos;
	struct super_block *sb = reiser4_get_current_sb();
	int blkbits = sb->s_blocksize_bits;

	/* This is a "cache" which holds the last block range checked during
	 * processing of an extent. This information is used to avoid allocating
	 * the same blocks multiple times, if two successive extents become
	 * overlapped (in terms of disk blocks) after padding.
	 *
	 * The problem with allocating the same blocks multiple times:
	 * our function try_allocate_blocks() is not idempotent. More precisely,
	 * after a positive result has been returned for a given range [A;B), we
	 * must not call try_allocate_blocks() on any range which overlaps [A;B),
	 * or we will get a false negative result.
	 * (Also, we must not call try_allocate_blocks() on any range which
	 * overlaps extents in the discard set.)
	 *
	 * Let's show that we can avoid false-negatives with this cache.
	 *
	 * 1. All blocks between the stored tail padding and the beginning of
	 *    the current extent are safe to allocate.
	 *
	 * 2. Let's analyze all combinations of the previous tail padding's check
	 *    result and the current head padding's disposition relative to the
	 *    previous tail padding. Note that we are speaking in terms of
	 *    occupied disk blocks.
	 *
	 * 2.0. The head padding does not overlap the tail padding.
	 *      In this case head padding is safe to allocate.
	 *
	 * 2.1. The tail padding is dirty. The head padding partially overlaps it.
	 *      In this case both parts of the head padding are safe to allocate.
	 *
	 * 2.2. The tail padding is dirty. The head padding completely covers it
	 *      (maybe extending back beyond).
	 *      In this case the head padding is transitively dirty.
	 *
	 * 2.3. The tail padding is clean. The head padding overlaps or covers it
	 *      (not extending back beyond).
	 *      In this case:
	 *      - the overlapping part of the head padding can be skipped
	 *      - the rest is safe to allocate
	 *
	 * 2.4. The tail padding is clean. The head padding extends beyond it.
	 *      This is not possible. It would mean that our head padding
	 *      shares an erase unit with the previous tail padding.
	 *      Such extent combinations are handled by the gluing code.
	 */

	reiser4_block_nr last_padding_start = 0;
	reiser4_block_nr last_padding_end = 0;
	int last_padding_clean = 0;

	d_off = get_super_private(sb)->discard.offset;
	d_uni = get_super_private(sb)->discard.unit;

	for (pos = head->next; pos != head; pos = pos->next) {
		int headp;
		int tailp;

		int p_headp;
		int p_tailp;

		reiser4_block_nr start;
		reiser4_block_nr len;
		reiser4_block_nr end;

		reiser4_block_nr estart, eend;

		__u64 a_start;
		__s64 a_len;

		__u64 p_start;
		__u64 p_len;
		__u64 p_end;

		check_blocknr_list_at(pos, head);

		start = blocknr_list_entry_start(pos);
		len = blocknr_list_entry_len(pos);
		estart = start;

		p_start = start << blkbits;
		p_len = len << blkbits;

		/*
		 * Step I. Cut or pad the head of extent
		 *
		 * This extent wasn't glued
		 */

		p_headp = precise_extent_headp(p_start, d_off, d_uni);
		headp = size_in_blocks(p_headp, blkbits);

		/*
		 * Our head padding can't extend back beyond the saved tail
		 * padding, if the latter is clean.
		 * (cf. situation 2.4 from the above description)
		 */
		assert("intelfx-80", ergo(last_padding_clean,
					  last_padding_start <= start - headp));

		if (headp == 0) {
			/*
			 * empty head padding
			 */
			a_start = p_start;
			a_len = p_len;
		} else {
			int headp_is_clean;

			/*
			 * If our discard unit is incomplete, don't pad.
			 */
			if (p_start < p_headp)
				headp_is_clean = 0;

			/*
			 * Maybe last checked extent is dirty and completely
			 * embedded in ours? Then our one is dirty too.
			 * (cf. situation 2.2 from the above description)
			 */
			else if (!last_padding_clean &&
				 last_padding_start >= start - headp &&
				 last_padding_end <= start)
				headp_is_clean = 0;

			/*
			 * Maybe last checked extent is clean and completely
			 * covers ours? Then our one is clean too.
			 * (cf. situation 2.3 from the above description)
			 */
			else if (last_padding_clean &&
				 last_padding_end >= start)
				headp_is_clean = 1;

			/*
			 * Maybe last checked extent is clean and partially
			 * overlaps ours? Then we must check the remaining part.
			 * (cf. situation 2.3 from the above description)
			 */
			else if (last_padding_clean &&
				 last_padding_end > start - headp) {
				headp_is_clean = try_allocate_blocks(last_padding_end, start - last_padding_end);
				if (headp_is_clean)
					estart = last_padding_end;
			}

			/*
			 * Otherwise check the whole padding.
			 * (cf. situations 2.0, 2.1 from the above description)
			 */
			else {
				headp_is_clean = try_allocate_blocks(start - headp, headp);
				if (headp_is_clean)
					estart -= headp;
			}

			if (headp_is_clean) {
				/*
				 * head padding is clean,
				 * pad the head
				 */
				a_start = p_start - p_headp;
				a_len = p_len + p_headp;
			} else {
				/*
				 * head padding is dirty,
				 * or discard unit is incomplete (can not
				 * check blocks outside of the partition),
				 * cut the head
				 */
				a_start = p_start + (d_uni - p_headp);
				a_len = p_len - (d_uni - p_headp);
			}
		}

		/*
		 * Step II. Try to glue all nearby extents to the tail
		 *          Cut or pad the tail of the last extent.
		 */
		end = start + len;
		eend = end;
		p_end = end << blkbits;

		p_tailp = precise_extent_tailp(p_end, d_off, d_uni);
		tailp = size_in_blocks(p_tailp, blkbits);
		/*
		 * This "gluing" loop updates
		 * @end, @p_end, @tailp, @p_tailp, @a_len, @eend
		 */
		while (1) {
			struct list_head *next;

			next = get_next_at(pos, head);
			check_blocknr_list_at(next, head);

			if (next && (p_end + p_tailp >= blocknr_list_entry_start(next) << blkbits)) {
				/*
				 * try to glue the next extent
				 */
				reiser4_block_nr next_start;
				reiser4_block_nr next_len;

				__u64 p_next_start;
				__u64 p_next_len;

				next_start = blocknr_list_entry_start(next);
				next_len = blocknr_list_entry_len(next);

				p_next_start = next_start << blkbits;
				p_next_len = next_len << blkbits;

				/*
				 * check space between the extents;
				 * if it is free, then allocate it
				 */
				if (try_allocate_blocks(end, next_start - end)) {
					/*
					 * jump to the glued extent
					 */
					a_len += (p_next_start + p_next_len - p_end);
					end = next_start + next_len;
					p_end = end << blkbits;

					p_tailp = precise_extent_tailp(p_end, d_off, d_uni);
					tailp = size_in_blocks(p_tailp, blkbits);
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
					a_len -= (d_uni - p_tailp);

					/*
					 * save the tail padding
					 */
					last_padding_start = end;
					last_padding_end = next_start;
					last_padding_clean = 0;
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
				if (tailp == 0) {
					/*
					 * empty tail padding.
					 *
					 * save a fake tail padding to aid debugging
					 */
					last_padding_start = end;
					last_padding_end = end;
					last_padding_clean = 1;

				} else if (try_allocate_blocks(end, tailp)) {
					/*
					 * tail padding is clean,
					 * pad the tail
					 */
					a_len += p_tailp;
					eend += tailp;

					/*
					 * save the tail padding
					 */
					last_padding_start = end;
					last_padding_end = end + tailp;
					last_padding_clean = 1;
				} else {
					/*
					 * tail padding is dirty,
					 * cut the tail
					 */
					a_len -= (d_uni - p_tailp);

					/*
					 * save the tail padding
					 */
					last_padding_start = end;
					last_padding_end = end + tailp;
					last_padding_clean = 0;
				}
				break;
			}
		}

		/*
		 * Step III. Discard the result
		 */
		if (a_len > 0) {
			assert("intelfx-74", estart < eend);
			assert("intelfx-75", estart <= start);
			assert("intelfx-77", start + len <= eend);

			/* here @eend becomes length */
			eend -= estart;
			assert("intelfx-79",
			       reiser4_check_blocks(&estart, &eend, 1));
			blocknr_list_update_extent(pos, &estart, &eend);

			ret = discard_precise_extent(sb->s_bdev,
						     a_start >> 9, a_len >> 9);
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
	ret = discard_precise_extents(&discard_set);

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
