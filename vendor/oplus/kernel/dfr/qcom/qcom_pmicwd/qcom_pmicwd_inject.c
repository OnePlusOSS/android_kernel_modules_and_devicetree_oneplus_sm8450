// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/***************************************************************
** OPLUS_SYSTEM_QCOM_PMICWD
** File : qcom_pmicwd_inject.c
** Description : qcom pmic watchdog driver
** Version : 1.0
******************************************************************/

/*
 * depend on msm export symbol: sys_reset_dev
 * sys_reset_dev: msm-5.4/drivers/input/misc/qpnp-power-on.c
*/

#include <linux/kthread.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <uapi/linux/sched/types.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "soc/oplus/system/oplus_project.h"
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/version.h>

#include "qcom_pmicwd.h"

static int pmicwd_inject_enable;
DEFINE_MUTEX(pmicwd_inject_lock);

static inline int pm_device_hang(enum pmicwd_inject_estage stage, enum pmicwd_inject_etype type) {
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		PWD_INFO("the phone will enter into infinite loops\n");
        schedule_timeout(msecs_to_jiffies(1000));
	}
	return 0;
}

static inline int pm_device_lock(enum pmicwd_inject_estage stage, enum pmicwd_inject_etype type) {
	mutex_lock(&pmicwd_inject_lock);
	PWD_ERR("pmicwd device will enter into dead lock\n");

	return 0;
}

static inline int pm_watchdog(enum pmicwd_inject_estage stage, enum pmicwd_inject_etype type) {
	preempt_disable();
	local_irq_disable();
	PWD_ERR("%s wdt issue begin!\n", __func__);
	while(1);

	return 0;
}

static struct pmicwd_injections pmicwd_inject = {
	.pmicwd_pdev = NULL,
	.cur_stage = PMICWD_INJECT_DISABLE,
	.cur_type = PMICWD_INJECT_NONE,
	.inject_cases = {
		{ PMICWD_ERR_INJECT_CASE(PMICWD_INJECT_DEVICE_HANG, PMICWD_INJECT_DEVICE_HANG, PMICWD_DEVICE_HANG_SUPPORTED, pm_device_hang) },
		{ PMICWD_ERR_INJECT_CASE(PMICWD_INJECT_DEVICE_LOCK, PMICWD_INJECT_DEVICE_LOCK, PMICWD_DEVICE_LOCK_SUPPORTED, pm_device_lock) },
		{ PMICWD_ERR_INJECT_CASE(PMICWD_INJECT_WDT, PMICWD_INJECT_WDT, PMICWD_WATCHDOG_SUPPORTED, pm_watchdog) },
		{}
	}
};

void pmicwd_return_injected(enum pmicwd_inject_estage stage) {
	struct pmicwd_injection_case *injectcase;

	if (!pmicwd_inject_enable) {
		return;
	}

	if(stage < PMICWD_INJECT_SUSPEND_PRE || stage > PMICWD_INJECT_SUSPEND_POST || stage != pmicwd_inject.cur_stage) {
		return;
	}

	injectcase = &pmicwd_inject.inject_cases[pmicwd_inject.cur_type];
	if((NULL != injectcase) && (injectcase->supported_stage & (1 << stage)) && (NULL != injectcase->inject_func)) {
		PWD_INFO("injected stage:%d, type:%d, name:%s \n", pmicwd_inject.cur_stage, pmicwd_inject.cur_type, injectcase->inject_name);
		injectcase->inject_func(pmicwd_inject.cur_stage, pmicwd_inject.cur_type);
	}

	return;
}

