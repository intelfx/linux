/*
 * msg.c - Lanyard Filesystem Log Message Handling
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
 * lanyfs_msg() - Throws messages out via printk.
 * @sb:				superblock
 * @prefix:			kernel message prefix (e.g. KERN_INFO)
 * @fmt:			format string of message
 */
void lanyfs_msg(struct super_block *sb, const char *prefix,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_info("%sLANYFS (%s): %pV\n", prefix, sb->s_id, &vaf);
	va_end(args);
}

/**
 * lanyfs_debug() - Throws debug messages out via printk.
 * @fmt:			format string of message
 */
#ifdef LANYFS_DEBUG
void lanyfs_debug(const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	pr_debug("LANYFS: %pV\n", &vaf);
	va_end(args);
}
#else
void lanyfs_debug(const char *fmt, ...) { }
#endif /* LANYFS_DEBUG */

/**
 * lanyfs_debug_function() - Prints current function's name and file.
 * @file:			file name
 * @func:			function name
 */
#ifdef LANYFS_DEBUG
void lanyfs_debug_function(const char *file, const char *func)
{
	/* yes, the order is not consistent */
	lanyfs_debug("function %s (%s)", func, file);
}
#else
void lanyfs_debug_function(const char *file, const char *func) { }
#endif /* LANYFS_DEBUG */

/**
 * lanyfs_debug_ts() - Prints human readable LanyFS timestamp.
 * @lts:			timestamp
 * @desc:			description
 */
#ifdef LANYFS_DEBUG
void lanyfs_debug_ts(const char *desc, struct lanyfs_ts *lts)
{
	lanyfs_debug("%s=%04u-%02u-%02uT%02u:%02u:%02u.%u%+03d:%02d",
		     desc, le16_to_cpu(lts->year), lts->mon, lts->day,
		     lts->hour, lts->min, lts->sec, lts->nsec,
		     lts->offset / 60, abs(lts->offset % 60));
}
#else
void lanyfs_debug_ts(const char *desc, struct lanyfs_ts *lts) { }
#endif /* LANYFS_DEBUG */

/**
 * lanyfs_debug_block() - Prints block's type and content.
 * @b:				block
 *
 * This is probably the most useful debug function. Use it to dump blocks
 * whenever you are unsure of its contents. It will slow down the
 * filesystem, though.
 */
#ifdef LANYFS_DEBUG
void lanyfs_debug_block(union lanyfs_b *b)
{
	lanyfs_debug("dumping block at %p", b);
	lanyfs_debug("type=0x%x", b->raw.type);
	lanyfs_debug("wrcnt=%u", le16_to_cpu(b->raw.wrcnt));
	/* sb */
	if (b->raw.type == LANYFS_TYPE_SB) {
		lanyfs_debug("magic=0x%x", le32_to_cpu(b->sb.magic));
		lanyfs_debug("major_version=%u", b->sb.major);
		lanyfs_debug("minor_version=%u", b->sb.minor);
		lanyfs_debug("address_length=%u", b->sb.addrlen);
		lanyfs_debug("blocksize=%u", b->sb.blocksize);
		lanyfs_debug("root_directory=%llu", le64_to_cpu(b->sb.rootdir));
		lanyfs_debug("total_blocks=%llu", le64_to_cpu(b->sb.blocks));
		lanyfs_debug("free_head=%llu", le64_to_cpu(b->sb.freehead));
		lanyfs_debug("free_tail=%llu", le64_to_cpu(b->sb.freetail));
		lanyfs_debug("free_blocks=%llu", le64_to_cpu(b->sb.freeblocks));
		lanyfs_debug_ts("created", &b->sb.created);
		lanyfs_debug_ts("checked", &b->sb.checked);
		lanyfs_debug_ts("updated", &b->sb.updated);
		lanyfs_debug("volume_label=%s", b->sb.label);
	}
	/* chain */
	if (b->raw.type == LANYFS_TYPE_CHAIN)
		lanyfs_debug("next=%llu", le64_to_cpu(b->chain.next));
	/* extender */
	if (b->raw.type == LANYFS_TYPE_EXT)
		lanyfs_debug("level=%u", b->ext.level);
	/* btree */
	if (b->raw.type == LANYFS_TYPE_DIR ||
	    b->raw.type == LANYFS_TYPE_FILE) {
		lanyfs_debug("btree_left=%llu", le64_to_cpu(b->vi_btree.left));
		lanyfs_debug("btree_right=%llu",
			     le64_to_cpu(b->vi_btree.right));
	}
	/* file */
	if (b->raw.type == LANYFS_TYPE_FILE) {
		lanyfs_debug("data=%llu", le64_to_cpu(b->file.data));
		lanyfs_debug("size=%llu", le64_to_cpu(b->file.size));
	}
	/* dir */
	if (b->raw.type == LANYFS_TYPE_DIR)
		lanyfs_debug("subtree=%llu", le64_to_cpu(b->dir.subtree));
	/* meta */
	if (b->raw.type == LANYFS_TYPE_DIR ||
	    b->raw.type == LANYFS_TYPE_FILE) {
		lanyfs_debug_ts("meta_created", &b->vi_meta.created);
		lanyfs_debug_ts("meta_modified", &b->vi_meta.modified);
		lanyfs_debug("meta_attr=%u", le16_to_cpu(b->vi_meta.attr));
		lanyfs_debug("meta_name=%s", b->vi_meta.name);
	}
}
#else
void lanyfs_debug_block(union lanyfs_b *b) { }
#endif /* LANYFS_DEBUG */
