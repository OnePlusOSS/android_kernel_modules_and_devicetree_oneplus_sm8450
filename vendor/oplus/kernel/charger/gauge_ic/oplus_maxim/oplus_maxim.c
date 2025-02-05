// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Oplus. All rights reserved.
 */
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/pinctrl/consumer.h>
#include <oplus_chg_module.h>
#include <asm/setup.h>
#include "oplus_ds28e30/1wire_protocol.h"
#include "../../oplus_gauge.h"
#include "oplus_charger.h"

#include "oplus_ds28e30/ds28e30.h"

#define AUTH_MESSAGE_LEN	   20
#define OPLUS_MAXIM_AUTH_TAG      "maxim_auth="
#define OPLUS_MAXIM_AUTH_SUCCESS  "maxim_auth=TRUE"
#define OPLUS_MAXIM_AUTH_FAILED   "maxim_auth=FALSE"
#define TEST_COUNT         15
struct maxim_test_result {
	int test_count_total;
	int test_count_now;
	int test_fail_count;
	int real_test_count_now;
	int real_test_fail_count;
};

struct maxim_hmac_status {
	int fail_count;
	int total_count;
	int real_fail_count;
	int real_total_count;
};

struct oplus_maxim_gauge_chip {
	bool support_maxim_in_lk;
	bool support_maxim_in_kernel;
	struct device *dev;
	int authenticate_result;
	struct pinctrl *pinctrl;
	struct pinctrl_state *maxim_active;
	int data_gpio;
	struct completion	is_complete;
	struct onewire_gpio_data gpio_info;
	struct maxim_sn_num_info sn_num_info;
	struct maxim_test_result test_result;
	struct maxim_hmac_status hmac_status;
	int try_count;
	struct delayed_work auth_work;
	struct delayed_work test_work;
	int cpu_id;
};

static char __oplus_chg_cmdline[COMMAND_LINE_SIZE];
static char *oplus_chg_cmdline = __oplus_chg_cmdline;
static struct oplus_maxim_gauge_chip *g_maxim_chip = NULL;
static int try_count = 0;

static const char *oplus_maxim_get_cmdline(void)
{
	struct device_node * of_chosen = NULL;
	char *maxim_auth = NULL;

	if (__oplus_chg_cmdline[0] != 0)
		return oplus_chg_cmdline;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		maxim_auth = (char *)of_get_property(
					of_chosen, "maxim_auth", NULL);
		if (!maxim_auth)
			chg_err("%s: failed to get maxim_auth\n", __func__);
		else {
			strcpy(__oplus_chg_cmdline, maxim_auth);
			chg_err("%s: maxim_auth: %s\n", __func__, maxim_auth);
		}
	} else {
		chg_err("%s: failed to get /chosen \n", __func__);
	}

	return oplus_chg_cmdline;
}

static bool oplus_maxim_check_auth_msg(void)
{
	bool ret = false;
	char *str = NULL;

	if (NULL == oplus_maxim_get_cmdline()) {
		chg_err("oplus_chg_check_auth_msg: cmdline is NULL!!!\n");
		return false;
	}

	str = strstr(oplus_maxim_get_cmdline(), OPLUS_MAXIM_AUTH_TAG);
	if (str == NULL) {
		chg_err("oplus_chg_check_auth_msg: Asynchronous authentication is not supported!!!\n");
		return false;
	}

	chg_info("oplus_chg_check_auth_msg: %s\n", str);
	if (0 == memcmp(str, OPLUS_MAXIM_AUTH_SUCCESS, sizeof(OPLUS_MAXIM_AUTH_SUCCESS))) {
		ret = true;
		chg_info("oplus_chg_check_auth_msg: %s\n", OPLUS_MAXIM_AUTH_SUCCESS);
	} else {
		ret = false;
		chg_info("oplus_chg_check_auth_msg: %s\n", OPLUS_MAXIM_AUTH_FAILED);
	}

	return ret;
}

