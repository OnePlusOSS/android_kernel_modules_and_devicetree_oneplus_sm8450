// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2024 Oplus. All rights reserved.
 */


#define pr_fmt(fmt) "[PLC]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/gfp.h>
#include <linux/sched/clock.h>

#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_chg_monitor.h>
#include <oplus_mms_gauge.h>
#include <oplus_mms_wired.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_voter.h>
#include <oplus_chg_cpa.h>
#include <oplus_chg_ufcs.h>
#include <oplus_chg_plc.h>

#define PLC_IBAT_AVG_NUM 10
#define PLC_INFO_LEN 1023

struct plc_data {
	int ibat_index;
	int ibat_cnts;
	int ibus_index;
	int ibus_cnts;
	int init_soc;
	int init_sm_soc;
	int init_ui_soc;
	int avg_ibat;
	int avg_curr;
	int avg_ibus;
	int ibat_low;
	int ibus_over;
	int enable_cnts;
	bool plc_check;
	bool init_status;
	int ibat_column[PLC_IBAT_AVG_NUM];
	int ibus_column[PLC_IBAT_AVG_NUM];
};

enum plc_track_type {
	PLC_TRACK_SOC_EXIT,
	PLC_TRACK_IBAT_EXIT,
	PLC_TRACK_IBUS_ENTER,
	PLC_TRACK_SOC_ADD,
};

struct plc_track_info {
	unsigned char msg[PLC_INFO_LEN];
	int index;
};

struct ibat_delta {
	int ibat;
	int ibus;
};

static struct ibat_delta plc_ibus_table[] = {
	{ 0, 0 },
	{ 100, 50 },
	{ 200, 100 },
	{ 400, 200 },
	{ 600, 300 },
	{ 800, 400 },
	{ 10000, 500 },
};

struct oplus_chg_plc {
	struct device *dev;
	struct oplus_mms *plc_topic;
	struct oplus_mms *gauge_topic;
	struct oplus_mms *comm_topic;
	struct oplus_mms *wired_topic;
	struct oplus_mms *ufcs_topic;

	struct mms_subscribe *comm_subs;
	struct mms_subscribe *wired_subs;
	struct mms_subscribe *plc_subs;
	struct mms_subscribe *gauge_subs;
	struct mms_subscribe *ufcs_subs;

	struct votable *ufcs_curr_votable;
	struct votable *ufcs_disable_votable;
	struct votable *ufcs_not_allow_votable;
	struct votable *output_suspend_votable;
	struct votable *plc_enable_votable;
	struct votable *wired_suspend_votable;

	struct plc_data data;
	struct plc_track_info plc_info;

	bool wired_online;
	bool ufcs_online;
	bool ufcs_charging;
	int plc_curr;
	int ui_soc;
	int sm_soc;
	int plc_status;
	int plc_support;
	int plc_buck;
	int plc_soc;

	struct delayed_work plc_current_work;
	struct delayed_work plc_disable_wait_work;
	struct delayed_work plc_track_work;
	struct delayed_work plc_vote_work;
};

static bool is_ufcs_disable_votable_available(struct oplus_chg_plc *chip)
{
	if (!chip->ufcs_disable_votable)
		chip->ufcs_disable_votable = find_votable("UFCS_DISABLE");
	return !!chip->ufcs_disable_votable;
}

static bool is_ufcs_not_allow_votable_available(struct oplus_chg_plc *chip)
{
	if (!chip->ufcs_not_allow_votable)
		chip->ufcs_not_allow_votable = find_votable("UFCS_NOT_ALLOW");
	return !!chip->ufcs_not_allow_votable;
}

static bool is_output_suspend_votable_available(struct oplus_chg_plc *chip)
{
	if (!chip->output_suspend_votable)
		chip->output_suspend_votable = find_votable("WIRED_CHARGING_DISABLE");
	return !!chip->output_suspend_votable;
}

static bool is_ufcs_curr_votable_available(struct oplus_chg_plc *chip)
{
	if (!chip->ufcs_curr_votable)
		chip->ufcs_curr_votable = find_votable("UFCS_CURR");
	return !!chip->ufcs_curr_votable;
}

static bool is_plc_enable_votable_available(struct oplus_chg_plc *chip)
{
	if (!chip->plc_enable_votable)
		chip->plc_enable_votable = find_votable("PLC_ENABLE");
	return !!chip->plc_enable_votable;
}

__maybe_unused static bool
is_wired_suspend_votable_available(struct oplus_chg_plc *chip)
{
	if (!chip->wired_suspend_votable)
		chip->wired_suspend_votable = find_votable("WIRED_CHARGE_SUSPEND");
	return !!chip->wired_suspend_votable;
}

static void oplus_plc_subscribe_gauge_topic(struct oplus_mms *topic, void *prv_data)
{
	struct oplus_chg_plc *chip = prv_data;

	chip->gauge_topic = topic;
}

static int oplus_plc_get_vote_curr(struct oplus_chg_plc *chip)
{
	int curr_vote = -EINVAL;

	if (chip->ufcs_online && chip->plc_support == CHG_PROTOCOL_UFCS)
		curr_vote = get_effective_result(chip->ufcs_curr_votable);

	return curr_vote;
}

static int oplus_plc_get_vote_disalbe_retry(struct oplus_chg_plc *chip)
{
	int value = -EINVAL;

	if (chip->ufcs_online && chip->plc_support == CHG_PROTOCOL_UFCS)
		value = get_client_vote(chip->ufcs_disable_votable, PLC_RETRY_VOTER);

	return value;
}

