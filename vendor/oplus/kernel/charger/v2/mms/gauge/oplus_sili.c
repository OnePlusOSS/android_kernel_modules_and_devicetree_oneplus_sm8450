// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Oplus. All rights reserved.
 */


#define pr_fmt(fmt) "[OPLUS_SILI]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/power_supply.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#endif
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_voter.h>
#include <oplus_mms.h>
#include <oplus_chg_monitor.h>
#include <oplus_mms_wired.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_vooc.h>
#include <oplus_batt_bal.h>
#include <oplus_parallel.h>
#include <oplus_chg_wls.h>
#include <linux/ktime.h>
#include <linux/sched/clock.h>
#include "oplus_gauge_common.h"

#ifndef CONFIG_OPLUS_CHARGER_MTK
#include <linux/soc/qcom/smem.h>
#endif


#define GAUGE_PARALLEL_IC_NUM_MIN 2
static bool is_support_parallel(struct oplus_mms_gauge *chip)
{
	if (chip == NULL) {
		chg_err("chip is NULL\n");
		return false;
	}

	if (chip->child_num >= GAUGE_PARALLEL_IC_NUM_MIN)
		return true;
	else
		return false;
}

static int oplus_mms_gauge_push_vbat_uv(struct oplus_mms_gauge *chip)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH, GAUGE_ITEM_VBAT_UV);
	if (msg == NULL) {
		chg_err("alloc vbat uv msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish vbat uv msg error, rc=%d\n", rc);
		kfree(msg);
	}
	chg_info(" [%d, %d]\n", chip->deep_spec.config.uv_thr, chip->deep_spec.config.count_thr);

	return rc;
}

#define GAUGE_INVALID_DEEP_COUNT_CALI	10
#define GAUGE_INVALID_DEEP_DICHG_COUNT	10
int oplus_gauge_show_deep_dischg_count(struct oplus_mms *topic)
{
	struct oplus_mms_gauge *chip;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return 0;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (!chip  || !chip->deep_spec.support)
		return GAUGE_INVALID_DEEP_DICHG_COUNT;

	if (is_support_parallel(chip))
		return chip->deep_spec.counts > chip->deep_spec.sub_counts ? chip->deep_spec.counts : chip->deep_spec.sub_counts;

	return chip->deep_spec.counts;
}

static int oplus_gauge_get_deep_dischg_count(struct oplus_mms_gauge *chip, struct oplus_chg_ic_dev *ic)
{
	int rc, temp = GAUGE_INVALID_DEEP_DICHG_COUNT;

	if (!chip  || !chip->deep_spec.support || !ic)
		return GAUGE_INVALID_DEEP_DICHG_COUNT;

	rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_DEEP_DISCHG_COUNT, &temp);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err(" get batt deep dischg count error, rc=%d\n", rc);
		return GAUGE_INVALID_DEEP_DICHG_COUNT;
	}

	return temp;
}

static void oplus_mms_gauge_set_deep_term_volt(struct oplus_mms *mms, int volt_mv)
{
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip || !chip->deep_spec.support)
		return;

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_DEEP_TERM_VOLT, &volt_mv);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't set gauge deep term volt, rc=%d\n", i, ic->manu_name, rc);
				continue;
			}
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_DEEP_TERM_VOLT, &volt_mv);
			if (rc < 0)
				chg_err("gauge[%d](%s): can't set gauge deep term volt, rc=%d\n", i, ic->manu_name, rc);
			return;
		}
	}
}

static int oplus_gauge_get_deep_term_volt(struct oplus_mms_gauge *chip)
{
	int rc = 0;
	int volt_mv = -EINVAL;

	if (!chip || !chip->deep_spec.support)
		return volt_mv;

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_DEEP_TERM_VOLT, &volt_mv);
	if (rc < 0)
		chg_err("get batt deep term volt error, rc=%d, volt_mv=%d\n", rc, volt_mv);
	return volt_mv;
}

void oplus_gauge_set_last_cc(struct oplus_mms_gauge *chip, int cc)
{
	int rc = 0;

	if (!chip || !chip->deep_spec.support)
		return;

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_SET_LAST_CC, &cc);
	if (rc < 0)
		chg_err("set batt last cc error, rc=%d, cc=%d\n", rc, cc);
}

static int oplus_gauge_get_last_cc(struct oplus_mms_gauge *chip)
{
	int rc = 0;
	int cc = -EINVAL;

	if (!chip || !chip->deep_spec.support)
		return cc;

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_LAST_CC, &cc);
	if (rc < 0)
		chg_err("get batt last cc error, rc=%d, cc=%d\n", rc, cc);
	return cc;
}

int oplus_gauge_get_deep_count_cali(struct oplus_mms *topic)
{
	int rc = -GAUGE_INVALID_DEEP_COUNT_CALI;
	struct oplus_mms_gauge *chip;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return rc;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (!chip  || !chip->deep_spec.support)
		return rc;

	return chip->deep_spec.config.count_cali;
}

#define GAUGE_LOW_ABNORMAL_TEMP (-200)
static int oplus_gauge_get_deep_dischg_temperature(struct oplus_mms_gauge *chip, int type)
{
	int gauge_temp = GAUGE_LOW_ABNORMAL_TEMP;
	union mms_msg_data data = { 0 };
	int rc;

	switch (type) {
	case STRATEGY_USE_BATT_TEMP:
		rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP,
					     &data, true);
		if (rc < 0) {
			chg_err("can't get battery temp, rc=%d\n", rc);
			return GAUGE_LOW_ABNORMAL_TEMP;
		}
		gauge_temp = data.intval;
		break;
	case STRATEGY_USE_SHELL_TEMP:
		rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SHELL_TEMP,
					     &data, false);
		if (rc < 0) {
			chg_err("can't get shell temp, rc=%d\n", rc);
			return GAUGE_LOW_ABNORMAL_TEMP;
		}
		gauge_temp = data.intval;
		break;
	default:
		break;
		chg_err("not support temp type, type=%d\n", type);
	}
	return gauge_temp;
}

static void oplus_gauge_ddbc_temp_thr_init(struct oplus_mms_gauge *chip)
{
	int i;

	if (!chip)
		return;

	for (i = 0; i < DDB_CURVE_TEMP_WARM; i++) {
		chip->deep_spec.ddbc_tbatt.range[i] = chip->deep_spec.ddbc_tdefault.range[i];
	}
}

int oplus_gauge_ddbc_get_temp_region(struct oplus_mms_gauge *chip)
{
	int i, gauge_temp;
	int temp_region = DDB_CURVE_TEMP_WARM;

	gauge_temp = oplus_gauge_get_deep_dischg_temperature(chip, chip->deep_spec.ddbc_tbatt.temp_type);

	for (i = 0; i < DDB_CURVE_TEMP_WARM; i++) {
		if (gauge_temp < chip->deep_spec.ddbc_tbatt.range[i]) {
			temp_region = i;
			break;
		}
	}
	return temp_region;
}

static void oplus_gauge_ddrc_temp_thr_init(struct oplus_mms_gauge *chip)
{
	int i;

	if (!chip)
		return;

	for (i = 0; i < DDB_CURVE_TEMP_WARM; i++) {
		chip->deep_spec.ddrc_tbatt.range[i] = chip->deep_spec.ddrc_tdefault.range[i];
	}
}


int oplus_gauge_ddrc_get_temp_region(struct oplus_mms_gauge *chip)
{
	int i, gauge_temp;
	int temp_region = DDB_CURVE_TEMP_WARM;

	gauge_temp = oplus_gauge_get_deep_dischg_temperature(chip, chip->deep_spec.ddrc_tbatt.temp_type);

	for (i = 0; i < DDB_CURVE_TEMP_WARM; i++) {
		if (gauge_temp < chip->deep_spec.ddrc_tbatt.range[i]) {
			temp_region = i;
			break;
		}
	}
	return temp_region;
}

static void oplus_gauge_ddrc_temp_thr_update(struct oplus_mms_gauge *chip,
				     enum ddb_temp_region now, enum ddb_temp_region pre)
{
	if (!chip)
		return;

	if ((pre > now) && (now >= DDB_CURVE_TEMP_COLD) && (now < DDB_CURVE_TEMP_WARM)) {
		chip->deep_spec.ddrc_tbatt.range[now] = chip->deep_spec.ddrc_tdefault.range[now] + BATT_TEMP_HYST;

		chg_info("now=%d, pre=%d, p[%d]update thr[%d] to %d\n", now, pre,
			chip->deep_spec.ddrc_tbatt.index_p, now, chip->deep_spec.ddrc_tbatt.range[now]);
	} else if ((pre < now) && (now >= DDB_CURVE_TEMP_COLD) && (now <= DDB_CURVE_TEMP_WARM)) {
		chip->deep_spec.ddrc_tbatt.range[now - 1] =
			chip->deep_spec.ddrc_tdefault.range[now - 1] - BATT_TEMP_HYST;

		chg_info("now=%d, pre = %d, p[%d]update thr[%d] to %d\n", now, pre,
			chip->deep_spec.ddrc_tbatt.index_p, now - 1, chip->deep_spec.ddrc_tbatt.range[now - 1]);
	}
}

