/*
 * lanyfs_km.c - Lanyard Filesystem Kernel Module
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
 * lanyfs_init() - Initialize LanyFS module.
 *
 * Initializes the inode cache and registers the filesystem.
 */
static int __init lanyfs_init(void)
{
	int err;
	lanyfs_debug_function(__FILE__, __func__);

	pr_info("lanyfs: register filesystem\n");
	lanyfs_debug("debug=enabled");
	err = lanyfs_inodecache_init();
	if (err)
		goto exit_err;
	err = register_filesystem(&lanyfs_file_system_type);
	if (err)
		goto exit_ino;
	return 0;

exit_ino:
	lanyfs_inodecache_destroy();
exit_err:
	pr_err("lanyfs: register filesystem failed\n");
	return err;
}

/**
 * lanyfs_exit() - Exit LanyFS module.
 *
 * Takes care of destroying inode cache and unregistering the filesystem.
 */
static void __exit lanyfs_exit(void)
{
	lanyfs_debug_function(__FILE__, __func__);
	pr_info("lanyfs: unregister filesystem\n");

	lanyfs_inodecache_destroy();
	unregister_filesystem(&lanyfs_file_system_type);
}

module_init(lanyfs_init);
module_exit(lanyfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Luedtke <mail@danrl.de>");
MODULE_DESCRIPTION("Lanyard Filesystem (LanyFS)");