static int oplus_maxim_parse_dt(struct oplus_maxim_gauge_chip *chip)
{
	int rc, len, i, j;
	struct device_node *node = chip->dev->of_node;
	unsigned char sn_num_total[MAX_SN_NUM_SIZE] = {0};

	chip->support_maxim_in_kernel = false;
	chip->support_maxim_in_lk = of_property_read_bool(node, "support_encryption_in_lk");
	chg_info("support_maxim_in_lk: %d\n", chip->support_maxim_in_lk);

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}
	chip->maxim_active = pinctrl_lookup_state(chip->pinctrl, "maxim_active");
	if (IS_ERR_OR_NULL(chip->maxim_active)) {
		chg_err(": %d Failed to get the maxim_active pinctrl handle\n", __LINE__);
		return -ENODEV;
	} else {
		pinctrl_select_state(chip->pinctrl, chip->maxim_active);
		chg_err(": %d set maxim_active pinctrl handle\n", __LINE__);
	}
	chip->data_gpio = of_get_named_gpio(node, "data-gpio", 0);
	if (chip->data_gpio < 0) {
		chg_err("maxim data_gpio not specified\n");
		return -1;
	} else {
		chg_err("chip->data_gpio %d\n", chip->data_gpio);
		if (gpio_is_valid(chip->data_gpio)) {
			rc = gpio_request(chip->data_gpio, "maxim-data-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->data_gpio);
				return -1;
			}
		} else {
			chg_err("maxim data_gpio invalid\n");
			return -1;
		}
	}

	rc = of_property_read_u32(node, "gpio-addr-set-out",
				&chip->gpio_info.onewire_gpio_cfg_addr_out);
	if (rc) {
		chg_err("maxim get gpio-addr-set-out failed %d\n", rc);
		return -1;
	} else {
		chg_info("cfg_addr_out 0x%x\n", chip->gpio_info.onewire_gpio_cfg_addr_out);
	}
	rc = of_property_read_u32(node, "gpio-addr-set-in",
				&chip->gpio_info.onewire_gpio_cfg_addr_in);
	if (rc) {
		chg_err("maxim get gpio-addr-set-in failed %d\n", rc);
		return -1;
	} else {
		chg_info("cfg_addr_in 0x%x\n", chip->gpio_info.onewire_gpio_cfg_addr_in);
	}
	rc = of_property_read_u32(node, "gpio-addr-level_high",
				&chip->gpio_info.onewire_gpio_level_addr_high);
	if (rc) {
		chg_err("maxim get gpio-addr-level_high failed %d\n", rc);
		return -1;
	} else {
		chg_info("gpio_level_addr_high 0x%x\n", chip->gpio_info.onewire_gpio_level_addr_high);
	}
	rc = of_property_read_u32(node, "gpio-addr-level_low",
				&chip->gpio_info.onewire_gpio_level_addr_low);
	if (rc) {
		chg_err("maxim get gpio-addr-level_low failed %d\n", rc);
		return -1;
	} else {
		chg_info("gpio_level_addr_low 0x%x\n", chip->gpio_info.onewire_gpio_level_addr_low);
	}
	rc = of_property_read_u32(node, "gpio-addr-data-in", &chip->gpio_info.onewire_gpio_in_addr);
	if (rc) {
		chg_err("maxim get gpio-addr-data-in failed %d\n", rc);
		return -1;
	} else {
		chg_info("gpio_in_addr 0x%x\n", chip->gpio_info.onewire_gpio_in_addr);
	}

	rc = of_property_read_u32(node, "gpio-addr-offset", &chip->gpio_info.gpio_addr_offset);
	if (rc) {
		chg_err("maxim get gpio-addr-offset failed %d\n", rc);
		return -1;
	} else {
		chg_info("gpio-addr-offset 0x%x\n", chip->gpio_info.gpio_addr_offset);
	}

	rc = of_property_read_u32(node, "gpio-set-out-val", &chip->gpio_info.onewire_gpio_cfg_out_val);
	if (rc) {
		chip->gpio_info.onewire_gpio_cfg_out_val = (0x1 << chip->gpio_info.gpio_addr_offset);
	}
	chg_info("gpio-set-out-val 0x%x\n", chip->gpio_info.onewire_gpio_cfg_out_val);

	rc = of_property_read_u32(node, "gpio-set-in-val", &chip->gpio_info.onewire_gpio_cfg_in_val);
	if (rc) {
		chip->gpio_info.onewire_gpio_cfg_in_val = (0x1 << chip->gpio_info.gpio_addr_offset);
	}
	chg_info("gpio-set-in-val 0x%x\n", chip->gpio_info.onewire_gpio_cfg_in_val);

	rc = of_property_read_u32(node, "gpio_level_high_val", &chip->gpio_info.onewire_gpio_level_high_val);
	if (rc) {
		chip->gpio_info.onewire_gpio_level_high_val = (0x1 << chip->gpio_info.gpio_addr_offset);
	}
	chg_info("gpio_level_high_val 0x%x\n", chip->gpio_info.onewire_gpio_level_high_val);

	rc = of_property_read_u32(node, "gpio_level_low_val", &chip->gpio_info.onewire_gpio_level_low_val);
	if (rc) {
		chip->gpio_info.onewire_gpio_level_low_val = (0x1 << chip->gpio_info.gpio_addr_offset);
	}
	chg_info("gpio_level_low_val 0x%x\n", chip->gpio_info.onewire_gpio_level_low_val);

	rc = of_property_read_u32(node, "write_begin_low_level_time", &chip->gpio_info.write_begin_low_level_time);
	if (rc) {
		chip->gpio_info.write_begin_low_level_time = 1000;
	}
	chg_info("write_begin_low_level_time %d\n", chip->gpio_info.write_begin_low_level_time);

	rc = of_property_read_u32(node, "write_relese_ic_time", &chip->gpio_info.write_relese_ic_time);
	if (rc) {
		chip->gpio_info.write_relese_ic_time = 5;
	}
	chg_info("write_relese_ic_time %d\n", chip->gpio_info.write_relese_ic_time);

	rc = of_property_read_u32(node, "cpu-id", &chip->cpu_id);
	if (rc) {
		chip->cpu_id = 7;
	}
	chg_info("cpu-id:%d\n", chip->cpu_id);

	chip->gpio_info.maxim_romid_crc_support = of_property_read_bool(node, "oplus,maxim_romid_crc_support");
	chg_info("maxim_romid_crc_support %d\n", chip->gpio_info.maxim_romid_crc_support);

	chip->support_maxim_in_kernel = true;
	chg_info("support_maxim_in_kernel: %d\n", chip->support_maxim_in_kernel);

	len = of_property_count_u8_elems(node, "oplus,batt_info");
	if (len < 0 || len > MAX_SN_NUM_SIZE) {
		chg_info("Count oplus,batt_info failed, rc = %d\n", len);
		return -1;
	}

	rc = of_property_read_u8_array(node, "oplus,batt_info", sn_num_total,
		len > MAX_SN_NUM_SIZE ? MAX_SN_NUM_SIZE : len);
	if (rc) {
		chg_err("maxim get oplus,batt_info failed %d\n", rc);
		return -1;
	}

	chip->sn_num_info.sn_num_number = len / BATT_SN_NUM_LEN;
	chg_info("parse oplus,batt_info,sn_num_number = %d\n", chip->sn_num_info.sn_num_number);
	for (j = 0; j < len/BATT_SN_NUM_LEN; j++) {
		memcpy(chip->sn_num_info.sn_num[j], &sn_num_total[j*BATT_SN_NUM_LEN], BATT_SN_NUM_LEN);
		for (i = 0; i < BATT_SN_NUM_LEN; i++) {
			chg_info("parse oplus,batt_info, sn_num[%d:%d] = 0x%x\n", j, i, chip->sn_num_info.sn_num[j][i]);
		}
	}

	return 0;
}


