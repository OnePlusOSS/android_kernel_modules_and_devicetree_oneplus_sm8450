// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[VIRTUAL_LEVEL_SHIFT]([%s][%d]): " fmt, __func__, __LINE__

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
#include <oplus_chg_monitor.h>

#define LEVEL_SHIFT_PHY_IC_NUM_MAX 3

struct oplus_virtual_level_shift_child {
	struct oplus_chg_ic_dev *ic_dev;
};

struct oplus_virtual_level_shift_ic {
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct oplus_virtual_level_shift_child *child_list;
	int child_num;
	int node_child_num;
	struct oplus_mms *error_topic;
	struct mms_subscribe *error_subs;
	int fpga_reset_status;
	int i2c_reset_status;
};

static struct oplus_chg_ic_virq oplus_level_shift_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_FPGA_RST },
};

static void oplus_level_shift_err_handler(struct oplus_chg_ic_dev *ic_dev, void *virq_data)
{
	struct oplus_virtual_level_shift_ic *chip = virq_data;

	oplus_chg_ic_move_err_msg(chip->ic_dev, ic_dev);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
}

static void oplus_level_shift_fpga_rst_handler(struct oplus_chg_ic_dev *ic_dev, void *virq_data)
{
	struct oplus_virtual_level_shift_ic *chip = virq_data;

	oplus_chg_ic_move_err_msg(chip->ic_dev, ic_dev);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_FPGA_RST);
}

static int oplus_level_shift_virq_register(struct oplus_virtual_level_shift_ic *chip)
{
	int i;
	int rc = 0;

	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_virq_register(
			chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_ERR,
			oplus_level_shift_err_handler, chip);
		if (rc < 0)
			chg_err("register OPLUS_IC_VIRQ_ERR error, rc=%d",
				rc);

		rc = oplus_chg_ic_virq_register(
			chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_FPGA_RST,
			oplus_level_shift_fpga_rst_handler, chip);
		if (rc < 0)
			chg_err("register OPLUS_IC_VIRQ_FPGA_RST error, rc=%d",
				rc);
	}

	return rc;
}

static int oplus_chg_level_shift_init(struct oplus_chg_ic_dev *ic_dev)
{
	int i;
	int rc = 0;
	int phy_ic_num = 0;
	struct oplus_chg_ic_dev *temp_ic_dev;
	struct oplus_virtual_level_shift_ic *chip;
	struct device_node *node;
	bool retry = false;
	static bool dev_initialized[LEVEL_SHIFT_PHY_IC_NUM_MAX];

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	node = chip->dev->of_node;

	for (i = 0; i < chip->node_child_num; i++) {
		if (i < LEVEL_SHIFT_PHY_IC_NUM_MAX && dev_initialized[i])
			continue;
		temp_ic_dev = of_get_oplus_chg_ic(node, "oplus,level_shift_ic", i);
		if (temp_ic_dev == NULL) {
			chg_debug("level_shift ic[%d] not found\n", i);
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
		if (i < LEVEL_SHIFT_PHY_IC_NUM_MAX)
			dev_initialized[i] = true;
	}

	if (retry) {
		return -EAGAIN;
	} else {
		chg_err("all level_shift ic init error\n");
		return -EINVAL;
	}

init_done:
	oplus_level_shift_virq_register(chip);
	ic_dev->online = true;
	return rc;
}

static int oplus_chg_level_shift_exit(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_level_shift_ic *chip;
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
		oplus_chg_ic_virq_release(chip->child_list[i].ic_dev, OPLUS_IC_VIRQ_FPGA_RST, chip);
		oplus_chg_ic_func(chip->child_list[i].ic_dev, OPLUS_IC_FUNC_EXIT);
	}

	return 0;
}

static int oplus_chg_level_shift_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct oplus_virtual_level_shift_ic *chip;
	int i;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++)
		oplus_chg_ic_func(chip->child_list[i].ic_dev, OPLUS_IC_FUNC_REG_DUMP);

	return 0;
}

static int oplus_chg_ls_fpga_rst(struct oplus_chg_ic_dev *ic_dev, int type)
{
	struct oplus_virtual_level_shift_ic *chip;
	int i;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL\n");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip)
		return -ENODEV;
	for (i = 0; i < chip->child_num; i++) {
		rc = oplus_chg_ic_func(chip->child_list[i].ic_dev,
				       OPLUS_IC_FUNC_BAL_HW_INIT, type);
		if (rc < 0)
			chg_err("child ic[%d] OPLUS_IC_FUNC_GAUGE_FPGA_RST error, rc=%d\n", i, rc);
	}

	return rc;
}

static int oplus_chg_level_shift_set_conver_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	struct oplus_virtual_level_shift_ic *chip;
	int i;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE, enable);
	}

	return 0;
}

static int oplus_chg_level_shift_set_hw_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	struct oplus_virtual_level_shift_ic *chip;
	int i;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_SET_HW_ENABLE, enable);
	}

	return 0;
}

static int oplus_chg_level_shift_get_enable(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	struct oplus_virtual_level_shift_ic *chip;
	int i;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	for (i = 0; i < chip->child_num; i++) {
		oplus_chg_ic_func(chip->child_list[i].ic_dev,
				  OPLUS_IC_FUNC_BAL_GET_ENABLE, enable);
		if (*enable)
			break;
	}

	return 0;
}

