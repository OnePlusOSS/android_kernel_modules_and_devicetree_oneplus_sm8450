// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[STRATEGY_DDRC]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <oplus_chg.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_comm.h>
#include <oplus_strategy.h>

enum ddrc_ratio_range {
	DDRC_RATIO_RANGE_MIN = 0,
	DDRC_RATIO_RANGE_LOW,
	DDRC_RATIO_RANGE_MID_LOW,
	DDRC_RATIO_RANGE_MID,
	DDRC_RATIO_RANGE_MID_HIGH,
	DDRC_RATIO_RANGE_HIGH,
	DDRC_RATIO_RANGE_MAX = DDRC_RATIO_RANGE_HIGH,
};

enum ddrc_temp_range {
	DDRC_TEMP_RANGE_COLD = 0,
	DDRC_TEMP_RANGE_COOL,
	DDRC_TEMP_RANGE_NORMAL,
	DDRC_TEMP_RANGE_WARM,
	DDRC_TEMP_RANGE_MAX = DDRC_TEMP_RANGE_WARM,
};

struct ddrc_ratio_curves {
	struct ddrc_temp_curves temp_curves[DDRC_TEMP_RANGE_MAX + 1];
};

#define NAME_LENGTH	16
struct ddrc_strategy {
	struct oplus_chg_strategy strategy;
	struct ddrc_temp_curves *curve;
	struct ddrc_ratio_curves ratio_curves[DDRC_RATIO_RANGE_MAX + 1];
	uint32_t ratio_range_data[DDRC_RATIO_RANGE_MAX];
	int32_t temp_range_data[DDRC_TEMP_RANGE_MAX];
	uint32_t temp_type;
	int curr_level;
	char topic_name[NAME_LENGTH];
	struct oplus_mms *topic;
};

#define DDRC_DATA_SIZE	sizeof(struct ddrc_strategy_data)

static const char * const ddrc_strategy_ratio[] = {
	[DDRC_RATIO_RANGE_MIN]		= "strategy_ratio_range_min",
	[DDRC_RATIO_RANGE_LOW]		= "strategy_ratio_range_low",
	[DDRC_RATIO_RANGE_MID_LOW]	= "strategy_ratio_range_mid_low",
	[DDRC_RATIO_RANGE_MID]		= "strategy_ratio_range_mid",
	[DDRC_RATIO_RANGE_MID_HIGH]	= "strategy_ratio_range_mid_high",
	[DDRC_RATIO_RANGE_HIGH]		= "strategy_ratio_range_high",
};

static const char * const ddrc_strategy_temp[] = {
	[DDRC_TEMP_RANGE_COLD]		= "strategy_temp_cold",
	[DDRC_TEMP_RANGE_COOL]		= "strategy_temp_cool",
	[DDRC_TEMP_RANGE_NORMAL]	= "strategy_temp_normal",
	[DDRC_TEMP_RANGE_WARM]		= "strategy_temp_warm",
};

static struct oplus_mms *comm_topic;
static struct oplus_mms *gauge_topic;

__maybe_unused static bool is_comm_topic_available(void)
{
	if (!comm_topic)
		comm_topic = oplus_mms_get_by_name("common");
	return !!comm_topic;
}

__maybe_unused static bool is_gauge_topic_available(void)
{
	if (!gauge_topic)
		gauge_topic = oplus_mms_get_by_name("gauge");
	return !!gauge_topic;
}

__maybe_unused static bool is_gauge_topic_available_by_ddrc(struct ddrc_strategy *ddrc)
{
	if (!ddrc->topic)
		ddrc->topic = oplus_mms_get_by_name(ddrc->topic_name);
	return !!ddrc->topic;
}

