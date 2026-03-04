// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

/* This file is used internally */

#ifndef __OPLUS_PLAT_UFCS_NOTIFY_H__
#define __OPLUS_PLAT_UFCS_NOTIFY_H__

enum plat_ufcs_notify_state {
	PLAT_UFCS_NOTIFY_EXIT,
	PLAT_UFCS_NOTIFY_DISABLE_MOS,
	UFCS_NOTIFY_EXIT_COMM,
	UFCS_NOTIFY_RESTART_COMM,
};

int plat_ufcs_reg_event_notifier(struct notifier_block *nb);
void plat_ufcs_unreg_event_notifier(struct notifier_block *nb);
void plat_ufcs_send_state(enum plat_ufcs_notify_state state, void *v);
#endif /* __OPLUS_PLAT_UFCS_NOTIFY_H__ */
