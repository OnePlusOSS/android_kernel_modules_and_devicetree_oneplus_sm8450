// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef __OPLUS_UFCS_DEBUG_H__
#define __OPLUS_UFCS_DEBUG_H__

#include <linux/kernel.h>
#include <linux/workqueue.h>

struct ufcs_class;
struct ufcs_msg;

struct ufcs_debug_refuse_info {
	u8 msg_type;
	u8 cmd_type;
	u8 reason;
	int count;
};

struct ufcs_debug_nck_info {
	u8 msg_type;
	u8 cmd_type;
	int count;
};

struct ufcs_debug_data {
	bool test_mode;
	struct ufcs_debug_refuse_info refuse_info;
	struct ufcs_debug_nck_info nck_info;

	struct work_struct disable_wd_work;
};

int ufcs_debug_set_test_request(struct ufcs_class *class, char *buf, size_t count);
int ufcs_debug_set_recv_msg(struct ufcs_class *class, const char *buf, size_t count);
int ufcs_debug_set_refuse_info(struct ufcs_class *class, char *buf, size_t count);
int ufcs_debug_set_nck_info(struct ufcs_class *class, char *buf, size_t count);
int ufcs_debug_check_refuse_info(struct ufcs_class *class, struct ufcs_msg *msg);
int ufcs_debug_check_nck_info(struct ufcs_class *class, struct ufcs_msg *msg);
int ufcs_debug_set_recv_invalid_msg(struct ufcs_class *class, u8 msg_type);

#endif /* __OPLUS_UFCS_DEBUG_H__ */
