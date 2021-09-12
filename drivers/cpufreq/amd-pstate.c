// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * amd-pstate.c - AMD Processor P-state Frequency Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Author: Huang Rui <ray.huang@amd.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/compiler.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include <acpi/processor.h>
#include <acpi/cppc_acpi.h>

#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/cpu_device_id.h>

#define AMD_PSTATE_TRANSITION_LATENCY	0x20000
#define AMD_PSTATE_TRANSITION_DELAY	500

static struct cpufreq_driver amd_pstate_driver;

struct amd_cpudata {
	int	cpu;

	struct freq_qos_request req[2];
	struct cpufreq_policy *policy;

	u64	cppc_req_cached;

	u32	highest_perf;
	u32	nominal_perf;
	u32	lowest_nonlinear_perf;
	u32	lowest_perf;

	u32	max_freq;
	u32	min_freq;
	u32	nominal_freq;
	u32	lowest_nonlinear_freq;
};

struct amd_pstate_perf_funcs {
	int (*enable)(bool enable);
	int (*init_perf)(struct amd_cpudata *cpudata);
	void (*update_perf)(struct amd_cpudata *cpudata,
			    u32 min_perf, u32 des_perf,
			    u32 max_perf, bool fast_switch);
};

static inline int pstate_enable(bool enable)
{
	return wrmsrl_safe(MSR_AMD_CPPC_ENABLE, enable ? 1 : 0);
}

static int cppc_enable(bool enable)
{
	int cpu, ret = 0;

	for_each_online_cpu(cpu) {
		ret = cppc_set_enable(cpu, enable ? 1 : 0);
		if (ret)
			return ret;
	}

	return ret;
}

static int
amd_pstate_enable(struct amd_pstate_perf_funcs *funcs, bool enable)
{
	if (!funcs)
		return -EINVAL;

	return funcs->enable(enable);
}

static int pstate_init_perf(struct amd_cpudata *cpudata)
{
	u64 cap1;

	int ret = rdmsrl_safe_on_cpu(cpudata->cpu, MSR_AMD_CPPC_CAP1,
				     &cap1);
	if (ret)
		return ret;

	/* Some AMD processors has specific power features that the cppc entry
	 * doesn't indicate the highest performance. It will introduce the
	 * feature in following days.
	 */
	WRITE_ONCE(cpudata->highest_perf, amd_get_highest_perf());

	WRITE_ONCE(cpudata->nominal_perf, CAP1_NOMINAL_PERF(cap1));
	WRITE_ONCE(cpudata->lowest_nonlinear_perf, CAP1_LOWNONLIN_PERF(cap1));
	WRITE_ONCE(cpudata->lowest_perf, CAP1_LOWEST_PERF(cap1));

	return 0;
}

static int cppc_init_perf(struct amd_cpudata *cpudata)
{
	struct cppc_perf_caps cppc_perf;

	int ret = cppc_get_perf_caps(cpudata->cpu, &cppc_perf);
	if (ret)
		return ret;

	WRITE_ONCE(cpudata->highest_perf, amd_get_highest_perf());

	WRITE_ONCE(cpudata->nominal_perf, cppc_perf.nominal_perf);
	WRITE_ONCE(cpudata->lowest_nonlinear_perf,
		   cppc_perf.lowest_nonlinear_perf);
	WRITE_ONCE(cpudata->lowest_perf, cppc_perf.lowest_perf);

	return 0;
}

static int amd_pstate_init_perf(struct amd_cpudata *cpudata)
{
	struct amd_pstate_perf_funcs *funcs = cpufreq_get_driver_data();

	if (!funcs)
		return -EINVAL;

	return funcs->init_perf(cpudata);
}

static void pstate_update_perf(struct amd_cpudata *cpudata,
			       u32 min_perf, u32 des_perf, u32 max_perf,
			       bool fast_switch)
{
	if (fast_switch)
		wrmsrl(MSR_AMD_CPPC_REQ, READ_ONCE(cpudata->cppc_req_cached));
	else
		wrmsrl_on_cpu(cpudata->cpu, MSR_AMD_CPPC_REQ,
			      READ_ONCE(cpudata->cppc_req_cached));
}

static void cppc_update_perf(struct amd_cpudata *cpudata,
			     u32 min_perf, u32 des_perf,
			     u32 max_perf, bool fast_switch)
{
	struct cppc_perf_ctrls perf_ctrls;

	perf_ctrls.max_perf = max_perf;
	perf_ctrls.min_perf = min_perf;
	perf_ctrls.desired_perf = des_perf;

	cppc_set_perf(cpudata->cpu, &perf_ctrls);
}

