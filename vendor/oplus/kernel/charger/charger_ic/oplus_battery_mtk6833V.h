/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_BATTERY_MTK6833_H__
#define __OPLUS_BATTERY_MTK6833_H__

#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/alarmtimer.h>
#include <linux/time.h>
#include "adapter_class.h"
#include <mtk_charger.h>
#include <uapi/linux/sched/types.h>
#include <linux/uaccess.h>

#include "../../../misc/mediatek/typec/tcpc/inc/tcpm.h"
struct mtk_charger;
#include "../../../power/supply/mtk_pe.h"
#include "../../../power/supply/mtk_pe2.h"
#include "../../../power/supply/mtk_pe4.h"
#include "../../../power/supply/mtk_pe5.h"
#include "../../../power/supply/mtk_smartcharging.h"
#include "../../../power/supply/mtk_pd.h"

#define PD_ICHG_LEAVE_THRESHOLD 	1000000 /* uA */
#define PD_STOP_BATTERY_SOC 		80

#ifdef __KERNEL__
#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
	__kernel_old_time_t tv_sec; /* seconds */
	long tv_nsec; /* nanoseconds */
};
#endif

struct timeval {
	__kernel_old_time_t tv_sec; /* seconds */
	__kernel_suseconds_t tv_usec; /* microseconds */
};

struct itimerspec {
	struct timespec it_interval; /* timer period */
	struct timespec it_value; /* timer expiration */
};

struct itimerval {
	struct timeval it_interval; /* timer interval */
	struct timeval it_value; /* current value */
};
#endif

enum {
	MAIN_CHARGER = 0,
	SLAVE_CHARGER = 1,
	TOTAL_CHARGER = 2,
	DIRECT_CHARGER = 10,
	MAIN_DIVIDER_CHARGER = 20,
	SLAVE_DIVIDER_CHARGER = 21,
};

/* charger_manager notify charger_consumer */
enum {
	CHARGER_NOTIFY_EOC,
	CHARGER_NOTIFY_START_CHARGING,
	CHARGER_NOTIFY_STOP_CHARGING,
	CHARGER_NOTIFY_ERROR,
	CHARGER_NOTIFY_NORMAL,
};

extern bool is_meta_mode(void);

enum charger_type {
	CHARGER_UNKNOWN = 0,
	STANDARD_HOST,		/* USB : 450mA */
	CHARGING_HOST,
	NONSTANDARD_CHARGER,	/* AC : 450mA~1A */
	STANDARD_CHARGER,	/* AC : ~1A */
	APPLE_2_1A_CHARGER, /* 2.1A apple charger */
	APPLE_1_0A_CHARGER, /* 1A apple charger */
	APPLE_0_5A_CHARGER, /* 0.5A apple charger */
	WIRELESS_CHARGER,
};

enum usb_state_enum {
	USB_SUSPEND = 0,
	USB_UNCONFIGURED,
	USB_CONFIGURED
};

enum mtk_pd_connect_type {
	MTK_PD_CONNECT_NONE,
	MTK_PD_CONNECT_HARD_RESET,
	MTK_PD_CONNECT_SOFT_RESET,
	MTK_PD_CONNECT_PE_READY_SNK,
	MTK_PD_CONNECT_PE_READY_SNK_PD30,
	MTK_PD_CONNECT_PE_READY_SNK_APDO,
	MTK_PD_CONNECT_TYPEC_ONLY_SNK,
};

static const enum power_supply_usb_type charger_psy_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
};

static const enum power_supply_property charger_psy_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_VOLTAGE_BOOT,
	POWER_SUPPLY_PROP_USB_TYPE,
};
struct charger_consumer {
	struct device *dev;
	void *cm;
	struct notifier_block *pnb;
	struct list_head list;
	bool hv_charging_disabled;
#ifdef OPLUS_FEATURE_CHG_BASIC
	bool support_ntc_01c_precision;
#endif
};

int oplus_pdc_setup(int *vbus_mv, int *ibus_ma);
int oplus_pdc_get(int *vbus_mv, int *ibus_ma);

static void mtk_charger_set_algo_log_level(struct mtk_charger *info, int level) __attribute__((unused));
static int mtk_chg_current_cmd_open(struct inode *node, struct file *file) __attribute__((unused));
static ssize_t mtk_chg_current_cmd_write(struct file *file,
                const char *buffer, size_t count, loff_t *data) __attribute__((unused));
static int mtk_chg_current_cmd_show(struct seq_file *m, void *data) __attribute__((unused));
static int mtk_chg_en_power_path_open(struct inode *node, struct file *file) __attribute__((unused));

static ssize_t mtk_chg_en_power_path_write(struct file *file,
                const char *buffer, size_t count, loff_t *data) __attribute__((unused));
