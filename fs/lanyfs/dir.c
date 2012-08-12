/*
 * dir.c - Lanyard Filesystem Directory Operations
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
 * lanyfs_empty() - Test wether a directory is empty or not.
 * @inode:			directory inode to test
 */
static int lanyfs_empty(struct inode *inode)
{
	lanyfs_debug_function(__FILE__, __func__);

	if (unlikely(!inode) || unlikely(!S_ISDIR(inode->i_mode)))
		return 0;
	return !LANYFS_I(inode)->subtree;
}

/**
 * __lanyfs_readdir() - Lists directory contents using recursion.
 * @n:				root node of tree to list
 * @fp:				file pointer
 * @dirent:			pointer to directory entry
 * @filldir:			function pointer, provides function 'filldir'
 *
 * This function is internal and is best be called by its wrapper function.
 */
static int __lanyfs_readdir(lanyfs_blk_t n, struct file *fp, void *dirent,
			    filldir_t filldir)
{
	struct inode *inode;
	struct lanyfs_lii *lii;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	/* this entry */
	err = 0;
	inode = lanyfs_iget(fp->f_dentry->d_sb, n);
	if (!inode)
		return -ENOMEM;
	lii = LANYFS_I(inode);
	err = filldir(dirent, lii->name, lii->len, fp->f_pos, inode->i_ino,
		      (inode->i_mode >> 12) & 0xF);
	if (err)
		goto exit_err;
	fp->f_pos++;
	/* left entry */
	if (lii->left)
		__lanyfs_readdir(lii->left, fp, dirent, filldir);
	/* right entry */
	if (lii->right)
		__lanyfs_readdir(lii->right, fp, dirent, filldir);
exit_err:
	iput(inode);
	return err;
}

/**
 * lanyfs_readdir() - Lists directory contents.
 * @fp:				file pointer
 * @dirent:			pointer to directory entry
 * @filldir:			function pointer, provides function 'filldir'
 */
static int lanyfs_readdir(struct file *fp, void *dirent, filldir_t filldir)
{
	ino_t ino;
	lanyfs_blk_t subtree;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	switch (fp->f_pos) {
	case 0:
		/* this dir */
		ino = fp->f_dentry->d_inode->i_ino;
		err = filldir(dirent, ".", 1, fp->f_pos, ino, DT_DIR);
		if (err)
			return err;
		fp->f_pos++;
		/* fall through */
	case 1:
		/* parent dir */
		ino = parent_ino(fp->f_dentry);
		err = filldir(dirent, "..", 2, fp->f_pos, ino, DT_DIR);
		if (err)
			return err;
		fp->f_pos++;
		/* fall through */
	case 2:
		/* this dir's entries */
		subtree = LANYFS_I(fp->f_dentry->d_inode)->subtree;
		if (subtree)
			__lanyfs_readdir(subtree, fp, dirent, filldir);
		break;
	default:
		return -ENOENT;
		break;
	}
	return 0;
}

/**
 * lanyfs_mkdir() - Creates a new directory.
 * @pdir:			current directory
 * @dentry:			directory to create
 * @mode:			requested mode of new directory
 */
static int lanyfs_mkdir(struct inode *pdir, struct dentry *dentry, umode_t mode)
{
	struct super_block *sb;
	lanyfs_blk_t addr;
	struct buffer_head *bh;
	struct lanyfs_dir *dir;
	struct inode *inode;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	/* length check */
	if (dentry->d_name.len >= LANYFS_NAME_LENGTH)
		return -ENAMETOOLONG;

	sb = pdir->i_sb;
	/* get free block */
	addr = lanyfs_enslave(sb);
	if (!addr)
		return -ENOSPC;

	/* create directory block */
	bh = sb_bread(sb, addr);

	if (!bh) {
		lanyfs_err(sb, "error reading block #%llu", (u64) addr);
		err = -EIO;
		goto exit_release;
	}
	dir = (struct lanyfs_dir *) bh->b_data;

	lock_buffer(bh);
	memset(bh->b_data, 0x00, 1 << LANYFS_SB(sb)->blocksize);
	dir->type = LANYFS_TYPE_DIR;
	lanyfs_time_lts_now(&dir->meta.created);
	dir->meta.modified = dir->meta.created;
	dir->meta.attr = lanyfs_mode_to_attr(mode, 0);
	strncpy(dir->meta.name, dentry->d_name.name, LANYFS_NAME_LENGTH - 1);
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (LANYFS_SB(sb)->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);

	inode = lanyfs_iget(sb, addr);
	if (!inode) {
		err = -ENOMEM;
		goto exit_release;
	}
	err = lanyfs_btree_add_inode(pdir, inode);
	if (err)
		goto exit_ino;
	inc_nlink(inode);
	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);
	return 0;

exit_ino:
	drop_nlink(inode);
	iput(inode);
exit_release:
	lanyfs_release(sb, addr);
	return err;
}

/**
 * lanyfs_rmdir() - Deletes a directory.
 * @dir:		parent directory
 * @dentry:		directory to remove
 */
static int lanyfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	lanyfs_blk_t addr;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	/* length check */
	if (dentry->d_name.len >= LANYFS_NAME_LENGTH)
		return -ENAMETOOLONG;

	/* empty check */
	if (!lanyfs_empty(dentry->d_inode))
		return -ENOTEMPTY;

	addr = dentry->d_inode->i_ino;

	/* remove block from binary tree */
	err = lanyfs_btree_del_inode(dir, dentry->d_name.name);
	if (err)
		return err;
	drop_nlink(dir);
	clear_nlink(dentry->d_inode);
	d_delete(dentry);

	/* set block free */
	lanyfs_release(dir->i_sb, addr);
	return 0;
}

