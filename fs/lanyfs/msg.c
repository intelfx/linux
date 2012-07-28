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
 * lanyfs_debug_function() - Prints the currents function name and file.
 * @file:			file name
 * @func:			function name
 *
 * Produces call traces that help debugging a lot.
 */
void lanyfs_debug_function(const char *file, const char *func)
{
	/* reverse order of arguments is intended */
	lanyfs_debug("%s (%s)", func, file);
}

/**
 * lanyfs_debug_ts() - Prints human readable LanyFS timestamp.
 * @lts:			timestamp
 * @desc:			description
 */
void lanyfs_debug_ts(const char *desc, struct lanyfs_ts *lts)
{
	lanyfs_debug("%s=%04u-%02u-%02uT%02u:%02u:%02u.%u%+03d:%02ld",
		     desc, le16_to_cpu(lts->year), lts->mon, lts->day,
		     lts->hour, lts->min, lts->sec, lts->nsec,
		     lts->offset / 60, abs(lts->offset % 60));
}

/**
 * lanyfs_debug_block() - Prints block's type and content.
 * @b:				block
 *
 * This is probably the most useful debug function. Use it to dump blocks
 * whenever you are unsure of its contents. It will slow down the
 * filesystem, though.
 */
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
