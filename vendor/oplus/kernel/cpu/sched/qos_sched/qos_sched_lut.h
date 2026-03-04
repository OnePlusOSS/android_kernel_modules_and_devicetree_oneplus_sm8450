/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */
#ifndef _OPLUS_QOS_SCHED_LUT_H
#define _OPLUS_QOS_SCHED_LUT_H

#include <linux/sched.h>
#include <linux/types.h>

#define MAX_QOS_LEVEL       (256)
#define QOS_LATENCY_SHIFT   (32)
#define QOS_LUT_NUM   (2)
#define QOS_LUT_TYPE_MASK   (7)
#define LUT_CTRL_MAGIC 'p'

struct qos_lut_ioctl_request {
	s32 lut_size;
	s32 type;
	s64 version;
};

struct qos_lut_item {
	s32 qos_level;
	s64 latency;
	union {
		s32 share;
		s32 prio;
	} share_or_prio;
	s32 uclamp_min;
	s32 uclamp_max;
	s32 stune;
};

enum QOS_LUT_TYPE {
	QOS_LUT_TASK = 1,
	QOS_LUT_PROCESS,
	QOS_LUT_GROUP = 4,
};

struct qos_lut {
	struct qos_lut_item *items;
};

struct qos_lut_info {
        u8    lut_num;
        s32   lut_size;
	struct qos_lut_item *mem;
};

struct qos_lut_ctl {
	s64 version;
	u8 cur;
	struct qos_lut_info *lut_info[QOS_LUT_NUM];
};

enum {
	UPDATE_LUT_REQUEST = 1,
	GET_LUT_VERSION,
	CTRL_LUT_MAX,
};


#define IOCTL_UPDATE_LUT_REQUEST \
	_IOW(LUT_CTRL_MAGIC, UPDATE_LUT_REQUEST, struct qos_lut_ioctl_request)
#define IOCTL_GET_LUT_VERSION \
	_IOR(LUT_CTRL_MAGIC, GET_LUT_VERSION, struct qos_lut_ioctl_request)

void qs_update_lut(struct qos_lut_item *new_items);
s32 qs_update_lut_request(struct qos_lut_ioctl_request *request);
s64 qs_get_lut_version(s32 type);
u64 qs_get_latecny_by_qos_level(u64 qos_level, s32 type);
struct qos_lut_ctl *qs_get_lut_ctl(s32 type);
void qs_set_lut_update_type(s32 type);
s32 qs_get_lut_update_type(void);
struct qos_lut_item *qs_get_lut_item(s32 qos_level, s32 type);
#endif  /* _OPLUS_QOS_SCHED_LUT_H */
