/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/include/linux/cpufreq_common.h
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 *
 * Author: Perry Yuan <Perry.Yuan@amd.com>
 */

#ifndef _LINUX_CPUFREQ_COMMON_H
#define _LINUX_CPUFREQ_COMMON_H
/*
 * EPP/EPB display strings corresponding to EPP index in the
 * energy_perf_strings[]
 *	index		String
 *-------------------------------------
 *	0		default
 *	1		performance
 *	2		balance_performance
 *	3		balance_power
 *	4		power
 */

#define HWP_EPP_PERFORMANCE		0x00
#define HWP_EPP_BALANCE_PERFORMANCE	0x80
#define HWP_EPP_BALANCE_POWERSAVE	0xC0
#define HWP_EPP_POWERSAVE		0xFF

enum energy_perf_value_index {
	EPP_INDEX_DEFAULT = 0,
	EPP_INDEX_PERFORMANCE,
	EPP_INDEX_BALANCE_PERFORMANCE,
	EPP_INDEX_BALANCE_POWERSAVE,
	EPP_INDEX_POWERSAVE,
};

static const char * const energy_perf_strings[] = {
	[EPP_INDEX_DEFAULT] = "default",
	[EPP_INDEX_PERFORMANCE] = "performance",
	[EPP_INDEX_BALANCE_PERFORMANCE] = "balance_performance",
	[EPP_INDEX_BALANCE_POWERSAVE] = "balance_power",
	[EPP_INDEX_POWERSAVE] = "power",
	NULL
};

static unsigned int epp_values[] = {
	[EPP_INDEX_DEFAULT] = 0, /* Unused index */
	[EPP_INDEX_PERFORMANCE] = HWP_EPP_PERFORMANCE,
	[EPP_INDEX_BALANCE_PERFORMANCE] = HWP_EPP_BALANCE_PERFORMANCE,
	[EPP_INDEX_BALANCE_POWERSAVE] = HWP_EPP_BALANCE_POWERSAVE,
	[EPP_INDEX_POWERSAVE] = HWP_EPP_POWERSAVE,
};
#endif /* _LINUX_CPUFREQ_COMMON_H */