static int oplus_plc_get_vote_allow_plc(struct oplus_chg_plc *chip)
{
	int value = -EINVAL;

	if (chip->ufcs_online && chip->plc_support == CHG_PROTOCOL_UFCS)
		value = get_client_vote(chip->ufcs_not_allow_votable, PLC_VOTER);

	return value;
}
static int oplus_plc_get_vote_allow_soc(struct oplus_chg_plc *chip)
{
	int value = -EINVAL;

	if (chip->ufcs_online && chip->plc_support == CHG_PROTOCOL_UFCS)
		value = get_client_vote(chip->ufcs_not_allow_votable, PLC_SOC_VOTER);

	return value;
}

static int oplus_plc_get_vote_charger_suspend(struct oplus_chg_plc *chip)
{
	int value = -EINVAL;

	value = get_client_vote(chip->wired_suspend_votable, PLC_VOTER);

	return value;
}

static int plc_info_debug_track = 0;
module_param(plc_info_debug_track, int, 0644);
MODULE_PARM_DESC(plc_info_debug_track, "debug track");
#define TRACK_UPLOAD_COUNT_MAX 3
#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int oplus_chg_plc_upload_plc_info(struct oplus_chg_plc *chip, char *deep_msg)
{
	struct oplus_mms *err_topic;
	struct mms_msg *msg;
	int rc;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	int curr_time;

	if (!chip)
		return -ENODEV;

	curr_time = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;

	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count >= TRACK_UPLOAD_COUNT_MAX)
		return -ENODEV;

	pre_upload_time = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;

	err_topic = oplus_mms_get_by_name("error");
	if (!err_topic) {
		chg_err("error topic not found\n");
		return -ENODEV;
	}

	msg = oplus_mms_alloc_str_msg(
		MSG_TYPE_ITEM, MSG_PRIO_MEDIUM, ERR_ITEM_PLC_INFO, deep_msg);
	if (msg == NULL) {
		chg_err("alloc plc error msg error\n");
		return -ENOMEM;
	}

	rc = oplus_mms_publish_msg(err_topic, msg);
	if (rc < 0) {
		chg_err("publish plc error msg error, rc=%d\n", rc);
		kfree(msg);
	}

	upload_count++;

	return rc;
}

static void oplus_gauge_plc_track_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_plc *chip =
		container_of(dwork, struct oplus_chg_plc, plc_track_work);

	int vbat_min_mv, batt_temp, ibat_ma, soc_now;
	int rc;
	union mms_msg_data data = { 0 };

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data, false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		soc_now = 0;
	} else {
		soc_now = data.intval;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data, false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		batt_temp = 0;
	} else {
		batt_temp = data.intval;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data, false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		vbat_min_mv = 0;
	} else {
		vbat_min_mv = data.intval;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data,
				     false);
	if (rc < 0) {
		chg_err("can't get ibat_ma, rc=%d\n", rc);
		ibat_ma = 0;
	} else {
		ibat_ma = data.intval;
	}

	if (chip->plc_info.index < PLC_INFO_LEN) {
		chip->plc_info.index += snprintf(&(chip->plc_info.msg[chip->plc_info.index]),
			PLC_INFO_LEN - chip->plc_info.index, "$$enable_cnts@@%d$$exit_soc@@%d"
			"$$exit_sm_soc@@%d$$exit_ui_soc@@%d$$exit_temp@@%d$$exit_vbat@@%d$$exit_ibat@@%d",
			chip->data.enable_cnts, soc_now, chip->sm_soc, chip->ui_soc, batt_temp, vbat_min_mv, ibat_ma);
	}
	chip->data.enable_cnts = 0;

	oplus_chg_plc_upload_plc_info(chip, chip->plc_info.msg);
	memset(&(chip->plc_info), 0, sizeof(chip->plc_info));
}

static void oplus_plc_get_deleta_track_msg(struct oplus_chg_plc *chip, int type)
{
	union mms_msg_data data = { 0 };
	int vbat_min_mv, batt_temp, ibat_ma, soc_now, curr_vote;
	int rc;

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data, false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		soc_now = 0;
	} else {
		soc_now = data.intval;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data, false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		batt_temp = 0;
	} else {
		batt_temp = data.intval;
	}

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data, false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		vbat_min_mv = 0;
	} else {
		vbat_min_mv = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data, false);
	if (rc < 0) {
		chg_err("can't get ibat_ma, rc=%d\n", rc);
		ibat_ma = 0;
	} else {
		ibat_ma = data.intval;
	}
	chip->data.init_ui_soc = chip->ui_soc;
	curr_vote = oplus_plc_get_vote_curr(chip);

	chg_info("[%d, %d, %d][%d, %d, %d, %d, %d, %d, %d, %d]\n", type, chip->data.init_sm_soc, chip->data.init_ui_soc,
		chip->sm_soc, soc_now, vbat_min_mv, batt_temp, ibat_ma, chip->data.avg_ibus, chip->data.avg_ibat, curr_vote);
	if (chip->plc_info.index < PLC_INFO_LEN)
		chip->plc_info.index += snprintf(&(chip->plc_info.msg[chip->plc_info.index]),
		PLC_INFO_LEN - chip->plc_info.index, "$$exit_type@@%d$$smooth_soc_%d@@%d$$soc_now_%d@@%d$$vbat_%d@@%d"
		"$$tbat_%d@@%d$$ibat_%d@@%d$$avg_ibus_%d@@%d$$avg_ibat_%d@@%d$$curr_vote_%d@@%d",
		type, chip->sm_soc, type, soc_now, type, vbat_min_mv, type, batt_temp, type, ibat_ma, type,
		chip->data.avg_ibus, type, chip->data.avg_ibat, type, curr_vote, type);
}

