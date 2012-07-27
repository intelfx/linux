/*
 * super.c - Lanyard Filesystem Superblock Operations
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
 * LANYFS_SB() - Returns pointer to filesystem private data.
 * @sb:				superblock
 */
struct lanyfs_fsi *LANYFS_SB(struct super_block *sb)
{
	/*
	 * Disabled by default, it produces a lot of noise.
	 * lanyfs_debug_function(__FILE__, __func__);
	 */
	return (struct lanyfs_fsi *) sb->s_fs_info;
}

/* --- mount options -------------------------------------------------------- */

enum {
	Opt_uid,
	Opt_gid,
	Opt_dmask,
	Opt_fmask,
	Opt_discard,
	Opt_nodiscard,
	Opt_flush,
	Opt_noflush,
	Opt_err
};

static const match_table_t lanyfs_super_tokens = {
	{Opt_uid,		"uid=%u"},
	{Opt_gid,		"gid=%u"},
	{Opt_dmask,		"dmask=%u"},
	{Opt_fmask,		"fmask=%u"},
	{Opt_discard,		"discard"},
	{Opt_nodiscard,		"nodiscard"},
	{Opt_flush,		"flush"},
	{Opt_noflush,		"noflush"},
	{Opt_err,		NULL}
};

/**
 * lanyfs_super_options() - Parses and saves mount options.
 * @sb:				superblock
 * @data:			mount options raw data string
 * @silent:			whether or not to be silent on error
 */
static int lanyfs_super_options(struct super_block *sb, char *data, int silent)
{
	struct lanyfs_opts *opts;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	lanyfs_debug_function(__FILE__, __func__);

	opts = &(LANYFS_SB(sb)->opts);

	/* defaults */
	opts->uid = current_uid();
	opts->uid = current_gid();
	opts->dmask = 0;
	opts->fmask = 0;
	opts->discard = 0;
	opts->flush = 0;

	/* no options given */
	if (!data)
		goto exit_ok;

	/* parse and apply given options */
	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;
		switch (match_token(p, lanyfs_super_tokens, args)) {
		case Opt_uid:
			if (match_int(&args[0], &option))
				goto exit_invalid;
			opts->uid = option;
			break;
		case Opt_gid:
			if (match_int(&args[0], &option))
				goto exit_invalid;
			opts->gid = option;
			break;
		case Opt_dmask:
			if (match_int(&args[0], &option))
				goto exit_invalid;
			opts->dmask = option;
			break;
		case Opt_fmask:
			if (match_int(&args[0], &option))
				goto exit_invalid;
			opts->fmask = option;
			break;
		case Opt_discard:
			opts->discard = 1;
			break;
		case Opt_nodiscard:
			opts->discard = 0;
			break;
		case Opt_flush:
			opts->flush = 1;
			break;
		case Opt_noflush:
			opts->flush = 0;
			break;
		default:
			goto exit_invalid;
			break;
		}
	}
exit_ok:
	lanyfs_debug("option_uid=%u", opts->uid);
	lanyfs_debug("option_gid=%u", opts->gid);
	lanyfs_debug("option_dmask=%u", opts->dmask);
	lanyfs_debug("option_fmask=%u", opts->fmask);
	lanyfs_debug("option_discard=%u", opts->discard);
	lanyfs_debug("option_flush=%u", opts->flush);
	return 0;
exit_invalid:
	if (!silent)
		lanyfs_msg(sb, KERN_ERR,
			   "invalid mount option or bad parameter \"%s\"", p);
	return -EINVAL;
}

/* --- superblock ----------------------------------------------------------- */

/**
 * lanyfs_super_sync() - Syncs the superblock to disk.
 * @sb:				superblock
 *
 * This function does the same as old VFS write_super(), back in the days
 * when VFS invoked the syncing by looking for ->sb_dirt every five seconds.
 * Today this function is invoked by LanyFS itself whenever it seems reasonable.
 */