static void oplus_gauge_ddbc_temp_thr_update(struct oplus_mms_gauge *chip,
				     enum ddb_temp_region now, enum ddb_temp_region pre)
{
	if (!chip)
		return;

	if ((pre > now) && (now >= DDB_CURVE_TEMP_COLD) && (now < DDB_CURVE_TEMP_WARM)) {
		chip->deep_spec.ddbc_tbatt.range[now] =
			chip->deep_spec.ddbc_tdefault.range[now] + BATT_TEMP_HYST;

		chg_info("now=%d, pre=%d, update thr[%d] to %d\n",
			  now, pre, now, chip->deep_spec.ddbc_tbatt.range[now]);
	} else if ((pre < now) && (now >= DDB_CURVE_TEMP_COLD) && (now <= DDB_CURVE_TEMP_WARM)) {
		chip->deep_spec.ddbc_tbatt.range[now - 1] =
			chip->deep_spec.ddbc_tdefault.range[now - 1] - BATT_TEMP_HYST;

		chg_info("now=%d, pre=%d, update thr[%d] to %d\n",
			  now, pre, now - 1, chip->deep_spec.ddbc_tbatt.range[now - 1]);
	}
}

#define DEEP_RATIO_HYST	10
void oplus_gauge_get_ratio_value(struct oplus_mms *mms)
{
	union mms_msg_data data = { 0 };
	int *cc = 0, *ratio = 0, counts = 0;
	int rc = 0;
	struct oplus_mms_gauge *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip || !chip->deep_spec.support)
		return;

	if (chip->sub_gauge && __ffs(chip->sub_gauge) < GAUGE_IC_NUM_MAX &&
	    mms == chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]) {
		cc = &chip->deep_spec.sub_cc;
		ratio = &chip->deep_spec.sub_ratio;
		counts = chip->deep_spec.sub_counts;
	} else {
		cc = &chip->deep_spec.cc;
		ratio = &chip->deep_spec.ratio;
		counts = chip->deep_spec.counts;
	}

	rc = oplus_mms_get_item_data(mms, GAUGE_ITEM_CC, &data, true);
	if (rc < 0) {
		chg_err("can't get cc, rc=%d\n", rc);
		*cc = 0;
	} else {
		*cc = data.intval;
	}

	if (*cc <= 0 || *cc >= INVALID_CC_VALUE) {
		if (!counts)
			*ratio = 100;
		else
			*ratio = counts * 10;

	} else {
		*ratio = counts * 10 / *cc;
	}
}

#define DEEP_DISCHG_UPDATE_CC_DELTA 5
void oplus_gauge_get_ddrc_status(struct oplus_mms *mms)
{
	struct oplus_mms_gauge *chip;
	int current_volt = 0, current_shut = 0, vterm = 0, vshut = 0;
	int	update_vterm = 0, update_vshut = 0;
	int *cc = 0, counts = 0, index_cc = 0, last_cc = 0;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip || !chip->deep_spec.support || IS_ERR_OR_NULL(chip->ddrc_strategy))
		return;

	oplus_gauge_get_ratio_value(mms);
	oplus_chg_strategy_init(chip->ddrc_strategy);
	rc = oplus_chg_strategy_get_metadata(chip->ddrc_strategy, &chip->ddrc_curve);

	if (rc < 0 || !chip->ddrc_curve.data || chip->ddrc_curve.num < 1)
		return;
	if (chip->sub_gauge && __ffs(chip->sub_gauge) < GAUGE_IC_NUM_MAX && mms == chip->gauge_topic_parallel[__ffs(chip->sub_gauge)])
	    cc = &chip->deep_spec.sub_cc;
	else
	    cc = &chip->deep_spec.cc;

	if (*cc <= 0 || *cc >= INVALID_CC_VALUE) {
		index_cc = 0;
		chip->deep_spec.config.count_thr = 0;
	} else {
		for (index_cc = chip->ddrc_curve.num - 1; index_cc > 0; index_cc--) {
			counts = chip->ddrc_curve.data[index_cc].count < chip->deep_spec.config.count_cali ?
				0 : (chip->ddrc_curve.data[index_cc].count - chip->deep_spec.config.count_cali);
			if (*cc >= counts) {
				chip->deep_spec.config.count_thr = counts;
				break;
			}
		}
	}

	vterm = chip->ddrc_curve.data[index_cc].vbat1;
	vshut = chip->ddrc_curve.data[index_cc].vbat0;
	current_volt = oplus_gauge_get_deep_term_volt(chip);
	current_shut = get_client_vote(chip->gauge_shutdown_voltage_votable, DEEP_COUNT_VOTER);
	if (current_shut <= 0)
		current_shut = vshut + current_volt - vterm;

	memset(&(chip->deep_info), 0, sizeof(chip->deep_info));
	chip->deep_info.index += snprintf(chip->deep_info.msg, DEEP_INFO_LEN, "$$track_reason@@vote$$temp_p@@%d"
		"$$temp_n@@%d$$ratio_p@@%d$$ratio_n@@%d$$vstep@@%d$$vterm_final@@%d$$term_now@@%d$$index@@%d",
		chip->deep_spec.ddrc_tbatt.index_p, chip->deep_spec.ddrc_tbatt.index_n, chip->deep_spec.config.index_r,
		chip->ddrc_curve.index_r, chip->deep_spec.config.volt_step, vterm, current_volt, index_cc);

	if (current_volt != vterm) {
		last_cc = oplus_gauge_get_last_cc(chip);
		if (last_cc <= 0 || chip->deep_spec.config.step_status || *cc < last_cc) {
			update_vterm = vterm;
			update_vshut = vshut;
			oplus_gauge_set_last_cc(chip, *cc);
			vote(chip->gauge_term_voltage_votable, DEEP_COUNT_VOTER, true,
				chip->ddrc_curve.data[index_cc].vbat1, false);
			vote(chip->gauge_shutdown_voltage_votable, SUPER_ENDURANCE_MODE_VOTER,
				!chip->super_endurance_mode_status, chip->deep_spec.config.term_voltage, false);
			vote(chip->gauge_shutdown_voltage_votable, DEEP_COUNT_VOTER,
				true, chip->ddrc_curve.data[index_cc].vbat0, false);
		} else if (*cc >= last_cc + DEEP_DISCHG_UPDATE_CC_DELTA) {
			oplus_gauge_set_last_cc(chip, *cc);
			if (vterm > current_volt)
				update_vterm = (vterm - current_volt) > chip->deep_spec.config.volt_step ?
				(current_volt + chip->deep_spec.config.volt_step) : vterm;
			else
				update_vterm = (current_volt - vterm) > chip->deep_spec.config.volt_step ?
				(current_volt - chip->deep_spec.config.volt_step) : vterm;


			if (vshut > current_shut)
				update_vshut = (vshut - current_shut) > chip->deep_spec.config.volt_step ?
				(current_shut + chip->deep_spec.config.volt_step) : vshut;
			else
				update_vshut = (current_shut - vshut) > chip->deep_spec.config.volt_step ?
				(current_shut - chip->deep_spec.config.volt_step) : vshut;

			if (update_vterm == vterm)
				update_vshut = vshut;

			vote(chip->gauge_term_voltage_votable, DEEP_COUNT_VOTER, true, update_vterm, false);
			vote(chip->gauge_shutdown_voltage_votable, SUPER_ENDURANCE_MODE_VOTER,
				!chip->super_endurance_mode_status, chip->deep_spec.config.term_voltage, false);
			vote(chip->gauge_shutdown_voltage_votable, DEEP_COUNT_VOTER,
				true, update_vshut, false);
		} else {
			vote(chip->gauge_term_voltage_votable, DEEP_COUNT_VOTER,
				true, current_volt, false);
			vote(chip->gauge_shutdown_voltage_votable, SUPER_ENDURANCE_MODE_VOTER,
				!chip->super_endurance_mode_status, chip->deep_spec.config.term_voltage, false);
			vote(chip->gauge_shutdown_voltage_votable, DEEP_COUNT_VOTER, true, current_shut, false);
		}
	} else {
		vote(chip->gauge_term_voltage_votable, DEEP_COUNT_VOTER, true,
			chip->ddrc_curve.data[index_cc].vbat1, false);
		vote(chip->gauge_shutdown_voltage_votable, SUPER_ENDURANCE_MODE_VOTER,
			!chip->super_endurance_mode_status, chip->deep_spec.config.term_voltage, false);
		vote(chip->gauge_shutdown_voltage_votable, DEEP_COUNT_VOTER, true,
			chip->ddrc_curve.data[index_cc].vbat0, false);
	}

	chg_info(" [%d, %d][%d, %d, %d, %d] [%d, %d, %d, %d, %d]\n", update_vterm, update_vshut,
		vterm, current_volt, vshut, current_shut, last_cc, *cc,
		chip->deep_spec.counts, chip->deep_spec.ratio, chip->deep_spec.config.step_status);

	chip->deep_spec.config.index_r = chip->ddrc_curve.index_r;
	chip->deep_spec.config.index_t = chip->ddrc_curve.index_t;
	chip->deep_spec.config.step_status = false;
}

