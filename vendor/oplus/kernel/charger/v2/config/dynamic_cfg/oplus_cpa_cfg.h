// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023 Oplus. All rights reserved.
 */

#define LOG_BUF_SIZE	4096
static char *g_log_buf;

static void oplus_cpa_update_protocol_list_config(
	struct oplus_param_head *param_head, struct oplus_cpa *cpa)
{
	struct oplus_cfg_data_head *data_head;
	uint32_t *buf;
	int i, rc;
	ssize_t data_len;
	int num;
	uint32_t data;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,protocol_list");
	while (data_head != NULL) {
		data_len = oplus_cfg_get_data_size(data_head);
		if (data_len % (sizeof(uint32_t) * 2) != 0) {
			chg_err("\"oplus,protocol_list\": Data format error\n");
			break;
		}
		num = data_len / sizeof(uint32_t) / 2;
		if (num > CHG_PROTOCOL_MAX) {
			chg_err("\"oplus,protocol_list\": too many items\n");
			break;
		}
		buf = kzalloc(data_len, GFP_KERNEL);
		if (buf == NULL) {
			chg_err("\"oplus,protocol_list\": alloc data buf error\n");
			break;
		}
		rc = oplus_cfg_get_data(data_head, (u8 *)buf, data_len);
		if (rc < 0) {
			chg_err("\"oplus,protocol_list\": get data error, rc=%d\n", rc);
			kfree(buf);
			break;
		}
		for (i = 0; i < num; i++) {
			cpa->protocol_prio_table[i].type = CHG_PROTOCOL_INVALID;
			cpa->protocol_prio_table[i].max_power_mw = 0;
			cpa->protocol_prio_table[i].power_mw = 0;
			data = le32_to_cpu(buf[i * 2]);
			if (data >= CHG_PROTOCOL_MAX) {
				chg_err("\"oplus,protocol_list\": index %d data error, data=%u\n", i, data);
				continue;
			} else {
				cpa->protocol_prio_table[i].type = data;
			}

			data = le32_to_cpu(buf[i * 2 + 1]);
			/* convert from w to mw */
			data *= 1000;
			cpa->protocol_prio_table[i].max_power_mw = data;

			/*
			* The following protocols have fixed power and do not
			* require secondary power information acquisition
			*/
			switch (cpa->protocol_prio_table[i].type) {
			case CHG_PROTOCOL_PD:
			case CHG_PROTOCOL_QC:
			case CHG_PROTOCOL_BC12:
				cpa->protocol_prio_table[i].power_mw = data;
				break;
			default:
				break;
			}
		}
		for (i = num; i < CHG_PROTOCOL_MAX; i++) {
			cpa->protocol_prio_table[i].type = CHG_PROTOCOL_INVALID;
			cpa->protocol_prio_table[i].max_power_mw = 0;
			cpa->protocol_prio_table[i].power_mw = 0;
		}
		kfree(buf);
		break;
	}
}

static void oplus_cpa_update_default_protocol_list_config(
	struct oplus_param_head *param_head, struct oplus_cpa *cpa)
{
	struct oplus_cfg_data_head *data_head;
	uint32_t *buf;
	int i, rc;
	ssize_t data_len;
	int num;
	uint32_t data;

	data_head = oplus_cfg_find_param_by_name(param_head, "oplus,default_protocol_list");
	while (data_head != NULL) {
		data_len = oplus_cfg_get_data_size(data_head);
		if (data_len % sizeof(uint32_t) != 0) {
			chg_err("\"oplus,default_protocol_list\": Data format error\n");
			break;
		}
		num = data_len / sizeof(uint32_t);
		if (num > CHG_PROTOCOL_MAX) {
			chg_err("\"oplus,default_protocol_list\": too many items\n");
			break;
		}
		buf = kzalloc(data_len, GFP_KERNEL);
		if (buf == NULL) {
			chg_err("\"oplus,default_protocol_list\": alloc data buf error\n");
			break;
		}
		rc = oplus_cfg_get_data(data_head, (u8 *)buf, data_len);
		if (rc < 0) {
			chg_err("\"oplus,default_protocol_list\": get data error, rc=%d\n", rc);
			kfree(buf);
			break;
		}
		cpa->default_protocol_type = 0;
		for (i = 0; i < num; i++) {
			data = le32_to_cpu(buf[i]);
			if (data >= CHG_PROTOCOL_MAX)
				chg_err("\"oplus,default_protocol_list\": index %d data error, data=%u\n", i, data);
			else
				cpa->default_protocol_type |= BIT(data);
		}
		kfree(buf);
		break;
	}
}

static int oplus_cpa_update_config(void *data, struct oplus_param_head *param_head)
{
	struct oplus_cpa *cpa;

	if (data == NULL) {
		chg_err("data is NULL\n");
		return -EINVAL;
	}
	if (param_head == NULL) {
		chg_err("param_head is NULL\n");
		return -EINVAL;
	}
	cpa = (struct oplus_cpa *)data;

	g_log_buf = kzalloc(LOG_BUF_SIZE, GFP_KERNEL);
	oplus_cpa_update_protocol_list_config(param_head, cpa);
	oplus_cpa_update_default_protocol_list_config(param_head, cpa);

	if (g_log_buf != NULL) {
		kfree(g_log_buf);
		g_log_buf = NULL;
	}

	return 0;
}

static int oplus_cpa_reg_debug_config(struct oplus_cpa *cpa)
{
	int rc;

	cpa->debug_cfg.type = OPLUS_CHG_CPA_PARAM;
	cpa->debug_cfg.update = oplus_cpa_update_config;
	cpa->debug_cfg.priv_data = cpa;
	rc = oplus_cfg_register(&cpa->debug_cfg);
	if (rc < 0)
		chg_err("cpa cfg register error, rc=%d\n", rc);

	return 0;
}

static void oplus_cpa_unreg_debug_config(struct oplus_cpa *cpa)
{
	oplus_cfg_unregister(&cpa->debug_cfg);
}
