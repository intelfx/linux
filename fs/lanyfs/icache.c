/*
 * icache.c - Lanyard Filesystem Inode Cache
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
 * DOC: LanyFS Inode Cache
 *
 * LanyFS uses the Kernel's slab cache API for maintaining a common cache for
 * VFS inodes and LanyFS inode private data.
 */

/* inode cache pointer */
static struct kmem_cache *lanyfs_inode_cachep;

/**
 * LANYFS_I() - Returns pointer to inode's private data.
 * @inode:			vfs inode
 */
struct lanyfs_lii *LANYFS_I(struct inode *inode)
{
	lanyfs_debug_function(__FILE__, __func__);

	return list_entry(inode, struct lanyfs_lii, vfs_inode);
}

/**
 * lanyfs_inodecache_kcminit() - Initializes an inode cache element.
 *
 * This function has to take care of initializing the inode pointed to by
 * vfs_inode! Also, this is not the inodecache initialization function, only
 * single elements are initialzed here.
 *
 * @ptr:			pointer to inode private data
 */
static void lanyfs_inodecache_kmcinit(void *ptr)
{
	lanyfs_debug_function(__FILE__, __func__);

	inode_init_once(&((struct lanyfs_lii *) ptr)->vfs_inode);
}

/**
 * lanyfs_inodecache_init() - Initializes the inode cache.
 *
 * If compiled with debug enabled, the cache is initialized with special flags
 * set. Mostly to catch references to uninitialized memory and to check for
 * buffer overruns.
 */
int lanyfs_inodecache_init(void)
{
	lanyfs_debug_function(__FILE__, __func__);

	lanyfs_inode_cachep = kmem_cache_create("lanyfs_inode_cache",
		sizeof(struct lanyfs_lii),
		0,
#ifdef LANYFS_DEBUG
		(SLAB_RED_ZONE | SLAB_POISON),
#else
		0,
#endif /* LANYFS_DEBUG */
		lanyfs_inodecache_kmcinit);
	if (!lanyfs_inode_cachep)
		return -ENOMEM;
	return 0;
}

/**
 * lanyfs_inodecache_destroy() - Destroys the inode cache.
 */
void lanyfs_inodecache_destroy(void)
{
	lanyfs_debug_function(__FILE__, __func__);

	kmem_cache_destroy(lanyfs_inode_cachep);
}

/**
 * lanyfs_alloc_inode() - Allocates an inode using the inode cache.
 * @sb:				superblock
 */
struct inode *lanyfs_alloc_inode(struct super_block *sb)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = kmem_cache_alloc(lanyfs_inode_cachep, GFP_NOFS);
	if (!lii)
		return NULL;
	spin_lock_init(&lii->lock);
	return &lii->vfs_inode;
}

/**
 * lanyfs_destroy_inode() - Removes an inode from inode cache.
 * @inode:			inode
 */
void lanyfs_destroy_inode(struct inode *inode)
{
	struct lanyfs_lii *lii;
	lanyfs_debug_function(__FILE__, __func__);

	lii = LANYFS_I(inode);
	kmem_cache_free(lanyfs_inode_cachep, lii);
}