void oplus_gauge_set_deep_dischg_count(struct oplus_mms *mms, int count)
{
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip  || !chip->deep_spec.support || count < 0)
		return;

	if (mms == chip->gauge_topic) {
		chip->deep_spec.counts = count;
		chip->deep_spec.sub_counts = count;
	} else if (chip->sub_gauge && __ffs(chip->sub_gauge) < GAUGE_IC_NUM_MAX &&
		   mms == chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]) {
		chip->deep_spec.sub_counts = count;
	} else {
		chip->deep_spec.counts = count;
	}

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_DEEP_DISCHG_COUNT, &count);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't set gauge dischg count, rc=%d\n", i, ic->manu_name, rc);
				continue;
			}
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_DEEP_DISCHG_COUNT, &count);
			if (rc < 0)
				chg_err("gauge[%d](%s): can't  set gauge dischg count, rc=%d\n", i, ic->manu_name, rc);
			return;
		}
	}
}

void oplus_gauge_set_deep_count_cali(struct oplus_mms *topic, int val)
{
	struct oplus_mms_gauge *chip;
	bool charging;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (!chip  || !chip->deep_spec.support || val < 0)
		return;
	charging = chip->wired_online || chip->wls_online;
	chip->deep_spec.config.count_cali = val;
	if (!charging) {
		chip->deep_spec.config.step_status = true;
		oplus_gauge_get_ddrc_status(chip->gauge_topic);
		if (chip->sub_gauge)
			oplus_gauge_get_ddrc_status(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]);
	}
	chg_info(" val = %d\n", val);
}

void oplus_gauge_set_deep_dischg_ratio_thr(struct oplus_mms *topic, int ratio)
{
	struct oplus_mms_gauge *chip;
	bool charging;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (!chip  || !chip->deep_spec.support || ratio < 0 || ratio > 100) {
		chg_err("ratio(%d) invalid\n", ratio);
		return;
	}
	charging = chip->wired_online || chip->wls_online;
	chip->deep_spec.config.ratio_default = ratio;
	chip->deep_spec.config.ratio_shake = chip->deep_spec.config.ratio_default;
	chip->deep_spec.config.ratio_status = false;
	if (!charging) {
		chip->deep_spec.config.step_status = true;
		oplus_gauge_get_ddrc_status(chip->gauge_topic);
		if (chip->sub_gauge)
			oplus_gauge_get_ddrc_status(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]);
	}
	chg_info(" chip->deep_spec.config.ratio_default = %d\n", chip->deep_spec.config.ratio_default);
}

#define GAUGE_INVALID_DEEP_COUNT_RATIO_THR	10
int oplus_gauge_get_deep_dischg_ratio_thr(struct oplus_mms *topic)
{
	int rc = -GAUGE_INVALID_DEEP_COUNT_RATIO_THR;
	struct oplus_mms_gauge *chip;

	if (topic == NULL) {
		chg_err("topic is NULL\n");
		return rc;
	}
	chip = oplus_mms_get_drvdata(topic);

	if (!chip  || !chip->deep_spec.support)
		return rc;

	return chip->deep_spec.config.ratio_default;
}

static int oplus_gauge_get_batt_id_info(struct oplus_mms_gauge *chip)
{
	int rc, temp = GPIO_STATUS_NOT_SUPPORT;

	if (!chip)
		return GPIO_STATUS_NOT_SUPPORT;


	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATTID_INFO, &temp);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err(" get battid info error, rc=%d\n", rc);
		return GPIO_STATUS_NOT_SUPPORT;
	}

	return temp;
}

static int oplus_gauge_get_batt_id_match_info(struct oplus_mms_gauge *chip)
{
	int rc, temp = ID_MATCH_IGNORE;

	if (!chip)
		return ID_MATCH_IGNORE;

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_BATTID_MATCH_INFO, &temp);
	if (rc < 0) {
		if (rc != -ENOTSUPP)
			chg_err(" get battid match info error, rc=%d\n", rc);
		return ID_MATCH_IGNORE;
	}

	return temp;
}

static void  oplus_mms_gauge_set_sili_spare_power_enable(struct oplus_mms *mms)
{
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_SILI_SPARE_POWER);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't set gauge sili spare power, rc=%d\n", i, ic->manu_name, rc);
				break;
			}
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_SILI_SPARE_POWER);
			if (rc < 0)
				chg_err("gauge[%d](%s): can't set set gauge sili spare power, rc=%d\n", i, ic->manu_name, rc);
			break;
		}
	}

	if (!rc)
		chip->deep_spec.spare_power_enable = true;
}

static void  oplus_mms_gauge_set_sili_ic_alg_cfg(struct oplus_mms *mms, int cfg)
{
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_CFG, cfg);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't set sili ic alg cfg, rc=%d\n", i, ic->manu_name, rc);
				continue;
			}
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_CFG, cfg);
			if (rc < 0)
				chg_err("gauge[%d](%s): can't set sili ic alg cfg, rc=%d\n", i, ic->manu_name, rc);
			return;
		}
	}
}

static void  oplus_mms_gauge_set_sili_ic_alg_term_volt(
			struct oplus_mms *mms, int volt)
{
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_TERM_VOLT, volt);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't set gauge sili ic alg term volt, rc=%d\n", i, ic->manu_name, rc);
				continue;
			}
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_SET_SILI_IC_ALG_TERM_VOLT, volt);
			if (rc < 0)
				chg_err("gauge[%d](%s): can't set gauge sili ic alg term volt, rc=%d\n", i, ic->manu_name, rc);
			return;
		}
	}
}

static int oplus_gauge_get_sili_simulate_term_volt(struct oplus_mms_gauge *chip, int *volt)
{
	int rc = 0;

	if (!chip || !volt)
		return -EINVAL;

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_SIMULATE_TERM_VOLT, volt);

	return rc;
}

static int oplus_gauge_get_sili_ic_alg_dsg_enable(struct oplus_mms_gauge *chip, bool *dsg_enable)
{
	int rc = 0;

	if (!chip || !dsg_enable)
		return -EINVAL;

	rc = oplus_chg_ic_func(chip->gauge_ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_DSG_ENABLE, dsg_enable);

	return rc;
}

static void oplus_mms_gauge_get_sili_ic_alg_term_volt(struct oplus_mms *mms, int *volt)
{
	int temp_volt = 0;
	int max_volt = 0;
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL || volt == NULL) {
		chg_err("mms or volt is NULL");
		return;
	}

	chip = oplus_mms_get_drvdata(mms);

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_TERM_VOLT, &temp_volt);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't get gauge sili ic alg term volt, rc=%d\n", i, ic->manu_name, rc);
				continue;
			}
			if (temp_volt > max_volt)
				max_volt = temp_volt;
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_TERM_VOLT, &max_volt);
			if (rc < 0)
				chg_err("gauge[%d](%s):  can't get gauge sili ic alg term volt, rc=%d\n", i, ic->manu_name, rc);
			return;
		}
	}
	*volt = max_volt;
}

int oplus_gauge_get_sili_alg_application_info(struct oplus_mms *mms, u8 *info, int len)
{
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_ALG_APPLICATION_INFO, info, len);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't get gauge sili alg application, rc=%d\n", i, ic->manu_name, rc);
				continue;
			}
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_ALG_APPLICATION_INFO, info, len);
			if (rc < 0)
				chg_err("gauge[%d](%s): can'tget gauge sili alg application, rc=%d\n", i, ic->manu_name, rc);
			return rc;
		}
	}

	return rc;
}

int oplus_gauge_get_sili_alg_lifetime_info(struct oplus_mms *mms, u8 *info, int len)
{
	int rc = 0;
	int i;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);

	if (mms == chip->gauge_topic) {
		for (i = 0; i < chip->child_num; i++) {
			ic = chip->child_list[i].ic_dev;
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_LIFETIME_INFO, info, len);
			if (rc < 0) {
				chg_err("gauge[%d](%s): can't get gauge sili lifetime info, rc=%d\n", i, ic->manu_name, rc);
				continue;
			}
		}
	} else {
		for (i = 0; i < chip->child_num; i++) {
			if (mms != chip->gauge_topic_parallel[i])
				continue;
			ic = chip->gauge_ic_comb[i];
			rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_LIFETIME_INFO, info, len);
			if (rc < 0)
				chg_err("gauge[%d](%s): can'tget gauge sili lifetime info, rc=%d\n", i, ic->manu_name, rc);
			return rc;
		}
	}

	return rc;
}

static void oplus_gauge_init_sili_status(struct oplus_mms_gauge *chip)
{
	int byb_match_status = 0, batt_match_status = 0;
	int bybid = 0, batt_id = 0;

	if (!chip)
		return;

	byb_match_status = oplus_wired_get_byb_id_match_info(chip->wired_topic);

	batt_match_status = oplus_gauge_get_batt_id_match_info(chip);
	if ((byb_match_status == ID_NOT_MATCH) && (batt_match_status == ID_MATCH_SILI))
		chip->deep_spec.sili_err = true;
	else
		chip->deep_spec.sili_err = false;

	bybid = oplus_wired_get_byb_id_info(chip->wired_topic);
	batt_id = oplus_gauge_get_batt_id_info(chip);

	snprintf(deep_id_info, DUMP_INFO_LEN, "$$deep_support@@%d$$byb_id@@%d$$batt_id@@%d$$sili_err@@%d$$counts@@%d$$uv_thr@@%d",
		chip->deep_spec.support, bybid, batt_id, chip->deep_spec.sili_err, chip->deep_spec.counts, chip->deep_spec.config.uv_thr);

	chg_info(" [%d, %d, %d, %d, %d, %d]\n", byb_match_status, batt_match_status, bybid, batt_id,
		chip->deep_spec.sili_err, chip->deep_spec.support);
}

