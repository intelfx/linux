/*
 * extender.c - Lanyard Filesystem Extender Operations
 *
 * Copyright (C) 2012  Dan Luedtke <mail@danrl.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "lanyfs_km.h"

/**
 * intpow() - Simple exponentiation function.
 * @b:				base
 * @n:				exponent
 *
 * This function does not cover special cases where @n equals zero and @b does
 * not equal zero.
 */
static inline int intpow(int b, int n)
{
	if (!n)
		return 1;
	while (n--)
		b *= b;
	return b;
}

/**
 * lanyfs_ext_get_slot() - Returns the address stored in an extender block slot.
 * @ext:			extender block to read from
 * @addrlen:			address length
 * @slot:			number of slot to read
 */
static inline lanyfs_blk_t lanyfs_ext_get_slot(struct lanyfs_ext *ext,
					       unsigned int addrlen,
					       unsigned int slot)
{
	lanyfs_blk_t addr;
	addr = 0;
	memcpy(&addr, &ext->stream + (slot * addrlen), addrlen);
	return le64_to_cpu(addr);
}

/**
 * lanyfs_ext_set_slot() - Stores an address in an extender block slot.
 * @ext:			extender block to write to
 * @addrlen:			address length
 * @slot:			number of slot to write
 * @addr:			address to store
 */
static inline void lanyfs_ext_set_slot(struct lanyfs_ext *ext,
				       unsigned int addrlen,
				       unsigned int slot, lanyfs_blk_t addr)
{
	addr = cpu_to_le64(addr);
	memcpy(&ext->stream + (slot * addrlen), &addr, addrlen);
}

/**
 * lanyfs_ext_kill_slot() - Resets the slot of an extender block to zero.
 * @ext:			extender block to write to
 * @addrlen:			address length
 * @slot:			number of slot to kill
 */
static inline void lanyfs_ext_kill_slot(struct lanyfs_ext *ext,
					unsigned int addrlen,
					unsigned int slot)
{
	memset(&ext->stream + (slot * addrlen), 0x00, addrlen);
}

/**
 * lanyfs_ext_iblock() - Gets the address of data block in a file.
 * @sb:				superblock
 * @addr:			address of extender block to read
 * @iblock:			file-internal block number
 * @res:			pointer to result storage
 *
 * Mapping a file-internal block (called iblock) to the correct on-disk block
 * requires reading its address from an extender block. Larger files use
 * multiple levels of extender blocks, so this function sometimes calls itselfs
 * when going down extender blocks level by level. On-disk addresses are always
 * stored in extender blocks of level 0. Once the on-disk address is found it is
 * saved to @res.
 */
int lanyfs_ext_iblock(struct super_block *sb, lanyfs_blk_t addr,
		      lanyfs_blk_t iblock, lanyfs_blk_t *res)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_ext *ext;
	unsigned int slot;
	lanyfs_debug_function(__FILE__, __func__);

	if (unlikely(!addr))
		return -EINVAL;

	fsi = LANYFS_SB(sb);
	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return -EIO;
	}
	ext = (struct lanyfs_ext *) bh->b_data;
	if (ext->level) {
		slot = (iblock / intpow(fsi->extmax, ext->level)) % fsi->extmax;
		addr = lanyfs_ext_get_slot(ext, fsi->addrlen, slot);
		brelse(bh);
		if (addr)
			return lanyfs_ext_iblock(sb, addr, iblock, res);
		return -EINVAL;
	}
	*res = lanyfs_ext_get_slot(ext, fsi->addrlen, (iblock % fsi->extmax));
	brelse(bh);
	return 0;
}

/**
 * lanyfs_ext_truncate() - Sets the on-disk size of a file.
 * @sb:				superblock
 * @addr:			address of extender block to read
 * @iblock:			new size in file-internal blocks
 *
 * Once again recursion is used to walk through all levels of extender blocks.
 * Blocks that are not needed anymore are returned to the free blocks pool by
 * this function. This is the lowest level of file size changes and usually
 * happens after VFS has already truncated the file's in-memory representation.
 */
int lanyfs_ext_truncate(struct super_block *sb, lanyfs_blk_t addr,
			lanyfs_blk_t iblock)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_ext *ext;
	unsigned int slot;
	lanyfs_debug_function(__FILE__, __func__);

	if (unlikely(!addr))
		return -EINVAL;

	fsi = LANYFS_SB(sb);
	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return -EIO;
	}
	ext = (struct lanyfs_ext *) bh->b_data;
	slot = (iblock / intpow(fsi->extmax, ext->level)) % fsi->extmax;
	lock_buffer(bh);
	while (slot < fsi->extmax) {
		addr = lanyfs_ext_get_slot(ext, fsi->addrlen, slot);
		if (addr) {
			if (ext->level) {
				/* barrier below this slot */
				lanyfs_ext_truncate(sb, addr, iblock);
				iblock = 0;
			} else {
				lanyfs_ext_kill_slot(ext, fsi->addrlen, slot);
				lanyfs_release(sb, addr);
			}
		}
		slot++;
	}
	le16_add_cpu(&ext->wrcnt, 1);
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (fsi->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}

