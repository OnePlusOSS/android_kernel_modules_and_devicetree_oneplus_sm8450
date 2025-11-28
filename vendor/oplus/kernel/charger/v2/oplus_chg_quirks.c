// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2024 . Oplus All rights reserved.
 */

#define pr_fmt(fmt) "[quirks]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/random.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/rtc.h>

#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/gfp.h>

#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include <linux/bitops.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/leds.h>
#include <linux/jiffies.h>


#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>
#include <oplus_mms_gauge.h>
#include <oplus_chg_state_retention.h>
#include <oplus_chg_quirks.h>
#include <oplus_chg_monitor.h>
#include <oplus_chg_cpa.h>

#define CONNECT_ERROR_COUNT_LEVEL 3
#define WAIT_QUIRKS_NOTIFY_PLUGIN_ENTER 150

struct oplus_quirks {
	struct device *dev;
	struct mms_subscribe *wired_subs;
	struct mms_subscribe *cpa_subs;
	struct oplus_mms *wired_topic;
	struct oplus_mms *cpa_topic;
	struct oplus_mms *comm_topic;
	atomic_t last_plugin_status;
	atomic_t oplus_quirks_init_status;
	unsigned long  keep_connect_jiffies;
	int plugout_retry;
	bool chg_quirks_support;
	int keep_connect;
	bool wired_online;
	bool is_usb_type;
	int irq_plugin;
	int wired_type;
	int diconnect_count_flag;

	struct delayed_work cpa_to_dcp_work;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	struct wake_lock suspend_lock;
#else
	struct wakeup_source *awake_lock;
#endif
	struct plug_info plug_info_head;
	struct timer_list update_plugin_timer;
	struct delayed_work voocphy_turn_on;
	enum oplus_chg_protocol_type cpa_current_type;
};

static struct oplus_quirks *g_quirks_chip;
static void oplus_quirks_update_plugin_timer(struct oplus_quirks *chip, unsigned int ms);

static void oplus_quirks_awake_init(struct oplus_quirks *chip)
{
	if (!chip) {
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND, "battery quirks connect suspend wakelock");

#else
	chip->awake_lock = wakeup_source_register(NULL, "oplus_quirks_wakelock");
#endif
}

static void oplus_quirks_set_awake(struct oplus_quirks *chip, int time_ms)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))

	if (awake)
		wake_lock(&chip->suspend_lock);
	else
		wake_unlock(&chip->suspend_lock);
#else
	if (!chip || !chip->awake_lock)
		return;

	chg_info(":%d\n", time_ms);

	__pm_wakeup_event(chip->awake_lock, time_ms);

	chg_info("end :%d\n", time_ms);
#endif
}
static int quirks_diconnect_count(void)
{
	int count = 0;
	struct plug_info *info;
	struct list_head *pos, *n;
	struct oplus_quirks *chip = g_quirks_chip;
	int i = 0;

	if (chip == NULL) {
		chg_err("g_quirks_chip is NULL\n");
		return -EINVAL;
	}

	if (list_empty(&chip->plug_info_head.list)) {
		chg_err("chip->plug_info_head.list null!\n");
		return 0;
	}

	list_for_each_safe(pos, n, &chip->plug_info_head.list) {
		info = list_entry(pos, struct plug_info, list);
		if (time_is_after_jiffies(info->plugout_jiffies +
			msecs_to_jiffies(KEEP_CONNECT_TIME_OUT))) {
			if (info->abnormal_diconnect == 1)
				count++;
		}
		i++;
		chg_debug("info%d[%d] plugout_jiffies:%lu,plugin_jiffies:%lu, jiffies:%lu,"
			"abnormal_diconnect:%d, count:%d, in_20s:%d\n",
			info->number, i, info->plugout_jiffies, info->plugin_jiffies, jiffies,
			info->abnormal_diconnect, count,
			time_is_after_jiffies(info->plugin_jiffies + msecs_to_jiffies(KEEP_CONNECT_TIME_OUT)));
	}
	info = &g_quirks_chip->plug_info_head;
	if (time_is_after_jiffies(info->plugout_jiffies + msecs_to_jiffies(KEEP_CONNECT_TIME_OUT))) {
		if (info->abnormal_diconnect == 1)
			count++;
	}
	i++;
	chg_err("info%d[%d] plugout_jiffies:%lu,plugin_jiffies:%lu, jiffies:%lu,"
		"abnormal_diconnect:%d, count:%d, in_20s:%d\n",
		info->number, i, info->plugout_jiffies, info->plugin_jiffies, jiffies,
		info->abnormal_diconnect, count,
		time_is_after_jiffies(info->plugin_jiffies + msecs_to_jiffies(KEEP_CONNECT_TIME_OUT)));
	return count;
}

