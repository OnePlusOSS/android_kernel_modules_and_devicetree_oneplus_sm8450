// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[VIRTUAL_BATT_BAL]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/slab.h>
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
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/of_irq.h>
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>
#endif
#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_hal_vooc.h>
#include <oplus_mms_gauge.h>

#define BATT_BAL_PHY_IC_NUM_MAX 3

struct oplus_virtual_batt_bal_child {
	struct oplus_chg_ic_dev *ic_dev;
};

struct oplus_virtual_batt_bal_ic {
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct oplus_virtual_batt_bal_child *child_list;
	int child_num;
	int node_child_num;
};

static struct oplus_chg_ic_virq oplus_batt_bal_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
};

static void oplus_batt_bal_err_handler(struct oplus_chg_ic_dev *ic_dev, void *virq_data)
{
	struct oplus_virtual_batt_bal_ic *chip = virq_data;

	oplus_chg_ic_move_err_msg(chip->ic_dev, ic_dev);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static int oplus_batt_bal_virq_register(struct oplus_virtual_batt_bal_ic *chip)
{
	int i;
	int rc = 0;

	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_virq_register(
			chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_ERR,
			oplus_batt_bal_err_handler, chip);
		if (rc < 0)
			chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d",
				rc);
	}

	return rc;
}

static int oplus_chg_batt_bal_init(struct oplus_chg_ic_dev *ic_dev)
{
	int i;
	int rc = 0;
	int phy_ic_num = 0;
	struct oplus_chg_ic_dev *temp_ic_dev;
	struct oplus_virtual_batt_bal_ic *chip;
	struct device_node *node;
	bool retry = false;
	static bool dev_initialized[BATT_BAL_PHY_IC_NUM_MAX];

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	node = chip->dev->of_node;

	for (i = 0; i < chip->node_child_num; i++) {
		if (i < BATT_BAL_PHY_IC_NUM_MAX && dev_initialized[i])
			continue;
		temp_ic_dev = of_get_oplus_chg_ic(node, "oplus,batt_bal_ic", i);
		if (temp_ic_dev == NULL) {
			chg_debug("batt_bal ic[%d] not found\n", i);
			retry = true;
			continue;
		}
		rc = oplus_chg_ic_func(temp_ic_dev, OPLUS_IC_FUNC_INIT);
		if (rc >= 0) {
			chip->child_list[phy_ic_num].ic_dev = temp_ic_dev;
			oplus_chg_ic_set_parent(chip->child_list[phy_ic_num].ic_dev, ic_dev);
			phy_ic_num += 1;
			chg_info("batt bal ic(=%s) init success, phy_ic_num=%d\n",
				 chip->child_list[i].ic_dev->name, phy_ic_num);
			if (phy_ic_num == chip->child_num)
				goto init_done;
		} else {
			retry = true;
			chg_err("switch ic(=%s) init error, rc=%d\n",
				temp_ic_dev->name, rc);
			break;
		}
		if (i < BATT_BAL_PHY_IC_NUM_MAX)
			dev_initialized[i] = true;
	}

	if (retry) {
		return -EAGAIN;
	} else {
		chg_err("all batt_bal ic init error\n");
		return -EINVAL;
	}

init_done:
	oplus_batt_bal_virq_register(chip);
	ic_dev->online = true;
	return rc;
}

static int oplus_chg_batt_bal_exit(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	ic_dev->online = false;
	for (i = 0; i < chip->child_num; i++) {
		oplus_chg_ic_virq_release(chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_ERR, chip);
		oplus_chg_ic_func(chip->child_list[i].ic_dev, OPLUS_IC_FUNC_EXIT);
	}

	return 0;
}

static int oplus_chg_batt_bal_set_vout(struct oplus_chg_ic_dev *ic_dev, int vout)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_SET_VOUT);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		vout = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_VOUT, vout);
		if (rc < 0)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_set_flow_dir(struct oplus_chg_ic_dev *ic_dev, int flow_dir)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_SET_FLOW_DIR);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		flow_dir = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_FLOW_DIR, flow_dir);
		if (rc < 0)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++)
		rc |= oplus_chg_ic_func(chip->child_list[i].ic_dev, OPLUS_IC_FUNC_REG_DUMP);

	return rc;
}

static int oplus_chg_batt_bal_set_iref(struct oplus_chg_ic_dev *ic_dev, int iref)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_SET_IREF);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		iref = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_IREF, iref);
		if (rc < 0)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_set_iterm(struct oplus_chg_ic_dev *ic_dev, int iterm)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_SET_ITERM);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		iterm = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_ITERM, iterm);
		if (rc < 0)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_set_conver_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		enable = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE, enable);
		if (rc < 0)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_set_hw_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_SET_HW_ENABLE);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		enable = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_HW_ENABLE, enable);
		if (rc < 0)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_get_enable(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_GET_ENABLE);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*enable = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_GET_ENABLE, enable);
		if (rc < 0 || *enable)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_get_pmos_enable(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		*enable = oplus_chg_ic_get_item_data(buf, 0);
		return 0;
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE, enable);
		if (rc < 0 || *enable)
			break;
	}

	return rc;
}