/**
 * lanyfs_ext_create() - Creates a new extender block.
 * @sb:				superblock
 * @level:			level of new extender block
 */
lanyfs_blk_t lanyfs_ext_create(struct super_block *sb, unsigned short level)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_ext *ext;
	lanyfs_blk_t addr;
	lanyfs_debug_function(__FILE__, __func__);

	fsi = LANYFS_SB(sb);

	addr = lanyfs_enslave(sb);
	if (!addr)
		return 0;
	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return 0;
	}
	ext = (struct lanyfs_ext *) bh->b_data;
	lock_buffer(bh);
	memset(ext, 0x00, 1 << fsi->blocksize);
	ext->type = LANYFS_TYPE_EXT;
	ext->level = level;
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (fsi->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return addr;
}

/**
 * __lanyfs_ext_grow() - Increases the on-disk size of a file.
 * @sb:				superblock
 * @addr:			address of extender block to start at
 *
 * This function is internal and is best be called by its wrapper function.
 */
static int __lanyfs_ext_grow(struct super_block *sb, lanyfs_blk_t addr)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_ext *ext;
	unsigned int slot;
	lanyfs_blk_t new;
	lanyfs_blk_t tmp;
	int ret;
	lanyfs_debug_function(__FILE__, __func__);


	if (unlikely(!addr))
		return -LANYFS_EPROTECTED;

	fsi = LANYFS_SB(sb);
	ret = -LANYFS_ENOTAKEN;
	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return -EIO;
	}
	ext = (struct lanyfs_ext *) bh->b_data;
	if (ext->level) {
		for (slot = fsi->extmax; slot; slot--) {
			tmp = lanyfs_ext_get_slot(ext, fsi->addrlen, slot - 1);
			if (!tmp && slot > 1)
				continue;
			ret = __lanyfs_ext_grow(sb, tmp);
			if (ret != -LANYFS_ENOEMPTY)
				goto exit_ret;
			if (slot >= fsi->extmax) {
				ret = -LANYFS_ENOEMPTY;
				goto exit_ret;
			}
			new = lanyfs_ext_create(sb, ext->level - 1);
			if (!new) {
				ret = -ENOSPC;
				goto exit_ret;
			}
			lock_buffer(bh);
			lanyfs_ext_set_slot(ext, fsi->addrlen, slot, new);
			unlock_buffer(bh);
			mark_buffer_dirty(bh);
			slot++;
		}
	} else {
		for (slot = 0; slot < fsi->extmax; slot++) {
			tmp = lanyfs_ext_get_slot(ext, fsi->addrlen, slot);
			if (tmp)
				continue;
			new = lanyfs_enslave(sb);
			if (!new) {
				ret = -ENOSPC;
				goto exit_ret;
			}
			lock_buffer(bh);
			lanyfs_ext_set_slot(ext, fsi->addrlen, slot, new);
			unlock_buffer(bh);
			mark_buffer_dirty(bh);
			ret = 0;
			goto exit_ret;
		}
		ret = -LANYFS_ENOEMPTY;
	}
exit_ret:
	if (fsi->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return ret;
}

/**
 * lanyfs_ext_grow() - Increases the on-disk size of a file by one block.
 * @sb:				superblock
 * @addr:			address of top-level extender block
 *
 * If all slots of all extender blocks of a file are occupied, a new level of
 * extender blocks has to be introduced. The new level extender block becomes
 * the new entry point thus changing the corresponding inodes private data. If
 * a new entry point is created, its address is stored in @addr. Upper layer
 * functions must update inode private data accordingly.
 */
int lanyfs_ext_grow(struct super_block *sb, lanyfs_blk_t *addr)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_ext *ext;
	lanyfs_blk_t new;
	int ret;
	lanyfs_debug_function(__FILE__, __func__);

	if (unlikely(!*addr))
		return -LANYFS_EPROTECTED;

	fsi = LANYFS_SB(sb);
	ret = __lanyfs_ext_grow(sb, *addr);

	if (ret == -LANYFS_ENOEMPTY) {
		/* all extender blocks are occupied, go one level up */
		bh = sb_bread(sb, *addr);
		if (unlikely(!bh)) {
			lanyfs_msg(sb, KERN_ERR, "block #%llu read error",
				   (u64) *addr);
			return -EIO;
		}
		ext = (struct lanyfs_ext *) bh->b_data;
		brelse(bh);

		new = lanyfs_ext_create(sb, ext->level + 1);
		if (!new)
			return -ENOSPC;
		bh = sb_bread(sb, new);
		if (unlikely(!bh)) {
			lanyfs_msg(sb, KERN_ERR, "block #%llu read error",
				   (u64) new);
			return -EIO;
		}
		ext = (struct lanyfs_ext *) bh->b_data;
		lock_buffer(bh);
		lanyfs_ext_set_slot(ext, fsi->addrlen, 0, *addr);
		unlock_buffer(bh);
		mark_buffer_dirty(bh);
		if (fsi->opts.flush)
			sync_dirty_buffer(bh);
		brelse(bh);
		*addr = new;
	}
	return ret;
}