int oplus_gauge_get_battery_type_str(char *type)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	return -ENOTSUPP; /* todo read cmdline */
#else
	size_t smem_size;
	static oplus_ap_feature_data *smem_data;
	struct device_node *node;

	if (!type)
		return -ENOTSUPP;

	node = of_find_node_by_path("/soc/oplus_chg_core");
	if (node == NULL)
		return -ENOTSUPP;
	if (!of_property_read_bool(node, "oplus,battery_type_by_smem"))
		return -ENOTSUPP;

	if (!smem_data) {
		smem_data = (oplus_ap_feature_data *)qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_OPLUS_CHG, &smem_size);
		if (IS_ERR_OR_NULL(smem_data)) {
			chg_err("unable to acquire smem oplus chg entry\n");
			return -EINVAL;
		}
		if (smem_data->size != sizeof(oplus_ap_feature_data)) {
			chg_err("size invalid %d %zu\n", smem_data->size, sizeof(oplus_ap_feature_data));
			return -EINVAL;
		}
		chg_info("current battery type str = %s\n", smem_data->battery_type_str);
	}

	snprintf(type, OPLUS_BATTERY_TYPE_LEN, "%s", smem_data->battery_type_str);
	return 0;
#endif
}

struct device_node *oplus_get_node_by_type(struct device_node *father_node)
{
	char battery_type_str[OPLUS_BATTERY_TYPE_LEN] = { 0 };
	struct device_node *sub_node = NULL;
	struct device_node *node = father_node;
	int rc = oplus_gauge_get_battery_type_str(battery_type_str);
	if (rc == 0) {
		sub_node = of_get_child_by_name(father_node, battery_type_str);
		if (sub_node)
			node = sub_node;
	}
	return node;
}


void oplus_mms_gauge_update_super_endurance_mode_status_work(struct work_struct *work)
{
	struct oplus_mms_gauge *chip =
		container_of(work, struct oplus_mms_gauge, update_super_endurance_mode_status_work);

	if (!chip->deep_spec.support)
		return;
	if (chip->deep_spec.sili_ic_alg_dsg_enable)
		oplus_mms_gauge_update_sili_ic_alg_term_volt(chip, true);

	vote(chip->gauge_shutdown_voltage_votable, SUPER_ENDURANCE_MODE_VOTER,
			!chip->super_endurance_mode_status, chip->deep_spec.config.term_voltage, false);
}

void oplus_mms_gauge_update_sili_ic_alg_term_volt(
	struct oplus_mms_gauge *chip, bool force)
{
	int alg_term_volt = 0;
	static bool first_record = true;

	mutex_lock(&chip->deep_spec.lock);
	if (!chip->deep_spec.sili_ic_alg_dsg_enable)
		goto not_handle;

	/* update just for uisoc < 15% */
	if (chip->ui_soc >= 15) {
		first_record = true;
		goto not_handle;
	}

	if (!chip->deep_spec.config.term_voltage ||
	    !is_client_vote_enabled(chip->gauge_term_voltage_votable, READY_VOTER)) {
		goto not_handle;
	}

	oplus_mms_gauge_get_sili_ic_alg_term_volt(chip->gauge_topic, &alg_term_volt);
	if (alg_term_volt && (force || first_record || !chip->deep_spec.sili_ic_alg_term_volt ||
	    chip->deep_spec.sili_ic_alg_term_volt > alg_term_volt)) {
		chip->deep_spec.sili_ic_alg_term_volt = alg_term_volt;
		oplus_mms_gauge_set_sili_ic_alg_term_volt(chip->gauge_topic, chip->deep_spec.sili_ic_alg_term_volt);
		if (!chip->super_endurance_mode_status)
			chip->deep_spec.config.uv_thr = alg_term_volt;
		else
			chip->deep_spec.config.uv_thr = alg_term_volt - GAUGE_TERM_VOLT_EFFECT_GAP_MV(100);
		chg_info("uv_thr=%d\n", chip->deep_spec.config.uv_thr);
		oplus_mms_gauge_push_vbat_uv(chip);
	}
	first_record = false;
not_handle:
	mutex_unlock(&chip->deep_spec.lock);
}

void oplus_mms_gauge_update_sili_ic_alg_cfg_work(struct work_struct *work)
{
	int rc;
	int alg_cfg;
	union mms_msg_data data = { 0 };
	struct oplus_mms_gauge *chip =
		container_of(work, struct oplus_mms_gauge, update_sili_ic_alg_cfg_work);

	if (!chip->deep_spec.support || !chip->deep_spec.sili_ic_alg_support) {
		chg_err("not support\n");
		return;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SILI_IC_ALG_CFG, &data, false);
	if (rc < 0)
		return;

	alg_cfg = data.intval;
	mutex_lock(&chip->deep_spec.lock);
	oplus_mms_gauge_set_sili_ic_alg_cfg(chip->gauge_topic, alg_cfg);
	oplus_gauge_get_sili_ic_alg_dsg_enable(chip, &chip->deep_spec.sili_ic_alg_dsg_enable);
	if (!chip->deep_spec.sili_ic_alg_dsg_enable) {
		vote(chip->gauge_term_voltage_votable, READY_VOTER, false, 0, true);
		vote(chip->gauge_shutdown_voltage_votable, SUPER_ENDURANCE_MODE_VOTER,
			!chip->super_endurance_mode_status, chip->deep_spec.config.term_voltage, false);
		vote(chip->gauge_shutdown_voltage_votable, READY_VOTER, false, 0, false);
		chip->deep_spec.sili_ic_alg_term_volt = 0;
		oplus_mms_gauge_set_sili_ic_alg_term_volt(chip->gauge_topic, chip->deep_spec.sili_ic_alg_term_volt);
	} else {
		vote(chip->gauge_shutdown_voltage_votable, READY_VOTER, true, INVALID_MAX_VOLTAGE, false);
		vote(chip->gauge_term_voltage_votable, READY_VOTER, true, INVALID_MAX_VOLTAGE, false);
	}
	mutex_unlock(&chip->deep_spec.lock);
	chg_info("alg_cfg=0x%x, sili_ic_alg_enable=%d\n", alg_cfg, chip->deep_spec.sili_ic_alg_dsg_enable);
}

void oplus_mms_gauge_update_sili_spare_power_enable_work(struct work_struct *work)
{
	int soc;
	int temp;
	bool enable;
	union mms_msg_data data = { 0 };
	struct oplus_mms_gauge *chip =
		container_of(work, struct oplus_mms_gauge, update_sili_spare_power_enable_work);

	if (!chip->deep_spec.support || !chip->deep_spec.spare_power_support || !chip->deep_spec.sili_ic_alg_dsg_enable) {
		chg_err("not support\n");
		return;
	}

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SPARE_POWER_ENABLE, &data, false);
	if (!!data.intval)
		enable = true;
	else
		enable = false;

	chg_info("enable=%d\n", enable);
	if (!is_support_parallel(chip)) {
		oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data, false);
		temp = data.intval;
		oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data, false);
		soc = data.intval;
	} else {
		oplus_mms_get_item_data(chip->gauge_topic_parallel[chip->main_gauge], GAUGE_ITEM_TEMP, &data, false);
		temp = data.intval;
		oplus_mms_get_item_data(chip->gauge_topic_parallel[chip->main_gauge], GAUGE_ITEM_SOC, &data, false);
		soc = data.intval;
	}

	chip->deep_spec.spare_power_enable = false;
	/* support for battery temp at 25C-40C and uisoc <= 5% and real soc > 0 */
	if (temp > 250 && temp < 400 && chip->ui_soc <= 5 && soc) {
		oplus_mms_gauge_set_sili_spare_power_enable(chip->gauge_topic);
		cancel_delayed_work(&chip->sili_spare_power_effect_check_work);
		schedule_delayed_work(&chip->sili_spare_power_effect_check_work, msecs_to_jiffies(2000));
	}
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SPARE_POWER_ENABLE, &data, true);
}

void oplus_mms_gauge_sili_spare_power_effect_check_work(struct work_struct *work)
{
	int alg_term_volt = 0;
	int spare_power_term_volt;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, sili_spare_power_effect_check_work);

	spare_power_term_volt = chip->deep_spec.config.spare_power_term_voltage;
	oplus_mms_gauge_get_sili_ic_alg_term_volt(chip->gauge_topic, &alg_term_volt);
	chg_info("ic_alg_term_volt=%d\n", alg_term_volt);

	if (abs(spare_power_term_volt - alg_term_volt) < GAUGE_TERM_VOLT_EFFECT_GAP_MV(20)) {
		chg_info("spare power set success\n");
	} else {
		oplus_mms_gauge_set_sili_spare_power_enable(chip->gauge_topic);
		schedule_delayed_work(&chip->sili_spare_power_effect_check_work, msecs_to_jiffies(2000));
	}
}

