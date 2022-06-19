// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic show_mem() implementation
 *
 * Copyright (C) 2008 Johannes Weiner <hannes@saeurebad.de>
 */

#include <linux/mm.h>
#include <linux/cma.h>
#include <linux/printbuf.h>

#include "slab.h"

static DEFINE_MUTEX(show_mem_buf_lock);
static char show_mem_buf[4096];

void show_mem(unsigned int filter, nodemask_t *nodemask)
{
	pg_data_t *pgdat;
	unsigned long total = 0, reserved = 0, highmem = 0;

	printk("Mem-Info:\n");
	show_free_areas(filter, nodemask);

	for_each_online_pgdat(pgdat) {
		int zoneid;

		for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++) {
			struct zone *zone = &pgdat->node_zones[zoneid];
			if (!populated_zone(zone))
				continue;

			total += zone->present_pages;
			reserved += zone->present_pages - zone_managed_pages(zone);

			if (is_highmem_idx(zoneid))
				highmem += zone->present_pages;
		}
	}

	printk("%lu pages RAM\n", total);
	printk("%lu pages HighMem/MovableOnly\n", highmem);
	printk("%lu pages reserved\n", reserved);
#ifdef CONFIG_CMA
	printk("%lu pages cma reserved\n", totalcma_pages);
#endif
#ifdef CONFIG_MEMORY_FAILURE
	printk("%lu pages hwpoisoned\n", atomic_long_read(&num_poisoned_pages));
#endif
	if (mutex_trylock(&show_mem_buf_lock)) {
		struct printbuf buf = PRINTBUF_EXTERN(show_mem_buf, sizeof(show_mem_buf));

		pr_info("Unreclaimable slab info:\n");
		dump_unreclaimable_slab(&buf);
		printk("%s", printbuf_str(&buf));
		printbuf_reset(&buf);

		printk("Shrinkers:\n");
		shrinkers_to_text(&buf);
		printk("%s", printbuf_str(&buf));
	}
}
