/*
 * chain.c - Lanyard Filesystem Chain Operations
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
 * lanyfs_chain_set_next() - Sets a chain block's successor.
 * @sb:				superblock
 * @addr:			address of chain block to manipulate
 * @next:			address of next chain block in chain
 */
int lanyfs_chain_set_next(struct super_block *sb, lanyfs_blk_t addr,
			   lanyfs_blk_t next)
{
	struct buffer_head *bh;
	struct lanyfs_chain *chain;
	lanyfs_debug_function(__FILE__, __func__);

	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return -EIO;
	}
	chain = (struct lanyfs_chain *) bh->b_data;
	lock_buffer(bh);
	chain->next = next;
	le16_add_cpu(&chain->wrcnt, 1);
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (LANYFS_SB(sb)->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}

/**
 * lanyfs_chain_create() - Creates a new chain block.
 * @sb:				superblock
 * @addr:			address of empty block
 */
int lanyfs_chain_create(struct super_block *sb, lanyfs_blk_t addr)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_chain *chain;
	lanyfs_debug_function(__FILE__, __func__);

	fsi = LANYFS_SB(sb);
	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return -EIO;
	}
	chain = (struct lanyfs_chain *) bh->b_data;
	lock_buffer(bh);
	memset(chain, 0x00, 1 << fsi->blocksize);
	chain->type = LANYFS_TYPE_CHAIN;
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (LANYFS_SB(sb)->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}

/**
 * lanyfs_chain_pop() - Gets address in first non-empty slot of a chain block.
 * @sb:				superblock
 * @addr:			address of chain block to read
 * @res:			result (address popped from chain block)
 */
int lanyfs_chain_pop(struct super_block *sb, lanyfs_blk_t addr,
		     lanyfs_blk_t *res)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_chain *chain;
	unsigned char *p;
	unsigned char *end;
	lanyfs_debug_function(__FILE__, __func__);

	fsi = LANYFS_SB(sb);
	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return -EIO;
	}
	chain = (struct lanyfs_chain *) bh->b_data;
	p = &chain->stream;
	end = p + ((fsi->chainmax - 1) * fsi->addrlen);
	lock_buffer(bh);
	while (p < end) {
		*res = 0;
		memcpy(res, p, fsi->addrlen);
		*res = le64_to_cpu(*res);
		if (*res) {
			memset(p, 0x00, fsi->addrlen);
			le16_add_cpu(&chain->wrcnt, 1);
			break;
		}
		p += fsi->addrlen;
	}
	unlock_buffer(bh);
	if (!*res) {
		*res = le64_to_cpu(chain->next);
		bforget(bh);
		return -LANYFS_ENOTAKEN;
	}
	mark_buffer_dirty(bh);
	if (fsi->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}

/**
 * lanyfs_chain_push() - Stores an address at first empty slot of a chain block.
 * @sb:				superblock
 * @addr:			address of chain block to manipulate
 * @rookie:			address to store
 */
int lanyfs_chain_push(struct super_block *sb, lanyfs_blk_t addr,
		      lanyfs_blk_t rookie)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_chain *chain;
	unsigned char *p;
	unsigned char *end;
	lanyfs_blk_t tmp;
	lanyfs_debug_function(__FILE__, __func__);

	fsi = LANYFS_SB(sb);
	bh = sb_bread(sb, addr);
	if (unlikely(!bh)) {
		lanyfs_msg(sb, KERN_ERR, "block #%llu read error", (u64) addr);
		return -EIO;
	}
	chain = (struct lanyfs_chain *) bh->b_data;
	p = &chain->stream;
	end = p + ((fsi->chainmax - 1) * fsi->addrlen);
	lock_buffer(bh);
	while (p < end) {
		tmp = 0;
		memcpy(&tmp, p, fsi->addrlen);
		tmp = le64_to_cpu(tmp);
		if (!tmp) {
			rookie = cpu_to_le64(rookie);
			memcpy(p, &rookie, fsi->addrlen);
			le16_add_cpu(&chain->wrcnt, 1);
			break;
		}
		p += fsi->addrlen;
	}
	unlock_buffer(bh);
	if (tmp) {
		bforget(bh);
		return -LANYFS_ENOEMPTY;
	}
	mark_buffer_dirty(bh);
	if (fsi->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}
