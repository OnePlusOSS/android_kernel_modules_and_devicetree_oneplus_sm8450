/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#ifndef _FRAME_BOOST_H_
#define _FRAME_BOOST_H_

#include "frame_ioctl.h"
#include "frame_info.h"
#include "frame_group.h"

#define ofb_debug(fmt, ...) \
	pr_info("[frame boost][%s]"fmt, __func__, ##__VA_ARGS__)

#define ofb_err(fmt, ...) \
	pr_err("[frame boost][%s]"fmt, __func__, ##__VA_ARGS__)

#define SLIDE_SCENE    (1 << 0)
#define INPUT_SCENE    (1 << 1)

enum STUNE_BOOST_TYPE {
	BOOST_DEF_MIGR = 0,
	BOOST_DEF_FREQ,
	BOOST_SF_MIGR,
	BOOST_SF_FREQ,
	BOOST_MAX_TYPE,
};

struct fbg_vendor_hook {
	void (*update_freq)(struct rq *rq, unsigned int flags);
};

extern int sysctl_frame_boost_enable;
extern int sysctl_frame_boost_debug;
extern int sysctl_slide_boost_enabled;
extern int sysctl_input_boost_enabled;
extern int stune_boost[BOOST_MAX_TYPE];
extern struct fbg_vendor_hook fbg_hook;

void util_systrace_c(unsigned long util, char *msg);
void cpus_systrace_c(unsigned int cpu, char *msg);
void zone_systrace_c(unsigned int zone, char *msg);
void fbg_sysctl_init(void);
#endif /* _FRAME_BOOST_H_ */
