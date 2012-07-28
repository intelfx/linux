/*
 * file.c - Lanyard Filesystem File Operations
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
 * lanyfs_getblk() - Maps a file-internal block to a on-disk block.
 * @inode:			file inode
 * @iblock:			file-internal block
 * @bh_result:			buffer head for result
 * @create:			create the requested block
 */
static int lanyfs_getblk(struct inode *inode, sector_t iblock,
			 struct buffer_head *bh_result, int create)
{
	struct super_block *sb;
	struct lanyfs_fsi *fsi;
	struct lanyfs_lii *lii;
	lanyfs_blk_t addr;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	sb = inode->i_sb;
	fsi = LANYFS_SB(sb);
	lii = LANYFS_I(inode);

	if (!lii->data) {
		spin_lock(&lii->lock);
		lii->data = lanyfs_ext_create(sb, 0);
		spin_unlock(&lii->lock);
		if (!lii->data)
			return -ENOSPC;
	}
	if (create) {
		spin_lock(&lii->lock);
		lanyfs_ext_grow(sb, &lii->data);
		spin_unlock(&lii->lock);
		set_buffer_new(bh_result);
		inode_add_bytes(inode, sb->s_blocksize);
		mark_inode_dirty(inode);
	}
	err = lanyfs_ext_iblock(sb, lii->data, iblock, &addr);
	if (err)
		return err;
	map_bh(bh_result, sb, addr);
	return 0;
}

/**
 * lanyfs_writepage() - Writes a full page to disk.
 * @page:			page to write
 * @wbc:			writeback control
 */
static int lanyfs_writepage(struct page *page, struct writeback_control *wbc)
{
	lanyfs_debug_function(__FILE__, __func__);

	return block_write_full_page(page, lanyfs_getblk, wbc);
}

/**
 * lanyfs_readpage() - Reads a full page from disk.
 * @fp:				file pointer
 * @page:			page to read
 */
static int lanyfs_readpage(struct file *fp, struct page *page)
{
	lanyfs_debug_function(__FILE__, __func__);

	return block_read_full_page(page, lanyfs_getblk);
}

/**
 * lanyfs_bmap() - Maps an on-disk block to a page.
 * @mapping:			address space mapping information
 * @block:			block to map
 */
static sector_t lanyfs_bmap(struct address_space *mapping, sector_t block)
{
	lanyfs_debug_function(__FILE__, __func__);

	return generic_block_bmap(mapping, block, lanyfs_getblk);
}

/* lanyfs address space operations */
const struct address_space_operations lanyfs_address_space_operations = {
	.readpage	= lanyfs_readpage,
	.writepage	= lanyfs_writepage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.bmap		= lanyfs_bmap,
};

/* lanyfs file operations */
const struct file_operations lanyfs_file_operations = {
	.open		= generic_file_open,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.fsync		= generic_file_fsync,
	.splice_read	= generic_file_splice_read,
/*	.release	= generic_file_release, */
	.llseek		= generic_file_llseek,
};
