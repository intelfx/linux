/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/include/linux/amd-pstate.h
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 *
 * Author: Meng Li <li.meng@amd.com>
 */

#ifndef _LINUX_AMD_PSTATE_H
#define _LINUX_AMD_PSTATE_H

#include <linux/pm_qos.h>

/*********************************************************************
 *                        AMD P-state INTERFACE                       *
 *********************************************************************/
/**
 * struct  amd_aperf_mperf
 * @aperf: actual performance frequency clock count
 * @mperf: maximum performance frequency clock count
 * @tsc:   time stamp counter
 */
struct amd_aperf_mperf {
	u64 aperf;
	u64 mperf;
	u64 tsc;
	u64 time;
};

/**
 * struct amd_cpudata - private CPU data for AMD P-State
 * @cpu: CPU number
 * @req: constraint request to apply
 * @cppc_req_cached: cached performance request hints
 * @highest_perf: the maximum performance an individual processor may reach,
 *		  assuming ideal conditions
 * @nominal_perf: the maximum sustained performance level of the processor,
 *		  assuming ideal operating conditions
 * @lowest_nonlinear_perf: the lowest performance level at which nonlinear power
 *			   savings are achieved
 * @lowest_perf: the absolute lowest performance level of the processor
 * @max_freq: the frequency that mapped to highest_perf
 * @min_freq: the frequency that mapped to lowest_perf
 * @nominal_freq: the frequency that mapped to nominal_perf
 * @lowest_nonlinear_freq: the frequency that mapped to lowest_nonlinear_perf
 * @cur: Difference of Aperf/Mperf/tsc count between last and current sample
 * @prev: Last Aperf/Mperf/tsc count value read from register
 * @freq: current cpu frequency value
 * @boost_supported: check whether the Processor or SBIOS supports boost mode
 * @epp_powersave: Last saved CPPC energy performance preference
				when policy switched to performance
 * @epp_policy: Last saved policy used to set energy-performance preference
 * @epp_cached: Cached CPPC energy-performance preference value
 * @policy: Cpufreq policy value
 * @sched_flags: Store scheduler flags for possible cross CPU update
 * @update_util_set: CPUFreq utility callback is set
 * @last_update: Time stamp of the last performance state update
 * @cppc_boost_min: Last CPPC boosted min performance state
 * @cppc_cap1_cached: Cached value of the last CPPC Capabilities MSR
 * @update_util: Cpufreq utility callback information
 * @sample: the stored performance sample
 *
 * The amd_cpudata is key private data for each CPU thread in AMD P-State, and
 * represents all the attributes and goals that AMD P-State requests at runtime.
 */
struct amd_cpudata {
	int	cpu;

	struct	freq_qos_request req[2];
	u64	cppc_req_cached;

	u32	highest_perf;
	u32	nominal_perf;
	u32	lowest_nonlinear_perf;
	u32	lowest_perf;

	u32	max_freq;
	u32	min_freq;
	u32	nominal_freq;
	u32	lowest_nonlinear_freq;

	struct amd_aperf_mperf cur;
	struct amd_aperf_mperf prev;

	u64	freq;
	bool	boost_supported;

	/* EPP feature related attributes*/
	s16	epp_powersave;
	s16	epp_policy;
	s16	epp_cached;
	u32	policy;
	u32	sched_flags;
	bool	update_util_set;
	u64	last_update;
	u64	last_io_update;
	u32	cppc_boost_min;
	u64	cppc_cap1_cached;
	struct	update_util_data update_util;
	struct	amd_aperf_mperf sample;
	bool suspended;
};

/**
 * struct amd_pstate_params - global parameters for the performance control
 * @ cppc_boost_disabled wheher the core performance boost disabled
 */
struct amd_pstate_params {
	bool cppc_boost_disabled;
};

#endif /* _LINUX_AMD_PSTATE_H */
