/*
 * inode.c - Lanyard Filesystem Inode Operations
 *
 * Copyright (C) 2012  Dan Luedtke <mail@danrl.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "lanyfs_km.h"

static const struct inode_operations lanyfs_file_inode_operations;

/**
 * lanyfs_inode_poke() - Updates all timestamps of an inode.
 * @inode:			inode to update
 *
 * Don't do this to unhashed inodes.
 */
void lanyfs_inode_poke(struct inode *inode)
{
	lanyfs_debug_function(__FILE__, __func__);

	if (inode) {
		spin_lock(&inode->i_lock);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		spin_unlock(&inode->i_lock);
		mark_inode_dirty(inode);
	}
}

/**
 * lanyfs_inode_rename() - Sets name of a directory or file.
 * @inode:			inode to rename
 * @name:			new name
 *
 * Attention! Callers must remove the inode from any binary tree *before*
 * setting a new name otherwise the tree will break.
 */
void lanyfs_inode_rename(struct inode *inode, const char *name)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(inode);
	spin_lock(&lii->lock);
	spin_lock(&inode->i_lock);
	memset(lii->name, 0x00, LANYFS_NAME_LENGTH);
	strncpy(lii->name, name, LANYFS_NAME_LENGTH - 1);
	lii->len = strlen(lii->name);
	spin_unlock(&inode->i_lock);
	spin_unlock(&lii->lock);
}

/**
 * lanyfs_iget() - Turns a file or directory block into an inode.
 * @sb:				superblock
 * @addr:			number of block to awake
 *
 * Checks for inode state, thus overloading an inode already woken up will
 * will just return that inode with increased reference count. Make sure to
 * always decrease the reference count after use. VFS recklessly kills all
 * referenced inodes on unmount which may lead to data loss.
 * Real overloading would endanger consistency.
 */
struct inode *lanyfs_iget(struct super_block *sb, lanyfs_blk_t addr)
{
	struct lanyfs_fsi *fsi;
	struct lanyfs_lii *lii;
	struct buffer_head *bh;
	union lanyfs_b *b;
	struct inode *inode;
	lanyfs_debug_function(__FILE__, __func__);

	if (!addr)
		return NULL;
	fsi = LANYFS_SB(sb);
	inode = iget_locked(sb, addr); /* !: implicit cast to unsigned long */
	if (!inode)
		return NULL;
	if (!(inode->i_state & I_NEW))
		return inode;
	lii = LANYFS_I(inode);
	bh = sb_bread(sb, addr);
	if (!bh) {
		lanyfs_debug("error reading block #%llu", (u64) addr);
		iget_failed(inode);
		return NULL;
	}
	b = (union lanyfs_b *) bh->b_data;
	switch (b->raw.type) {
	case LANYFS_TYPE_DIR:
		/* directory specific fields */
		lii->subtree = le64_to_cpu(b->dir.subtree);
		inode->i_op = &lanyfs_dir_inode_operations;
		inode->i_fop = &lanyfs_dir_operations;
		inode->i_mode = lanyfs_attr_to_mode(sb,
			le16_to_cpu(b->vi_meta.attr), S_IFDIR);
		inode->i_size = 1 << fsi->blocksize;
		break;
	case LANYFS_TYPE_FILE:
		/* file specific fields */
		lii->data = le64_to_cpu(b->file.data);
		inode->i_op = &lanyfs_file_inode_operations;
		inode->i_fop = &lanyfs_file_operations;
		inode->i_mapping->a_ops = &lanyfs_address_space_operations;
		inode->i_mode = lanyfs_attr_to_mode(sb,
			le16_to_cpu(b->vi_meta.attr), S_IFREG);
		inode->i_size = le64_to_cpu(b->file.size);
		break;
	default:
		brelse(bh);
		iget_failed(inode);
		return NULL;
		break;
	}
	/* binary tree */
	lii->left = le64_to_cpu(b->vi_btree.left);
	lii->right = le64_to_cpu(b->vi_btree.right);
	/* times */
	lanyfs_time_lts_to_kts(&b->vi_meta.modified, &inode->i_mtime);
	inode->i_atime = inode->i_ctime = inode->i_mtime;
	lanyfs_time_lts_to_kts(&b->vi_meta.created, &lii->created);
	/* name */
	memset(lii->name, 0x00, LANYFS_NAME_LENGTH);
	strncpy(lii->name, b->vi_meta.name, LANYFS_NAME_LENGTH - 1);
	lii->len = strlen(lii->name);
	/* uid, gid */
	inode->i_uid = fsi->opts.uid;
	inode->i_gid = fsi->opts.gid;
	/* blksize */
	inode->i_blkbits = fsi->blocksize;
	unlock_new_inode(inode);
	brelse(bh);
	return inode;
}