static void lanyfs_super_sync(struct super_block *sb)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_sb *rawsb;
	lanyfs_debug_function(__FILE__, __func__);

	fsi = LANYFS_SB(sb);
	bh = sb_bread(sb, LANYFS_SUPERBLOCK);
	if (!bh) {
		lanyfs_debug("error reading block #%llu", LANYFS_SUPERBLOCK);
		return;
	}
	rawsb = (struct lanyfs_sb *) bh->b_data;
	fsi->updated = current_kernel_time();
	lock_buffer(bh);
	le16_add_cpu(&rawsb->wrcnt, 1);
	rawsb->freehead = cpu_to_le64(fsi->freehead);
	rawsb->freetail = cpu_to_le64(fsi->freetail);
	rawsb->freeblocks = cpu_to_le64(fsi->freeblocks);
	/*
	 * number of valid blocks is not synced back at the moment, but it may
	 * as soon as a reliable badblocks-detection is implemented
	 * lanysb->blocks = cpu_to_le64(fsi->blocks);
	 */
	lanyfs_time_kts_to_lts(&fsi->updated, &rawsb->updated);
	unlock_buffer(bh);
	mark_buffer_dirty(bh);
	if (fsi->opts.flush)
		sync_dirty_buffer(bh);
	brelse(bh);
}

/**
 * lanyfs_put_super() - Prepare the superblock for unmounting.
 * @sb:				superblock
 *
 * This function is called by VFS with the superblock lock held.
 */
static void lanyfs_put_super(struct super_block *sb)
{
	lanyfs_debug_function(__FILE__, __func__);

	lanyfs_super_sync(sb);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

/**
 * lanyfs_kill_super() - Safely closes the filesystem.
 * @sb:				superblock
 *
 * Cleanup of filesystem private data is done in lanyfs_put_super().
 */
static void lanyfs_kill_super(struct super_block *sb)
{
	lanyfs_debug_function(__FILE__, __func__);
	kill_block_super(sb);
}


/**
 * lanyfs_fill_super() - Initialize the superblock.
 * @sb:				superblock
 * @options:			arbitrary mount options
 * @silent:			whether or not to be silent on error
 *
 * This is the most important function for LanyFS since all device-specific
 * configuration like address length and blocksize takes place here. It is also
 * an implementation as close to the specifications as possible, thus serving
 * as an example implementation for other operating systems or alternate kernel
 * modules.
 */
static int lanyfs_fill_super(struct super_block *sb, void *options, int silent)
{
	struct lanyfs_fsi *fsi;
	struct buffer_head *bh;
	struct lanyfs_sb *lanysb;
	struct inode *inode;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	inode = NULL;
	err = 0;

	/* allocate filesystem private data */
	fsi = kzalloc(sizeof(*fsi), GFP_KERNEL);
	if (!fsi)
		return -ENOMEM;
	spin_lock_init(&fsi->lock);
	sb->s_fs_info = fsi;

	/* set blocksize to minimum size for fetching superblock */
	if (!sb_set_blocksize(sb, 1 << LANYFS_MIN_BLOCKSIZE)) {
		if (!silent)
			lanyfs_msg(sb, KERN_ERR,
				   "error setting blocksize to %d bytes",
				   1 << LANYFS_MIN_BLOCKSIZE);
		return -EIO;
	}

	/* fetch superblock */
	bh = sb_bread(sb, LANYFS_SUPERBLOCK);
	if (!bh) {
		if (!silent)
			lanyfs_msg(sb, KERN_ERR, "error reading superblock");
		return -EIO;
	}
	lanysb = (struct lanyfs_sb *) bh->b_data;

	/* check magic */
	if (lanysb->magic != cpu_to_le32(LANYFS_SUPER_MAGIC)) {
		if (!silent)
			lanyfs_msg(sb, KERN_INFO, "bad magic 0x%x",
				   lanysb->magic);
		goto exit_invalid;
	}
	sb->s_magic = LANYFS_SUPER_MAGIC;

	/* check block type */
	if (lanysb->type != LANYFS_TYPE_SB) {
		if (!silent)
			lanyfs_msg(sb, KERN_ERR, "bad superblock type 0x%x",
				   lanysb->type);
		goto exit_invalid;
	}

	/* check version */
	if (lanysb->major > LANYFS_MAJOR_VERSION) {
		if (!silent)
			lanyfs_msg(sb, KERN_ERR, "major version mismatch");
		goto exit_invalid;
	}

	/* check address length */
	if (lanysb->addrlen < LANYFS_MIN_ADDRLEN ||
	    lanysb->addrlen > LANYFS_MAX_ADDRLEN) {
		if (!silent)
			lanyfs_msg(sb, KERN_ERR, "unsupported address length");
		goto exit_invalid;
	}
	fsi->addrlen = lanysb->addrlen;

	/* check blocksize */
	if (lanysb->blocksize < LANYFS_MIN_BLOCKSIZE ||
	    lanysb->blocksize > LANYFS_MAX_BLOCKSIZE) {
		if (!silent)
			lanyfs_msg(sb, KERN_ERR, "unsupported blocksize");
		goto exit_invalid;
	}
	fsi->blocksize = lanysb->blocksize;

	/* more filesystem private data */
	fsi->rootdir = le64_to_cpu(lanysb->rootdir);
	fsi->freehead = le64_to_cpu(lanysb->freehead);
	fsi->freetail = le64_to_cpu(lanysb->freehead);
	fsi->freeblocks = le64_to_cpu(lanysb->freeblocks);
	fsi->blocks = le64_to_cpu(lanysb->blocks);
	fsi->chainmax = ((1 << fsi->blocksize) \
			- offsetof(struct lanyfs_chain, stream)) / fsi->addrlen;
	fsi->extmax = ((1 << fsi->blocksize) \
		      - offsetof(struct lanyfs_ext, stream)) / fsi->addrlen;
	lanyfs_time_lts_to_kts(&lanysb->updated, &fsi->updated);

	/* superblock debug messages */
	lanyfs_debug_block((union lanyfs_b *) bh->b_data);

	/* release block buffer */
	brelse(bh);

	/* parse mount options */
	save_mount_options(sb, options);
	err = lanyfs_super_options(sb, (char *) options, silent);
	if (err)
		return err;

	/* set blocksize to correct size */
	if (!sb_set_blocksize(sb, 1 << fsi->blocksize)) {
		if (!silent)
			lanyfs_msg(sb, KERN_ERR,
				   "error setting blocksize to %d bytes",
				   1 << fsi->blocksize);
		return -EIO;
	}
	/* default flags */
	sb->s_maxbytes = 0xffffffff; /* TODO: hmmmmm */
	sb->s_op = &lanyfs_super_operations;
	sb->s_time_gran = 1;
	sb->s_flags = MS_NOSUID | MS_NOATIME | MS_NODIRATIME;

	/* make root directory */
	inode = lanyfs_iget(sb, fsi->rootdir);
	if (!inode)
		return -ENOMEM;

	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		iput(inode);
		return -ENOMEM;
	}
	return 0;

exit_invalid:
	brelse(bh);
	if (!silent)
		lanyfs_msg(sb, KERN_INFO, "no valid lanyard filesystem found");
	return -EINVAL;
}