static void oplus_maxim_auth_work(struct work_struct *work)
{
	int ret = false;
	if (!g_maxim_chip) {
		return;
	}
	try_count++;
	g_maxim_chip->test_result.real_test_count_now++;
	g_maxim_chip->hmac_status.real_total_count++;
	onewire_set_gpio_config_out();
	ret = authenticate_ds28e30(&g_maxim_chip->sn_num_info, 0);
	if (ret == false) {
		g_maxim_chip->test_result.real_test_fail_count++;
		g_maxim_chip->hmac_status.real_fail_count++;
		if (try_count < g_maxim_chip->try_count) {
			schedule_delayed_work_on(g_maxim_chip->cpu_id, &g_maxim_chip->auth_work, msecs_to_jiffies(100));
			return;
		} else {
			complete(&g_maxim_chip->is_complete);
		}
		try_count = 0;
	} else {
		try_count = 0;
		g_maxim_chip->authenticate_result = true;
		complete(&g_maxim_chip->is_complete);
	}
}

int oplus_maxim_auth(void)
{
	if (!g_maxim_chip) {
		return 0;
	}
	reinit_completion(&g_maxim_chip->is_complete);
	schedule_delayed_work_on(g_maxim_chip->cpu_id, &g_maxim_chip->auth_work, 0);
	if (!wait_for_completion_timeout(&g_maxim_chip->is_complete,
			msecs_to_jiffies(5000 * g_maxim_chip->try_count))) {
		chg_err("time out!\n");
	}
	cancel_delayed_work_sync(&g_maxim_chip->auth_work);
	return g_maxim_chip->authenticate_result;
}