void oplus_mms_gauge_sili_term_volt_effect_check_work(struct work_struct *work)
{
	int rc;
	int simulate_volt = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, sili_term_volt_effect_check_work);

	if (chip->deep_spec.sili_ic_alg_dsg_enable) {
		chg_err("dsg enable, not need check\n");
		return;
	}

	rc = oplus_gauge_get_sili_simulate_term_volt(chip, &simulate_volt);
	if (rc < 0)
		return;

	chg_info("expect term voltage=%d, simulate volt=%d\n", chip->deep_spec.config.term_voltage, simulate_volt);
	if (abs(chip->deep_spec.config.term_voltage - simulate_volt) < GAUGE_TERM_VOLT_EFFECT_GAP_MV(20))
		chg_info("self-developed expect term voltage set success\n");
	else
		oplus_mms_gauge_set_deep_term_volt(chip->gauge_topic, chip->deep_spec.config.term_voltage);
}

int oplus_gauge_parse_deep_spec(struct oplus_mms_gauge *chip)
{
	struct device_node *node;
	int i = 0, rc = 0, length;
	u32 data;
	struct device_node *curves_node;
	struct device_node *startegy_node;
	struct deep_dischg_limits *config = &chip->deep_spec.config;

	if (!chip)
		return -ENODEV;

	node = oplus_get_node_by_type(chip->dev->of_node);

	rc = of_property_count_elems_of_size(node, "deep_spec,term_coeff", sizeof(u32));
	if (rc < 0) {
		chg_err("Count deep spec term_coeff failed, rc=%d\n", rc);
	} else {
		length = rc;
		if (length % DDT_COEFF_SIZE == 0 &&
		    length / DDT_COEFF_SIZE <= DDC_CURVE_MAX) {
			rc = of_property_read_u32_array(node, "deep_spec,term_coeff", (u32 *)chip->deep_spec.term_coeff,
							length);
			chip->deep_spec.term_coeff_size = length / DDT_COEFF_SIZE;
		}
	}
	rc = of_property_read_u32(node, "deep_spec,uv_thr",
			&chip->deep_spec.config.uv_thr);
	if (rc < 0)
		chip->deep_spec.config.uv_thr = 3000;

	rc = of_property_read_u32(node, "deep_spec,count_cali",
			&chip->deep_spec.config.count_cali);
	if (rc < 0)
		chip->deep_spec.config.count_cali = 0;

	rc = of_property_read_u32(node, "deep_spec,count_thr",
			&chip->deep_spec.config.count_thr);
	if (rc < 0)
		chip->deep_spec.config.count_thr = 1;

	rc = of_property_read_u32(node, "deep_spec,spare_power_term_voltage",
			&chip->deep_spec.config.spare_power_term_voltage);
	if (rc < 0)
		chip->deep_spec.config.spare_power_term_voltage = 2700;

	rc = of_property_read_u32(node, "deep_spec,vbat_soc",
			&chip->deep_spec.config.soc);
	if (rc < 0)
		chip->deep_spec.config.soc = 10;

	chip->deep_spec.support = of_property_read_bool(node, "deep_spec,support");
	chip->deep_spec.spare_power_support = of_property_read_bool(node, "deep_spec,spare_power_support");
	chip->deep_spec.sili_ic_alg_support = of_property_read_bool(node, "deep_spec,sili_ic_alg_support");

	rc = of_property_read_u32(node, "deep_spec,ratio_thr",
				&chip->deep_spec.config.ratio_default);
	if (rc < 0)
		chip->deep_spec.config.ratio_default = 30;
	chip->deep_spec.config.ratio_shake = chip->deep_spec.config.ratio_default;
	chip->deep_spec.config.sub_ratio_shake = chip->deep_spec.config.ratio_default;

	rc = of_property_count_elems_of_size(node, "deep_spec,count_step", sizeof(u32));
	if (rc < 0) {
		chg_err("Count deep spec count_step curve failed, rc=%d\n", rc);
	} else {
		length = rc;
		rc = of_property_read_u32_array(node, "deep_spec,count_step",
								(u32 *)chip->deep_spec.step_curves.limits, length);
		chip->deep_spec.step_curves.nums = length / 3;
	}

	if (chip->deep_spec.sili_ic_alg_support) {
		rc = of_property_count_elems_of_size(node, "deep_spec,sili_alg_cfg_list", sizeof(u32));
		if (rc > 0 && rc <= SILI_CFG_TYPE_MAX) {
			chip->deep_spec.sili_ic_alg_cfg.nums = rc;
			of_property_read_u32_array(node, "deep_spec,sili_alg_cfg_list",
								(u32 *)chip->deep_spec.sili_ic_alg_cfg.list, rc);
		}
	}

	rc = of_property_read_u32(node, "deep_spec,volt_step", &chip->deep_spec.config.volt_step);
	if (rc < 0)
		chip->deep_spec.config.volt_step = 100;

	curves_node = of_get_child_by_name(node, "deep_spec,ddbc_curve");
	if (!curves_node) {
		chg_err("Can not find deep_spec,ddbc_curve node\n");
		rc = -ENODEV;
		goto ddbc_strategy_err;
	}
	rc = of_property_read_u32(curves_node, "oplus,temp_type", &data);
	if (rc < 0) {
		chg_err("oplus,temp_type reading failed, rc=%d\n", rc);
		chip->deep_spec.ddbc_tbatt.temp_type = STRATEGY_USE_SHELL_TEMP;
	} else {
		chip->deep_spec.ddbc_tbatt.temp_type = (int)data;
	}

	rc = of_property_count_elems_of_size(curves_node, "oplus,temp_range", sizeof(u32));
	if (rc < 0) {
		chg_err("Count temp_range failed, rc=%d\n", rc);
		rc = -ENODEV;
		goto ddbc_strategy_err;
	}
	length = rc;
	rc = of_property_read_u32_array(curves_node, "oplus,temp_range",
							(u32 *)chip->deep_spec.ddbc_tbatt.range, length);
	if (rc < 0) {
		chg_err("get oplus,temp_range property error, rc=%d\n", rc);
		rc = -ENODEV;
		goto ddbc_strategy_err;
	}
	for (i = 0; i < DDB_CURVE_TEMP_WARM; i++)
		chip->deep_spec.ddbc_tdefault.range[i] = chip->deep_spec.ddbc_tbatt.range[i];

	for (i = 0; i <= DDB_CURVE_TEMP_WARM; i++) {
		rc = of_property_count_elems_of_size(curves_node, ddbc_curve_range_name[i], sizeof(u32));
		if (rc < 0) {
			chg_err("Count ddbc_curve_range_name %s failed, rc=%d\n",
				ddbc_curve_range_name[i], rc);
			rc = -ENODEV;
			goto ddbc_strategy_err;
		}
		length = rc;
		rc = of_property_read_u32_array(curves_node, ddbc_curve_range_name[i],
						(u32 *)chip->deep_spec.batt_curves[i].limits, length);
		if (rc < 0) {
			chg_err("parse chip->deep_spec.batt_curves[%d].limits failed, rc=%d\n", i, rc);
		}
		chip->deep_spec.batt_curves[i].nums = length / 3;
	}

	rc = of_property_read_string(node, "deep_spec,ddrc_strategy_name", (const char **)&config->ddrc_strategy_name);
	if (rc < 0) {
		chg_err("oplus,ddrc_strategy_name reading failed, rc=%d\n", rc);
		config->ddrc_strategy_name = "ddrc_curve";
	}
	startegy_node = of_get_child_by_name(oplus_get_node_by_type((chip->dev->of_node)), "ddrc_strategy");
	if (startegy_node == NULL) {
		chg_err("ddrc_strategy not found\n");
		rc = -ENODEV;
		goto ddrc_strategy_err;
	}
	chip->ddrc_strategy = oplus_chg_strategy_alloc_by_node(config->ddrc_strategy_name, startegy_node);
	if (IS_ERR_OR_NULL(chip->ddrc_strategy)) {
		chg_err("alloc deep dischg ratio startegy error, rc=%ld", PTR_ERR(chip->ddrc_strategy));
		chip->ddrc_strategy = NULL;
		rc = -ENODEV;
		goto ddrc_strategy_err;
	}

	rc = of_property_read_u32(startegy_node, "oplus,temp_type", &data);
	if (rc < 0) {
		chg_err("ddrc_tbatt oplus,temp_type reading failed, rc=%d\n", rc);
		chip->deep_spec.ddrc_tbatt.temp_type = STRATEGY_USE_SHELL_TEMP;
	} else {
		chip->deep_spec.ddrc_tbatt.temp_type = (int)data;
	}

	rc = of_property_count_elems_of_size(startegy_node, "oplus,temp_range", sizeof(u32));
	if (rc < 0) {
		chg_err("Count ratio_temp_range failed, rc=%d\n", rc);
		chip->ddrc_strategy = NULL;
		rc = -ENODEV;
		goto ddrc_strategy_err;
	}
	length = rc;
	rc = of_property_read_u32_array(startegy_node, "oplus,temp_range",
							(u32 *)chip->deep_spec.ddrc_tbatt.range, length);
	if (rc < 0) {
		chg_err("ddrc_tbatt get oplus,temp_range property error, rc=%d\n", rc);
	}
	for (i = 0; i < DDB_CURVE_TEMP_WARM; i++)
		chip->deep_spec.ddrc_tdefault.range[i] = chip->deep_spec.ddrc_tbatt.range[i];

ddbc_strategy_err:
ddrc_strategy_err:

	return rc;
}