/**
 * lanyfs_mount() - Mounts a LanyFS device.
 * @fs_type:			describes the filesystem
 * @flags:			mount flags
 * @device_name:		the device name we are mounting
 * @data:			arbitrary mount options
 */
static struct dentry *lanyfs_mount(struct file_system_type *fs_type, int flags,
				   const char *device_name, void *data)
{
	return mount_bdev(fs_type, flags, device_name, data, lanyfs_fill_super);
}

/* --- free space management ------------------------------------------------ */

/**
 * lanyfs_enslave() - Picks a block from the free blocks pool.
 * @sb:				superblock
 *
 * Returns zero on error, e.g. on ENOSPC.
 */
lanyfs_blk_t lanyfs_enslave(struct super_block *sb)
{
	struct lanyfs_fsi *fsi;
	lanyfs_blk_t addr;
	lanyfs_debug_function(__FILE__, __func__);

	fsi = LANYFS_SB(sb);
	if (unlikely(!fsi->freehead || !fsi->freetail || !fsi->freeblocks))
		return 0;

	spin_lock(&fsi->lock);
	switch (lanyfs_chain_pop(sb, fsi->freehead, &addr)) {
	case -LANYFS_ENOTAKEN:
		/* no occupied slot left, enslave chain block itself instead */
		swap(addr, fsi->freehead);
		if (addr == fsi->freetail)
			fsi->freetail = fsi->freehead;
		/* fall through */
	case 0:
		fsi->freeblocks--;
		spin_unlock(&fsi->lock);
		lanyfs_super_sync(sb);
		lanyfs_debug("enslaved block #%llu", addr);
		return addr;
		break;
	default:
		spin_unlock(&fsi->lock);
		break;
	}
	return 0;

}

/**
 * lanyfs_release() - Returns a block to the free blocks pool.
 * @sb:				superblock
 * @addr:			address of block to be returned
 *
 * Blocks are literally recycled, blocks remain unused as long as possible to
 * distribute write cycles all over the device.
 */