/**
 * lanyfs_unlink() - Deletes a file.
 * @dir:			containing directory
 * @dentry:			directory entry to remove
 */
static int lanyfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb;
	struct inode *inode;
	lanyfs_blk_t addr;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	sb = dir->i_sb;
	inode = dentry->d_inode;
	addr = inode->i_ino;

	/* free space used by inode */
	err = vmtruncate(inode, 0);
	if (err)
		return err;

	err = lanyfs_btree_del_inode(dir, dentry->d_name.name);
	if (err)
		return err;

	drop_nlink(inode);
	lanyfs_inode_poke(dir);
	lanyfs_release(sb, addr);
	return 0;
}

/**
 * lanyfs_rename() - Renames and/or moves a directory or file.
 * @old_dir:			old directory
 * @old_dentry:			old directory entry
 * @new_dir:			new directory
 * @new_dentry:			new directory entry
 *
 * Case I:
 * Just rename a/foo to a/bar.
 *
 * Case II:
 * Just move a/foo to b/foo.
 *
 * Case III:
 * Rename and move a/foo to b/bar.
 *
 * Caution:
 * Operations may overwrite existing objects!
 */
static int lanyfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			 struct inode *new_dir, struct dentry *new_dentry)
{
	struct super_block *sb;
	const char *old_name;
	const char *new_name;
	struct inode *old_inode;
	struct inode *new_inode;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	sb = old_dir->i_sb;
	old_name = old_dentry->d_name.name;
	new_name = new_dentry->d_name.name;
	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;

	/* remove target if it exists */
	if (new_inode) {
		if (S_ISDIR(old_inode->i_mode)) {
			if (!lanyfs_empty(new_inode)) 
				return -ENOTEMPTY;
			lanyfs_rmdir(new_dir, new_dentry);
		} else {
			lanyfs_unlink(new_dir, new_dentry);
		}
	}

	/* remove node from old binary tree */
	lanyfs_btree_del_inode(old_dir, old_name);
	lanyfs_btree_clear_inode(old_inode);

	/* change name */
	lanyfs_inode_rename(old_inode, new_name);

	/* add node to new binary tree */
	err = lanyfs_btree_add_inode(new_dir, old_inode);
	if (err)
		return err;
	lanyfs_inode_poke(old_inode);
	lanyfs_inode_poke(old_dir);
	lanyfs_inode_poke(new_dir);
	return 0;
}

/**
 * lanyfs_create() - Creates a new file.
 * @dir:			parent directory
 * @dentry:			directory entry of file to create
 * @mode:			file mode
 * @excl:			exclusive flag
 *
 * Creates a new file in @dir with mode @mode and the name requested in @dentry.
 * @excl is ignored by LanyFS. There are not many filesystems
 * using the flag at all. This was nameidata before 3.5 which LanyFS
 * did not use either.
 */
static int lanyfs_create(struct inode *dir, struct dentry *dentry,
			 umode_t mode, bool excl)
{
	struct super_block *sb;
	struct lanyfs_fsi *fsi;
	lanyfs_blk_t addr;
	struct buffer_head *bh;
	struct inode *inode;
	struct lanyfs_file *file;
	lanyfs_debug_function(__FILE__, __func__);

	sb = dir->i_sb;
	fsi = LANYFS_SB(sb);

	/* length check */
	if (dentry->d_name.len >= LANYFS_NAME_LENGTH)
		return -ENAMETOOLONG;

	/* get free block */
	addr = lanyfs_enslave(sb);
	if (!addr)
		return -ENOSPC;

	/* create file block */
	bh = sb_bread(sb, addr);
	if (!bh) {
		lanyfs_err(sb, "error reading block #%llu", (u64) addr);
		return -EIO;
	}
	file = (struct lanyfs_file *) bh->b_data;
	lock_buffer(bh);
	memset(bh->b_data, 0x00, 1 << fsi->blocksize);
	file->type = LANYFS_TYPE_FILE;
	lanyfs_time_lts_now(&file->meta.created);
	file->meta.modified = file->meta.created;
	file->meta.attr = lanyfs_mode_to_attr(mode, 0);
	strncpy(file->meta.name, dentry->d_name.name, LANYFS_NAME_LENGTH - 1);
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (fsi->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);

	/* VFS */
	inode = lanyfs_iget(sb, addr);
	if (!inode) {
		dput(dentry);
		drop_nlink(inode);
		iput(inode);
		lanyfs_release(sb, addr);
		return -ENOMEM;
	}
	lanyfs_btree_add_inode(dir, inode);
	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);
	return 0;
}

/* lanyfs dir operations */
const struct file_operations lanyfs_dir_operations = {
	.readdir		= lanyfs_readdir,
};

/* lanyfs dir inode operations */
const struct inode_operations lanyfs_dir_inode_operations = {
	.lookup			= lanyfs_lookup,
	.create			= lanyfs_create,
	.mkdir			= lanyfs_mkdir,
	.rmdir			= lanyfs_rmdir,
	.rename			= lanyfs_rename,
	.unlink			= lanyfs_unlink,
};