static ssize_t pmicwd_inject_proc_read(struct file *file, char __user *buf,
		size_t count, loff_t *off) {
	struct pmicwd_injection_case *injectcase;
	char page[(PMICWD_INJECT_NONE + 1) * MAX_SYMBOL_LEN] = {0};
	int len = 0, i = 0;

	len = snprintf(&page[len], (PMICWD_INJECT_NONE + 1) * MAX_SYMBOL_LEN - len, "enable = %d, curr_stage = %d, curr_type = %d\n",
			pmicwd_inject_enable, pmicwd_inject.cur_stage, pmicwd_inject.cur_type);
	len += snprintf(&page[len], (PMICWD_INJECT_NONE + 1) * MAX_SYMBOL_LEN - len, "supported inject cases:\n");
	PWD_INFO("pmicwd_inject_proc_read:pmicwd_inject:0x%p, cases:0x%p \n", (void*)&pmicwd_inject, (void*)pmicwd_inject.inject_cases);
	PWD_INFO("pmicwd_inject_proc_read:case 0 name:%s \n", pmicwd_inject.inject_cases[0].inject_name);
	for (injectcase = pmicwd_inject.inject_cases; injectcase->inject_name; injectcase++, i++) {
		PWD_INFO("pmicwd_inject_proc_read:case %d name:%s \n", i, injectcase->inject_name);
		if (injectcase->inject_type == pmicwd_inject.cur_type) {
			len += snprintf(&page[len], ((PMICWD_INJECT_NONE + 1) * MAX_SYMBOL_LEN - 1 - len), "[%d] = [%s]\n", i, injectcase->inject_name);
		} else {
			len += snprintf(&page[len], ((PMICWD_INJECT_NONE + 1) * MAX_SYMBOL_LEN - 1 - len), "[%d] = %s\n", i, injectcase->inject_name);
		}
	}
	PWD_INFO("pmicwd_inject_proc_read: i = %d, len = %d \n", i, len);

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf, page, (len < count ? len : count))) {
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static ssize_t pmicwd_inject_proc_write(struct file *file, const char __user *buf,
		size_t count, loff_t *off) {
	enum pmicwd_inject_estage stage = PMICWD_INJECT_DISABLE;
	enum pmicwd_inject_etype type = PMICWD_INJECT_NONE;
	struct pmicwd_injection_case *injectcase;
	int ret = 0;
	char buffer[64] = {0};
	int max_len[] = {BUFF_SIZE, BUFF_SIZE, BUFF_SIZE};
	int part;
	char delim[] = {',', ',', '\n'};
	char *start, *end;

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count)) {
		PWD_ERR("%s: read proc input error.\n", __func__);
		return count;
	}

	buffer[count] = '\0';
	/* validate the length of each of the 2 parts */
	start = buffer;
	for (part = 0; part < 3; part++) {
		end = strchr(start, delim[part]);
		if (end == NULL || (end - start) > max_len[part]) {
			return count;
		}
		start = end + 1;
	}

	ret = sscanf(buffer, "%d,%d,%d", &pmicwd_inject_enable, &stage, &type);
	if(ret <= 0) {
		PWD_ERR("%s: format input error\n", __func__);
		return count;
	}
	if(stage > PMICWD_INJECT_DISABLE || type > PMICWD_INJECT_NONE) {
		PWD_ERR("%s: unsupported input error -> stage=%d type=%d \n", __func__, stage, type);
		stage = PMICWD_INJECT_DISABLE;
		type = PMICWD_INJECT_NONE;
		return count;
	}

	if(stage == pmicwd_inject.cur_stage && type ==  pmicwd_inject.cur_type) {
		PWD_ERR("%s: config no change error (stage=%d type=%d) -> (stage=%d type=%d) \n", __func__, pmicwd_inject.cur_stage, pmicwd_inject.cur_type, stage, type);
		return count;
	}

	for (injectcase = pmicwd_inject.inject_cases; injectcase->inject_name; injectcase++) {
		if (injectcase->supported_stage & (1 << stage)) {
			PWD_INFO("%s: stage/type mapping matched (inject_name = %s, support_stage = %d)-> (stage=%d type=%d), break!\n",
				__func__, injectcase->inject_name, injectcase->supported_stage, stage, type);
			break;
		} else {
			PWD_ERR("%s: stage/type mapping missed (inject_name = %s, support_stage = %d)-> (stage=%d type=%d), continue..\n",
				__func__, injectcase->inject_name, injectcase->supported_stage, stage, type);
		}
	}
	if (NULL == injectcase->inject_name) {
		PWD_ERR("%s: unsopported stage/type mapping error (stage=%d type=%d) \n", __func__, stage, type);
		return count;
	}

	pmicwd_inject.cur_stage = stage;
	pmicwd_inject.cur_type = type;

	return count;
}

static struct proc_ops pmicwd_inject_pops = {
	.proc_open = simple_open,
	.proc_read = pmicwd_inject_proc_read,
	.proc_write = pmicwd_inject_proc_write,
	.proc_lseek = default_llseek,
};


void pmicwd_err_inject_init(struct platform_device *pdev) {
	struct proc_dir_entry *pe;

	pe = proc_create("pmicwd_inject", 0664, NULL, &pmicwd_inject_pops);
	if (!pe) {
		PWD_ERR("pmicwd_err_inject:Failed to register err inject interface\n");
		return;
	}

	pmicwd_inject.pmicwd_pdev = pdev;
	pmicwd_inject_enable = false;

	return;
}