int oplus_mms_gauge_sili_ic_alg_cfg_init(struct oplus_mms_gauge *chip)
{
	int i;
	int rc = 0;
	int alg_cfg = 0;

	if (!chip->deep_spec.sili_ic_alg_support)
		return -EINVAL;

	for (i = 0; i < chip->deep_spec.sili_ic_alg_cfg.nums; i++)
		alg_cfg |= BIT(chip->deep_spec.sili_ic_alg_cfg.list[i]);

	oplus_mms_gauge_set_sili_ic_alg_cfg(chip->gauge_topic, alg_cfg);
	oplus_gauge_get_sili_ic_alg_dsg_enable(chip, &chip->deep_spec.sili_ic_alg_dsg_enable);
	chg_info("alg_cfg:0x%x, sili_ic_alg_enable:%d\n", alg_cfg, chip->deep_spec.sili_ic_alg_dsg_enable);

	if (chip->deep_spec.sili_ic_alg_dsg_enable) {
		vote(chip->gauge_shutdown_voltage_votable, READY_VOTER, true, INVALID_MAX_VOLTAGE, false);
		vote(chip->gauge_term_voltage_votable, READY_VOTER, true, INVALID_MAX_VOLTAGE, false);
		chg_info("sili_ic_alg_dsg_enable end\n");
	}
	return rc;
}


int oplus_mms_gauge_update_sili_ic_alg_term_volt_data(
					struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int volt = 0;
	int rc;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);
	rc = oplus_chg_ic_func(chip->gauge_ic,
		OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_TERM_VOLT, &volt);
	if (rc < 0) {
		chg_err("get sili ic alg term volt error, rc=%d\n", rc);
		return -EINVAL;
	}

	data->intval = volt;
	return 0;
}

int oplus_mms_sub_gauge_update_sili_ic_alg_term_volt(
					struct oplus_mms *mms, union mms_msg_data *data)
{
	int rc = -1;
	int i;
	int volt = 0;
	struct oplus_mms_gauge *chip;
	struct oplus_chg_ic_dev *ic;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);

	for (i = 0; i < chip->child_num; i++) {
		if (mms != chip->gauge_topic_parallel[i])
			continue;
		ic = chip->gauge_ic_comb[i];
		rc = oplus_chg_ic_func(ic, OPLUS_IC_FUNC_GAUGE_GET_SILI_IC_ALG_TERM_VOLT, &volt);
		if (rc < 0)
			chg_err("gauge[%d](%s): can't get gauge sili ci alg term volt, rc=%d\n", i, ic->manu_name, rc);
		break;
	}

	if (rc == 0)
		data->intval = volt;

	return rc;
}

int oplus_mms_gauge_update_sili_ic_alg_dsg_enable(
					struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	data->intval = chip->deep_spec.sili_ic_alg_dsg_enable;
	return 0;
}

static int mms_gauge_debug_track = 0;
module_param(mms_gauge_debug_track, int, 0644);
MODULE_PARM_DESC(mms_gauge_debug_track, "debug track");
#define TRACK_UPLOAD_COUNT_MAX 3
#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int oplus_mms_gauge_upload_deep_dischg(struct oplus_mms_gauge *chip, char *deep_msg, bool main_batt)
{
	struct oplus_mms *err_topic;
	struct mms_msg *msg;
	int rc;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	static int sub_upload_count = 0;
	static int sub_pre_upload_time = 0;
	int curr_time;

	if (!chip)
		return -ENODEV;

	curr_time = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	if (main_batt) {
		if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
			upload_count = 0;

		if (upload_count >= TRACK_UPLOAD_COUNT_MAX)
			return -ENODEV;

		pre_upload_time = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	} else {
		if (curr_time - sub_pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
			sub_upload_count = 0;

		if (sub_upload_count >= TRACK_UPLOAD_COUNT_MAX)
			return -ENODEV;

		sub_pre_upload_time = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	}

	err_topic = oplus_mms_get_by_name("error");
	if (!err_topic) {
		chg_err("error topic not found\n");
		return -ENODEV;
	}

	msg = oplus_mms_alloc_str_msg(
		MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, ERR_ITEM_DEEP_DISCHG_INFO, deep_msg);
	if (msg == NULL) {
		chg_err("alloc usbtemp error msg error\n");
		return -ENOMEM;
	}

	rc = oplus_mms_publish_msg(err_topic, msg);
	if (rc < 0) {
		chg_err("publish deep dischg error msg error, rc=%d\n", rc);
		kfree(msg);
	}
	if (main_batt)
		upload_count++;
	else
		sub_upload_count++;

	return rc;
}

static void oplus_gauge_update_deep_dischg(struct oplus_mms_gauge *chip)
{
	union mms_msg_data data = { 0 };
	unsigned long update_delay = 0;
	static int cnts = 0;
	int ui_soc, vbat_min_mv, batt_temp, ibat_ma;
	int rc, i, iterm, vterm, ctime;
	bool charging, low_curr = false, track_check = false;
	int step = 1;
	int temp_region = DDB_CURVE_TEMP_WARM;

	charging = chip->wired_online || chip->wls_online;
	if (charging) {
		cnts = 0;
		return;
	}

	ui_soc = chip->ui_soc;
	rc = oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UI_SOC, &data,
					true);
	if (rc < 0) {
		chg_err("can't get ui_soc, rc=%d\n", rc);
		chip->ui_soc = 0;
	} else {
		chip->ui_soc = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		vbat_min_mv = 0;
	} else {
		vbat_min_mv = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		batt_temp = 0;
	} else {
		batt_temp = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get ibat_ma, rc=%d\n", rc);
		ibat_ma = 0;
	} else {
		ibat_ma = data.intval;
	}

	if (chip->deep_spec.step_curves.nums) {
		for (i = chip->deep_spec.step_curves.nums - 1; i > 0; i--) {
			if (batt_temp >= chip->deep_spec.step_curves.limits[i].temp)
				break;
		}
		step = chip->deep_spec.step_curves.limits[i].step;
	}

	temp_region = chip->deep_spec.ddbc_tbatt.index_n;
	for (i = 0; i < chip->deep_spec.batt_curves[temp_region].nums; i++) {
		iterm = chip->deep_spec.batt_curves[temp_region].limits[i].iterm;
		vterm = chip->deep_spec.batt_curves[temp_region].limits[i].vterm;
		ctime = chip->deep_spec.batt_curves[temp_region].limits[i].ctime;
		if ((ibat_ma <= iterm) && (vbat_min_mv <= vterm)) {
			low_curr = true;
			break;
		}
	}

	if (low_curr) {
		if (++cnts >= ctime) {
			cnts = 0;
			chip->deep_spec.counts += step;
			track_check = true;
			if (is_support_parallel(chip))
				oplus_gauge_set_deep_dischg_count(chip->gauge_topic_parallel[chip->main_gauge],
								  chip->deep_spec.counts);
			else
				oplus_gauge_set_deep_dischg_count(chip->gauge_topic, chip->deep_spec.counts);
			oplus_gauge_get_ratio_value(chip->gauge_topic);
		} else {
			update_delay = msecs_to_jiffies(5000);
		}
	} else {
		cnts = 0;
		update_delay = msecs_to_jiffies(5000);
	}

	if (track_check || mms_gauge_debug_track) {
		track_check = false;
		mms_gauge_debug_track = 0;
		memset(&(chip->deep_info), 0, sizeof(chip->deep_info));
		chip->deep_info.index += snprintf(chip->deep_info.msg, DEEP_INFO_LEN,
			"$$track_reason@@deep_dischg$$trange@@%d", temp_region);
		schedule_delayed_work(&chip->deep_track_work, 0);
	}

	if (update_delay > 0)
		schedule_delayed_work(&chip->deep_dischg_work, update_delay);
}