static int clear_quirks_diconnect_count(void)
{
	struct plug_info *info;
	struct list_head *pos, *n;
	struct oplus_quirks *chip = g_quirks_chip;

	if (chip == NULL) {
		chg_err("g_quirks_chip is NULL\n");
		return 0;
	}

	if (list_empty(&chip->plug_info_head.list)) {
		chg_err("chip->plug_info_head.list null!\n");
		return 0;
	}

	list_for_each_safe(pos, n, &chip->plug_info_head.list) {
		info = list_entry(pos, struct plug_info, list);
		info->abnormal_diconnect = 0;
	}
	info = &g_quirks_chip->plug_info_head;
	info->abnormal_diconnect = 0;
	chg_err("!!\n");
	return 0;
}

static int clear_quirks_connect_status(void)
{
	struct oplus_quirks *chip = g_quirks_chip;

	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}
	chip->keep_connect = 0;
	chip->diconnect_count_flag = CONNECT_ERROR_COUNT_LEVEL;
	chg_err("clear_quirks!!\n");
	return 0;
}

static int oplus_quirks_keep_connect_status(void)
{
	struct oplus_quirks *chip = g_quirks_chip;

	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}
	if (list_empty(&g_quirks_chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return 0;
	}
	if (chip->keep_connect) {
		chg_info("keep_connect!:last_plugin_status:%d, keep_connect:%d,"
				"keep_connect_jiffies:%lu, jiffies:%lu\n",
				atomic_read(&chip->last_plugin_status), chip->keep_connect,
				chip->keep_connect_jiffies, jiffies);
		return 1;
	}
	return 0;
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static void update_plugin_status(unsigned long data)
#else
static void update_plugin_status(struct timer_list *unused)
#endif
{
	struct oplus_quirks *chip = g_quirks_chip;

	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return;
	}
	if (list_empty(&g_quirks_chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return;
	}

	if (atomic_read(&chip->last_plugin_status) == 0) {
		clear_quirks_diconnect_count();
		chip->keep_connect = 0;

		if (chip->plugout_retry < PLUGOUT_RETRY) {
			chg_err("retry\n");
			chip->plugout_retry++;
			oplus_quirks_set_awake(chip, PLUGOUT_WAKEUP_TIMEOUT);
			mod_timer(&chip->update_plugin_timer, jiffies + msecs_to_jiffies(ABNORMAL_DISCONNECT_INTERVAL));
		} else {
			chg_err("unwakeup\n");
		}
	} else {
		chg_err("do nothing\n");
	}
	chg_err(":last_plugin_status:%d, keep_connect:%d, keep_connect_jiffies:%lu, jiffies:%lu\n",
			atomic_read(&chip->last_plugin_status), chip->keep_connect, chip->keep_connect_jiffies, jiffies);
	return;
}

static void oplus_quirks_update_plugin_timer(struct oplus_quirks *chip, unsigned int ms)
{
	if (!chip) {
		chg_err("oplus_vooc_chip is not ready\n");
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	mod_timer(&chip->update_plugin_timer, jiffies+msecs_to_jiffies(25000));
#else
	mod_timer(&chip->update_plugin_timer, jiffies + msecs_to_jiffies(ms));
#endif
}

static int oplus_quirks_notify_plugin(bool plugin)
{
	struct oplus_quirks *chip = g_quirks_chip;
	struct plug_info *info;
	static struct plug_info *last_plug_info = NULL;
	int count;
	int cc_online = 0;
	static int last_plugin = -1;

	if (!chip) {
		chg_err("g_quirks_chip null!\n");
		return 0;
	}

	if ((chip->diconnect_count_flag >= CONNECT_ERROR_COUNT_LEVEL || chip->is_usb_type) && !plugin) {
		chg_err("now is dcp disconnect three times or usb_type!\n");
		return 0;
	}

	if (atomic_read(&chip->oplus_quirks_init_status) == 0) {
		chg_err("oplus_quirks_init is not completed!\n");
		return 0;
	}

	if (list_empty(&chip->plug_info_head.list)) {
		chg_err("g_quirks_chip->plug_info_head.list null!\n");
		return 0;
	}
	if (last_plug_info == NULL) {
		last_plug_info = list_entry(&g_quirks_chip->plug_info_head.list, struct plug_info, list);
	}

	chg_debug("plugin:%d, last_plugin:%d\n", plugin, last_plugin);
	if (last_plugin == plugin) {
		chg_debug("plugin:%d, last_plugin:%d, return\n", plugin, last_plugin);
		return 0;
	}
	last_plugin = plugin;
	if (!list_empty(&chip->plug_info_head.list)) {
		if (atomic_read(&chip->last_plugin_status) == plugin || plugin == 1)
			info = last_plug_info;
		else
			info = list_entry(last_plug_info->list.next, struct plug_info, list);
		last_plug_info = info;
		atomic_set(&chip->last_plugin_status, plugin);
		if (plugin == 0) {
			chip->plugout_retry = 0;
			count = quirks_diconnect_count();
			info->plugout_jiffies = jiffies;
			chip->keep_connect_jiffies = jiffies;
			cc_online = oplus_wired_get_hw_detect_recheck();
			chg_debug("%d:count:%d, cc_online:%d", __LINE__, count, cc_online);
			oplus_quirks_set_awake(chip, PLUGOUT_WAKEUP_TIMEOUT);
			if (cc_online == CC_DETECT_PLUGIN) {
				if (count >= CONNECT_ERROR_COUNT_LEVEL_3) {
					chip->keep_connect = 0;
					chg_err("quirks_diconnect_count:%d, cc_online:%d\n", count, cc_online);
					oplus_quirks_update_plugin_timer(chip, ABNORMAL_DISCONNECT_INTERVAL);
				} else {
					chip->keep_connect = 1;
					oplus_quirks_update_plugin_timer(chip, ABNORMAL_DISCONNECT_INTERVAL);
					chg_err("quirks_diconnect_count:%d, cc_online:%d, after 1s, check plugin again\n",
						count, cc_online);
				}
			} else {
				chip->keep_connect = 0;
				clear_quirks_diconnect_count();
				chg_err("quirks_diconnect_count:%d, cc_online:%d, do not keep connect\n",
					count, cc_online);
			}
		} else {
			info->plugin_jiffies = jiffies;
			if (time_is_after_jiffies(info->plugout_jiffies +
				msecs_to_jiffies(ABNORMAL_DISCONNECT_INTERVAL))) {
				info->abnormal_diconnect = 1;
				chg_err("abnormal_diconnect, info->plugout_jiffies:%lu, info->plugin_jiffies:%lu",
					info->plugout_jiffies, info->plugin_jiffies);
			}
		}
	}
	chg_debug(":last_plugin_status:%d, keep_connect:%d keep_connect_jiffies:%lu, jiffies:%lu\n",
		atomic_read(&chip->last_plugin_status), chip->keep_connect, chip->keep_connect_jiffies, jiffies);
	return 0;
}

static void oplus_quirks_cpa_to_dcp_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_quirks *chip = container_of(dwork, struct oplus_quirks,
						   cpa_to_dcp_work);

	if (!chip->irq_plugin) {
		if(quirks_diconnect_count() >= CONNECT_ERROR_COUNT_LEVEL)
			chip->diconnect_count_flag = CONNECT_ERROR_COUNT_LEVEL;
		if (chip->diconnect_count_flag >= CONNECT_ERROR_COUNT_LEVEL) {
			clear_quirks_connect_status();
			clear_quirks_diconnect_count();
		}
	}
}

static void oplus_quirks_chg_cpa_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_quirks *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case CPA_ITEM_ALLOW:
			oplus_mms_get_item_data(chip->cpa_topic, id, &data, false);
			if (data.intval != CHG_PROTOCOL_INVALID)
				chip->cpa_current_type = data.intval;
			if (chip->cpa_current_type != CHG_PROTOCOL_BC12)
				chip->diconnect_count_flag = 1;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_quirks_chg_subscribe_cpa_topic(struct oplus_mms *topic,
						  void *prv_data)
{
	struct oplus_quirks *chip = prv_data;

	chip->cpa_topic = topic;
	chip->cpa_subs =
		oplus_mms_subscribe(chip->cpa_topic, chip,
				    oplus_quirks_chg_cpa_subs_callback, "quirks");
	if (IS_ERR_OR_NULL(chip->cpa_subs)) {
		chg_err("subscribe cpa topic error, rc=%ld\n",
			PTR_ERR(chip->cpa_subs));
		return;
	}
}

static void oplus_quirks_wired_subs_callback(struct mms_subscribe *subs,
						enum mms_msg_type type, u32 id, bool sync)
{
	struct oplus_quirks *chip = subs->priv_data;
	union mms_msg_data data = { 0 };

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_ONLINE:
			oplus_mms_get_item_data(chip->wired_topic, id, &data, false);
			chip->wired_online = !!data.intval;
			break;
		case WIRED_ITEM_PRESENT:
			oplus_mms_get_item_data(chip->wired_topic, id, &data, false);
			chip->irq_plugin = !!data.intval;
			if (chip->cpa_current_type == CHG_PROTOCOL_BC12)
				schedule_delayed_work(&chip->cpa_to_dcp_work, 0);
			else
				chip->diconnect_count_flag = 1;
			break;
		case WIRED_ITEM_CHG_TYPE:
			oplus_mms_get_item_data(chip->wired_topic, id, &data, false);
			chip->wired_type = data.intval;
			if (chip->wired_type == OPLUS_CHG_USB_TYPE_PD_SDP ||
				chip->wired_type == OPLUS_CHG_USB_TYPE_SDP ||
				chip->wired_type == OPLUS_CHG_USB_TYPE_CDP) {
				chip->is_usb_type = true;
				clear_quirks_connect_status();
			} else if (chip->wired_type != OPLUS_CHG_USB_TYPE_UNKNOWN) {
				chip->is_usb_type = false;
			}
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_quirks_quirks_wired_topic(struct oplus_mms *topic,
						  void *prv_data)
{
	struct oplus_quirks *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    oplus_quirks_wired_subs_callback, "quirks");
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("quirks wired topic error, rc=%ld\n",
			PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_ONLINE, &data, true);
	chip->wired_online = !!data.intval;
}

static void oplus_quirks_common_topic_ready(struct oplus_mms *topic,
						void *prv_data)
{
	struct oplus_quirks *chip = prv_data;

	chip->comm_topic = topic;

	oplus_mms_wait_topic("wired", oplus_quirks_quirks_wired_topic, chip);
	oplus_mms_wait_topic("cpa", oplus_quirks_chg_subscribe_cpa_topic, chip);
}

static int oplus_chg_quirks_probe(struct platform_device *pdev)
{
	int i = 0;
	struct plug_info *info;
	struct list_head *pos, *n;
	struct oplus_quirks *quirks_chip_init;

	quirks_chip_init = devm_kzalloc(&pdev->dev, sizeof(struct oplus_quirks), GFP_KERNEL);
	if (!quirks_chip_init) {
		chg_err("quirks_chip_init already kzalloc init fail!\n");
		return 0;
	}
	quirks_chip_init->dev = &pdev->dev;
	platform_set_drvdata(pdev, quirks_chip_init);
	oplus_mms_wait_topic("common", oplus_quirks_common_topic_ready, quirks_chip_init);
	g_quirks_chip = quirks_chip_init;

	oplus_quirks_awake_init(g_quirks_chip);

	INIT_LIST_HEAD(&g_quirks_chip->plug_info_head.list);

	for (i = 0; i < PLUGINFO_MAX_NUM; i++) {
		info = kmalloc(sizeof(struct plug_info), GFP_KERNEL);
		if (!info)
			goto error;
		memset(info, 0, sizeof(struct plug_info));
		list_add(&info->list, &g_quirks_chip->plug_info_head.list);
		info->number = i;
		chg_err("%d\n", i);
	}

	atomic_set(&g_quirks_chip->last_plugin_status, 0);
	g_quirks_chip->keep_connect = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	init_timer(g_quirks_chip->update_plugin_timer);
	g_quirks_chip->update_plugin_timer.data = (unsigned long)g_quirks_chip;
	g_quirks_chip->update_plugin_timer.function = update_plugin_status;
#else
	timer_setup(&g_quirks_chip->update_plugin_timer, update_plugin_status, 0);
#endif
	atomic_set(&g_quirks_chip->oplus_quirks_init_status, 1);

	INIT_DELAYED_WORK(&g_quirks_chip->cpa_to_dcp_work, oplus_quirks_cpa_to_dcp_work);

	return 0;
error:
	chg_err("init error\n");
	if (!list_empty(&g_quirks_chip->plug_info_head.list)) {
		list_for_each_safe(pos, n, &g_quirks_chip->plug_info_head.list) {
			info = list_entry(pos, struct plug_info, list);
			list_del(&info->list);
			kfree(info);
		}
	}
	kfree(quirks_chip_init);
	return -1;
}

static int oplus_chg_quirks_remove(struct platform_device *pdev)
{
	struct oplus_quirks *quirks_chip_init = platform_get_drvdata(pdev);
	struct plug_info *info;
	struct list_head *pos, *n;

	if (!quirks_chip_init)
		return -ENOMEM;

	list_for_each_safe(pos, n, &g_quirks_chip->plug_info_head.list) {
		info = list_entry(pos, struct plug_info, list);
		list_del(&info->list);
		kfree(info);
	}

	g_quirks_chip = NULL;

	if (!IS_ERR_OR_NULL(quirks_chip_init->wired_subs))
		oplus_mms_unsubscribe(quirks_chip_init->wired_subs);
	devm_kfree(&pdev->dev, quirks_chip_init);

	return 0;
}

static const struct of_device_id oplus_chg_quirks_match[] = {
	{ .compatible = "oplus,chg_quirks" },
	{},
};

static struct platform_driver oplus_chg_quirks_driver = {
	.driver		= {
		.name = "oplus-chg_quirks",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(oplus_chg_quirks_match),
	},
	.probe		= oplus_chg_quirks_probe,
	.remove		= oplus_chg_quirks_remove,
};
static int quirks_retention_register(void);
/* never return an error value */
static __init int oplus_chg_quirks_init(void)
{
	quirks_retention_register();
	return platform_driver_register(&oplus_chg_quirks_driver);
}

static __exit void oplus_chg_quirks_exit(void)
{
	platform_driver_unregister(&oplus_chg_quirks_driver);
}

oplus_chg_module_core_register(oplus_chg_quirks);

static struct oplus_chg_retention_desc quirks_retention_desc = {
	.name = "oplus_retention_quirks",
	.state_retention = oplus_quirks_keep_connect_status,
	.disconnect_count = quirks_diconnect_count,
	.clear_disconnect_count = clear_quirks_diconnect_count,
	.state_retention_notify = oplus_quirks_notify_plugin,
};

static int quirks_retention_register(void)
{
	return oplus_chg_retention_register(&quirks_retention_desc);
}
