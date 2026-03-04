// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#define LOG_BUF_SIZE	256

static void oplus_batt_bal_update_volt_diff_thr_config(
	struct oplus_param_head *param_head, struct oplus_batt_bal_chip *chip)
{
	struct oplus_cfg_data_head *data_head;
	int32_t buf[BATT_TEMP_REGION_MAX - 1];
	char log_buf[LOG_BUF_SIZE] = {0};
	int i, rc;
	int index = 0;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,bal-volt-diff-thr");
	if (data_head == NULL)
		return;
	rc = oplus_cfg_get_data(data_head, (u8 *)buf, sizeof(buf));
	if (rc < 0) {
		chg_err("get oplus,bal-volt-diff-thr data error, rc=%d\n", rc);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		chip->cfg.chg_volt_diff_thr[i] = le32_to_cpu(buf[i]);
		index += snprintf(log_buf + index, LOG_BUF_SIZE - index - 1, "%s%d",
				(i == 0) ? "" : ", ", chip->cfg.chg_volt_diff_thr[i]);
	}

	chg_info("[TEST]:bal_volt_diff_thr = { %s }\n", log_buf);
}

static void oplus_batt_bal_update_bal_curr_thr_config(
	struct oplus_param_head *param_head, struct oplus_batt_bal_chip *chip)
{
	struct oplus_cfg_data_head *data_head;
	int32_t buf[BATT_TEMP_REGION_MAX - 1];
	char log_buf[LOG_BUF_SIZE] = {0};
	int i, rc;
	int index = 0;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,bal-curr-thr");
	if (data_head == NULL)
		return;
	rc = oplus_cfg_get_data(data_head, (u8 *)buf, sizeof(buf));
	if (rc < 0) {
		chg_err("get oplus,bal-curr-thr data error, rc=%d\n", rc);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		chip->cfg.chg_curr_thr[i] = le32_to_cpu(buf[i]);
		index += snprintf(log_buf + index, LOG_BUF_SIZE - index - 1, "%s%d",
				(i == 0) ? "" : ", ", chip->cfg.chg_curr_thr[i]);
	}

	chg_info("[TEST]:bal_curr_thr = { %s }\n", log_buf);
}

static void oplus_batt_bal_update_bal_vout_thr_config(
	struct oplus_param_head *param_head, struct oplus_batt_bal_chip *chip)
{
	struct oplus_cfg_data_head *data_head;
	int32_t buf[CTRL_REGION_MAX];
	char log_buf[LOG_BUF_SIZE] = {0};
	int i, rc;
	int index = 0;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,bal-vout-thr");
	if (data_head == NULL)
		return;
	rc = oplus_cfg_get_data(data_head, (u8 *)buf, sizeof(buf));
	if (rc < 0) {
		chg_err("get oplus,bal-vout-thr data error, rc=%d\n", rc);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(buf); i++) {
		chip->cfg.vout_thr[i] = le32_to_cpu(buf[i]);
		index += snprintf(log_buf + index, LOG_BUF_SIZE - index - 1, "%s%d",
				(i == 0) ? "" : ", ", chip->cfg.vout_thr[i]);
	}

	chg_info("[TEST]:bal_vout_thr = { %s }\n", log_buf);
}

static void oplus_batt_bal_update_max_iref_thr_config(
	struct oplus_param_head *param_head, struct oplus_batt_bal_chip *chip)
{
	struct oplus_cfg_data_head *data_head;
	int32_t buf;
	int rc;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,bal-max-iref-thr");
	if (data_head == NULL)
		return;

	rc = oplus_cfg_get_data(data_head, (u8 *)&buf, sizeof(buf));
	if (rc < 0) {
		chg_err("get oplus,bal-max-iref-thr data error, rc=%d\n", rc);
		return;
	}

	chip->cfg.bal_max_iref_thr = le32_to_cpu(buf);
	chg_info("[TEST]:bal_max_iref_thr = %d\n", chip->cfg.bal_max_iref_thr);
}