static int
amd_pstate_update_perf(struct amd_cpudata *cpudata, u32 min_perf,
		       u32 des_perf, u32 max_perf, bool fast_switch)
{
	struct amd_pstate_perf_funcs *funcs = cpufreq_get_driver_data();

	if (!funcs)
		return -EINVAL;

	funcs->update_perf(cpudata, min_perf, des_perf,
			   max_perf, fast_switch);

	return 0;
}

static int
amd_pstate_update(struct amd_cpudata *cpudata, u32 min_perf,
		  u32 des_perf, u32 max_perf, bool fast_switch)
{
	u64 prev = READ_ONCE(cpudata->cppc_req_cached);
	u64 value = prev;

	value &= ~REQ_MIN_PERF(~0L);
	value |= REQ_MIN_PERF(min_perf);

	value &= ~REQ_DES_PERF(~0L);
	value |= REQ_DES_PERF(des_perf);

	value &= ~REQ_MAX_PERF(~0L);
	value |= REQ_MAX_PERF(max_perf);

	if (value == prev)
		return 0;

	WRITE_ONCE(cpudata->cppc_req_cached, value);

	return amd_pstate_update_perf(cpudata, min_perf, des_perf,
				      max_perf, fast_switch);
}

static int amd_pstate_verify(struct cpufreq_policy_data *policy)
{
	cpufreq_verify_within_cpu_limits(policy);

	return 0;
}

static int amd_pstate_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	int ret;
	struct cpufreq_freqs freqs;
	struct amd_cpudata *cpudata = policy->driver_data;
	unsigned long amd_max_perf, amd_min_perf, amd_des_perf,
		      amd_cap_perf;

	if (!cpudata->max_freq)
		return -ENODEV;

	amd_cap_perf = READ_ONCE(cpudata->highest_perf);
	amd_min_perf = READ_ONCE(cpudata->lowest_nonlinear_perf);
	amd_max_perf = amd_cap_perf;

	freqs.old = policy->cur;
	freqs.new = target_freq;

	amd_des_perf = DIV_ROUND_CLOSEST(target_freq * amd_cap_perf,
					 cpudata->max_freq);

	cpufreq_freq_transition_begin(policy, &freqs);
	ret = amd_pstate_update(cpudata, amd_min_perf, amd_des_perf,
				amd_max_perf, false);
	cpufreq_freq_transition_end(policy, &freqs, false);

	return ret;
}

static void amd_pstate_adjust_perf(unsigned int cpu,
				   unsigned long min_perf,
				   unsigned long target_perf,
				   unsigned long capacity)
{
	unsigned long amd_max_perf, amd_min_perf, amd_des_perf,
		      amd_cap_perf, lowest_nonlinear_perf;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	struct amd_cpudata *cpudata = policy->driver_data;

	amd_cap_perf = READ_ONCE(cpudata->highest_perf);
	lowest_nonlinear_perf = READ_ONCE(cpudata->lowest_nonlinear_perf);

	if (target_perf < capacity)
		amd_des_perf = DIV_ROUND_UP(amd_cap_perf * target_perf,
					    capacity);

	amd_min_perf = READ_ONCE(cpudata->highest_perf);
	if (min_perf < capacity)
		amd_min_perf = DIV_ROUND_UP(amd_cap_perf * min_perf, capacity);

	if (amd_min_perf < lowest_nonlinear_perf)
		amd_min_perf = lowest_nonlinear_perf;

	amd_max_perf = amd_cap_perf;
	if (amd_max_perf < amd_min_perf)
		amd_max_perf = amd_min_perf;

	amd_des_perf = clamp_t(unsigned long, amd_des_perf,
			       amd_min_perf, amd_max_perf);

	amd_pstate_update(cpudata, amd_min_perf, amd_des_perf,
			  amd_max_perf, true);
}

static unsigned int amd_pstate_fast_switch(struct cpufreq_policy *policy,
					   unsigned int target_freq)
{
	u64 ratio;
	struct amd_cpudata *cpudata = policy->driver_data;
	unsigned long amd_max_perf, amd_min_perf, amd_des_perf, nominal_perf;

	if (!cpudata->max_freq)
		return -ENODEV;

	amd_max_perf = READ_ONCE(cpudata->highest_perf);
	amd_min_perf = READ_ONCE(cpudata->lowest_nonlinear_perf);

	amd_des_perf = DIV_ROUND_UP(target_freq * amd_max_perf,
				    cpudata->max_freq);

	amd_pstate_update(cpudata, amd_min_perf, amd_des_perf,
			  amd_max_perf, true);

	nominal_perf = READ_ONCE(cpudata->nominal_perf);
	ratio = div_u64(amd_des_perf << SCHED_CAPACITY_SHIFT, nominal_perf);

	return cpudata->nominal_freq * ratio >> SCHED_CAPACITY_SHIFT;
}

