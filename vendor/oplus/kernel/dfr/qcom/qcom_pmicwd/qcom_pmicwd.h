/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __QCOM_PMICWD_H__
#define __QCOM_PMICWD_H__

#include <linux/regmap.h>
#include <linux/input/qpnp-power-on.h>

struct pmicwd_desc {
        struct qpnp_pon    *pon;
        struct task_struct *wd_task;
        struct mutex       wd_task_mutex;
        unsigned int       pmicwd_state;           /* |reserver|rst type|timeout|enable| */
        u8                 suspend_state;          /* record the suspend state */
        signed long        state_update_time;      /* record the last suspend state update time */
};

enum pmicwd_suspend_state {
	PM_SUSPENDED_OR_RESUMED = 0,
	PM_SUSPEND_PRE = 0x10,
	PM_DEVICE_SUSPEND_PRE = 0x20,
	PM_DEVICE_SUSPEND = 0x30,
	PM_DEVICE_SUSPEND_LATE = 0x40,
	PM_DEVICE_SUSPEND_NOIRQ = 0x50,
	PM_DEVICE_RESUME_NOIRQ = 0x60,
	PM_DEVICE_RESUME_EARLY = 0x70,
	PM_DEVICE_RESUME = 0x80,
	PM_DEVICE_COMPLETE = 0x90,
	PM_SUSPEND_POST = 0xa0,
};

enum pmicwd_inject_estage {
	PMICWD_INJECT_SUSPEND_PRE,
	PMICWD_INJECT_DEVICE_SUSPEND_PRE,
	PMICWD_INJECT_DEVICE_SUSPEND,
	PMICWD_INJECT_DEVICE_SUSPEND_LATE,
	PMICWD_INJECT_DEVICE_SUSPEND_NOIRQ,
	PMICWD_INJECT_DEVICE_RESUME_NOIRQ,
	PMICWD_INJECT_DEVICE_RESUME_EARLY,
	PMICWD_INJECT_DEVICE_RESUME,
	PMICWD_INJECT_DEVICE_RESUME_COMPLETE,
	PMICWD_INJECT_SUSPEND_POST,
	PMICWD_INJECT_DISABLE
};

enum pmicwd_inject_etype {
	PMICWD_INJECT_DEVICE_HANG,
	PMICWD_INJECT_DEVICE_LOCK,
	PMICWD_INJECT_WDT,
	PMICWD_INJECT_NONE
};

typedef int (*PMICWD_INJECT_FUN)(enum pmicwd_inject_estage stage, enum pmicwd_inject_etype type);

struct pmicwd_injection_case {
	enum pmicwd_inject_etype inject_type;
	const char *inject_name;
	unsigned int supported_stage;
	PMICWD_INJECT_FUN inject_func;
};

struct pmicwd_injections {
	struct platform_device *pmicwd_pdev;
	enum pmicwd_inject_estage cur_stage;
	enum pmicwd_inject_etype cur_type;
	struct pmicwd_injection_case inject_cases[];
};

#define PMICWD_DEVICE_HANG_SUPPORTED ((1 << PMICWD_INJECT_SUSPEND_PRE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_PRE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_LATE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_NOIRQ) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_NOIRQ) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_EARLY) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_COMPLETE) \
										| (1 << PMICWD_INJECT_SUSPEND_POST))

#define PMICWD_DEVICE_LOCK_SUPPORTED ((1 << PMICWD_INJECT_SUSPEND_PRE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_PRE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_LATE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_NOIRQ) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_NOIRQ) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_EARLY) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_COMPLETE) \
										| (1 << PMICWD_INJECT_SUSPEND_POST))

#define PMICWD_WATCHDOG_SUPPORTED ((1 << PMICWD_INJECT_SUSPEND_PRE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_PRE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_LATE) \
										| (1 << PMICWD_INJECT_DEVICE_SUSPEND_NOIRQ) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_NOIRQ) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_EARLY) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME) \
										| (1 << PMICWD_INJECT_DEVICE_RESUME_COMPLETE) \
										| (1 << PMICWD_INJECT_SUSPEND_POST))

#define PMICWD_ERR_INJECT_CASE(etype, ename, support, efunc)	\
	.inject_type = etype, .inject_name = #ename, .supported_stage = support, .inject_func = efunc,

