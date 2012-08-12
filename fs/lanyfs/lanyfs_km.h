/*
 * lanyfs_km.h - Lanyard Filesystem Header for Kernel Module
 *
 * Copyright (C) 2012  Dan Luedtke <mail@danrl.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __LANYFS_KM_H_
#define __LANYFS_KM_H_

#include <linux/module.h>
#include <linux/slab.h>		/* kmemcache */
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/magic.h>	/* LANYFS_SUPER_MAGIC */
#include <linux/writeback.h>	/* current_uid() etc. */
#include <linux/parser.h>	/* mount option parser */
#include <linux/blkdev.h>	/* issue discard */
#include <linux/statfs.h>	/* struct kstatfs */
#include <linux/seq_file.h>	/* seq_puts() */
#include <linux/namei.h>	/* struct nameidata */
#include <linux/spinlock.h>

#include "lanyfs_lnx.h"		/* kernel space data structures */

/*
 * error codes
 * lanyfs uses standard error codes whenever possible
 */
#define LANYFS_ERRNO_BASE	2050
#define LANYFS_EPROTECTED	(LANYFS_ERRNO_BASE + 0)
#define LANYFS_ENOEMPTY		(LANYFS_ERRNO_BASE + 1)
#define LANYFS_ENOTAKEN		(LANYFS_ERRNO_BASE + 2)