static int amd_get_min_freq(struct amd_cpudata *cpudata)
{
	struct cppc_perf_caps cppc_perf;

	int ret = cppc_get_perf_caps(cpudata->cpu, &cppc_perf);
	if (ret)
		return ret;

	/* Switch to khz */
	return cppc_perf.lowest_freq * 1000;
}

static int amd_get_max_freq(struct amd_cpudata *cpudata)
{
	struct cppc_perf_caps cppc_perf;
	u32 max_perf, max_freq, nominal_freq, nominal_perf;
	u64 boost_ratio;

	int ret = cppc_get_perf_caps(cpudata->cpu, &cppc_perf);
	if (ret)
		return ret;

	nominal_freq = cppc_perf.nominal_freq;
	nominal_perf = READ_ONCE(cpudata->nominal_perf);
	max_perf = READ_ONCE(cpudata->highest_perf);

	boost_ratio = div_u64(max_perf << SCHED_CAPACITY_SHIFT,
			      nominal_perf);

	max_freq = nominal_freq * boost_ratio >> SCHED_CAPACITY_SHIFT;

	/* Switch to khz */
	return max_freq * 1000;
}

static int amd_get_nominal_freq(struct amd_cpudata *cpudata)
{
	struct cppc_perf_caps cppc_perf;
	u32 nominal_freq;

	int ret = cppc_get_perf_caps(cpudata->cpu, &cppc_perf);
	if (ret)
		return ret;

	nominal_freq = cppc_perf.nominal_freq;

	/* Switch to khz */
	return nominal_freq * 1000;
}

static int amd_get_lowest_nonlinear_freq(struct amd_cpudata *cpudata)
{
	struct cppc_perf_caps cppc_perf;
	u32 lowest_nonlinear_freq, lowest_nonlinear_perf,
	    nominal_freq, nominal_perf;
	u64 lowest_nonlinear_ratio;

	int ret = cppc_get_perf_caps(cpudata->cpu, &cppc_perf);
	if (ret)
		return ret;

	nominal_freq = cppc_perf.nominal_freq;
	nominal_perf = READ_ONCE(cpudata->nominal_perf);

	lowest_nonlinear_perf = cppc_perf.lowest_nonlinear_perf;

	lowest_nonlinear_ratio = div_u64(lowest_nonlinear_perf <<
					 SCHED_CAPACITY_SHIFT, nominal_perf);

	lowest_nonlinear_freq = nominal_freq * lowest_nonlinear_ratio >> SCHED_CAPACITY_SHIFT;

	/* Switch to khz */
	return lowest_nonlinear_freq * 1000;
}

static int amd_pstate_init_freqs_in_cpudata(struct amd_cpudata *cpudata,
					    u32 max_freq, u32 min_freq,
					    u32 nominal_freq,
					    u32 lowest_nonlinear_freq)
{
	if (!cpudata)
		return -EINVAL;

	/* Initial processor data capability frequencies */
	cpudata->max_freq = max_freq;
	cpudata->min_freq = min_freq;
	cpudata->nominal_freq = nominal_freq;
	cpudata->lowest_nonlinear_freq = lowest_nonlinear_freq;

	return 0;
}

static struct amd_pstate_perf_funcs pstate_funcs = {
	.enable = pstate_enable,
	.init_perf = pstate_init_perf,
	.update_perf = pstate_update_perf,
};

static struct amd_pstate_perf_funcs cppc_funcs = {
	.enable = cppc_enable,
	.init_perf = cppc_init_perf,
	.update_perf = cppc_update_perf,
};