#define QPNP_PON_WD_RST_S1_TIMER(pon)		((pon)->base + 0x54)
#define QPNP_PON_WD_RST_S2_TIMER(pon)		((pon)->base + 0x55)
#define QPNP_PON_WD_RST_S2_CTL(pon)			((pon)->base + 0x56)
#define QPNP_PON_WD_RST_S2_CTL2(pon)		((pon)->base + 0x57)
#define QPNP_PON_WD_RESET_PET(pon)  		((pon)->base + 0x58)
#define QPNP_PON_RT_STS(pon)				((pon)->base + 0x10)

#define QPNP_PON_GEN3_INT_SET_TYPE(pon)       ((pon)->base + 0x11)
#define QPNP_PON_GEN3_INT_POLARITY_HIGH(pon)       ((pon)->base + 0x12)
#define QPNP_PON_GEN3_INT_POLARITY_LOW(pon)       ((pon)->base + 0x13)
#define QPNP_PON_GEN3_INT_LATCHED_CLR(pon)       ((pon)->base + 0x14)
#define QPNP_PON_GEN3_INT_EN_SET(pon)       ((pon)->base + 0x15)
#define QPNP_PON_GEN3_INT_EN_CLR(pon)       ((pon)->base + 0x16)
#define QPNP_PON_GEN3_WD_RST_S1_TIMER(pon)       ((pon)->base + 0x4c)
#define QPNP_PON_GEN3_WD_RST_S2_TIMER(pon)       ((pon)->base + 0x4d)
#define QPNP_PON_GEN3_WD_RST_S2_CTL(pon)         ((pon)->base + 0x4e)
#define QPNP_PON_GEN3_WD_RST_S2_CTL2(pon)        ((pon)->base + 0x4f)
#define QPNP_PON_GEN3_WD_RESET_PET(pon)          ((pon)->base + 0x50)
#define PMIC_WD_INT_BIT_MASK		BIT(3)

#define QPNP_PON_S2_CNTL_TYPE_MASK		(0xF)
#define QPNP_PON_WD_S2_TIMER_MASK		(0x7F)
#define QPNP_PON_WD_S1_TIMER_MASK		(0x7F)
#define QPNP_PON_WD_RESET_PET_MASK		BIT(0)

#define PMIC_WD_DEFAULT_TIMEOUT 254
#define PMIC_WD_DEFAULT_ENABLE 1

#define PON_GEN3_PBS                            0x08
#define PON_GEN3_HLOS                           0x09
#define QPNP_PON_WD_EN                          BIT(7)

#define BUFF_SIZE 64
#define MAX_SYMBOL_LEN 64

#define PWD_TAG "[PMICWD]"
#define PWD_DEBUG(fmt, ...) printk(KERN_DEBUG PWD_TAG pr_fmt(fmt), ##__VA_ARGS__)
#define PWD_INFO(fmt, ...) printk(KERN_INFO PWD_TAG pr_fmt(fmt), ##__VA_ARGS__)
#define PWD_WARN(fmt, ...) printk(KERN_WARNING PWD_TAG pr_fmt(fmt), ##__VA_ARGS__)
#define PWD_ERR(fmt, ...) printk(KERN_ERR PWD_TAG pr_fmt(fmt), ##__VA_ARGS__)

#undef ASSERT
#define ASSERT(x) BUG_ON(!(x))

#define PMICWD_STATE_CHECK(func, wd) \
	if(!(wd->pmicwd_state & 0xff)) { \
		PWD_ERR("%s  pmicwd_state disabled, return!\n", func); \
		return 0; \
	}

#define PMICWD_STATE_CHECK_VOID(func, wd) \
	if(!(wd->pmicwd_state & 0xff)) { \
		PWD_ERR("%s  pmicwd_state disabled, return!\n", func); \
		return; \
	}

static inline int dup_qpnp_pon_masked_write(struct qpnp_pon *pon, u16 addr, u8 mask, u8 val) {
        int rc;

        rc = regmap_update_bits(pon->regmap, addr, mask, val);
        if (rc) {
                PWD_ERR("Register write failed, addr=0x%04X, rc=%d\n", addr, rc);
        }

        return rc;
}

void kpdpwr_init(void);
void pmicwd_init(struct platform_device *pdev);
void pmicwd_err_inject_init(struct platform_device *pdev);
void pmicwd_return_injected(enum pmicwd_inject_estage stage);
int qpnp_pon_wd_pet(struct qpnp_pon *pon);

#endif  /* __QCOM_PMICWD_H__ */