/**
 * lanyfs_lookup() - Looks up an inode in a directory by name.
 * @dir:			inode of containing directory
 * @dentry:			directory entry to look up
 * @flags:			lookup flags
 *
 * The @flags are ignored by LanyFS. There are not many filesystems
 * using the flags at all. This was nameidata before 3.5 which LanyFS
 * did not use either.
 */
struct dentry *lanyfs_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags)
{
	struct inode *inode;
	lanyfs_debug_function(__FILE__, __func__);

	/* length check */
	if (dentry->d_name.len >= LANYFS_NAME_LENGTH)
		return ERR_PTR(-ENAMETOOLONG);

	inode = lanyfs_btree_lookup(dir, dentry->d_name.name);
	if (inode)
		return d_splice_alias(inode, dentry);
	return NULL;
}

/**
 * lanyfs_write_inode() - Writes inode to disk.
 * @inode:			VFS inode
 * @wbc:			writeback control (unused)
 */
int lanyfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct buffer_head *bh;
	struct lanyfs_lii *lii;
	union lanyfs_b *b;
	u16 attr;
	lanyfs_debug_function(__FILE__, __func__);

	if (!inode->i_nlink)
		return 0;

	lii = LANYFS_I(inode);
	bh = sb_bread(inode->i_sb, inode->i_ino);
	if (!bh) {
		lanyfs_debug("error reading block #%llu", (u64) inode->i_ino);
		return -EIO;
	}
	b = (union lanyfs_b *) bh->b_data;
	spin_lock(&lii->lock);
	spin_lock(&inode->i_lock);
	lock_buffer(bh);
	switch (b->raw.type) {
	case LANYFS_TYPE_DIR:
		/* directory specific fields */
		b->dir.subtree = cpu_to_le64(lii->subtree);
		break;
	case LANYFS_TYPE_FILE:
		/* file specific fields */
		b->file.data = cpu_to_le64(lii->data);
		b->file.size = cpu_to_le64(inode->i_size);
		break;
	default:
		spin_unlock(&inode->i_lock);
		spin_unlock(&lii->lock);
		unlock_buffer(bh);
		bforget(bh);
		return -EINVAL;
		break;
	}
	/* name */
	memset(b->vi_meta.name, 0x00, LANYFS_NAME_LENGTH);
	strncpy(b->vi_meta.name, lii->name, LANYFS_NAME_LENGTH - 1);
	/* latest time *anything* changed always becomes modification time */
	lanyfs_time_sync_inode(inode);
	lanyfs_time_kts_to_lts(&inode->i_mtime, &b->vi_meta.modified);
	/* mode */
	attr = le16_to_cpu(b->vi_meta.attr);
	attr = lanyfs_mode_to_attr(inode->i_mode, attr);
	b->vi_meta.attr = cpu_to_le16(attr);
	/* binary tree */
	b->vi_btree.left = cpu_to_le64(lii->left);
	b->vi_btree.right = cpu_to_le64(lii->right);
	/* write counter */
	le16_add_cpu(&b->raw.wrcnt, 1);

	spin_unlock(&inode->i_lock);
	spin_unlock(&lii->lock);
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (LANYFS_SB(inode->i_sb)->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
	return 0;
}