static void oplus_maxim_test_func(struct work_struct *work)
{
	int ret;
	if (!g_maxim_chip) {
		return;
	}

	while (g_maxim_chip->test_result.test_count_now < g_maxim_chip->test_result.test_count_total) {
		g_maxim_chip->test_result.test_count_now++;
		g_maxim_chip->try_count = TEST_COUNT;
		ret = oplus_maxim_auth();
		if (ret == false) {
			g_maxim_chip->test_result.test_fail_count++;
		}
	}
}

int oplus_maxim_get_external_auth_hmac(void)
{
	int ret;
	if (!g_maxim_chip) {
		return false;
	}
	if(g_maxim_chip->authenticate_result == false) {
		g_maxim_chip->hmac_status.total_count++;
		g_maxim_chip->try_count = TEST_COUNT;
		ret = oplus_maxim_auth();
		if (ret == false) {
			g_maxim_chip->hmac_status.fail_count++;
		}
	}
	return g_maxim_chip->authenticate_result;
}

int oplus_maxim_start_test(int count)
{
	if (!g_maxim_chip) {
		return -1;
	}
	cancel_delayed_work_sync(&g_maxim_chip->test_work);
	g_maxim_chip->test_result.test_count_now = 0;
	g_maxim_chip->test_result.test_count_total = count;
	g_maxim_chip->test_result.test_fail_count = 0;
	g_maxim_chip->test_result.real_test_count_now = 0;
	g_maxim_chip->test_result.real_test_fail_count = 0;
	schedule_delayed_work_on(g_maxim_chip->cpu_id, &g_maxim_chip->test_work, 0);
	return 0;
}

