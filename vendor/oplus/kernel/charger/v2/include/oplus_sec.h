// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 *
 */
#ifndef OPLUS_CHG_SEC_H
#define OPLUS_CHG_SEC_H

#include <linux/miscdevice.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

struct oplus_sec;

struct oplus_sec* oplus_sec_init(void);
void oplus_sec_release(struct oplus_sec *chip);
int oplus_sec_test_helper(struct oplus_mms *gauge_topic, int cmd);

#endif /* OPLUS_CHG_SEC_H */