static int oplus_chg_batt_bal_set_pmos_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	struct oplus_virtual_batt_bal_ic *chip;
	int i;
	int rc = 0;
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	struct oplus_chg_ic_overwrite_data *data;
	const void *buf;
#endif

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	data = oplus_chg_ic_get_overwrite_data(ic_dev, OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE);
	if (unlikely(data != NULL)) {
		buf = (const void *)data->buf;
		if (!oplus_chg_ic_debug_data_check(buf, data->size))
			goto skip_overwrite;
		enable = oplus_chg_ic_get_item_data(buf, 0);
	}
skip_overwrite:
#endif
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE, enable);
		if (rc < 0)
			break;
	}

	return rc;
}

static void *oplus_chg_batt_bal_get_func(struct oplus_chg_ic_dev *ic_dev,
				   enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_chg_batt_bal_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_chg_batt_bal_exit);
		break;
	case OPLUS_IC_FUNC_BAL_GET_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_GET_ENABLE,
					       oplus_chg_batt_bal_get_enable);
		break;
	case OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE,
					       oplus_chg_batt_bal_get_pmos_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE,
					       oplus_chg_batt_bal_set_pmos_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_HW_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_HW_ENABLE,
					       oplus_chg_batt_bal_set_hw_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE,
						oplus_chg_batt_bal_set_conver_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_VOUT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_VOUT,
						oplus_chg_batt_bal_set_vout);
		break;
	case OPLUS_IC_FUNC_BAL_SET_ITERM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_ITERM,
						oplus_chg_batt_bal_set_iterm);
		break;
	case OPLUS_IC_FUNC_BAL_SET_IREF:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_IREF,
						oplus_chg_batt_bal_set_iref);
		break;
	case OPLUS_IC_FUNC_BAL_SET_FLOW_DIR:
			func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_FLOW_DIR,
							oplus_chg_batt_bal_set_flow_dir);
			break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_chg_batt_bal_reg_dump);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
static ssize_t oplus_chg_batt_bal_get_func_data(struct oplus_chg_ic_dev *ic_dev,
				     enum oplus_chg_ic_func func_id,
				     void *buf)
{
	bool enable;
	int *item_data;
	ssize_t rc = 0;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	   (func_id != OPLUS_IC_FUNC_EXIT))
		return -EINVAL;

	switch (func_id) {
	case OPLUS_IC_FUNC_BAL_GET_ENABLE:
		oplus_chg_ic_debug_data_init(buf, 1);
		rc = oplus_chg_batt_bal_get_enable(ic_dev, &enable);
		if (rc < 0)
			break;
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		*item_data = enable;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
		break;
	case OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE:
		oplus_chg_ic_debug_data_init(buf, 1);
		rc = oplus_chg_batt_bal_get_pmos_enable(ic_dev, &enable);
		if (rc < 0)
			break;
		item_data = oplus_chg_ic_get_item_data_addr(buf, 0);
		*item_data = enable;
		*item_data = cpu_to_le32(*item_data);
		rc = oplus_chg_ic_debug_data_size(1);
			break;
	default:
		chg_err("this func(=%d) is not supported to get\n", func_id);
		return -ENOTSUPP;
		break;
	}

	return rc;
}