static void *oplus_chg_level_shift_get_func(struct oplus_chg_ic_dev *ic_dev,
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
					       oplus_chg_level_shift_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_chg_level_shift_exit);
		break;
	case OPLUS_IC_FUNC_BAL_GET_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_GET_ENABLE,
					       oplus_chg_level_shift_get_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_HW_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_HW_ENABLE,
					       oplus_chg_level_shift_set_hw_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE,
						oplus_chg_level_shift_set_conver_enable);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_chg_level_shift_reg_dump);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static int oplus_level_shift_child_init(struct oplus_virtual_level_shift_ic *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	rc = of_property_count_elems_of_size(node, "oplus,level_shift_ic",
					     sizeof(u32));
	if (rc < 0) {
		chg_err("can't get level_shift_ic, rc=%d\n", rc);
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
		sizeof(struct oplus_virtual_level_shift_child) * chip->child_num, GFP_KERNEL);
	if (chip->child_list == NULL) {
		rc = -ENOMEM;
		chg_err("alloc level_shift_ic table memory error\n");
		return rc;
	}

	return 0;
}

static void oplus_virtual_ls_error_subs_callback(struct mms_subscribe *subs,
					   enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_virtual_level_shift_ic *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	if (!chip)
		return;

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case ERR_ITEM_FPGA_RESET:
			oplus_mms_get_item_data(chip->error_topic, ERR_ITEM_FPGA_RESET, &data,
						false);
			chip->fpga_reset_status = data.intval;
			chg_err("ERR_ITEM_FPGA_RESET, reset_status=%d\n", chip->fpga_reset_status);
			if (chip->fpga_reset_status == FPGA_RESET_START)
				oplus_chg_ls_fpga_rst(chip->ic_dev, FPGA_RESET_START);
			else if (chip->fpga_reset_status == FPGA_RESET_END)
				oplus_chg_ls_fpga_rst(chip->ic_dev, FPGA_RESET_END);
			break;
		case ERR_ITEM_I2C_RESET:
			oplus_mms_get_item_data(chip->error_topic, ERR_ITEM_I2C_RESET, &data,
						false);
			chip->i2c_reset_status = data.intval;
			chg_err("ERR_ITEM_I2C_RESET, reset_status=%d\n", chip->i2c_reset_status);
			if (chip->i2c_reset_status == GUAGE_I2C_RST_START)
				oplus_chg_ls_fpga_rst(chip->ic_dev, GUAGE_I2C_RST_START);
			else if (chip->i2c_reset_status == GUAGE_I2C_RST_END)
				oplus_chg_ls_fpga_rst(chip->ic_dev, GUAGE_I2C_RST_END);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_virtual_ls_subscribe_error_topic(struct oplus_mms *topic, void *prv_data)
{
	struct oplus_virtual_level_shift_ic *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->error_topic = topic;
	chip->error_subs =
		oplus_mms_subscribe(chip->error_topic, chip,
				    oplus_virtual_ls_error_subs_callback, "virtual_ls");
	if (IS_ERR_OR_NULL(chip->error_subs)) {
		chg_err("subscribe error topic error, rc=%ld\n",
			PTR_ERR(chip->error_subs));
		return;
	}
	oplus_mms_subs_move_to_top(chip->error_subs);

	oplus_mms_get_item_data(chip->error_topic, ERR_ITEM_FPGA_RESET, &data, true);
	chip->fpga_reset_status = data.intval;
	if (chip->fpga_reset_status == FPGA_RESET_END)
		oplus_chg_ls_fpga_rst(chip->ic_dev, FPGA_RESET_END);
}

static int oplus_virtual_level_shift_probe(struct platform_device *pdev)
{
	struct oplus_virtual_level_shift_ic *chip;
	struct device_node *node = pdev->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_virtual_level_shift_ic),
			    GFP_KERNEL);
	if (chip == NULL) {
		chg_err("alloc memory error\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, chip);

	rc = oplus_level_shift_child_init(chip);
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
	snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "level_shift-virtual:%d", ic_index);
	snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_level_shift_get_func;
	ic_cfg.virq_data = oplus_level_shift_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(oplus_level_shift_virq_table);
	ic_cfg.of_node = node;
	chip->ic_dev =
		devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}

	oplus_mms_wait_topic("error", oplus_virtual_ls_subscribe_error_topic, chip);
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
static void oplus_virtual_level_shift_remove(struct platform_device *pdev)
#else
static int oplus_virtual_level_shift_remove(struct platform_device *pdev)
#endif
{
	struct oplus_virtual_level_shift_ic *chip = platform_get_drvdata(pdev);

	if (chip == NULL) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
		return -ENODEV;
#else
		return;
#endif
	}

	if (chip->ic_dev->online)
		oplus_chg_level_shift_exit(chip->ic_dev);
	devm_oplus_chg_ic_unregister(&pdev->dev, chip->ic_dev);
	devm_kfree(&pdev->dev, chip->child_list);
	devm_kfree(&pdev->dev, chip);
	platform_set_drvdata(pdev, NULL);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	return 0;
#endif
}

static void oplus_virtual_level_shift_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id oplus_virtual_level_shift_match[] = {
	{ .compatible = "oplus,virtual_level_shift" },
	{},
};

static struct platform_driver oplus_virtual_level_shift_driver = {
	.driver		= {
		.name = "oplus-virtual_level_shift",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_virtual_level_shift_match),
	},
	.probe		= oplus_virtual_level_shift_probe,
	.remove		= oplus_virtual_level_shift_remove,
	.shutdown	= oplus_virtual_level_shift_shutdown,
};

static __init int oplus_virtual_level_shift_init(void)
{
	return platform_driver_register(&oplus_virtual_level_shift_driver);
}

static __exit void oplus_virtual_level_shift_exit(void)
{
	platform_driver_unregister(&oplus_virtual_level_shift_driver);
}

oplus_chg_module_register(oplus_virtual_level_shift);
