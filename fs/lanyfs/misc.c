/*
 * misc.c - Lanyard Filesystem Miscellaneous Operations
 *
 * Copyright (C) 2012  Dan Luedtke <mail@danrl.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "lanyfs_km.h"

/* --- time ----------------------------------------------------------------- */

/**
 * lanyfs_time_lts_to_kts() - Converts LanyFS timestamp to Kernel timespec.
 * @lts:			lanyfs timestamp (source)
 * @kts:			kernel timespec (target)
 *
 * WARNING: This function will overflow on 2106-02-07 06:28:16 on
 * machines where long is only 32-bit! Replace mktime() before that date!
 */
void lanyfs_time_lts_to_kts(struct lanyfs_ts *lts, struct timespec *kts)
{
	kts->tv_sec = mktime(le16_to_cpu(lts->year), lts->mon, lts->day,
			     lts->hour, lts->min, lts->sec);
	kts->tv_sec += le16_to_cpu(lts->offset) * 60;
	kts->tv_nsec = le32_to_cpu(lts->nsec);
}

/**
 * lanyfs_time_kts_to_lts() - Converts Kernel timespec to LanyFS timestamp.
 * @kts:			kernel timespec (source)
 * @lts:			lanyfs timestamp (target)
 *
 * Depends on global variable 'sys_tz' of type 'timezone'.
 */
void lanyfs_time_kts_to_lts(struct timespec *kts, struct lanyfs_ts *lts)
{
	struct tm tm;
	lanyfs_debug_function(__FILE__, __func__);

	time_to_tm(kts->tv_sec, 0, &tm);
	lts->year = cpu_to_le16(tm.tm_year + 1900);
	lts->mon = tm.tm_mon + 1;
	lts->day = tm.tm_mday;
	lts->hour = tm.tm_hour;
	lts->min = tm.tm_min;
	lts->sec = tm.tm_sec;
	lts->nsec = cpu_to_le32(kts->tv_nsec);
	lts->offset = cpu_to_le16(sys_tz.tz_minuteswest * -1);
}

/**
 * lanyfs_time_lts_now() - Convert current time to LanyFS timestamp.
 * @lts:			lanyfs timestamp (target)
 */
void lanyfs_time_lts_now(struct lanyfs_ts *lts)
{
	struct timespec now;
	lanyfs_debug_function(__FILE__, __func__);

	now = current_kernel_time();
	lanyfs_time_kts_to_lts(&now, lts);
}

/**
 * lanyfs_time_sync_inode() - Syncs inode's timestamps.
 * @inode:			inode to sync
 *
 * All times (atime, mtime, ctime) will be set to the latest timestamp.
 */
void lanyfs_time_sync_inode(struct inode *inode)
{
	if (!inode)
		return;
	if (timespec_compare(&inode->i_mtime, &inode->i_ctime) > 0)
		inode->i_ctime = inode->i_mtime;
	else
		inode->i_mtime = inode->i_ctime;
	if (timespec_compare(&inode->i_atime, &inode->i_mtime) > 0)
		inode->i_mtime = inode->i_ctime = inode->i_atime;
	else
		inode->i_atime = inode->i_mtime;
}

/* --- mode ----------------------------------------------------------------- */

/**
 * lanyfs_attr_to_mode() - Converts LanyFS metadata attributes to unix mode.
 * @sb:				superblock
 * @attr:			lanyfs metadata attributes
 * @t:				type of file (S_IFDIR, S_IFREG)
 */
umode_t lanyfs_attr_to_mode(struct super_block *sb, u16 attr, umode_t t)
{
	umode_t mode;

	mode = S_IRWXUGO;
	if (attr & LANYFS_ATTR_NOWRITE)
		mode &= ~S_IWUGO;
	if (attr & LANYFS_ATTR_NOEXEC)
		mode &= ~S_IXUGO;
	if (t == S_IFDIR)
		mode &= ~LANYFS_SB(sb)->opts.dmask;
	else if (t == S_IFREG)
		mode &= ~LANYFS_SB(sb)->opts.fmask;
	return mode | t;
}

/**
 * lanyfs_mode_to_attr() - Convert unix mode to LanyFS metadata attributes.
 * @mode:			mode to convert
 * @base:			lanyfs metadata attributes to preserve
 */
inline u16 lanyfs_mode_to_attr(mode_t mode, u16 base)
{
	if (!(mode & S_IWUGO))
		base |= LANYFS_ATTR_NOWRITE;
	if (!(mode & S_IXUGO))
		base |= LANYFS_ATTR_NOEXEC;
	return base;
}
