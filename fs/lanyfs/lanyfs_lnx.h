/*
 * lanyfs_lnx.h - Lanyard Filesystem Header for Linux Kernel
 *
 * Copyright (C) 2012  Dan Luedtke <mail@danrl.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __LANYFS_LNX_H_
#define __LANYFS_LNX_H_

/* filesystem version */
#define LANYFS_MAJOR_VERSION	1
#define LANYFS_MINOR_VERSION	4

/* important numbers */
#define LANYFS_SUPERBLOCK	0	/* address of on-disk superblock */
#define LANYFS_MIN_ADDRLEN	1	/* mimimun address length (in bytes) */
#define LANYFS_MAX_ADDRLEN	8	/* maximum address length (in bytes) */
#define LANYFS_MIN_BLOCKSIZE	9	/* mimimun blocksize 2**9 */
#define LANYFS_MAX_BLOCKSIZE	12	/* maximum blocksize 2**12 */
#define LANYFS_NAME_LENGTH	256	/* maximum length of label/name */

/* block type identifiers */
#define LANYFS_TYPE_DIR		0x10
#define LANYFS_TYPE_FILE	0x20
#define LANYFS_TYPE_CHAIN	0x70
#define LANYFS_TYPE_EXT		0x80
#define LANYFS_TYPE_SB		0xD0

/* directory and file attributes */
#define LANYFS_ATTR_NOWRITE	(1<<0)
#define LANYFS_ATTR_NOEXEC	(1<<1)
#define LANYFS_ATTR_HIDDEN	(1<<2)
#define LANYFS_ATTR_ARCHIVE	(1<<3)

/**
 * struct lanyfs_ts - ISO8601-like LanyFS timestamp
 * @year:			gregorian year (0 to 9999)
 * @mon:			month of year (1 to 12)
 * @day:			day of month (1 to 31)
 * @hour:			hour of day (0 to 23)
 * @min:			minute of hour (0 to 59)
 * @sec:			second of minute (0 to 59 normal, 0 to 60 if
 *				leap second)
 * @__reserved_0:		reserved
 * @nsec:			nanosecond (0 to 10^9)
 * @offset:			signed UTC offset in minutes
 * @__reserved_1:		reserved
 */
struct lanyfs_ts {
	__le16			year;
	__u8			mon;
	__u8			day;
	__u8			hour;
	__u8			min;
	__u8			sec;
	unsigned char		__reserved_0[1];
	__le32			nsec;
	__s16			offset;
	unsigned char		__reserved_1[2];
};

/**
 * struct lanyfs_raw - LanyFS raw block
 * @type:			identifies the blocks purpose
 * @__reserved_0:		reserved
 * @wrcnt:			write counter
 * @data:			first byte of data
 */
struct lanyfs_raw {
	__u8			type;
	unsigned char		__reserved_0;
	__le16			wrcnt;
	unsigned char		data;
};

/**
 * struct lanyfs_sb - LanyFS superblock
 * @type:			identifies the blocks purpose
 * @__reserved_0:		reserved
 * @wrcnt:			write counter
 * @major:			major version of filesystem
 * @__reserved_1:		reserved
 * @minor:			minor version of filesystem
 * @__reserved_2:		reserved
 * @magic:			identifies the filesystem
 * @blocksize:			blocksize (exponent to base 2)
 * @__reserved_3:		reserved
 * @addrlen:			length of block addresses in bytes
 * @__reserved_4:		reserved
 * @rootdir:			address of root directory block
 * @blocks:			number of blocks on the device
 * @freehead:			start of free blocks chain
 * @freetail:			end of free blocks chain
 * @freeblocks:			number of free blocks
 * @created:			date and time of filesystem creation
 * @updated:			date and time of last superblock field change
 * @checked:			date and time of last successful filesystem
 *				check
 * @badblocks:			start of bad blocks chain
 * @__reserved_5:		reserved
 * @label:			optional label for the filesystem
 */
struct lanyfs_sb {
	__u8			type;
	unsigned char		__reserved_0;
	__le16			wrcnt;
	__le32			magic;
	__u8			major;
	unsigned char		__reserved_1;
	__u8			minor;
	unsigned char		__reserved_2;
	__u8			blocksize;
	unsigned char		__reserved_3;
	__u8			addrlen;
	unsigned char		__reserved_4;
	__le64			rootdir;
	__le64			blocks;
	__le64			freehead;
	__le64			freetail;
	__le64			freeblocks;
	struct lanyfs_ts	created;
	struct lanyfs_ts	updated;
	struct lanyfs_ts	checked;
	__le64			badblocks;
	unsigned char		__reserved_5[8];
	char			label[LANYFS_NAME_LENGTH];
};

/**
 * struct lanyfs_btree - binary tree components
 * @left:			address of left node of binary tree
 * @right:			address of right node of binary tree
 */
struct lanyfs_btree {
	__le64			left;
	__le64			right;
};