/**
 * lanyfs_setattr() - Sets the attributes of an directory entry.
 * @dentry:			directory entry to manipulate
 * @attr:			new attributes
 *
 * This is the point where VFS tells us what it likes to change. We can then
 * decide what changes we like and what changes we would like to reject.
 * File size changes are also invoked from here and delegated to vmtruncate,
 * which in turn calls lanyfs_truncate() after some checks.
 */
static int lanyfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct lanyfs_fsi *fsi;
	struct inode *inode;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	inode = dentry->d_inode;
	fsi = LANYFS_SB(inode->i_sb);

/*
 * TODO
	if ((err = inode_change_ok(inode, attr)));
		return err;
*/
	/* no uid changes */
	if ((attr->ia_valid & ATTR_UID) &&
	    (attr->ia_uid != fsi->opts.uid))
		return 0;
	/* no gid changes */
	if ((attr->ia_valid & ATTR_GID) &&
	    (attr->ia_gid != fsi->opts.gid))
		return 0;
	/* directories and files can be set read-only */
	if (attr->ia_valid & ATTR_MODE) {
		if (attr->ia_mode & S_IWUSR)
			attr->ia_mode = inode->i_mode | S_IWUGO;
		else
			attr->ia_mode = inode->i_mode & ~S_IWUGO;
	}
	/* files can be set non-executable */
	if ((attr->ia_valid & ATTR_MODE) && !S_ISDIR(inode->i_mode)) {
		if (attr->ia_mode & S_IXUSR)
			attr->ia_mode = inode->i_mode | S_IXUGO;
		else
			attr->ia_mode = inode->i_mode & ~S_IXUGO;
	}
	/* apply masks */
	if (S_ISDIR(inode->i_mode))
		attr->ia_mode &= ~fsi->opts.dmask;
	else
		attr->ia_mode &= ~fsi->opts.fmask;
	/* size change */
	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		inode_dio_wait(inode);
		err = vmtruncate(inode, attr->ia_size);
		if (err)
			return err;
	}
	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

/** lanyfs_getattr() - Gets directory entry attributes.
 * @mnt:			VFS mount
 * @dentry:			directory entry to read
 * @kstat:			pointer to result storage
 *
 * This function does not differ much from the standard VFS getattr() currently.
 */
static int lanyfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
			  struct kstat *kstat)
{
	struct inode *inode;
	lanyfs_debug_function(__FILE__, __func__);

	inode = dentry->d_inode;
	kstat->dev = inode->i_sb->s_dev;
	kstat->ino = inode->i_ino;
	kstat->mode = inode->i_mode;
	kstat->nlink = inode->i_nlink;
	kstat->uid = inode->i_uid;
	kstat->gid = inode->i_gid;
	kstat->rdev = inode->i_rdev;
	kstat->size = i_size_read(inode);
	kstat->atime = inode->i_atime;
	kstat->mtime = inode->i_mtime;
	kstat->ctime = inode->i_ctime;
	kstat->blksize = (1 << inode->i_blkbits);
	kstat->blocks = inode->i_blocks;
	return 0;
}

/** lanyfs_truncate() - Truncates a file.
 * @inode:			inode of file to truncate
 */
static void lanyfs_truncate(struct inode *inode)
{
	struct lanyfs_fsi *fsi;
	struct lanyfs_lii *lii;
	lanyfs_blk_t iblock;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(inode);
	if (!lii->data)
		return;
	fsi = LANYFS_SB(inode->i_sb);
	iblock = inode->i_size / (1 << fsi->blocksize);
	if (inode->i_size % (1 << fsi->blocksize))
		iblock++;
	lanyfs_ext_truncate(inode->i_sb, lii->data, iblock);
}

/* lanyfs file inode operations */
static const struct inode_operations lanyfs_file_inode_operations = {
	.lookup		= lanyfs_lookup,
	.setattr	= lanyfs_setattr,
	.getattr	= lanyfs_getattr,
	.truncate	= lanyfs_truncate,
};