int oplus_maxim_get_test_result(int *count_total, int *count_now, int *fail_count)
{
	if (!g_maxim_chip) {
		return -1;
	}
	*count_total = g_maxim_chip->test_result.test_count_total;
	*count_now = g_maxim_chip->test_result.test_count_now;
	*fail_count = g_maxim_chip->test_result.test_fail_count;
	chg_err("count_total:%d,count_now:%d,fail_count:%d,real_count:%d,real_fail:%d\n",
			*count_total, *count_now, *fail_count,
			g_maxim_chip->test_result.real_test_count_now,
			g_maxim_chip->test_result.real_test_fail_count);
	return 0;
}

int oplus_maxim_get_hmac_status(int *status, int *fail_count, int *total_count,
	int *real_fail_count, int *real_total_count) {
	if (!g_maxim_chip) {
		return -1;
	}
	*status = g_maxim_chip->authenticate_result;
	*fail_count = g_maxim_chip->hmac_status.fail_count;
	*total_count = g_maxim_chip->hmac_status.total_count;
	*real_fail_count = g_maxim_chip->hmac_status.real_fail_count;
	*real_total_count = g_maxim_chip->hmac_status.real_total_count;
	chg_err("status:%d,fail_count:%d,total_count:%d,real_fail_count:%d,real_total_count:%d\n",
			*status, *fail_count, *total_count, *real_fail_count, *real_total_count);
	return 0;
}