static int oplus_plc_push_plc_curr(struct oplus_chg_plc *chip)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH, PLC_ITEM_CURR);
	if (msg == NULL) {
		chg_err("alloc plc curr msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->plc_topic, msg);
	if (rc < 0) {
		chg_err("publish plc curr msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static int oplus_plc_push_dischg_plc(struct oplus_chg_plc *chip, bool disable)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH, PLC_ITEM_DISCHG_NORMAL, disable);
	if (msg == NULL) {
		chg_err("alloc dischg plc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->plc_topic, msg);
	if (rc < 0) {
		chg_err("publish dischg plc msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static int oplus_plc_push_dischg_soc(struct oplus_chg_plc *chip, bool disable)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH, PLC_ITEM_DISCHG_SOC, disable);
	if (msg == NULL) {
		chg_err("alloc dischg soc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->plc_topic, msg);
	if (rc < 0) {
		chg_err("publish dischg soc msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static int oplus_plc_push_dischg_retry(struct oplus_chg_plc *chip, bool disable)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_int_msg(MSG_TYPE_ITEM, MSG_PRIO_HIGH, PLC_ITEM_DISCHG_RETRY, disable);
	if (msg == NULL) {
		chg_err("alloc dischg retry msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->plc_topic, msg);
	if (rc < 0) {
		chg_err("publish dischg retry msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return rc;
}

static void oplus_plc_init_status(struct oplus_chg_plc *chip)
{
	union mms_msg_data data = { 0 };
	int vbat_min_mv, batt_temp, ibat_ma;
	int rc;

	if (chip->data.init_status)
		return;

	chip->data.init_status = true;
	chip->data.init_ui_soc = chip->ui_soc;
	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_SOC, &data, false);
	chip->data.init_soc = data.intval;
	chip->data.init_sm_soc = chip->sm_soc;

	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_TEMP, &data, false);
	if (rc < 0) {
		chg_err("can't get batt_temp, rc=%d\n", rc);
		batt_temp = 0;
	} else {
		batt_temp = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_VOL_MIN, &data, false);
	if (rc < 0) {
		chg_err("can't get vbat_min, rc=%d\n", rc);
		vbat_min_mv = 0;
	} else {
		vbat_min_mv = data.intval;
	}
	rc = oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data, false);
	if (rc < 0) {
		chg_err("can't get ibat_ma, rc=%d\n", rc);
		ibat_ma = 0;
	} else {
		ibat_ma = data.intval;
	}
	chip->plc_curr = PLC_IBUS_DEFAULT;
	oplus_plc_push_plc_curr(chip);
	chg_info("[%d, %d]\n", chip->data.init_sm_soc, chip->data.init_ui_soc);

	if (chip->plc_info.index < PLC_INFO_LEN)
		chip->plc_info.index += snprintf(&(chip->plc_info.msg[chip->plc_info.index]),
		PLC_INFO_LEN - chip->plc_info.index, "$$plc_buck@@%d$$plc_support@@%d$$init_sm_soc@@%d"
		"$$init_ui_soc@@%d$$init_soc@@%d$$vbat_min@@%d$$tbat@@%d$$ibat_ma@@%d", chip->plc_buck,
		chip->plc_support, chip->data.init_sm_soc, chip->data.init_ui_soc, chip->data.init_soc,
		batt_temp, vbat_min_mv, ibat_ma);
}

static void oplus_plc_reset_status(struct oplus_chg_plc *chip)
{
	chip->data.plc_check = false;
	chip->data.init_status = false;
	chip->data.ibat_index = 0;
	chip->data.ibat_cnts = 0;
	chip->data.ibus_index = 0;
	chip->data.ibus_cnts = 0;
	chip->data.ibat_low = 0;
	chip->data.ibus_over = 0;

	memset(chip->data.ibat_column, 0, PLC_IBAT_AVG_NUM);
	memset(chip->data.ibus_column, 0, PLC_IBAT_AVG_NUM);
}

static void oplus_plc_read_ibatt(struct oplus_chg_plc *chip)
{
	union mms_msg_data data = { 0 };
	int ibus_pmic = 0;

	oplus_mms_get_item_data(chip->gauge_topic, GAUGE_ITEM_CURR, &data, true);
	if (chip->data.ibus_index >= PLC_IBAT_AVG_NUM)
		chip->data.ibus_index = chip->data.ibus_index % PLC_IBAT_AVG_NUM;
	chip->data.ibat_column[chip->data.ibat_index] = data.intval;
	chip->data.ibat_index = (++chip->data.ibat_index) % PLC_IBAT_AVG_NUM;
	chip->data.ibat_cnts++;
	if (!chip->data.ibat_index)
		chip->data.plc_check = true;
	else
		chip->data.plc_check = false;

	ibus_pmic = oplus_wired_get_ibus();
	chip->data.ibus_column[chip->data.ibus_index] = ibus_pmic;
	chip->data.ibus_index = (++chip->data.ibus_index) % PLC_IBAT_AVG_NUM;
	chip->data.ibus_cnts++;
}

static int oplus_plc_get_avg_ibat(struct oplus_chg_plc *chip)
{
	int sum = 0, i;

	for (i = 0; i < PLC_IBAT_AVG_NUM; i++)
		sum += chip->data.ibat_column[i];

	chip->data.avg_ibat = sum / PLC_IBAT_AVG_NUM;
	return chip->data.avg_ibat;
}

static int oplus_plc_get_avg_ibus(struct oplus_chg_plc *chip)
{
	int sum = 0, i;

	for (i = 0; i < PLC_IBAT_AVG_NUM; i++)
		sum += chip->data.ibus_column[i];

	chip->data.avg_ibus = sum / PLC_IBAT_AVG_NUM;
	return sum / PLC_IBAT_AVG_NUM;
}

static int oplus_plc_get_delta_ibat(struct oplus_chg_plc *chip)
{
	int avg_ibat = 0, delta_ibat = 0, batt_num = 2;
	int asize = 0, i, ibat1 = 0, ibat2 = 0, ibus1 = 0, ibus2 = 0;

#define PLC_DELTA_ISTEP 50

	avg_ibat = oplus_plc_get_avg_ibat(chip);
	batt_num = oplus_gauge_get_batt_num();
	asize = sizeof(plc_ibus_table) / sizeof(struct ibat_delta);

	if (abs(avg_ibat) <= plc_ibus_table[0].ibat) {
		delta_ibat = plc_ibus_table[0].ibus;
	} else if (abs(avg_ibat) >= plc_ibus_table[asize - 1].ibat) {
		delta_ibat = plc_ibus_table[asize - 1].ibus;
	} else {
		ibat1 = plc_ibus_table[0].ibat;
		ibus1 = plc_ibus_table[0].ibus;

		for (i = 1; i < asize; i++) {
			if (abs(avg_ibat) < plc_ibus_table[i].ibat) {
				ibat2 = plc_ibus_table[i].ibat;
				ibus2 = plc_ibus_table[i].ibus;
				break;
			}
			ibat1 = plc_ibus_table[i].ibat;
			ibus1 = plc_ibus_table[i].ibus;
		}
		delta_ibat = ((abs(avg_ibat) - ibat1) * ibus2 + (ibat2 - abs(avg_ibat)) * ibus1) / (ibat2 - ibat1);
	}
	delta_ibat *= batt_num;
	delta_ibat = (delta_ibat / PLC_DELTA_ISTEP) * PLC_DELTA_ISTEP;

	return delta_ibat;
}

static int oplus_plc_check_plc_ibus(struct oplus_chg_plc *chip)
{
	int soc_now = 0, delta_soc = 0;
	int delta_ibat = 0, curr_vote = 0;
	int ibus_plc = PLC_IBUS_DEFAULT;

#define PLC_DELTA_SOC_MAX 3

	soc_now = chip->sm_soc;
	delta_soc = soc_now - chip->data.init_ui_soc;
	curr_vote = oplus_plc_get_vote_curr(chip);
	delta_ibat = oplus_plc_get_delta_ibat(chip);

	if (chip->data.init_ui_soc >= chip->plc_soc) {
		if (oplus_plc_get_vote_allow_soc(chip) < 0)
			oplus_plc_get_deleta_track_msg(chip, PLC_TRACK_SOC_EXIT);

		oplus_plc_push_dischg_soc(chip, true);
		vote(chip->output_suspend_votable, PLC_VOTER, true, 1, false);
		ibus_plc = PLC_IBUS_DEFAULT;
	} else if ((chip->data.init_ui_soc > chip->data.init_sm_soc) && (chip->data.init_ui_soc > soc_now)) {
		ibus_plc = PLC_IBUS_MAX;
	} else if ((chip->data.init_ui_soc < chip->data.init_sm_soc) && (chip->data.init_ui_soc < soc_now)) {
		ibus_plc = PLC_IBUS_MIN;
		if (delta_soc >= PLC_DELTA_SOC_MAX) {
			if (oplus_plc_get_vote_allow_soc(chip) < 0)
				oplus_plc_get_deleta_track_msg(chip, PLC_TRACK_SOC_EXIT);
			oplus_plc_push_dischg_soc(chip, true);
			vote(chip->output_suspend_votable, PLC_VOTER, true, 1, false);
			ibus_plc = PLC_IBUS_DEFAULT;
		}
	} else {
		if (delta_soc < 0) {
			ibus_plc = PLC_IBUS_MAX;
		} else if (delta_soc == 0) {
			if (chip->data.avg_ibat > 0)
				ibus_plc = curr_vote + delta_ibat;
			else
				ibus_plc = curr_vote - delta_ibat;
		} else if (delta_soc < PLC_DELTA_SOC_MAX) {
			ibus_plc = PLC_IBUS_MIN;
		} else {
			if (oplus_plc_get_vote_allow_soc(chip) < 0)
				oplus_plc_get_deleta_track_msg(chip, PLC_TRACK_SOC_EXIT);
			oplus_plc_push_dischg_soc(chip, true);
			vote(chip->output_suspend_votable, PLC_VOTER, true, 1, false);
			ibus_plc = PLC_IBUS_DEFAULT;
		}
	}

	if (ibus_plc < PLC_IBUS_MIN)
		ibus_plc = PLC_IBUS_MIN;
	if (ibus_plc > PLC_IBUS_MAX)
		ibus_plc = PLC_IBUS_MAX;

	return ibus_plc;
}

static void oplus_plc_ibat_check(struct oplus_chg_plc *chip)
{
	int ibus_pmic = 0, ibus_plc = 0;

#define PLC_IBAT_LOW_CNTS 4
#define PLC_IBUS_HIGH_CNTS 4
#define PLC_IBUS_HIGH_MAX 600
#define PLC_SUSPEND_DELAY 1000

	ibus_pmic = oplus_plc_get_avg_ibus(chip);
	ibus_plc = oplus_plc_check_plc_ibus(chip);

	if (chip->ufcs_charging && ibus_plc <= PLC_IBUS_MIN && chip->data.avg_ibat < 0)
		chip->data.ibat_low++;
	else
		chip->data.ibat_low = 0;

	if (chip->ufcs_charging && (chip->data.ibat_low == PLC_IBAT_LOW_CNTS) && chip->plc_status == PLC_STATUS_ENABLE &&
		oplus_plc_get_vote_allow_plc(chip) <= 0) {
		oplus_plc_push_dischg_plc(chip, true);
		vote(chip->output_suspend_votable, PLC_VOTER, true, 1, false);
		oplus_plc_get_deleta_track_msg(chip, PLC_TRACK_IBAT_EXIT);
		return;
	}

	if (!chip->ufcs_charging && ibus_pmic > PLC_IBUS_HIGH_MAX)
		chip->data.ibus_over++;
	else
		chip->data.ibus_over = 0;

	if (!chip->ufcs_charging && chip->plc_status == PLC_STATUS_ENABLE && oplus_plc_get_vote_allow_plc(chip) > 0 &&
		chip->data.ibus_over == PLC_IBUS_HIGH_CNTS) {
		if (chip->plc_status == PLC_STATUS_ENABLE && oplus_plc_get_vote_allow_plc(chip) > 0) {
			chip->plc_curr = 0;
			oplus_plc_push_plc_curr(chip);
		}
		vote(chip->output_suspend_votable, PLC_VOTER, false, 0, false);
		vote(chip->wired_suspend_votable, PLC_VOTER, true, 1, false);
		msleep(PLC_SUSPEND_DELAY);
		vote(chip->wired_suspend_votable, PLC_VOTER, false, 0, false);
		oplus_plc_push_dischg_plc(chip, false);
		oplus_plc_get_deleta_track_msg(chip, PLC_TRACK_IBUS_ENTER);
	}

	if (chip->ufcs_online && (chip->plc_status == PLC_STATUS_ENABLE || chip->plc_status == PLC_STATUS_WAIT)) {
		chip->plc_curr = ibus_plc;
		oplus_plc_push_plc_curr(chip);
	}
}

static void oplus_plc_monitor_current_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_plc *chip = container_of(dwork, struct oplus_chg_plc, plc_current_work);

#define PLC_MONITOR_CURRENT_DELAY 1000

	if (!is_ufcs_not_allow_votable_available(chip) || !is_ufcs_disable_votable_available(chip) ||
		!is_output_suspend_votable_available(chip) || !is_wired_suspend_votable_available(chip) ||
		!is_ufcs_curr_votable_available(chip))
		return;

	if ((chip->plc_status == PLC_STATUS_ENABLE || chip->plc_status == PLC_STATUS_WAIT) &&
		(chip->ufcs_online && chip->plc_support == CHG_PROTOCOL_UFCS) && !chip->plc_buck) {
		oplus_plc_init_status(chip);
		oplus_plc_read_ibatt(chip);
		if (chip->data.plc_check)
			oplus_plc_ibat_check(chip);
		schedule_delayed_work(&chip->plc_current_work, msecs_to_jiffies(PLC_MONITOR_CURRENT_DELAY));
	} else {
		oplus_plc_reset_status(chip);
		chip->plc_curr = 0;
		oplus_plc_push_plc_curr(chip);
	}
}

static void oplus_plc_disable_wait_work(struct work_struct *work)
{
	int enable_vote = PLC_STATUS_WAIT;
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_plc *chip =
		container_of(dwork, struct oplus_chg_plc, plc_disable_wait_work);

	enable_vote = get_client_vote(chip->plc_enable_votable, PLC_VOTER);

	if (get_client_vote(chip->plc_enable_votable, PLC_VOTER) == PLC_STATUS_WAIT) {
		if (!chip->ufcs_online)
			vote(chip->plc_enable_votable, PLC_VOTER, true, PLC_STATUS_NOT_ALLOW, false);
		else
			vote(chip->plc_enable_votable, PLC_VOTER, true, PLC_STATUS_DISABLE, false);
	}
}

static void oplus_plc_vote_enable_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_plc *chip =
		container_of(dwork, struct oplus_chg_plc, plc_vote_work);

	if (chip->ufcs_online)
		vote(chip->plc_enable_votable, PLC_VOTER, true, PLC_STATUS_DISABLE, false);
	else
		vote(chip->plc_enable_votable, PLC_VOTER, true, PLC_STATUS_NOT_ALLOW, false);
}

static void oplus_plc_comm_subs_callback(struct mms_subscribe *subs,
					 enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_plc *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_TIMER:
		break;
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_UI_SOC:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			chip->ui_soc = data.intval;
			break;
		case COMM_ITEM_SMOOTH_SOC:
			oplus_mms_get_item_data(chip->comm_topic, id, &data,
						false);
			if ((chip->plc_status == PLC_STATUS_ENABLE || chip->plc_status == PLC_STATUS_WAIT)
				&& data.intval != chip->sm_soc)
				oplus_plc_get_deleta_track_msg(chip, PLC_TRACK_SOC_ADD);
			chip->sm_soc = data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}


static void oplus_plc_subscribe_comm_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_plc *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->comm_topic = topic;
	chip->comm_subs =
		oplus_mms_subscribe(topic, chip,
				    oplus_plc_comm_subs_callback, "plc");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe comm topic error, rc=%ld\n",
			PTR_ERR(chip->comm_subs));
		return;
	}

	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_UI_SOC, &data, false);
	chip->ui_soc = data.intval;
	oplus_mms_get_item_data(chip->comm_topic, COMM_ITEM_SMOOTH_SOC, &data, false);
	chip->sm_soc = data.intval;
}

static void oplus_plc_wired_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_plc *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_TIMER:
		break;
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			oplus_mms_get_item_data(chip->wired_topic, id, &data,
						false);
			chip->wired_online = !!data.intval;
			if ((!chip->wired_online && chip->data.enable_cnts > 0) || plc_info_debug_track)
				schedule_delayed_work(&chip->plc_track_work, 0);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_plc_subscribe_wired_topic(struct oplus_mms *topic,
					     void *prv_data)
{
	struct oplus_chg_plc *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    oplus_plc_wired_subs_callback, "plc");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data,
				true);
	chip->wired_online = !!data.intval;
}

static void oplus_plc_ufcs_subs_callback(struct mms_subscribe *subs,
					  enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_plc *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case UFCS_ITEM_CHARGING:
			oplus_mms_get_item_data(chip->ufcs_topic, id, &data,
						false);
			chip->ufcs_charging = !!data.intval;
			break;
		case UFCS_ITEM_ONLINE:
			oplus_mms_get_item_data(chip->ufcs_topic, id, &data,
						false);
			chip->ufcs_online = !!data.intval;
			if (is_plc_enable_votable_available(chip) && chip->plc_support == CHG_PROTOCOL_UFCS)
				schedule_delayed_work(&chip->plc_vote_work, 0);

			if (is_ufcs_curr_votable_available(chip) && chip->plc_support == CHG_PROTOCOL_UFCS
				&& !chip->plc_buck)
				schedule_delayed_work(&chip->plc_current_work, 0);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_plc_subscribe_ufcs_topic(struct oplus_mms *topic, void *prv_data)
{
	struct oplus_chg_plc *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->ufcs_topic = topic;
	chip->ufcs_subs =
		oplus_mms_subscribe(topic, chip, oplus_plc_ufcs_subs_callback, "plc");
	if (IS_ERR_OR_NULL(chip->ufcs_subs)) {
		chg_err("subscribe ufcs topic error, rc=%ld\n",
			PTR_ERR(chip->ufcs_subs));
		return;
	}

	oplus_mms_get_item_data(topic, UFCS_ITEM_CHARGING, &data, true);
	chip->ufcs_charging = !!data.intval;
	oplus_mms_get_item_data(topic, UFCS_ITEM_ONLINE, &data, true);
	chip->ufcs_online = !!data.intval;

	chip->data.plc_check = false;
	chip->data.ibat_cnts = 0;
	chip->data.init_status = false;

	if (chip->ufcs_online && chip->plc_support == CHG_PROTOCOL_UFCS && is_ufcs_curr_votable_available(chip)
		&& (chip->plc_status == PLC_STATUS_ENABLE || chip->plc_status == PLC_STATUS_WAIT)) {
		chip->plc_curr = PLC_IBUS_DEFAULT;
		oplus_plc_push_plc_curr(chip);
		if (!chip->plc_buck)
			schedule_delayed_work(&chip->plc_current_work, 0);
	}
}

static void oplus_plc_plc_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_chg_plc *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case PLC_ITEM_SUPPORT:
			oplus_mms_get_item_data(chip->plc_topic, id, &data, false);
			chip->plc_support = data.intval;
			break;
		case PLC_ITEM_BUCK:
			oplus_mms_get_item_data(chip->plc_topic, id, &data, false);
			chip->plc_buck = data.intval;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static int oplus_plc_subscribe_plc_topic(struct oplus_chg_plc *chip)
{
	chip->plc_subs =
		oplus_mms_subscribe(chip->plc_topic, chip,
				    oplus_plc_plc_subs_callback,
				    "chg_comm");
	if (IS_ERR_OR_NULL(chip->plc_subs)) {
		chg_err("subscribe plc topic error, rc=%ld\n",
			PTR_ERR(chip->plc_subs));
		return PTR_ERR(chip->plc_subs);
	}

	return 0;
}

static int oplus_plc_publish_enable_cnts(struct oplus_chg_plc *chip)
{
	struct mms_msg *msg;
	int rc;

	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  PLC_ITEM_ENABLE_CNTS);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -ENOMEM;
	}
	rc = oplus_mms_publish_msg(chip->plc_topic, msg);
	if (rc < 0) {
		chg_err("publish enable enable cnts msg error, rc=%d\n", rc);
		kfree(msg);
		return rc;
	}

	return 0;
}

#define PLC_DISABLE_WAIT_DELAY		1000
static int oplus_plc_enable_vote_callback(struct votable *votable, void *data, int enable, const char *client, bool step)
{
	struct oplus_chg_plc *chip = data;
	struct mms_msg *msg;
	int rc;

	if (chip->plc_status == enable || !is_ufcs_not_allow_votable_available(chip) ||
		!is_ufcs_disable_votable_available(chip) || !is_output_suspend_votable_available(chip)
		|| !is_wired_suspend_votable_available(chip))
		return 0;
	if (enable == PLC_STATUS_WAIT) {
		chip->plc_status = PLC_STATUS_WAIT;
		schedule_delayed_work(&chip->plc_disable_wait_work, msecs_to_jiffies(PLC_DISABLE_WAIT_DELAY));
	} else {
		if ((chip->plc_status == PLC_STATUS_DISABLE) && (enable == PLC_STATUS_ENABLE)) {
			if (chip->wired_online && !chip->ufcs_charging)
				vote(chip->output_suspend_votable, PLC_VOTER, true, 1, false);
			oplus_plc_reset_status(chip);
			chip->data.enable_cnts++;
			oplus_plc_publish_enable_cnts(chip);
		}

		chip->plc_status = enable;
		if (chip->plc_status == PLC_STATUS_DISABLE) {
			if ((oplus_plc_get_vote_allow_plc(chip) > 0 || oplus_plc_get_vote_allow_soc(chip) > 0) &&
				oplus_plc_get_vote_charger_suspend(chip) <= 0)  {
				if (chip->wired_online) {
					vote(chip->wired_suspend_votable, PLC_VOTER, true, 1, false);
					msleep(PLC_SUSPEND_DELAY);
					vote(chip->wired_suspend_votable, PLC_VOTER, false, 0, false);
				}
				vote(chip->output_suspend_votable, PLC_VOTER, false, 0, false);
				oplus_plc_push_dischg_plc(chip, false);
				oplus_plc_push_dischg_soc(chip, false);
			} else if (chip->ufcs_charging && oplus_plc_get_vote_disalbe_retry(chip) <= 0)  {
				oplus_plc_push_dischg_retry(chip, true);
			}
			vote(chip->output_suspend_votable, PLC_VOTER, false, 1, false);
		} else if (chip->plc_status == PLC_STATUS_ENABLE && chip->ufcs_charging && chip->plc_buck &&
			oplus_plc_get_vote_allow_plc(chip) < 0) {
			oplus_plc_push_dischg_plc(chip, true);
			vote(chip->output_suspend_votable, PLC_VOTER, true, 1, false);
		}
	}
	chg_info("call %d plc by %s\n", enable, client);
	msg = oplus_mms_alloc_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				  PLC_ITEM_STATUS);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -EINVAL;
	}
	rc = oplus_mms_publish_msg(chip->plc_topic, msg);
	if (rc < 0) {
		chg_err("publish plc status msg error, rc=%d\n", rc);
		kfree(msg);
		return -EINVAL;
	}

	schedule_delayed_work(&chip->plc_current_work, msecs_to_jiffies(1000));
	return 0;
}

static int oplus_plc_update_support_status(struct oplus_mms *mms,
					    union mms_msg_data *data)
{
	struct oplus_chg_plc *chip;

	if (mms == NULL) {
		chg_err("topic is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	data->intval = chip->plc_support;
	return 0;
}

static int oplus_plc_update_enable_status(struct oplus_mms *mms,
					    union mms_msg_data *data)
{
	struct oplus_chg_plc *chip;

	if (mms == NULL) {
		chg_err("topic is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	data->intval = chip->plc_status;
	return 0;
}

static int oplus_plc_update_buck_status(struct oplus_mms *mms,
					    union mms_msg_data *data)
{
	struct oplus_chg_plc *chip;

	if (mms == NULL) {
		chg_err("topic is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	data->intval = chip->plc_buck;
	return 0;
}

static int oplus_plc_update_plc_curr(struct oplus_mms *mms,
					    union mms_msg_data *data)
{
	struct oplus_chg_plc *chip;

	if (mms == NULL) {
		chg_err("topic is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	data->intval = chip->plc_curr;
	return 0;
}

static int oplus_plc_update_enable_cnts(struct oplus_mms *mms,
					    union mms_msg_data *data)
{
	struct oplus_chg_plc *chip;

	if (mms == NULL) {
		chg_err("topic is NULL");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL");
		return -EINVAL;
	}

	chip = oplus_mms_get_drvdata(mms);
	if (!chip) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	data->intval = chip->data.enable_cnts;
	return 0;
}

static void oplus_plc_topic_update(struct oplus_mms *mms, bool publish)
{
}

static struct mms_item oplus_plc_item[] = {
	{
		.desc = {
			.item_id = PLC_ITEM_SUPPORT,
			.update = oplus_plc_update_support_status,
		}
	}, {
		.desc = {
			.item_id = PLC_ITEM_STATUS,
			.update = oplus_plc_update_enable_status,
		}
	}, {
		.desc = {
			.item_id = PLC_ITEM_BUCK,
			.update = oplus_plc_update_buck_status,
		}
	}, {
		.desc = {
			.item_id = PLC_ITEM_CURR,
			.update = oplus_plc_update_plc_curr,
		}
	}, {
		.desc = {
			.item_id = PLC_ITEM_DISCHG_NORMAL,
		}
	}, {
		.desc = {
			.item_id = PLC_ITEM_DISCHG_SOC,
		}
	}, {
		.desc = {
			.item_id = PLC_ITEM_DISCHG_RETRY,
		}
	}, {
		.desc = {
			.item_id = PLC_ITEM_ENABLE_CNTS,
			.update = oplus_plc_update_enable_cnts,
		}
	}
};

static const struct oplus_mms_desc oplus_plc_desc = {
	.name = "plc",
	.type = OPLUS_MMS_TYPE_PLC,
	.item_table = oplus_plc_item,
	.item_num = ARRAY_SIZE(oplus_plc_item),
	.update_items = NULL,
	.update_items_num = 0,
	.update_interval = 0, /* ms */
	.update = oplus_plc_topic_update,
};

static int oplus_plc_topic_init(struct oplus_chg_plc *chip)
{
	struct oplus_mms_config mms_cfg = {};
	int rc;

	mms_cfg.drv_data = chip;
	mms_cfg.of_node = chip->dev->of_node;

	chip->plc_topic =
		devm_oplus_mms_register(chip->dev, &oplus_plc_desc, &mms_cfg);
	if (IS_ERR(chip->plc_topic)) {
		chg_err("Couldn't register plc topic\n");
		rc = PTR_ERR(chip->plc_topic);
		return rc;
	}
	oplus_plc_subscribe_plc_topic(chip);
	oplus_mms_wait_topic("common", oplus_plc_subscribe_comm_topic, chip);
	oplus_mms_wait_topic("wired", oplus_plc_subscribe_wired_topic, chip);
	oplus_mms_wait_topic("ufcs", oplus_plc_subscribe_ufcs_topic, chip);
	oplus_mms_wait_topic("gauge", oplus_plc_subscribe_gauge_topic, chip);
	return 0;
}

static int oplus_plc_parse_dt(struct oplus_chg_plc *chip)
{
	struct device_node *node = oplus_get_node_by_type(chip->dev->of_node);
	int rc;

	rc = of_property_read_u32(node, "oplus,plc_support",
				  &chip->plc_support);
	if (rc < 0) {
		chg_err("get oplus,plc_support property error, rc=%d\n",
			rc);
		chip->plc_support = 0;
	}
	rc = of_property_read_u32(node, "oplus,plc_buck",
				  &chip->plc_buck);
	if (rc < 0) {
		chg_err("get oplus,plc_buck property error, rc=%d\n",
			rc);
		chip->plc_buck = 0;
	}
	rc = of_property_read_u32(node, "oplus,plc_soc",
				  &chip->plc_soc);
	if (rc < 0) {
		chg_err("get oplus,plc_soc property error, rc=%d\n",
			rc);
		chip->plc_soc = 90;
	}

	return 0;
}

static int oplus_plc_vote_init(struct oplus_chg_plc *chip)
{
	int rc = 0;

	if (!chip->plc_support)
		return rc;

	chip->plc_enable_votable = create_votable("PLC_ENABLE", VOTE_MAX, oplus_plc_enable_vote_callback, chip);
	if (IS_ERR(chip->plc_enable_votable)) {
		rc = PTR_ERR(chip->plc_enable_votable);
		chip->plc_enable_votable = NULL;
		return rc;
	}

	vote(chip->plc_enable_votable, PLC_VOTER, true, PLC_STATUS_NOT_ALLOW, false);

	return 0;
}

static int oplus_chg_plc_probe(struct platform_device *pdev)
{
	struct oplus_chg_plc *chip;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_plc), GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc oplus_chg_plc struct buffer error\n");
		return -ENOMEM;
	}
	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	oplus_plc_parse_dt(chip);

	rc = oplus_plc_topic_init(chip);
	if (rc < 0)
		goto topic_reg_err;

	INIT_DELAYED_WORK(&chip->plc_current_work, oplus_plc_monitor_current_work);
	INIT_DELAYED_WORK(&chip->plc_disable_wait_work, oplus_plc_disable_wait_work);
	INIT_DELAYED_WORK(&chip->plc_track_work, oplus_gauge_plc_track_work);
	rc = oplus_plc_vote_init(chip);
	if (rc < 0)
		goto vote_init_err;

	INIT_DELAYED_WORK(&chip->plc_vote_work, oplus_plc_vote_enable_work);

	return 0;

vote_init_err:
	if (chip->plc_enable_votable != NULL)
		destroy_votable(chip->plc_enable_votable);

topic_reg_err:
	devm_kfree(&pdev->dev, chip);
	return rc;
}

static int oplus_chg_plc_remove(struct platform_device *pdev)
{
	struct oplus_chg_plc *chip = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(chip->comm_subs))
		oplus_mms_unsubscribe(chip->comm_subs);
	if (!IS_ERR_OR_NULL(chip->gauge_subs))
		oplus_mms_unsubscribe(chip->gauge_subs);
	if (!IS_ERR_OR_NULL(chip->ufcs_subs))
		oplus_mms_unsubscribe(chip->ufcs_subs);
	if (!IS_ERR_OR_NULL(chip->wired_subs))
		oplus_mms_unsubscribe(chip->wired_subs);
	if (!IS_ERR_OR_NULL(chip->plc_subs))
		oplus_mms_unsubscribe(chip->plc_subs);

	if (chip->plc_enable_votable != NULL)
		destroy_votable(chip->plc_enable_votable);
	devm_kfree(&pdev->dev, chip);

	return 0;
}

static const struct of_device_id oplus_chg_plc_match[] = {
	{ .compatible = "oplus,plc_charge" },
	{},
};

static struct platform_driver oplus_chg_plc_driver = {
	.driver = {
		.name = "oplus-plc_charge",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_plc_match),
	},
	.probe = oplus_chg_plc_probe,
	.remove = oplus_chg_plc_remove,
};

static __init int oplus_chg_plc_init(void)
{
	return platform_driver_register(&oplus_chg_plc_driver);
}

static __exit void oplus_chg_plc_exit(void)
{
	platform_driver_unregister(&oplus_chg_plc_driver);
}

oplus_chg_module_register(oplus_chg_plc);