static int oplus_chg_batt_bal_set_func_data(struct oplus_chg_ic_dev *ic_dev,
				 enum oplus_chg_ic_func func_id,
				 const void *buf, size_t buf_len)
{
	int rc = 0;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	   (func_id != OPLUS_IC_FUNC_EXIT))
		return -EINVAL;

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_init(ic_dev);
		break;
	case OPLUS_IC_FUNC_EXIT:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_exit(ic_dev);
		break;
	case OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_set_pmos_enable(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	case OPLUS_IC_FUNC_BAL_SET_HW_ENABLE:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_set_hw_enable(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	case OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_set_conver_enable(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	case OPLUS_IC_FUNC_BAL_SET_VOUT:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_set_vout(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	case OPLUS_IC_FUNC_BAL_SET_ITERM:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_set_iterm(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	case OPLUS_IC_FUNC_BAL_SET_IREF:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_set_iref(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	case OPLUS_IC_FUNC_BAL_SET_FLOW_DIR:
		if (!oplus_chg_ic_debug_data_check(buf, buf_len))
			return -EINVAL;
		rc = oplus_chg_batt_bal_set_flow_dir(ic_dev, oplus_chg_ic_get_item_data(buf, 0));
		break;
	default:
		chg_err("this func(=%d) is not supported to set\n", func_id);
		return -ENOTSUPP;
		break;
	}

	return rc;
}

enum oplus_chg_ic_func oplus_chg_batt_bal_overwrite_funcs[] = {
	OPLUS_IC_FUNC_BAL_GET_ENABLE,
	OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE,
	OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE,
	OPLUS_IC_FUNC_BAL_SET_HW_ENABLE,
	OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE,
	OPLUS_IC_FUNC_BAL_SET_VOUT,
	OPLUS_IC_FUNC_BAL_SET_ITERM,
	OPLUS_IC_FUNC_BAL_SET_IREF,
	OPLUS_IC_FUNC_BAL_SET_FLOW_DIR,
};
#endif /* CONFIG_OPLUS_CHG_IC_DEBUG */

static int oplus_batt_bal_child_init(struct oplus_virtual_batt_bal_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	rc = of_property_count_elems_of_size(node, "oplus,batt_bal_ic",
					     sizeof(u32));
	if (rc < 0) {
		chg_err("can't get batt_bal_ic, rc=%d\n", rc);
		return rc;
	}
	chip->node_child_num = rc;

	rc = of_property_read_u32(node, "oplus,phy_ic_num", &chip->child_num);
	if (rc < 0) {
			chg_err("can't get phy_ic_num, rc=%d\n", rc);
			return rc;
	}

	if (chip->child_num > chip->node_child_num) {
		chg_err("phy_ic_num config err, rc=%d\n", rc);
		return -EINVAL;
	}

	chip->child_list = devm_kzalloc(chip->dev,
		sizeof(struct oplus_virtual_batt_bal_child) * chip->child_num, GFP_KERNEL);
	if (chip->child_list == NULL) {
		rc = -ENOMEM;
		chg_err("alloc batt_bal_ic table memory error\n");
		return rc;
	}

	return 0;
}

static int oplus_virtual_batt_bal_probe(struct platform_device *pdev)
{
	struct oplus_virtual_batt_bal_ic *chip;
	struct device_node *node = pdev->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_virtual_batt_bal_ic),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	rc = oplus_batt_bal_child_init(chip);
	if (rc < 0) {
		chg_err("child list init error, rc=%d\n", rc);
		goto child_init_err;
	}

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto reg_ic_err;
	}

	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto reg_ic_err;
	}

	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "batt_bal-virtual:%d", ic_index);
	snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_batt_bal_get_func;
	ic_cfg.virq_data = oplus_batt_bal_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(oplus_batt_bal_virq_table);
	ic_cfg.of_node = node;
	chip->ic_dev =
		devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}
#ifdef CONFIG_OPLUS_CHG_IC_DEBUG
	chip->ic_dev->debug.get_func_data = oplus_chg_batt_bal_get_func_data;
	chip->ic_dev->debug.set_func_data = oplus_chg_batt_bal_set_func_data;
	oplus_chg_ic_func_table_sort(oplus_chg_batt_bal_overwrite_funcs,
		ARRAY_SIZE(oplus_chg_batt_bal_overwrite_funcs));
	chip->ic_dev->debug.overwrite_funcs = oplus_chg_batt_bal_overwrite_funcs;
	chip->ic_dev->debug.func_num = ARRAY_SIZE(oplus_chg_batt_bal_overwrite_funcs);
#endif

	chg_err("probe success\n");
	return 0;

reg_ic_err:
	devm_kfree(&pdev->dev, chip->child_list);
child_init_err:
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

	chg_err("probe error\n");
	return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
static void oplus_virtual_batt_bal_remove(struct platform_device *pdev)
#else
static int oplus_virtual_batt_bal_remove(struct platform_device *pdev)
#endif
{
	struct oplus_virtual_batt_bal_ic *chip = platform_get_drvdata(pdev);

	if (chip == NULL) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
		return -ENODEV;
#else
		return;
#endif
	}

	if (chip->ic_dev->online)
		oplus_chg_batt_bal_exit(chip->ic_dev);
	devm_oplus_chg_ic_unregister(&pdev->dev, chip->ic_dev);
	devm_kfree(&pdev->dev, chip->child_list);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	return 0;
#endif
}

static void oplus_virtual_batt_bal_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id oplus_virtual_batt_bal_match[] = {
	{ .compatible = "oplus,virtual_batt_bal" },
	{},
};

static struct platform_driver oplus_virtual_batt_bal_driver = {
	.driver		= {
		.name = "oplus-virtual_batt_bal",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_virtual_batt_bal_match),
	},
	.probe		= oplus_virtual_batt_bal_probe,
	.remove		= oplus_virtual_batt_bal_remove,
	.shutdown	= oplus_virtual_batt_bal_shutdown,
};

static __init int oplus_virtual_batt_bal_init(void)
{
	return platform_driver_register(&oplus_virtual_batt_bal_driver);
}

static __exit void oplus_virtual_batt_bal_exit(void)
{
	platform_driver_unregister(&oplus_virtual_batt_bal_driver);
}

oplus_chg_module_register(oplus_virtual_batt_bal);