static int oplus_maxim_probe(struct platform_device *pdev)
{
	int ret;
	bool flag;
	struct oplus_external_auth_chip	*external_auth_chip = NULL;

	chg_info("%s: entery\n", __func__);
	g_maxim_chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_maxim_gauge_chip), GFP_KERNEL);
	if (!g_maxim_chip) {
		chg_err("Failed to allocate memory\n");
		return -ENOMEM;
	}
	g_maxim_chip->authenticate_result = false;
	g_maxim_chip->test_result.test_count_total = 0;
	g_maxim_chip->test_result.test_fail_count = 0;
	g_maxim_chip->test_result.test_count_now = 0;
	g_maxim_chip->test_result.real_test_count_now = 0;
	g_maxim_chip->test_result.real_test_fail_count = 0;
	g_maxim_chip->try_count = TEST_COUNT;
	g_maxim_chip->dev = &pdev->dev;
	g_maxim_chip->dev = &pdev->dev;
	platform_set_drvdata(pdev, g_maxim_chip);
	oplus_maxim_parse_dt(g_maxim_chip);

	init_completion(&g_maxim_chip->is_complete);
	INIT_DELAYED_WORK(&g_maxim_chip->auth_work, oplus_maxim_auth_work);
	INIT_DELAYED_WORK(&g_maxim_chip->test_work, oplus_maxim_test_func);

	flag = oplus_maxim_check_auth_msg();
	if (g_maxim_chip->support_maxim_in_lk && flag) {
		chg_info("%s get lk auth success .\n", __func__);
		ret = true;

		/*LK already auth success, Kernel not auth again.*/
		g_maxim_chip->authenticate_result = true;
	} else {
		chg_info("%s: lk auth  failed\n", __func__);
		if (g_maxim_chip->support_maxim_in_kernel == false) {
			chg_err("%s: not support kernel auth\n", __func__);
			return 0;
		}
		g_maxim_chip->authenticate_result = false;
		g_maxim_chip->gpio_info.gpio_cfg_out_reg = devm_ioremap(&pdev->dev,
					g_maxim_chip->gpio_info.onewire_gpio_cfg_addr_out, 0x4);
		g_maxim_chip->gpio_info.gpio_cfg_in_reg = devm_ioremap(&pdev->dev,
					g_maxim_chip->gpio_info.onewire_gpio_cfg_addr_in, 0x4);
		g_maxim_chip->gpio_info.gpio_out_high_reg = devm_ioremap(&pdev->dev,
					g_maxim_chip->gpio_info.onewire_gpio_level_addr_high, 0x4);
		g_maxim_chip->gpio_info.gpio_out_low_reg = devm_ioremap(&pdev->dev,
					g_maxim_chip->gpio_info.onewire_gpio_level_addr_low, 0x4);
		g_maxim_chip->gpio_info.gpio_in_reg = devm_ioremap(&pdev->dev,
					g_maxim_chip->gpio_info.onewire_gpio_in_addr, 0x4);
		chg_info("out_reg is 0x%p, in_reg is 0x%p, high_reg 0x%p, low_reg 0x%p, reg 0x%p",
			g_maxim_chip->gpio_info.gpio_cfg_out_reg,
			g_maxim_chip->gpio_info.gpio_cfg_in_reg,
			g_maxim_chip->gpio_info.gpio_out_high_reg,
			g_maxim_chip->gpio_info.gpio_out_low_reg,
			g_maxim_chip->gpio_info.gpio_in_reg);

		chg_info("addr_out is 0x%x, addr_in is 0x%x, addr_high 0x%x, addr_low 0x%x, addr 0x%x",
			g_maxim_chip->gpio_info.onewire_gpio_cfg_addr_out,
			g_maxim_chip->gpio_info.onewire_gpio_cfg_addr_in,
			g_maxim_chip->gpio_info.onewire_gpio_level_addr_high,
			g_maxim_chip->gpio_info.onewire_gpio_level_addr_low,
			g_maxim_chip->gpio_info.onewire_gpio_in_addr);

		chg_err("check kernel auth.\n");
		ret = onewire_init(&g_maxim_chip->gpio_info);
		if (ret < 0) {
			chg_err("onewire_init failed, ret=%d\n", ret);
			g_maxim_chip->support_maxim_in_kernel = false;
		} else {
			flag = oplus_maxim_get_external_auth_hmac();
			if(flag== true) {
				chg_info("%s: Authenticated flag %d succ\n", __func__, flag);
				g_maxim_chip->authenticate_result = true;
			} else {
				chg_info("%s: Authenticated flag %d failed\n", __func__, flag);
			}
		}
	}

	external_auth_chip = devm_kzalloc(&pdev->dev,
			sizeof(struct oplus_external_auth_chip), GFP_KERNEL);
	if (!external_auth_chip) {
		ret = -ENOMEM;
		goto error;
	}

	external_auth_chip->get_external_auth_hmac = oplus_maxim_get_external_auth_hmac;
	external_auth_chip->start_test_external_hmac = oplus_maxim_start_test;
	external_auth_chip->get_hmac_test_result = oplus_maxim_get_test_result;
	external_auth_chip->get_hmac_status = oplus_maxim_get_hmac_status;
	oplus_external_auth_init(external_auth_chip);

	chg_info(" register %s\n", g_maxim_chip->dev->of_node->name);
	chg_info(" oplus_maxim_probe sucess\n");
	return 0;
error:
	if (g_maxim_chip) {
		kfree(g_maxim_chip);
	}
	chg_err("oplus_maxim_probe fail :%d\n", ret);
	return ret;
}

static void oplus_maxim_shutdown(struct platform_device *pdev)
{
	return;
}

static const struct of_device_id of_oplus_maxim_match[] = {
	{ .compatible = "oplus-maxim", },
	{},
};
MODULE_DEVICE_TABLE(of, of_oplus_maxim_match);

static struct platform_driver oplus_maxim_driver = {
	.probe		= oplus_maxim_probe,
	.shutdown	= oplus_maxim_shutdown,
	.driver		= {
		.name	= "oplus-maxim",
		.of_match_table = of_oplus_maxim_match,
	},
};

static __init int oplus_maxim_driver_init(void)
{
	int ret;
	chg_info("%s: start\n", __func__);
	ret = platform_driver_register(&oplus_maxim_driver);
	return ret;
}

static __exit void oplus_maxim_driver_exit(void)
{
	platform_driver_unregister(&oplus_maxim_driver);
}


oplus_chg_module_register(oplus_maxim_driver);

MODULE_DESCRIPTION("oplus maxim driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:oplus-maxim");