int lanyfs_release(struct super_block *sb, lanyfs_blk_t addr)
{
	struct lanyfs_fsi *fsi;
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	if (!likely(addr))
		return -LANYFS_EPROTECTED;

	fsi = LANYFS_SB(sb);

	/* device was full, released block becomes chain block */
	if (unlikely(!fsi->freehead || !fsi->freetail || !fsi->freeblocks)) {
		err = lanyfs_chain_create(sb, addr);
		if (err)
			goto exit_err;
		spin_lock(&fsi->lock);
		fsi->freehead = fsi->freetail = addr;
		fsi->freeblocks = 1;
		spin_unlock(&fsi->lock);
		lanyfs_super_sync(sb);
		return 0;
	}

	/* append block to existing chain */
	err = lanyfs_chain_push(sb, fsi->freetail, addr);
	if (err == -LANYFS_ENOEMPTY) {
		/* chain block was full, create a new one and append it */
		err = lanyfs_chain_create(sb, addr);
		if (err)
			goto exit_err;
		err = lanyfs_chain_set_next(sb, fsi->freetail, addr);
		spin_lock(&fsi->lock);
		fsi->freetail = addr;
		fsi->freeblocks++;
		spin_unlock(&fsi->lock);
		return 0;
	} else if (err) {
		goto exit_err;
	}
	return 0;
exit_err:
	lanyfs_msg(sb, KERN_WARNING, "error freeing block #%llu", (u64) addr);
	return err;
}

/* --- statistics ----------------------------------------------------------- */

/**
 * lanyfs_show_stats() - Eventually shows extended filesystem statistics.
 * @m:				seq-file to write to
 * @dentry:			root directory entry
 *
 * This function is still in development.
 * Currently unknown: Where does the output (read: seq_file writes) of this
 * function show up?
 */
static int lanyfs_show_stats(struct seq_file *m, struct dentry *dentry)
{
	seq_printf(m, "Can we try with real bullets now? (Mathilda)\n");
	return 0;
}

/**
 * lanyfs_statfs() - Provides filesystem statistics.
 * @dentry:			directory entry
 * @buf:			buffer for filesystem statistics
 */
static int lanyfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct lanyfs_fsi *fsi;
	u64 fsid;
	lanyfs_debug_function(__FILE__, __func__);
	fsi = LANYFS_SB(dentry->d_sb);
	fsid = huge_encode_dev(dentry->d_sb->s_bdev->bd_dev);
	buf->f_type = LANYFS_SUPER_MAGIC;
	buf->f_bsize = 1 << fsi->blocksize;
	buf->f_blocks = fsi->blocks;
	buf->f_bfree = fsi->freeblocks;
	buf->f_bavail = buf->f_bfree;
	buf->f_files = fsi->blocks;
	buf->f_ffree = fsi->freeblocks;
	/* Nobody knows what f_fsid is supposed to contain, cp. statfs(2)! */
	buf->f_fsid.val[0] = (u32) fsid;
	buf->f_fsid.val[1] = (u32) (fsid >> 32);
	buf->f_namelen = LANYFS_NAME_LENGTH;
	return 0;
}

/* --- vfs interface -------------------------------------------------------- */

/* lanyfs filesystem type */
struct file_system_type lanyfs_file_system_type = {
	.name			= "lanyfs",
	.fs_flags		= FS_REQUIRES_DEV,
	.mount			= lanyfs_mount,
	.kill_sb		= lanyfs_kill_super,
	.owner			= THIS_MODULE,
};

/* lanyfs superblock operations */
const struct super_operations lanyfs_super_operations = {
	.alloc_inode		= lanyfs_alloc_inode,
	.destroy_inode		= lanyfs_destroy_inode,
	.dirty_inode		= NULL,
	.write_inode		= lanyfs_write_inode,
	.drop_inode		= generic_drop_inode, /* generic is fine */
	.evict_inode		= NULL,
	.put_super		= lanyfs_put_super,
	.sync_fs		= NULL,
	.freeze_fs		= NULL, /* for LVM */
	.unfreeze_fs		= NULL, /* for LVM */
	.statfs			= lanyfs_statfs,
	.remount_fs		= NULL,
	.umount_begin		= NULL,
	.show_options		= generic_show_options, /* generic is fine */
	.show_devname		= NULL, /* default is fine for lanyfs */
	.show_path		= NULL, /* default is fine for lanyfs */
	.show_stats		= lanyfs_show_stats,
	.bdev_try_to_free_page	= NULL,
	.nr_cached_objects	= NULL, /* for sb cache shrinking */
	.free_cached_objects	= NULL, /* for sb cache shrinking */
};
