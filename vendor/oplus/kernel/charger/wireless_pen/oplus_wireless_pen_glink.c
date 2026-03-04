// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/iio/consumer.h>

#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/system/oplus_project.h>
#endif

#include <linux/remoteproc/qcom_rproc.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include "oplus_wireless_pen_glink.h"

#ifndef CONFIG_OPLUS_CHARGER_MTK
struct wireless_pen_glink {
	struct device *dev;
	struct pmic_glink_client *client;
	atomic_t state;
	struct work_struct subsys_up_work;
	struct mutex hboost_set_lock;
	struct completion ack;
};

struct wireless_pen_glink *g_wpgdev = NULL;

static int wireless_pen_hboost_req_write(void *data, int len)
{
	int rc;
	struct wireless_pen_glink *wpgdev = g_wpgdev;

	if (!wpgdev) {
		printk(KERN_ERR "[%s]wpgdev null\n", __func__);
		return -ENODEV;
	}
	/*
	 * When the subsystem goes down, it's better to return the last
	 * known values until it comes back up. Hence, return 0 so that
	 * pmic_glink_write() is not attempted until pmic glink is up.
	 */
	if (atomic_read(&wpgdev->state) == PMIC_GLINK_STATE_DOWN) {
		dev_err(wpgdev->dev, "[%s]glink state is down\n", __func__);
		return -ENOTCONN;
	}

	mutex_lock(&wpgdev->hboost_set_lock);
	reinit_completion(&wpgdev->ack);
	rc = pmic_glink_write(wpgdev->client, data, len);
	if (!rc) {
		rc = wait_for_completion_timeout(&wpgdev->ack, msecs_to_jiffies(BC_WAIT_TIME_MS));
		if (!rc) {
			dev_err(wpgdev->dev, "[%s]Error, timed out sending message\n", __func__);
			mutex_unlock(&wpgdev->hboost_set_lock);
			return -ETIMEDOUT;
		}

		rc = 0;
	}
	mutex_unlock(&wpgdev->hboost_set_lock);

	return rc;
}

int wireless_pen_send_hboost_volt_req(uint8_t value)
{
	struct wireless_pen_set_hboost_vout_req req_msg = { { 0 } };

	if (!g_wpgdev) {
		printk(KERN_ERR "[%s]g_wpgdev null\n", __func__);
		return -ENODEV;
	}
	req_msg.reg_vout = value;
	req_msg.hdr.owner = MSG_OWNER_HBOOST;
	req_msg.hdr.type = MSG_TYPE_REQ_RESP;
	req_msg.hdr.opcode = OEM_OPCODE_SET_HBOOST_VOLT;

	dev_err(g_wpgdev->dev, "[%s] value = %d\n", __func__, value);

	return wireless_pen_hboost_req_write(&req_msg, sizeof(req_msg));
}


static void handle_hboost_notification(struct wireless_pen_glink *wpgdev,
	struct wireless_pen_set_hboost_vout_resp *resp_msg, size_t len)
{
	if (!wpgdev || !resp_msg) {
		dev_err(wpgdev->dev, "[%s]wpgdev alloc fail\n", __func__);
		return;
	}


	if (resp_msg->ret_code != 0) {
		dev_err(wpgdev->dev, "[%s] set vout fail\n", __func__);
		return;
	}

	complete(&wpgdev->ack);
}

static int wireless_pen_glink_callback(void *priv, void *data, size_t len)
{
	struct pmic_glink_hdr *hdr = data;
	struct wireless_pen_glink *wpgdev = priv;

	if (!wpgdev || !hdr) {
		dev_err(wpgdev->dev, "[%s] wpgdev null \n", __func__);
		return -EINVAL;
	}

	if (hdr->opcode == OEM_OPCODE_SET_HBOOST_VOLT)
		handle_hboost_notification(wpgdev, data, len);

	return 0;
}

static void wireless_pen_glink_state_cb(void *priv, enum pmic_glink_state state)
{
	struct wireless_pen_glink *wpgdev = priv;

	dev_err(wpgdev->dev, "[%s]state: %d\n\n", __func__, state);

	atomic_set(&wpgdev->state, state);
	if (state == PMIC_GLINK_STATE_UP)
		schedule_work(&wpgdev->subsys_up_work);
}

static void subsys_up_work_func(struct work_struct *work)
{
	struct wireless_pen_glink *wpgdev =
		container_of(work, struct wireless_pen_glink, subsys_up_work);

	if (!wpgdev)
		return;

	dev_err(wpgdev->dev, "[%s]\n", __func__);
}

static int wireless_pen_glink_probe(struct platform_device *pdev)
{
	struct wireless_pen_glink *wpgdev;
	struct device *dev = &pdev->dev;
	struct pmic_glink_client_data client_data = { };
	int rc;

	wpgdev = devm_kzalloc(&pdev->dev, sizeof(*wpgdev), GFP_KERNEL);
	if (!wpgdev) {
		dev_err(dev, "[%s]wpgdev alloc fail\n", __func__);
		return -ENOMEM;
	}

	atomic_set(&wpgdev->state, PMIC_GLINK_STATE_UP);
	wpgdev->dev = &pdev->dev;

	client_data.id = MSG_OWNER_HBOOST;
	client_data.name = "wireless_pen";
	client_data.msg_cb = wireless_pen_glink_callback;
	client_data.priv = wpgdev;
	client_data.state_cb = wireless_pen_glink_state_cb;

	wpgdev->client = pmic_glink_register_client(dev, &client_data);
	if (IS_ERR(wpgdev->client)) {
		rc = PTR_ERR(wpgdev->client);
		if (rc != -EPROBE_DEFER)
			dev_err(dev, "Error in registering with pmic_glink %d\n", rc);
		devm_kfree(wpgdev->dev, wpgdev);
		return rc;
	}

	INIT_WORK(&wpgdev->subsys_up_work, subsys_up_work_func);
	mutex_init(&wpgdev->hboost_set_lock);
	init_completion(&wpgdev->ack);

	g_wpgdev = wpgdev;
	dev_err(dev, "[%s] success\n", __func__);
	return 0;
}

static int wireless_pen_glink_remove(struct platform_device *pdev)
{
	if (!g_wpgdev)
		return 0;

	pmic_glink_unregister_client(g_wpgdev->client);
	devm_kfree(g_wpgdev->dev, g_wpgdev);
	g_wpgdev = NULL;
	return 0;
}

static const struct of_device_id wireless_pen_match_table[] = {
	{ .compatible = "oplus,wireless_pen_glink" },
	{},
};

static struct platform_driver wireless_pen_glink_driver = {
	.driver = {
		.name = "wireless_pen_glink",
		.of_match_table = wireless_pen_match_table,
	},
	.probe = wireless_pen_glink_probe,
	.remove = wireless_pen_glink_remove,
};

void wireless_pen_glink_init(void)
{
	if (g_wpgdev) {
		dev_err(g_wpgdev->dev, "[%s]already init\n", __func__);
		return;
	}

	platform_driver_register(&wireless_pen_glink_driver);
}

void wireless_pen_glink_exit(void)
{
	platform_driver_unregister(&wireless_pen_glink_driver);
}
#else /*ifndef CONFIG_OPLUS_CHARGER_MTK*/
void wireless_pen_glink_init(void)
{
}

void wireless_pen_glink_exit(void)
{
}

int wireless_pen_send_hboost_volt_req(uint8_t value)
{
	return 0;
}
#endif/*CONFIG_OPLUS_CHARGER_MTK*/
MODULE_DESCRIPTION("QTI Glink wireless pen driver");
MODULE_LICENSE("GPL v2");