/* messaging */
#define lanyfs_info(sb, fmt, ...)					\
	do {								\
		if (sb)							\
			pr_info("lanyfs (%s): " pr_fmt(fmt) "\n",	\
				sb->s_id, ##__VA_ARGS__);		\
	} while (0)
#define lanyfs_err(sb, fmt, ...)					\
	do {								\
		if (sb)							\
			pr_err("lanyfs (%s): " pr_fmt(fmt) "\n",	\
			       sb->s_id, ##__VA_ARGS__);		\
	} while (0)
#define lanyfs_warn(sb, fmt, ...)					\
	do {								\
		if (sb)							\
			pr_warning("lanyfs (%s): " pr_fmt(fmt) "\n",	\
				   sb->s_id, ##__VA_ARGS__);		\
	} while (0)

/* debug messaging */
#ifdef LANYFS_DEBUG
#define lanyfs_debug(fmt, ...)						\
	printk(KERN_DEBUG "lanyfs: " pr_fmt(fmt) "\n", ##__VA_ARGS__)
#else
#define lanyfs_debug(fmt, ...)						\
	do { } while (0)
#endif /* !LANYFS_DEBUG */

/**
 * typedef lanyfs_blk_t - the address of a logical block
 *
 * Every time you typedef without need, a kitten dies somewhere!
 * However, sector_t assumes 512-byte sectors and blkcnt_t is for the number
 * of total blocks. Please eliminate this typedef if you find a reasonable type.
 * May equal sector in some configurations, so basically it is like sector_t,
 * but not the same.
 */
typedef u64 lanyfs_blk_t;

/**
 * struct lanyfs_opts - mount options
 * @uid:			userid of all files and directories
 * @gid:			grouid of all files and direcotries
 * @dmask:			directory mask
 * @fmask:			file mask
 * @discard:			issue discard requests on block freeing
 * @flush:			force instant writing of changed data
 */
struct lanyfs_opts {
	uid_t			uid;
	gid_t			gid;
	unsigned int		dmask;
	unsigned int		fmask;
	unsigned int		discard:1,
				flush:1;
};

/**
 * struct lanyfs_fsi - filesystem private data
 * @blocksize:			blocksize (exponent to base 2)
 * @addrlen:			address length in bytes
 * @rootdir:			address of root directory
 * @blocks:			number of good blocks on the device
 * @freehead:			address of first extender for free blocks
 * @freetail:			address of last extender for free blocks
 * @freeblocks:			number of free blocks
 * @updated:			date and time of last superblock field change
 * @chainmax:			maximum number of slots per chain block
 * @extmax:			maximum number of slots per extender block
 * @opts:			mount options
 * @lock:			spinlock for filesystem private data
 *
 * Elements @freehead, @freetail, @blocks, @freeblocks, and @updated will be
 * written back to disk on change or when VFS is syncing superblocks.
 * Other elements are informational and must not be changed, but even if
 * changed, their values won't be written back to disk.
 */
struct lanyfs_fsi {
	unsigned int		blocksize;
	unsigned int		addrlen;
	lanyfs_blk_t		rootdir;
	lanyfs_blk_t		blocks;
	lanyfs_blk_t		freehead;
	lanyfs_blk_t		freetail;
	lanyfs_blk_t		freeblocks;
	struct timespec		updated;
	unsigned int		chainmax;
	unsigned int		extmax;
	struct lanyfs_opts	opts;
	spinlock_t		lock;
};

/**
 * struct lanyfs_lii - inode private data
 * @left:			address of left node of binary tree
 * @right:			address of right node of binary tree
 * @subtree:			binary tree root (directory only)
 * @data:			address of first extender (file only)
 * @created:			directory or file creation time
 * @name:			directory or file name
 * @len:			length of directory or file name
 * @vfs_inode:			virtual filesystem inode
 * @lock:			spinlock for inode private data
 *
 * Field @created is not synced back to disk, even if changed.
 *
 * We could save up to 8 byte of memory per inode if we union @subtree and
 * @data, but then we must distinct between directory and file when destroying
 * inode private data. Maybe later :)
 */
struct lanyfs_lii {
	lanyfs_blk_t		left;
	lanyfs_blk_t		right;
	union {
		lanyfs_blk_t	subtree;	/* directory only */
		lanyfs_blk_t	data;		/* file only */
	};
	struct timespec		created;
	char			name[LANYFS_NAME_LENGTH];
	unsigned int		len;
	struct inode		vfs_inode;
	spinlock_t		lock;
};

/* msg.c */
extern void lanyfs_debug_function(const char *, const char *);
extern void lanyfs_debug_ts(const char *, struct lanyfs_ts *);
extern void lanyfs_debug_block(union lanyfs_b *);

/* misc.c */
extern void lanyfs_time_lts_now(struct lanyfs_ts *);
extern void lanyfs_time_kts_to_lts(struct timespec *, struct lanyfs_ts *);
extern void lanyfs_time_lts_to_kts(struct lanyfs_ts *, struct timespec *);
extern void lanyfs_time_poke_inode(struct inode *);
extern void lanyfs_time_sync_inode(struct inode *);
extern umode_t lanyfs_attr_to_mode(struct super_block *, u16, umode_t);
extern u16 lanyfs_mode_to_attr(mode_t, u16);

/* icache.c */
extern struct lanyfs_lii *LANYFS_I(struct inode *);
extern int lanyfs_inodecache_init(void);
extern void lanyfs_inodecache_destroy(void);
extern struct inode *lanyfs_alloc_inode(struct super_block *);
extern void lanyfs_destroy_inode(struct inode *);

/* inode.c */
extern void lanyfs_inode_poke(struct inode *inode);
extern void lanyfs_inode_rename(struct inode *inode, const char *name);
extern struct inode *lanyfs_iget(struct super_block *sb, lanyfs_blk_t ino);
extern struct dentry *lanyfs_lookup(struct inode *dir, struct dentry *dentry,
				    unsigned int flags);
extern int lanyfs_write_inode(struct inode *inode,
			      struct writeback_control *wbc);

/* btree.c */
extern int lanyfs_btree_add_inode(struct inode *dir, struct inode *rookie);
extern int lanyfs_btree_del_inode(struct inode *dir, const char *name);
extern struct inode *lanyfs_btree_lookup(struct inode *dir, const char *name);
extern void lanyfs_btree_clear_inode(struct inode *inode);

/* dir.c */
extern const struct file_operations lanyfs_dir_operations;
extern const struct inode_operations lanyfs_dir_inode_operations;

/* extender.c */
extern int lanyfs_ext_iblock(struct super_block *sb, lanyfs_blk_t addr,
			     lanyfs_blk_t iblock, lanyfs_blk_t *res);
extern int lanyfs_ext_truncate(struct super_block *sb, lanyfs_blk_t addr,
			       lanyfs_blk_t iblock);
extern lanyfs_blk_t lanyfs_ext_create(struct super_block *sb,
				      unsigned short level);
extern int lanyfs_ext_grow(struct super_block *sb, lanyfs_blk_t *addr);

/* file.c */
extern const struct address_space_operations lanyfs_address_space_operations;
extern const struct file_operations lanyfs_file_operations;

/* chain.c */
extern int lanyfs_chain_set_next(struct super_block *sb, lanyfs_blk_t addr,
				 lanyfs_blk_t next);
extern int lanyfs_chain_create(struct super_block *sb, lanyfs_blk_t addr);
extern int lanyfs_chain_pop(struct super_block *sb, lanyfs_blk_t addr,
			    lanyfs_blk_t *res);
extern int lanyfs_chain_push(struct super_block *sb, lanyfs_blk_t addr,
			     lanyfs_blk_t rookie);

/* super.c */
extern struct lanyfs_fsi *LANYFS_SB(struct super_block *sb);
extern lanyfs_blk_t lanyfs_enslave(struct super_block *sb);
extern int lanyfs_release(struct super_block *sb, lanyfs_blk_t addr);
extern const struct super_operations lanyfs_super_operations;
extern struct file_system_type lanyfs_file_system_type;

#endif /* __LANYFS_KM_H_ */
