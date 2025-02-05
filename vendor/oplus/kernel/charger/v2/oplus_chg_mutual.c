// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[MUTUAL]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/errno.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <oplus_chg.h>
#include <oplus_chg_module.h>
#include <oplus_chg_mutual.h>
#include <linux/slab.h>

#define MUTUAL_CMD_DATA_LEN		256
#define MUTUAL_CMD_TIME_MS		3000

struct oplus_mutual_cmd {
	unsigned int cmd;
	unsigned int data_size;
	unsigned char data_buf[MUTUAL_CMD_DATA_LEN];
};

struct oplus_chg_mutual {
	bool mutual_cmd_data_ok;
	bool mutual_hidl_handle_cmd_ready;
	struct mutex mutual_read_lock;
	struct mutex mutual_cmd_data_lock;
	struct mutex mutual_cmd_ack_lock;
	struct oplus_mutual_cmd mutual_cmd;
	struct completion mutual_cmd_ack;
	wait_queue_head_t mutual_read_wq;
	struct class mutual_class;
};

static DEFINE_MUTEX(list_lock);
static LIST_HEAD(mutual_list);
static struct oplus_chg_mutual *g_mutual_chip;
static ATOMIC_NOTIFIER_HEAD(chg_mutual_notifier);

static void oplus_chg_response_mutual_cmd(
			struct oplus_chg_mutual *chip, const char *buf, size_t count);
static ssize_t oplus_chg_send_mutual_cmd(
		struct oplus_chg_mutual *chip, char *buf);

static struct oplus_chg_mutual *oplus_mutual_get_chip(void)
{
	return g_mutual_chip;
}

static void oplus_mutual_set_chip(struct oplus_chg_mutual *chip)
{
	g_mutual_chip = chip;
}

static struct oplus_chg_mutual *mutual_chip_find_by_cmd(
			enum oplus_chg_mutual_cmd_type cmd)
{
	struct oplus_chg_mutual_notifier *notifier;

	mutex_lock(&list_lock);
	list_for_each_entry(notifier, &mutual_list, list) {
		if (notifier->name == NULL)
			continue;
		if (notifier->cmd == cmd) {
			mutex_unlock(&list_lock);
			return (struct oplus_chg_mutual *)(notifier->priv_data);
		}
	}
	mutex_unlock(&list_lock);

	return NULL;
}

int oplus_chg_reg_mutual_notifier(struct oplus_chg_mutual_notifier *notifier)
{
	struct oplus_chg_mutual_notifier *notifier_temp;

	if (notifier == NULL || notifier->name == NULL)
		return -ENOENT;

	chg_info("owner:%s, cmd:%d\n", notifier->name, notifier->cmd);
	mutex_lock(&list_lock);
	list_for_each_entry(notifier_temp, &mutual_list, list) {
		if (notifier_temp->name == NULL)
			continue;
		if (notifier_temp->cmd == notifier->cmd) {
			chg_err("the same mutual notifier name already exists\n");
			mutex_unlock(&list_lock);
			return -EINVAL;
		}
	}
	list_add(&notifier->list, &mutual_list);
	notifier->priv_data = oplus_mutual_get_chip();
	mutex_unlock(&list_lock);

	return atomic_notifier_chain_register(&chg_mutual_notifier, &(notifier->nb));
}

int oplus_chg_unreg_mutual_notifier(struct oplus_chg_mutual_notifier *notifier)
{
	if (notifier == NULL)
		return -ENOENT;

	chg_info("owner:%s, cmd:%d\n", notifier->name, notifier->cmd);
	mutex_lock(&list_lock);
	notifier->priv_data = NULL;
	list_del(&notifier->list);
	mutex_unlock(&list_lock);

	return atomic_notifier_chain_unregister(&chg_mutual_notifier, &(notifier->nb));
}

static void oplus_chg_mutual_event(unsigned long param, char *buf)
{
	atomic_notifier_call_chain(&chg_mutual_notifier, param, buf);
}

static void oplus_chg_mutual_init(struct oplus_chg_mutual *chip)
{
	chip->mutual_cmd_data_ok = false;
	chip->mutual_hidl_handle_cmd_ready = false;
	mutex_init(&chip->mutual_read_lock);
	mutex_init(&chip->mutual_cmd_data_lock);
	mutex_init(&chip->mutual_cmd_ack_lock);
	init_waitqueue_head(&chip->mutual_read_wq);
	init_completion(&chip->mutual_cmd_ack);
}

int oplus_chg_set_mutual_cmd(
		enum oplus_chg_mutual_cmd_type cmd, u32 data_size, const void *data_buf)
{
	int rc;
	struct oplus_chg_mutual *chip;

	if ((data_size && !data_buf) || (!data_size && data_buf)) {
		chg_err("data_size or data_buf is NULL\n");
		return CMD_ERROR_DATA_NULL;
	}

	chip = mutual_chip_find_by_cmd(cmd);
	if (chip == NULL) {
		chg_err("chip is NULL\n");
		return CMD_ERROR_CHIP_NULL;
	}

	if (data_size > MUTUAL_CMD_DATA_LEN) {
		chg_err("cmd data size is invalid\n");
		return CMD_ERROR_DATA_INVALID;
	}

	if (!chip->mutual_hidl_handle_cmd_ready) {
		chg_err("hidl not read\n");
		return CMD_ERROR_HIDL_NOT_READY;
	}