static void oplus_gauge_update_sub_deep_dischg(struct oplus_mms_gauge *chip)
{
	union mms_msg_data data = { 0 };
	unsigned long update_delay = 0;
	static int sub_cnts = 0;
	int sub_vbat_mv, sub_batt_temp, sub_ibat_ma;
	int rc, i, iterm, vterm, sub_ctime;
	bool charging, track_check = false;
	int sub_step = 1;
	bool sub_low_curr = false;
	int temp_region = DDB_CURVE_TEMP_WARM;

	charging = chip->wired_online || chip->wls_online;
	if (charging) {
		sub_cnts = 0;
		return;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
				     GAUGE_ITEM_VOL_MIN, &data, false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		sub_vbat_mv = 0;
	} else {
		sub_vbat_mv = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
				     GAUGE_ITEM_TEMP, &data, false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		sub_batt_temp = 0;
	} else {
		sub_batt_temp = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
				     GAUGE_ITEM_CURR, &data, false);
	if (rc < 0) {
		chg_err("can't get ibat_ma, rc=%d\n", rc);
		sub_ibat_ma = 0;
	} else {
		sub_ibat_ma = data.intval;
	}

	if (chip->deep_spec.step_curves.nums) {
		for (i = chip->deep_spec.step_curves.nums - 1; i > 0; i--) {
			if (sub_batt_temp >= chip->deep_spec.step_curves.limits[i].temp)
				break;
		}
		sub_step = chip->deep_spec.step_curves.limits[i].step;
	}

	temp_region = chip->deep_spec.ddbc_tbatt.index_n;
	for (i = 0; i < chip->deep_spec.batt_curves[temp_region].nums; i++) {
		iterm = chip->deep_spec.batt_curves[temp_region].limits[i].iterm;
		vterm = chip->deep_spec.batt_curves[temp_region].limits[i].vterm;
		sub_ctime = chip->deep_spec.batt_curves[temp_region].limits[i].ctime;
		if ((sub_ibat_ma <= iterm) && (sub_vbat_mv <= vterm)) {
			sub_low_curr = true;
			break;
		}
	}

	if (sub_low_curr) {
		if (++sub_cnts >= sub_ctime) {
			sub_cnts = 0;
			chip->deep_spec.sub_counts += sub_step;
			track_check = true;
			oplus_gauge_set_deep_dischg_count(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
				chip->deep_spec.sub_counts);
			oplus_gauge_get_ratio_value(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]);
		} else {
			update_delay = msecs_to_jiffies(5000);
		}
	} else {
		sub_cnts = 0;
		update_delay = msecs_to_jiffies(5000);
	}


	if (track_check || mms_gauge_debug_track) {
		track_check = false;
		mms_gauge_debug_track = 0;
		memset(&(chip->deep_info), 0, sizeof(chip->deep_info));
		chip->deep_info.index += snprintf(chip->deep_info.msg, DEEP_INFO_LEN,
			"$$track_reason@@deep_dischg$$trange@@%d", temp_region);
		schedule_delayed_work(&chip->sub_deep_track_work, 0);
	}

	if (update_delay > 0)
		schedule_delayed_work(&chip->sub_deep_dischg_work, update_delay);
}

void oplus_gauge_deep_dischg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, deep_dischg_work);

	oplus_gauge_update_deep_dischg(chip);
}

void oplus_gauge_sub_deep_dischg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, sub_deep_dischg_work);

	oplus_gauge_update_sub_deep_dischg(chip);
}

void oplus_gauge_deep_ratio_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, deep_ratio_work);

	mutex_lock(&chip->deep_spec.lock);
	if ((chip->deep_spec.ddrc_tbatt.index_n >= DDB_CURVE_TEMP_NORMAL &&
		chip->deep_spec.ddrc_tbatt.index_p < DDB_CURVE_TEMP_NORMAL) ||
		(chip->deep_spec.ddrc_tbatt.index_n < DDB_CURVE_TEMP_NORMAL &&
		chip->deep_spec.ddrc_tbatt.index_p >= DDB_CURVE_TEMP_NORMAL)) {
		chip->deep_spec.config.step_status = true;
		oplus_gauge_get_ddrc_status(chip->gauge_topic);
		if (chip->sub_gauge)
			oplus_gauge_get_ddrc_status(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]);
		chip->deep_spec.ddrc_tbatt.index_p = chip->deep_spec.ddrc_tbatt.index_n;
	} else if (chip->deep_spec.ddrc_tbatt.index_n >= DDB_CURVE_TEMP_NORMAL) {
		oplus_gauge_get_ddrc_status(chip->gauge_topic);
		if (chip->sub_gauge)
			oplus_gauge_get_ddrc_status(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]);
		chip->deep_spec.ddrc_tbatt.index_p = chip->deep_spec.ddrc_tbatt.index_n;
	}
	mutex_unlock(&chip->deep_spec.lock);
	schedule_delayed_work(&chip->deep_track_work, 0);
}

void oplus_gauge_deep_temp_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, deep_temp_work);
	int tratio_now = DDB_CURVE_TEMP_COLD, tcurve_now = DDB_CURVE_TEMP_COLD;
	int tratio_pre, tcurve_pre;
	bool charging;
#define TEMP_CNTS 3

	if (!chip->deep_spec.support)
		return;

	charging = chip->wired_online || chip->wls_online;
	tratio_now = oplus_gauge_ddrc_get_temp_region(chip);
	tcurve_now = oplus_gauge_ddbc_get_temp_region(chip);
	tratio_pre = chip->deep_spec.ddrc_tbatt.index_n;
	tcurve_pre = chip->deep_spec.ddbc_tbatt.index_n;

	mutex_lock(&chip->deep_spec.lock);
	if (tratio_pre != tratio_now) {
		chip->deep_spec.cnts.ratio++;
		if (chip->deep_spec.cnts.ratio >= TEMP_CNTS) {
			chip->deep_spec.ddrc_tbatt.index_n = tratio_now;
			oplus_gauge_ddrc_temp_thr_init(chip);
			oplus_gauge_ddrc_temp_thr_update(chip, tratio_now, tratio_pre);
		}
	} else {
		chip->deep_spec.cnts.ratio = 0;
		if (chip->deep_spec.ddrc_tbatt.index_n < DDB_CURVE_TEMP_NORMAL &&
			chip->deep_spec.ddrc_tbatt.index_p > tratio_now &&
			!charging && (chip->ui_soc < chip->deep_spec.config.soc)) {
			chip->deep_spec.config.step_status = true;
			oplus_gauge_get_ddrc_status(chip->gauge_topic);
			if (chip->sub_gauge)
				oplus_gauge_get_ddrc_status(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]);
			chip->deep_spec.ddrc_tbatt.index_p = tratio_now;
		}
	}

	if (tcurve_pre != tcurve_now) {
		chip->deep_spec.cnts.dischg++;
		if (chip->deep_spec.cnts.dischg >= TEMP_CNTS) {
			oplus_gauge_ddbc_temp_thr_init(chip);
			oplus_gauge_ddbc_temp_thr_update(chip, tcurve_now, tcurve_pre);
			chip->deep_spec.ddbc_tbatt.index_n = tcurve_now;
		}
	} else {
		chip->deep_spec.cnts.dischg = 0;
	}
	mutex_unlock(&chip->deep_spec.lock);

	schedule_delayed_work(&chip->deep_temp_work, msecs_to_jiffies(5000));
}

void oplus_gauge_deep_dischg_check(struct oplus_mms_gauge *chip)
{
	union mms_msg_data data = { 0 };
	bool charging;

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UI_SOC, &data, false);
	chip->ui_soc = data.intval;

	if (!chip->deep_spec.support)
		return;
	charging = chip->wired_online || chip->wls_online;

	if (!charging && (chip->ui_soc >= chip->deep_spec.config.soc)) {
		schedule_delayed_work(&chip->deep_dischg_work, 0);
		if (chip->sub_gauge)
			schedule_delayed_work(&chip->sub_deep_dischg_work, 0);
		schedule_delayed_work(&chip->deep_ratio_work, 0);
	} else {
		cancel_delayed_work(&chip->deep_dischg_work);
		if (chip->sub_gauge)
			cancel_delayed_work(&chip->sub_deep_dischg_work);
	}
}

void oplus_gauge_deep_id_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, deep_id_work);

	oplus_chg_ic_creat_err_msg(chip->child_list[chip->main_gauge].ic_dev, OPLUS_IC_ERR_BATT_ID, 0, deep_id_info);
	oplus_chg_ic_virq_trigger(chip->child_list[chip->main_gauge].ic_dev, OPLUS_IC_VIRQ_ERR);
}

void oplus_gauge_deep_track_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, deep_track_work);

	int vbat_min_mv, batt_temp, ibat_ma, term_volt;
	int bybid = 0, batt_id = 0;
	int rc;
	union mms_msg_data data = { 0 };

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data, false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		vbat_min_mv = 0;
	} else {
		vbat_min_mv = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		batt_temp = 0;
	} else {
		batt_temp = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get ibat_ma, rc=%d\n", rc);
		ibat_ma = 0;
	} else {
		ibat_ma = data.intval;
	}
	term_volt = get_effective_result(chip->gauge_term_voltage_votable);

	bybid = oplus_wired_get_byb_id_info(chip->wired_topic);
	batt_id = oplus_gauge_get_batt_id_info(chip);
	chip->deep_info.index += snprintf(&(chip->deep_info.msg[chip->deep_info.index]),
		DEEP_INFO_LEN - chip->deep_info.index, "$$dischg_counts@@%d$$count_thr@@%d$$count_cali@@%d$$cc@@%d"
		"$$ratio@@%d$$vbat_uv@@%d$$vterm@@%d$$vbat_min@@%d$$tbat@@%d$$ui_soc@@%d$$ibat_ma@@%d$$bybid@@%d"
		"$$batt_id@@%d$$sili_err@@%d", chip->deep_spec.counts, chip->deep_spec.config.count_thr,
		chip->deep_spec.config.count_cali, chip->deep_spec.cc, chip->deep_spec.ratio,
		chip->deep_spec.config.uv_thr, term_volt, vbat_min_mv, batt_temp, chip->ui_soc,
		ibat_ma, bybid, batt_id, chip->deep_spec.sili_err);
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_REG_INFO, &data, true);
	if (rc == 0 && data.strval && strlen(data.strval)) {
		chg_err("[main_gauge_reg_info] %s", data.strval);
		chip->deep_info.index += snprintf(&(chip->deep_info.msg[chip->deep_info.index]),
			DEEP_INFO_LEN - chip->deep_info.index, "$$maingaugeinfo@@%s", data.strval);
	}

	oplus_mms_gauge_upload_deep_dischg(chip, chip->deep_info.msg, 1);
	memset(&(chip->deep_info), 0, sizeof(chip->deep_info));
}