/**
 * struct lanyfs_vi_btree - aligned binary tree components
 * @__padding_0:		padding
 * @left:			address of left node of binary tree
 * @right:			address of right node of binary tree
 *
 * Used to access binary tree components independent from underlying block type.
 * This creates a virtual block.
 */
struct lanyfs_vi_btree	{
	unsigned char		__padding_0[8];
	__le64			left;
	__le64			right;
};

/**
 * struct lanyfs_meta - lanyfs metadata
 * @created:			date and time of creation
 * @modified:			date and time of last modification
 * @__reserved_0:		reserved
 * @attr:			directory or file attributes
 * @name:			name of file or directory
 */
struct lanyfs_meta {
	struct lanyfs_ts	created;
	struct lanyfs_ts	modified;
	unsigned char		__reserved_0[14];
	__le16			attr;
	char			name[LANYFS_NAME_LENGTH];
};

/**
 * struct lanyfs_vi_meta - aligned lanyfs metadata
 * @__padding_0:		padding
 * @created:			date and time of creation
 * @modified:			date and time of last modification
 * @__reserved_0:		reserved
 * @attr:			directory or file attributes
 * @name:			name of file or directory
 *
 * Used to access meta data independent from underlying block type.
 * This creates a virtual block.
 */
struct lanyfs_vi_meta	{
	unsigned char		__padding_0[56];
	struct lanyfs_ts	created;
	struct lanyfs_ts	modified;
	unsigned char		__reserved_0[14];
	__le16			attr;
	unsigned char		name[LANYFS_NAME_LENGTH];
};

/**
 * struct lanyfs_dir - LanyFS directory block
 * @type:			identifies the blocks purpose
 * @__reserved_0:		reserved
 * @wrcnt:			write counter
 * @__reserved_1:		reserved
 * @btree:			binary tree components
 * @subtree:			binary tree root of directory's contents
 * @__reserved_2:		reserved
 * @meta:			directory metadata
 */
struct lanyfs_dir {
	__u8			type;
	unsigned char		__reserved_0;
	__le16			wrcnt;
	unsigned char		__reserved_1[4];
	struct lanyfs_btree	btree;
	__le64			subtree;
	unsigned char		__reserved_2[24];
	struct lanyfs_meta	meta;
};

/**
 * struct lanyfs_file - LanyFS file block
 * @type:			identifies the blocks purpose
 * @__reserved_0:		reserved
 * @wrcnt:			write counter
 * @__reserved_1:		reserved
 * @btree:			binary tree components
 * @data:			address of extender for data blocks
  * @size:			size of file in bytes
 * @__reserved_2:		reserved
 * @meta:			file metadata
 */
struct lanyfs_file {
	__u8			type;
	unsigned char		__reserved_0;
	__le16			wrcnt;
	unsigned char		__reserved_1[4];
	struct lanyfs_btree	btree;
	__le64			data;
	__le64			size;
	unsigned char		__reserved_2[16];
	struct lanyfs_meta	meta;
};

/**
 * struct lanyfs_chain - LanyFS chain block (size-independent)
 * @type:			identifies the blocks purpose
 * @__reserved_0:		reserved
 * @wrcnt:			write counter
 * @__reserved_1:		reserved
 * @next:			address of next chain block
 * @stream:			start of block address stream
 */
struct lanyfs_chain {
	__u8			type;
	unsigned char		__reserved_0;
	__le16			wrcnt;
	unsigned char		__reserved_1[4];
	__le64			next;
	unsigned char		stream;
};

/**
 * struct lanyfs_ext - LanyFS extender block (size-independent)
 * @type:			identifies the blocks purpose
 * @__reserved_0:		reserved
 * @wrcnt:			write counter
 * @level:			depth of indirection
 * @stream:			start of block address stream
 */
struct lanyfs_ext {
	__u8			type;
	unsigned char		__reserved_0;
	__le16			wrcnt;
	__u8			level;
	unsigned char		stream;
};

/**
 * struct lanyfs_data - LanyFS data block
 * @stream:			start of data stream
 */
struct lanyfs_data {
	unsigned char		stream;
};

/** union lanyfs_b - lanyfs block
 * @raw:			raw block
 * @sb:				superblock
 * @dir:			directory block
 * @file:			file block
 * @ext:			extender block
 * @data:			data block
 * @vi_btree:			binary tree virtual block
 * @vi_meta:			metadata virtual block
 */
union lanyfs_b {
	struct lanyfs_raw	raw;
	struct lanyfs_sb	sb;
	struct lanyfs_dir	dir;
	struct lanyfs_file	file;
	struct lanyfs_chain	chain;
	struct lanyfs_ext	ext;
	struct lanyfs_data	data;
	struct lanyfs_vi_btree	vi_btree;
	struct lanyfs_vi_meta	vi_meta;
};

#endif /* __LANYFS_LNX_H */