static void oplus_batt_bal_update_b1_design_cap_config(
	struct oplus_param_head *param_head, struct oplus_batt_bal_chip *chip)
{
	struct oplus_cfg_data_head *data_head;
	int32_t buf;
	int rc;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,bal-b1-design-cap");
	if (data_head == NULL)
		return;

	rc = oplus_cfg_get_data(data_head, (u8 *)&buf, sizeof(buf));
	if (rc < 0) {
		chg_err("get oplus,bal-b1-design-cap data error, rc=%d\n", rc);
		return;
	}

	chip->cfg.b1_design_cap = le32_to_cpu(buf);
	chg_info("[TEST]:b1_design_cap = %d\n", chip->cfg.b1_design_cap);
}

static void oplus_batt_bal_update_b2_design_cap_config(
	struct oplus_param_head *param_head, struct oplus_batt_bal_chip *chip)
{
	struct oplus_cfg_data_head *data_head;
	int32_t buf;
	int rc;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,bal-b2-design-cap");
	if (data_head == NULL)
		return;

	rc = oplus_cfg_get_data(data_head, (u8 *)&buf, sizeof(buf));
	if (rc < 0) {
		chg_err("get oplus,bal-b2-design-cap data error, rc=%d\n", rc);
		return;
	}

	chip->cfg.b2_design_cap = le32_to_cpu(buf);
	chg_info("[TEST]:b2_design_cap = %d\n", chip->cfg.b2_design_cap);
}

static void oplus_batt_bal_update_fastchg_b2_to_b1_max_curr_config(
	struct oplus_param_head *param_head, struct oplus_batt_bal_chip *chip)
{
	struct oplus_cfg_data_head *data_head;
	int32_t buf;
	int rc;

	data_head = oplus_cfg_find_param_by_name(
		param_head, "oplus,bal-fastchg-b2-to-b1-max-curr-thr");
	if (data_head == NULL)
		return;

	rc = oplus_cfg_get_data(data_head, (u8 *)&buf, sizeof(buf));
	if (rc < 0) {
		chg_err("get oplus,bal-fastchg-b2-to-b1-max-curr-thr data error, rc=%d\n", rc);
		return;
	}

	chip->cfg.fastchg_b2_to_b1_max_curr_thr = le32_to_cpu(buf);
	chg_info("[TEST]:fastchg_b2_to_b1_max_curr_thr = %d\n",
		chip->cfg.fastchg_b2_to_b1_max_curr_thr);
}

static int oplus_batt_bal_update_config(void *data, struct oplus_param_head *param_head)
{
	struct oplus_batt_bal_chip *chip;

	if (data == NULL) {
		chg_err("data is NULL\n");
		return -EINVAL;
	}
	if (param_head == NULL) {
		chg_err("param_head is NULL\n");
		return -EINVAL;
	}
	chip = (struct oplus_batt_bal_chip *)data;

	oplus_batt_bal_update_volt_diff_thr_config(param_head, chip);
	oplus_batt_bal_update_bal_curr_thr_config(param_head, chip);
	oplus_batt_bal_update_bal_vout_thr_config(param_head, chip);
	oplus_batt_bal_update_max_iref_thr_config(param_head, chip);
	oplus_batt_bal_update_b1_design_cap_config(param_head, chip);
	oplus_batt_bal_update_b2_design_cap_config(param_head, chip);
	oplus_batt_bal_update_fastchg_b2_to_b1_max_curr_config(param_head, chip);

	return 0;
}

static int oplus_batt_bal_reg_debug_config(struct oplus_batt_bal_chip *chip)
{
	int rc;

	chip->debug_cfg.type = OPLUS_BATT_BAL_PARAM;
	chip->debug_cfg.update = oplus_batt_bal_update_config;
	chip->debug_cfg.priv_data = chip;
	rc = oplus_cfg_register(&chip->debug_cfg);
	if (rc < 0)
		chg_err("batt_bal cfg register error, rc=%d\n", rc);

	return 0;
}

static void oplus_batt_bal_unreg_debug_config(struct oplus_batt_bal_chip *chip)
{
	oplus_cfg_unregister(&chip->debug_cfg);
}