void oplus_gauge_sub_deep_track_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_mms_gauge *chip =
		container_of(dwork, struct oplus_mms_gauge, sub_deep_track_work);

	int vbat_min_mv, batt_temp, ibat_ma, term_volt;
	int bybid = 0, batt_id = 0;
	int rc;
	union mms_msg_data data = { 0 };

	rc = oplus_mms_get_item_data(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
		GAUGE_ITEM_VOL_MIN, &data, false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		vbat_min_mv = 0;
	} else {
		vbat_min_mv = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
		GAUGE_ITEM_TEMP, &data, false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		batt_temp = 0;
	} else {
		batt_temp = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
		GAUGE_ITEM_CURR, &data, false);
	if (rc < 0) {
		chg_err("can't get ibat_ma, rc=%d\n", rc);
		ibat_ma = 0;
	} else {
		ibat_ma = data.intval;
	}
	term_volt = get_effective_result(chip->gauge_term_voltage_votable);

	chip->deep_info.index += snprintf(&(chip->deep_info.msg[chip->deep_info.index]),
		DEEP_INFO_LEN - chip->deep_info.index, "$$sub_dischg_counts@@%d$$count_thr@@%d$$count_cali@@%d$$sub_cc@@%d$$sub_ratio@@%d"
		"$$vbat_uv@@%d$$vterm@@%d$$vbat_min@@%d$$tbat@@%d$$ui_soc@@%d$$ibat_ma@@%d$$bybid@@%d$$batt_id@@%d$$sili_err@@%d",
		chip->deep_spec.sub_counts, chip->deep_spec.config.count_thr, chip->deep_spec.config.count_cali, chip->deep_spec.sub_cc,
		chip->deep_spec.sub_ratio, chip->deep_spec.config.uv_thr, term_volt, vbat_min_mv, batt_temp,
		chip->ui_soc, ibat_ma, bybid, batt_id, chip->deep_spec.sili_err);

	rc = oplus_mms_get_item_data(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)],
		GAUGE_ITEM_REG_INFO, &data, true);
	if (rc == 0 && data.strval && strlen(data.strval)) {
		chg_err("[sub_gauge_reg_info] %s", data.strval);
		chip->deep_info.index += snprintf(&(chip->deep_info.msg[chip->deep_info.index]),
			DEEP_INFO_LEN - chip->deep_info.index, "$$subgaugeinfo@@%s", data.strval);
	}

	oplus_mms_gauge_upload_deep_dischg(chip, chip->deep_info.msg, 0);
	memset(&(chip->deep_info), 0, sizeof(chip->deep_info));
}


int oplus_mms_gauge_update_spare_power_enable(
					struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	data->intval = chip->deep_spec.spare_power_enable;
	return 0;
}


int oplus_mms_gauge_update_vbat_uv(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);
	data->intval = chip->deep_spec.config.uv_thr;
	chg_info("[%d, %d]\n", chip->deep_spec.config.uv_thr, chip->deep_spec.config.count_thr);
	return 0;
}

int oplus_mms_gauge_update_ratio_trange(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);

	if (!chip  || !chip->deep_spec.support)
		return 0;

	data->intval = chip->deep_spec.ddrc_tbatt.index_n;

	return 0;
}

int oplus_mms_gauge_update_deep_ratio(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}
	chip = oplus_mms_get_drvdata(mms);
	data->intval = chip->deep_spec.ratio;
	return 0;
}

int oplus_mms_gauge_get_si_prop(struct oplus_mms *mms, union mms_msg_data *data)
{
	struct oplus_mms_gauge *chip;
	int rc = 0;

	if (mms == NULL) {
		chg_err("mms is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);
	data->intval = chip->deep_spec.support;
	return rc;
}

void oplus_mms_gauge_sili_init(struct oplus_mms_gauge *chip)
{
	if (!chip->deep_spec.support)
		return;

	vote(chip->gauge_shutdown_voltage_votable, READY_VOTER, true, INVALID_MAX_VOLTAGE, false);
	vote(chip->gauge_term_voltage_votable, READY_VOTER, true, INVALID_MAX_VOLTAGE, false);
	vote(chip->gauge_shutdown_voltage_votable, SPEC_VOTER, true, chip->deep_spec.config.uv_thr, false);

	chip->deep_spec.counts = oplus_gauge_get_deep_dischg_count(chip, chip->gauge_ic);
	if (chip->sub_gauge)
		chip->deep_spec.sub_counts = oplus_gauge_get_deep_dischg_count(chip, chip->gauge_ic_comb[__ffs(chip->sub_gauge)]);
	chip->deep_spec.ddrc_tbatt.index_n = oplus_gauge_ddrc_get_temp_region(chip);
	chip->deep_spec.ddrc_tbatt.index_p = chip->deep_spec.ddrc_tbatt.index_n;
	chip->deep_spec.ddbc_tbatt.index_n = oplus_gauge_ddbc_get_temp_region(chip);
	oplus_gauge_get_ddrc_status(chip->gauge_topic);
	if (chip->sub_gauge)
		oplus_gauge_get_ddrc_status(chip->gauge_topic_parallel[__ffs(chip->sub_gauge)]);

	vote(chip->gauge_term_voltage_votable, READY_VOTER, false, 0, false);
	vote(chip->gauge_shutdown_voltage_votable, SUPER_ENDURANCE_MODE_VOTER,
		!chip->super_endurance_mode_status, chip->deep_spec.config.term_voltage, false);
	vote(chip->gauge_shutdown_voltage_votable, READY_VOTER, false, 0, false);

	schedule_delayed_work(&chip->deep_temp_work, msecs_to_jiffies(5000));
}

void oplus_mms_gauge_deep_dischg_init(struct oplus_mms_gauge *chip)
{
	oplus_gauge_deep_dischg_check(chip);
	oplus_gauge_init_sili_status(chip);
	if (chip->deep_spec.support)
		schedule_delayed_work(&chip->deep_id_work, PUSH_DELAY_MS);
}


int oplus_gauge_shutdown_voltage_vote_callback(struct votable *votable, void *data, int volt, const char *client,
						      bool step)
{
	struct oplus_mms_gauge *chip = data;

	if (!chip->deep_spec.support)
		return 0;

	if (volt >= INVALID_MAX_VOLTAGE || volt <= INVALID_MIN_VOLTAGE) {
		chg_info("volt %d invalid, client %s\n", volt, client);
		return 0;
	}

	chg_info("shutdown voltage vote client %s, volt = %d\n", client, volt);
	chip->deep_spec.config.uv_thr = volt;
	return oplus_mms_gauge_push_vbat_uv(chip);
}

static int oplus_mms_gauge_push_fcc_coeff(struct oplus_mms_gauge *chip, int coeff)
{
	struct mms_msg *msg;
	int rc;

	if (!chip->deep_spec.support)
		return 0;

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, GAUGE_ITEM_FCC_COEFF, coeff);
	if (msg == NULL) {
		chg_err("alloc battery subboard msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish fcc coeff, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static int oplus_mms_gauge_push_soh_coeff(struct oplus_mms_gauge *chip, int coeff)
{
	struct mms_msg *msg;
	int rc;

	if (!chip->deep_spec.support)
		return 0;

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, GAUGE_ITEM_SOH_COEFF, coeff);
	if (msg == NULL) {
		chg_err("alloc battery subboard msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->gauge_topic, msg);
	if (rc < 0) {
		chg_err("publish soh coeff, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

#define DEEP_DISCHG_UPDATE_VOLT_DELTA 100
int oplus_gauge_term_voltage_vote_callback(struct votable *votable, void *data, int volt, const char *client,
						  bool step)
{
	struct oplus_mms_gauge *chip = data;
	int current_volt = 0;
	int i = 0;

	if (!chip->deep_spec.support)
		return 0;

	if (volt >= INVALID_MAX_VOLTAGE || volt <= INVALID_MIN_VOLTAGE) {
		chg_info("volt %d invalid, client %s\n", volt, client);
		return 0;
	}

	current_volt = oplus_gauge_get_deep_term_volt(chip);

	for (i = chip->deep_spec.term_coeff_size - 1; i >= 0; i--) {
		if (volt >= chip->deep_spec.term_coeff[i].term_voltage) {
			chip->deep_spec.config.current_fcc_coeff = chip->deep_spec.term_coeff[i].fcc_coeff;
			chip->deep_spec.config.current_soh_coeff = chip->deep_spec.term_coeff[i].soh_coeff;
			break;
		}
	}

	oplus_mms_gauge_push_fcc_coeff(chip, chip->deep_spec.config.current_fcc_coeff);
	oplus_mms_gauge_push_soh_coeff(chip, chip->deep_spec.config.current_soh_coeff);
	chg_info("term voltage vote client %s, volt = %d\n", client, volt);
	chip->deep_spec.config.term_voltage = volt;
	if (current_volt != volt || step) {
		oplus_mms_gauge_set_deep_term_volt(chip->gauge_topic, volt);
		cancel_delayed_work(&chip->sili_term_volt_effect_check_work);
		schedule_delayed_work(&chip->sili_term_volt_effect_check_work, msecs_to_jiffies(2000));
	}
	return 0;
}