	mutex_lock(&chip->mutual_cmd_ack_lock);
	mutex_lock(&chip->mutual_cmd_data_lock);
	memset(&chip->mutual_cmd, 0, sizeof(struct oplus_mutual_cmd));
	chip->mutual_cmd.cmd = cmd;
	chip->mutual_cmd.data_size = data_size;
	memcpy(chip->mutual_cmd.data_buf, data_buf, data_size);
	chip->mutual_cmd_data_ok = true;
	mutex_unlock(&chip->mutual_cmd_data_lock);
	wake_up(&chip->mutual_read_wq);

	reinit_completion(&chip->mutual_cmd_ack);
	rc = wait_for_completion_timeout(&chip->mutual_cmd_ack, msecs_to_jiffies(MUTUAL_CMD_TIME_MS));
	if (!rc) {
		chg_err("Error, timed out sending message\n");
		mutex_unlock(&chip->mutual_cmd_ack_lock);
		return CMD_ERROR_TIME_OUT;
	}
	rc = CMD_ACK_OK;
	chg_info("success\n");
	mutex_unlock(&chip->mutual_cmd_ack_lock);

	return rc;
}

static void oplus_chg_response_mutual_cmd(
			struct oplus_chg_mutual *chip, const char *buf, size_t count)
{
	unsigned long param;
	struct oplus_mutual_cmd *mutual_cmd;

	if (buf == NULL) {
		chg_err("buf is NULL\n");
		return;
	}

	if (chip == NULL) {
		chg_err("chip is NULL\n");
		return;
	}

	if (count != sizeof(struct oplus_mutual_cmd)) {
		chg_err("!!!size of buf is not matched\n");
		return;
	}

	mutual_cmd = (struct oplus_mutual_cmd *)buf;
	chg_info("!!!cmd[%d]\n", mutual_cmd->cmd);

	param = mutual_info_to_param(mutual_cmd->cmd, mutual_cmd->data_size);
	oplus_chg_mutual_event(param, mutual_cmd->data_buf);
	complete(&chip->mutual_cmd_ack);

	return;
}

static ssize_t oplus_chg_send_mutual_cmd(
		struct oplus_chg_mutual *chip, char *buf)
{
	int rc = 0;
	struct oplus_mutual_cmd cmd;

	if (buf == NULL) {
		chg_err("buf is NULL\n");
		return -ENODEV;
	}

	if (chip == NULL) {
		chg_err("chip is NULL\n");
		return -ENODEV;
	}

	chip->mutual_hidl_handle_cmd_ready = true;

	mutex_lock(&chip->mutual_read_lock);
	rc = wait_event_interruptible(chip->mutual_read_wq, chip->mutual_cmd_data_ok);
	mutex_unlock(&chip->mutual_read_lock);
	if (rc)
		return rc;
	if (!chip->mutual_cmd_data_ok)
		chg_err("oplus chg false wakeup, rc=%d\n", rc);
	mutex_lock(&chip->mutual_cmd_data_lock);
	chip->mutual_cmd_data_ok = false;
	memcpy(&cmd, &chip->mutual_cmd, sizeof(struct oplus_mutual_cmd));
	mutex_unlock(&chip->mutual_cmd_data_lock);
	memcpy(buf, &cmd, sizeof(struct oplus_mutual_cmd));
	chg_info("success to copy to user space cmd:%d, size:%d\n",
		((struct oplus_mutual_cmd *)buf)->cmd, ((struct oplus_mutual_cmd *)buf)->data_size);

	return sizeof(struct oplus_mutual_cmd);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0))
static ssize_t cmd_store(const struct class *c,
				const struct class_attribute *attr, const char *buf, size_t count)
#else
static ssize_t cmd_store(struct class *c, struct class_attribute *attr,
				const char *buf, size_t count)
#endif
{
	struct oplus_chg_mutual *chip = container_of(c, struct oplus_chg_mutual,
						mutual_class);

	oplus_chg_response_mutual_cmd(chip, buf, count);

	return count;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0))
static ssize_t cmd_show(const struct class *c,
				const struct class_attribute *attr, char *buf)
#else
static ssize_t cmd_show(struct class *c,
				struct class_attribute *attr, char *buf)
#endif
{
	ssize_t count;
	struct oplus_chg_mutual *chip = container_of(c, struct oplus_chg_mutual,
						mutual_class);

	count = oplus_chg_send_mutual_cmd(chip, buf);

	return count;
}
static CLASS_ATTR_RW(cmd);

static struct attribute *mutual_class_attrs[] = {
	&class_attr_cmd.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mutual_class);

static int oplus_mutual_class_init(struct oplus_chg_mutual *chip)
{
	int rc;

	chip->mutual_class.name = "oplus_mutual";
	chip->mutual_class.class_groups = mutual_class_groups;
	rc = class_register(&chip->mutual_class);
	if (rc < 0) {
		chg_err("failed to create oplus_mutual_class rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static __init int oplus_chg_mutual_module_init(void)
{
	int rc;
	struct oplus_chg_mutual *chip;

	chip = kzalloc(sizeof(struct oplus_chg_mutual), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	oplus_chg_mutual_init(chip);
	rc = oplus_mutual_class_init(chip);
	if (rc < 0)
		return rc;
	oplus_mutual_set_chip(chip);
	chg_info("success\n");

	return 0;
}

static __exit void oplus_chg_mutual_module_exit(void)
{
	struct oplus_chg_mutual *chip = oplus_mutual_get_chip();

	if (chip) {
		class_unregister(&chip->mutual_class);
		kfree(chip);
	}
	oplus_mutual_set_chip(NULL);
}

oplus_chg_module_core_register(oplus_chg_mutual_module);
