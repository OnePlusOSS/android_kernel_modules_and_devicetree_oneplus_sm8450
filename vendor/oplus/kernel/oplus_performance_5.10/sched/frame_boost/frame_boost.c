// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>

#include "frame_boost.h"


struct fbg_vendor_hook fbg_hook;

/******************
 * debug function
 *******************/
static noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

void util_systrace_c(unsigned long util, char *msg)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "C|9999|%s|%lu\n", msg, util);
	tracing_mark_write(buf);
}

void cpus_systrace_c(unsigned int cpu, char *msg)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "C|9999|%s|%u\n", msg, cpu);
	tracing_mark_write(buf);
}

void zone_systrace_c(unsigned int zone, char *msg)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "C|9999|frame_zone_%s|%u\n", msg, zone);
	tracing_mark_write(buf);
}

/******************
 * moduler function
 *******************/
static int __init oplus_frame_boost_init(void)
{
	int ret = 0;

	fbg_sysctl_init();

	ret = frame_ioctl_init();
	if (ret != 0)
		return ret;

	ret = frame_info_init();
	if (ret != 0)
		goto out;

	ret = frame_group_init();
	if (ret != 0)
		goto out;

	/* please register your hooks at the end of init. */
	register_frame_group_vendor_hooks();

	ofb_debug("oplus_bsp_frame_boost.ko init succeed!!\n");

out:
	return ret;
}

module_init(oplus_frame_boost_init);
MODULE_DESCRIPTION("Oplus Frame Boost Moduler");
MODULE_LICENSE("GPL v2");