static int mtk_chg_en_power_path_show(struct seq_file *m, void *data) __attribute__((unused));
static int mtk_chg_en_safety_timer_open(struct inode *node, struct file *file) __attribute__((unused));

static ssize_t mtk_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *data) __attribute__((unused));
static int mtk_chg_en_safety_timer_show(struct seq_file *m, void *data) __attribute__((unused));

extern bool mtk_pdc_check_charger(struct mtk_charger *info);
extern void mtk_pdc_plugout_reset(struct mtk_charger *info);
extern void mtk_pdc_set_max_watt(struct mtk_charger *info, int watt);
extern int mtk_pdc_get_max_watt(struct mtk_charger *info);
extern int mtk_pdc_get_setting(struct mtk_charger *info, int *vbus,
				int *cur, int *idx);
extern void mtk_pdc_init_table(struct mtk_charger *info);
extern bool mtk_pdc_init(struct mtk_charger *info);
extern int mtk_pdc_setup(struct mtk_charger *info, int idx);
extern void mtk_pdc_plugout(struct mtk_charger *info);
extern void mtk_pdc_check_cable_impedance(struct mtk_charger *info);
extern void mtk_pdc_reset(struct mtk_charger *info);
extern bool mtk_pdc_check_leave(struct mtk_charger *info);

/* charger related module interface */
extern int charger_manager_notifier(struct mtk_charger *info, int event);
extern int mtk_switch_charging_init(struct mtk_charger *info);
extern int mtk_switch_charging_init2(struct mtk_charger *info);
extern int mtk_dual_switch_charging_init(struct mtk_charger *info);
extern int mtk_linear_charging_init(struct mtk_charger *info);
static void _wake_up_charger(struct mtk_charger *info);
extern int mtk_get_dynamic_cv(struct mtk_charger *info, unsigned int *cv);
extern bool is_dual_charger_supported(struct mtk_charger *info);
extern int charger_enable_vbus_ovp(struct mtk_charger *pinfo, bool enable);

/* pmic API */
extern unsigned int upmu_get_rgs_chrdet(void);
extern int pmic_get_vbus(void);
extern int pmic_get_charging_current(void);
extern int pmic_get_battery_voltage(void);
extern int pmic_get_bif_battery_voltage(int *vbat);
extern int pmic_is_bif_exist(void);
extern int pmic_enable_hw_vbus_ovp(bool enable);
extern bool pmic_is_battery_exist(void);

int notify_adapter_event(struct notifier_block *notifier,
			unsigned long evt, void *val);

#define SUB_BOARD_PULL_UP_R 200000 /* 200K */
typedef enum {
	STEP_CHG_STATUS_STEP1 = 0,	/*16C~44C*/
	STEP_CHG_STATUS_STEP2,
	STEP_CHG_STATUS_STEP3,
	STEP_CHG_STATUS_STEP4,
	STEP_CHG_STATUS_INVALID
} OPLUS_STEP_CHG_STATUS;

enum {
	CHARGER_NORMAL_CHG_CURVE,
	CHARGER_FASTCHG_SVOOC_CURVE,
	CHARGER_FASTCHG_VOOC_AND_QCPD_CURVE,
	CHARGER_FASTCHG_PPS_AND_UFCS_CURVE,
};

struct mtk_pmic {
	struct mtk_charger* oplus_info;
	int sub_board_pull_up_r;
};

extern enum charger_type mt_get_charger_type(void);
extern int mt6360_get_vbus_rising(void);
extern int mt6360_check_charging_enable(void);
extern int mt6360_suspend_charger(bool suspend);
extern int mt6360_set_rechg_voltage(int rechg_mv);
extern int mt6360_reset_charger(void);
extern int mt6360_set_chging_term_disable(bool disable);
extern int mt6360_aicl_enable(bool enable);
extern int mt6360_set_register(unsigned int addr, u8 mask, u8 data);
extern int mt6360_enter_shipmode(void);

extern void mt_power_off(void);
extern void mt_usb_connect(void);
extern void mt_usb_disconnect(void);

bool oplus_pmic_check_chip_is_null(void);
extern int oplus_get_typec_sbu_voltage(void);
extern void oplus_set_water_detect(bool enable);
extern int oplus_get_water_detect(void);
extern enum power_supply_type mt6360_get_hvdcp_type(void);
void mt6360_enable_hvdcp_detect(void);
extern void oplus_notify_hvdcp_detect_stat(void);
void oplus_gauge_set_event(int event);
extern bool is_mtksvooc_project;

#define BQ2591X 3
#endif /* __OPLUS_BATTERY_MTK6833_H__ */