static int amd_pstate_cpu_init(struct cpufreq_policy *policy)
{
	int min_freq, max_freq, nominal_freq, lowest_nonlinear_freq, ret;
	unsigned int cpu = policy->cpu;
	struct device *dev;
	struct amd_cpudata *cpudata;

	dev = get_cpu_device(policy->cpu);
	if (!dev)
		return -ENODEV;

	cpudata = kzalloc(sizeof(*cpudata), GFP_KERNEL);
	if (!cpudata)
		return -ENOMEM;

	cpudata->cpu = cpu;
	cpudata->policy = policy;

	ret = amd_pstate_init_perf(cpudata);
	if (ret)
		goto free_cpudata1;

	min_freq = amd_get_min_freq(cpudata);
	max_freq = amd_get_max_freq(cpudata);
	nominal_freq = amd_get_nominal_freq(cpudata);
	lowest_nonlinear_freq = amd_get_lowest_nonlinear_freq(cpudata);

	if (min_freq < 0 || max_freq < 0 || min_freq > max_freq) {
		dev_err(dev, "min_freq(%d) or max_freq(%d) value is incorrect\n",
			min_freq, max_freq);
		ret = -EINVAL;
		goto free_cpudata1;
	}

	policy->cpuinfo.transition_latency = AMD_PSTATE_TRANSITION_LATENCY;
	policy->transition_delay_us = AMD_PSTATE_TRANSITION_DELAY;

	policy->min = min_freq;
	policy->max = max_freq;

	policy->cpuinfo.min_freq = min_freq;
	policy->cpuinfo.max_freq = max_freq;

	/* It will be updated by governor */
	policy->cur = policy->cpuinfo.min_freq;

	if (boot_cpu_has(X86_FEATURE_AMD_CPPC_EXT))
		policy->fast_switch_possible = true;

	ret = freq_qos_add_request(&policy->constraints, &cpudata->req[0],
				   FREQ_QOS_MIN, policy->cpuinfo.min_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to add min-freq constraint (%d)\n", ret);
		goto free_cpudata1;
	}

	ret = freq_qos_add_request(&policy->constraints, &cpudata->req[1],
				   FREQ_QOS_MAX, policy->cpuinfo.max_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to add max-freq constraint (%d)\n", ret);
		goto free_cpudata2;
	}

	ret = amd_pstate_init_freqs_in_cpudata(cpudata, max_freq, min_freq,
					       nominal_freq,
					       lowest_nonlinear_freq);
	if (ret) {
		dev_err(dev, "Failed to init cpudata (%d)\n", ret);
		goto free_cpudata3;
	}

	policy->driver_data = cpudata;

	return 0;

free_cpudata3:
	freq_qos_remove_request(&cpudata->req[1]);
free_cpudata2:
	freq_qos_remove_request(&cpudata->req[0]);
free_cpudata1:
	kfree(cpudata);
	return ret;
}

static int amd_pstate_cpu_exit(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata;

	cpudata = policy->driver_data;

	freq_qos_remove_request(&cpudata->req[1]);
	freq_qos_remove_request(&cpudata->req[0]);
	kfree(cpudata);

	return 0;
}

static struct cpufreq_driver amd_pstate_driver = {
	.flags		= CPUFREQ_CONST_LOOPS | CPUFREQ_NEED_UPDATE_LIMITS,
	.verify		= amd_pstate_verify,
	.target		= amd_pstate_target,
	.fast_switch    = amd_pstate_fast_switch,
	.init		= amd_pstate_cpu_init,
	.exit		= amd_pstate_cpu_exit,
	.name		= "amd-pstate",
};

static int __init amd_pstate_init(void)
{
	int ret;
	struct amd_pstate_perf_funcs *funcs;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return -ENODEV;

	if (!acpi_cpc_valid()) {
		pr_debug("%s, the _CPC object is not present in SBIOS\n",
			 __func__);
		return -ENODEV;
	}

	/* don't keep reloading if cpufreq_driver exists */
	if (cpufreq_get_current_driver())
		return -EEXIST;

	/* capability check */
	if (boot_cpu_has(X86_FEATURE_AMD_CPPC_EXT)) {
		pr_debug("%s, AMD CPPC extension functionality is supported\n",
			 __func__);
		funcs = &pstate_funcs;
		amd_pstate_driver.adjust_perf = amd_pstate_adjust_perf;
	} else {
		funcs = &cppc_funcs;
	}

	/* enable amd pstate feature */
	ret = amd_pstate_enable(funcs, true);
	if (ret) {
		pr_err("%s, failed to enable amd-pstate with return %d\n",
		       __func__, ret);
		return ret;
	}

	amd_pstate_driver.driver_data = funcs;

	ret = cpufreq_register_driver(&amd_pstate_driver);
	if (ret) {
		pr_err("%s, return %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

static void __exit amd_pstate_exit(void)
{
	struct amd_pstate_perf_funcs *funcs;

	funcs = cpufreq_get_driver_data();

	cpufreq_unregister_driver(&amd_pstate_driver);

	amd_pstate_enable(funcs, false);
}

module_init(amd_pstate_init);
module_exit(amd_pstate_exit);

MODULE_AUTHOR("Huang Rui <ray.huang@amd.com>");
MODULE_DESCRIPTION("AMD Processor P-state Frequency Driver");
MODULE_LICENSE("GPL");