static int __read_signed_data_from_node(struct device_node *node,
					const char *prop_str,
					s32 *addr, int len_max)
{
	int rc = 0, length;

	if (!node || !prop_str || !addr) {
		chg_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(s32));
	if (rc < 0) {
		chg_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	if (length != len_max) {
		chg_err("entries(%d) num error, only %d allowed\n", length,
			len_max);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str, (u32 *)addr, length);
	if (rc) {
		chg_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return rc;
}

static int __read_unsigned_data_from_node(struct device_node *node,
					  const char *prop_str, u32 *addr,
					  int len_max)
{
	int rc = 0, length;

	if (!node || !prop_str || !addr) {
		chg_err("Invalid parameters passed\n");
		return -EINVAL;
	}

	rc = of_property_count_elems_of_size(node, prop_str, sizeof(u32));
	if (rc < 0) {
		chg_err("Count %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	length = rc;

	if (length != len_max) {
		chg_err("entries(%d) num error, only %d allowed\n", length,
			len_max);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(node, prop_str, (u32 *)addr, length);
	if (rc < 0) {
		chg_err("Read %s failed, rc=%d\n", prop_str, rc);
		return rc;
	}

	return length;
}

static int ddrc_strategy_get_ratio(struct ddrc_strategy *ddrc, int *ratio)
{
	union mms_msg_data data = { 0 };
	int rc;

	if (!is_gauge_topic_available_by_ddrc(ddrc)) {
		chg_err("gauge topic not found\n");
		return -ENODEV;
	}
	rc = oplus_mms_get_item_data(ddrc->topic, GAUGE_ITEM_RATIO_VALUE,
				     &data, true);
	if (rc < 0) {
		chg_err("can't get ratio, rc=%d\n", rc);
		return rc;
	}
	*ratio = data.intval;

	return 0;
}

static int ddrc_strategy_get_temp(struct ddrc_strategy *ddrc, int *temp)
{
	union mms_msg_data data = { 0 };
	int rc;

	switch (ddrc->temp_type) {
	case STRATEGY_USE_BATT_TEMP:
		if (!is_gauge_topic_available()) {
			chg_err("gauge topic not found\n");
			return -ENODEV;
		}
		rc = oplus_mms_get_item_data(gauge_topic, GAUGE_ITEM_TEMP,
					     &data, true);
		if (rc < 0) {
			chg_err("can't get battery temp, rc=%d\n", rc);
			return rc;
		}

		*temp = data.intval;
		break;
	case STRATEGY_USE_SHELL_TEMP:
		if (!is_comm_topic_available()) {
			chg_err("common topic not found\n");
			return -ENODEV;
		}
		rc = oplus_mms_get_item_data(comm_topic, COMM_ITEM_SHELL_TEMP,
					     &data, false);
		if (rc < 0) {
			chg_err("can't get shell temp, rc=%d\n", rc);
			return rc;
		}

		*temp = data.intval;
		break;
	default:
		chg_err("not support temp type, type=%d\n", ddrc->temp_type);
		return -EINVAL;
	}

	return 0;
}

static enum ddrc_ratio_range
ddrc_get_ratio_region(struct ddrc_strategy *ddrc)
{
	int ratio;
	enum ddrc_ratio_range ratio_region = DDRC_RATIO_RANGE_MAX;
	int i;
	int rc;

	rc = ddrc_strategy_get_ratio(ddrc, &ratio);
	if (rc < 0) {
		chg_err("can't get ratio, rc=%d\n", rc);
		return DDRC_RATIO_RANGE_MAX;
	}

	for (i = 0; i < DDRC_RATIO_RANGE_MAX; i++) {
		if (ratio < ddrc->ratio_range_data[i]) {
			ratio_region = i;
			break;
		}
	}
	return ratio_region;
}

static enum ddrc_temp_range
ddrc_get_temp_region(struct ddrc_strategy *ddrc)
{
	int temp, i, rc;
	enum ddrc_temp_range temp_region = DDRC_TEMP_RANGE_MAX;
	union mms_msg_data data = { 0 };

	if (!is_gauge_topic_available()) {
		chg_err("gauge topic not found\n");
		return temp_region;
	}
	rc = oplus_mms_get_item_data(gauge_topic, GAUGE_ITEM_RATIO_TRANGE,
				     &data, true);

	if (rc < 0 || data.intval < DDRC_TEMP_RANGE_COLD || data.intval > DDRC_TEMP_RANGE_MAX) {
		rc = ddrc_strategy_get_temp(ddrc, &temp);
		if (rc < 0) {
			chg_err("can't get temp, rc=%d\n", rc);
			return DDRC_TEMP_RANGE_MAX;
		}

		for (i = 0; i < DDRC_TEMP_RANGE_MAX; i++) {
			if (temp <= ddrc->temp_range_data[i]) {
				temp_region = i;
				break;
			}
		}
		return temp_region;
	}

	temp_region = data.intval;

	return temp_region;
}

static struct oplus_chg_strategy *
ddrc_strategy_alloc(unsigned char *buf, size_t size)
{
	return ERR_PTR(-ENOTSUPP);
}

static struct oplus_chg_strategy *
ddrc_strategy_alloc_by_node(struct device_node *node)
{
	struct ddrc_strategy *ddrc;
	u32 data;
	int rc;
	int i, j;
	int length;
	struct device_node *soc_node;
	const char *topic_name;

	if (node == NULL) {
		chg_err("node is NULL\n");
		return ERR_PTR(-EINVAL);
	}

	ddrc = kzalloc(sizeof(struct ddrc_strategy), GFP_KERNEL);
	if (ddrc == NULL) {
		chg_err("alloc strategy memory error\n");
		return ERR_PTR(-ENOMEM);
	}

	memset(ddrc->topic_name, 0, sizeof(ddrc->topic_name));
	rc = of_property_read_string(node, "oplus,gauge_topic_name", &topic_name);
	if (rc < 0) {
		snprintf(ddrc->topic_name, sizeof(ddrc->topic_name), "gauge");
	} else {
		snprintf(ddrc->topic_name, sizeof(ddrc->topic_name), "%s", topic_name);
		chg_info("ddrc gauge topic name: %s\n", ddrc->topic_name);
	}

	rc = of_property_read_u32(node, "oplus,temp_type", &data);
	if (rc < 0) {
		chg_err("oplus,temp_type reading failed, rc=%d\n", rc);
		ddrc->temp_type = STRATEGY_USE_SHELL_TEMP;
	} else {
		ddrc->temp_type = (uint32_t)data;
	}
	rc = __read_unsigned_data_from_node(node, "oplus,ratio_range",
					    (u32 *)ddrc->ratio_range_data,
					    DDRC_RATIO_RANGE_MAX);
	if (rc < 0) {
		chg_err("get oplus,ratio_range property error, rc=%d\n", rc);
		goto base_info_err;
	}
	rc = __read_signed_data_from_node(node, "oplus,temp_range",
					  (s32 *)ddrc->temp_range_data,
					  DDRC_TEMP_RANGE_MAX);
	if (rc < 0) {
		chg_err("get oplus,temp_range property error, rc=%d\n", rc);
		goto base_info_err;
	}

	for (i = 0; i <= DDRC_RATIO_RANGE_MAX; i++) {
		soc_node = of_get_child_by_name(node, ddrc_strategy_ratio[i]);
		if (!soc_node) {
			chg_err("can't find %s node\n", ddrc_strategy_ratio[i]);
			rc = -ENODEV;
			goto data_err;
		}

		for (j = 0; j <= DDRC_TEMP_RANGE_MAX; j++) {
			length = of_property_count_elems_of_size(
				soc_node, ddrc_strategy_temp[j], sizeof(u32));
			if (length < 0) {
				chg_err("can't find %s property, rc=%d\n",
					ddrc_strategy_temp[j], length);
				goto data_err;
			}
			rc = length * sizeof(u32);
			if (rc % DDRC_DATA_SIZE != 0) {
				chg_err("buf size does not meet the requirements, size=%d\n", rc);
				rc = -EINVAL;
				goto data_err;
			}

			ddrc->ratio_curves[i].temp_curves[j].num = rc / DDRC_DATA_SIZE;
			ddrc->ratio_curves[i].temp_curves[j].data = kzalloc(rc , GFP_KERNEL);
			if (ddrc->ratio_curves[i].temp_curves[j].data == NULL) {
				chg_err("alloc strategy data memory error\n");
				rc = -ENOMEM;
				goto data_err;
			}

			rc = of_property_read_u32_array(
					soc_node, ddrc_strategy_temp[j],
					(u32 *)ddrc->ratio_curves[i].temp_curves[j].data,
					length);
			if (rc < 0) {
				chg_err("read %s property error, rc=%d\n",
					ddrc_strategy_temp[j], rc);
				goto data_err;
			}
		}
	}

	return (struct oplus_chg_strategy *)ddrc;

data_err:
	for (i = 0; i <= DDRC_RATIO_RANGE_MAX; i++) {
		for (j = 0; j <= DDRC_TEMP_RANGE_MAX; j++) {
			if (ddrc->ratio_curves[i].temp_curves[j].data != NULL) {
				kfree(ddrc->ratio_curves[i].temp_curves[j].data);
				ddrc->ratio_curves[i].temp_curves[j].data = NULL;
			}
		}
	}
base_info_err:
	kfree(ddrc);
	return ERR_PTR(rc);
}

#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
#define TMP_BUF_SIZE 10
static struct oplus_chg_strategy *ddrc_strategy_alloc_by_param_head(const char *node_name, struct oplus_param_head *head)
{
	struct ddrc_strategy *ddrc;
	int rc;
	int i, j, k;
	struct oplus_cfg_data_head *data_head;
	int32_t buf[TMP_BUF_SIZE];
	ssize_t data_len;
	char *str_buf;
	int index = 0;

	if (node_name == NULL) {
		chg_err("node_name is NULL\n");
		return ERR_PTR(-EINVAL);
	}
	if (head == NULL) {
		chg_err("head is NULL\n");
		return ERR_PTR(-EINVAL);
	}

	ddrc = kzalloc(sizeof(struct ddrc_strategy), GFP_KERNEL);
	if (ddrc == NULL) {
		chg_err("alloc strategy memory error\n");
		return ERR_PTR(-ENOMEM);
	}
	str_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (str_buf == NULL) {
		chg_err("alloc str_buf memory error\n");
		rc = -ENOMEM;
		goto str_buf_err;
	}

	index = snprintf(str_buf, PAGE_SIZE - 1, "%s:oplus,temp_type", node_name);
	if (index < 0 || index >= PAGE_SIZE) {
		rc = -EFAULT;
		goto base_info_err;
	}
	str_buf[index] = 0;
	data_head = oplus_cfg_find_param_by_name(head, str_buf);
	if (data_head == NULL) {
		rc = -ENODATA;
		chg_err("get oplus,temp_type data head error\n");
		goto base_info_err;
	}
	rc = oplus_cfg_get_data(data_head, (u8 *)buf, sizeof(buf[0]));
	if (rc < 0) {
		chg_err("get oplus,temp_type data error, rc=%d\n", rc);
		goto base_info_err;
	}
	ddrc->temp_type = (uint32_t)(le32_to_cpu(buf[0]));

	index = snprintf(str_buf, PAGE_SIZE - 1, "%s:oplus,ratio_range", node_name);
	if (index < 0 || index >= PAGE_SIZE) {
		rc = -EFAULT;
		goto base_info_err;
	}
	str_buf[index] = 0;
	data_head = oplus_cfg_find_param_by_name(head, str_buf);
	if (data_head == NULL) {
		rc = -ENODATA;
		chg_err("get oplus,soc_range data head error\n");
		goto base_info_err;
	}
	data_len = oplus_cfg_get_data_size(data_head);
	if (data_len / sizeof(buf[0]) != DDRC_RATIO_RANGE_MAX) {
		rc = -EINVAL;
		chg_err("configuration data size error, data_len=%ld\n", data_len / sizeof(buf[0]));
		goto base_info_err;
	}
	rc = oplus_cfg_get_data(data_head, (u8 *)buf, data_len);
	if (rc < 0) {
		chg_err("get oplus,soc_range data error, rc=%d\n", rc);
		goto base_info_err;
	}
	for (i = 0; i < DDRC_RATIO_RANGE_MAX; i++)
		ddrc->ratio_range_data[i] = (uint32_t)(le32_to_cpu(buf[i]));

	index = snprintf(str_buf, PAGE_SIZE - 1, "%s:oplus,temp_range", node_name);
	if (index < 0 || index >= PAGE_SIZE) {
		rc = -EFAULT;
		goto base_info_err;
	}
	str_buf[index] = 0;
	data_head = oplus_cfg_find_param_by_name(head, str_buf);
	if (data_head == NULL) {
		rc = -ENODATA;
		chg_err("get oplus,temp_range data head error\n");
		goto base_info_err;
	}
	data_len = oplus_cfg_get_data_size(data_head);
	if (data_len / sizeof(buf[0]) != DDRC_TEMP_RANGE_MAX) {
		rc = -EINVAL;
		chg_err("configuration data size error, data_len=%ld\n", data_len / sizeof(buf[0]));
		goto base_info_err;
	}
	rc = oplus_cfg_get_data(data_head, (u8 *)buf, data_len);
	if (rc < 0) {
		chg_err("get oplus,temp_range data error, rc=%d\n", rc);
		goto base_info_err;
	}
	for (i = 0; i < DDRC_TEMP_RANGE_MAX; i++)
		ddrc->temp_range_data[i] = (uint32_t)le32_to_cpu(buf[i]);

	for (i = 0; i <= DDRC_RATIO_RANGE_MAX; i++) {
		for (j = 0; j <= DDRC_TEMP_RANGE_MAX; j++) {
			index = snprintf(str_buf, PAGE_SIZE - 1, "%s:%s:%s", node_name, ddrc_strategy_ratio[i],
					 ddrc_strategy_temp[j]);
			if (index < 0 || index >= PAGE_SIZE) {
				rc = -EINVAL;
				goto data_err;
			}
			str_buf[index] = 0;

			data_head = oplus_cfg_find_param_by_name(head, str_buf);
			if (data_head == NULL) {
				rc = -ENODATA;
				chg_err("get %s:%s:%s data head error\n", node_name, ddrc_strategy_ratio[i],
					ddrc_strategy_temp[j]);
				goto data_err;
			}
			data_len = oplus_cfg_get_data_size(data_head);
			if (data_len % DDRC_DATA_SIZE != 0) {
				chg_err("%s:%s:%s: buf size does not meet the requirements, size=%ld\n", node_name,
					ddrc_strategy_ratio[i], ddrc_strategy_temp[j], data_len);
				rc = -EINVAL;
				goto data_err;
			}
			ddrc->ratio_curves[i].temp_curves[j].num = data_len / DDRC_DATA_SIZE;
			ddrc->ratio_curves[i].temp_curves[j].data = kzalloc(data_len, GFP_KERNEL);
			if (ddrc->ratio_curves[i].temp_curves[j].data == NULL) {
				chg_err("alloc strategy data memory error\n");
				rc = -ENOMEM;
				goto data_err;
			}
			rc = oplus_cfg_get_data(data_head, (u8 *)ddrc->ratio_curves[i].temp_curves[j].data, data_len);
			if (rc < 0) {
				chg_err("get %s:%s:%s data error, rc=%d\n", node_name, ddrc_strategy_ratio[i],
					ddrc_strategy_temp[j], rc);
				goto data_err;
			}
			for (k = 0; k < ddrc->ratio_curves[i].temp_curves[j].num; k++) {
				ddrc->ratio_curves[i].temp_curves[j].data[k].count =
					le32_to_cpu(ddrc->ratio_curves[i].temp_curves[j].data[k].count);
				ddrc->ratio_curves[i].temp_curves[j].data[k].vbat0 =
					le32_to_cpu(ddrc->ratio_curves[i].temp_curves[j].data[k].vbat0);
				ddrc->ratio_curves[i].temp_curves[j].data[k].vbat1 =
					le32_to_cpu(ddrc->ratio_curves[i].temp_curves[j].data[k].vbat1);
				ddrc->ratio_curves[i].temp_curves[j].data[k].index =
					le32_to_cpu(ddrc->ratio_curves[i].temp_curves[j].data[k].index);
			}
		}
	}

	return (struct oplus_chg_strategy *)ddrc;

data_err:
	for (i = 0; i <= DDRC_RATIO_RANGE_MAX; i++) {
		for (j = 0; j <= DDRC_TEMP_RANGE_MAX; j++) {
			if (ddrc->ratio_curves[i].temp_curves[j].data != NULL) {
				kfree(ddrc->ratio_curves[i].temp_curves[j].data);
				ddrc->ratio_curves[i].temp_curves[j].data = NULL;
			}
		}
	}
base_info_err:
	kfree(str_buf);
str_buf_err:
	kfree(ddrc);
	return ERR_PTR(rc);
}
#endif /* CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER */

static int ddrc_strategy_release(struct oplus_chg_strategy *strategy)
{
	struct ddrc_strategy *ddrc;
	int i, j;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	ddrc = (struct ddrc_strategy *)strategy;

	for (i = 0; i <= DDRC_RATIO_RANGE_MAX; i++) {
		for (j = 0; j <= DDRC_TEMP_RANGE_MAX; j++) {
			if (ddrc->ratio_curves[i].temp_curves[j].data != NULL) {
				kfree(ddrc->ratio_curves[i].temp_curves[j].data);
				ddrc->ratio_curves[i].temp_curves[j].data = NULL;
			}
		}
	}
	kfree(ddrc);

	return 0;
}

static int ddrc_strategy_init(struct oplus_chg_strategy *strategy)
{
	struct ddrc_strategy *ddrc;
	enum ddrc_temp_range temp_range;
	enum ddrc_ratio_range ratio_range;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	ddrc = (struct ddrc_strategy *)strategy;

	ratio_range = ddrc_get_ratio_region(ddrc);
	temp_range = ddrc_get_temp_region(ddrc);
	ddrc->curve = &ddrc->ratio_curves[ratio_range].temp_curves[temp_range];
	ddrc->curve->index_r = ratio_range;
	ddrc->curve->index_t = temp_range;

	chg_info("use %s:%s curve\n", ddrc_strategy_ratio[ratio_range], ddrc_strategy_temp[temp_range]);

	return 0;
}

static int ddrc_strategy_get_data(struct oplus_chg_strategy *strategy, void *ret)
{
	struct ddrc_strategy *ddrc;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	if (ret == NULL) {
		chg_err("ret is NULL\n");
		return -EINVAL;
	}

	ddrc = (struct ddrc_strategy *)strategy;

	memcpy(ret, ddrc->curve, sizeof(*ddrc->curve));
	return 0;
}

static int ddrc_strategy_get_metadata(struct oplus_chg_strategy *strategy, void *ret)
{
	struct ddrc_strategy *ddrc;

	if (strategy == NULL) {
		chg_err("strategy is NULL\n");
		return -EINVAL;
	}
	if (ret == NULL) {
		chg_err("ret is NULL\n");
		return -EINVAL;
	}

	ddrc = (struct ddrc_strategy *)strategy;

	memcpy(ret, ddrc->curve, sizeof(*ddrc->curve));
	return 0;
}

static struct oplus_chg_strategy_desc ddrc_strategy_desc = {
	.name = "ddrc_curve",
	.strategy_init = ddrc_strategy_init,
	.strategy_release = ddrc_strategy_release,
	.strategy_alloc = ddrc_strategy_alloc,
	.strategy_alloc_by_node = ddrc_strategy_alloc_by_node,
#if IS_ENABLED(CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER)
	.strategy_alloc_by_param_head = ddrc_strategy_alloc_by_param_head,
#endif
	.strategy_get_data = ddrc_strategy_get_data,
	.strategy_get_metadata = ddrc_strategy_get_metadata,
};

int ddrc_strategy_register(void)
{
	return oplus_chg_strategy_register(&ddrc_strategy_desc);
}

