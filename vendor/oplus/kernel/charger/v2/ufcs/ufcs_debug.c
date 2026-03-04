// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[DEBUG]([%s][%d]): " fmt, __func__, __LINE__

#include "ufcs_debug.h"

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>

#include "ufcs_core.h"
#include "ufcs_msg.h"
#include "ufcs_event.h"

#define TEST_REQ_DATA_NUM	5
#define REFUSE_DATA_NUM		4
#define NCK_DATA_NUM		3

static bool split_string(char *str, char *split_parts[], unsigned num) {
	int i = 0;
	char *part, *tmp;

	tmp = str;
	part = strsep(&tmp, ":");
	while (part != NULL && i < num) {
		split_parts[i] = part;
		i++;
		part = strsep(&tmp, ":");
	}

	if (i < num)
		return false;
	return true;
}

static int parse_test_request_data(char *buf, bool *test, bool *vol_acc,
	u8 *addr, u8 *msg_type, u8 *cmd_type)
{
	char *info[TEST_REQ_DATA_NUM] = { 0 };
	long int num;
	int rc;

	if (!split_string(buf, info, TEST_REQ_DATA_NUM)) {
		ufcs_err("data format error\n");
		return -EINVAL;
	}
	ufcs_info("test_enable[%s]:vol_acc[%s]:addr[%s]:msg_type[%s]:cmd_type[%s]\n",
		  info[0], info[1], info[2], info[3], info[4]);

	/* parse test_enable */
	if (strcmp(info[0], "0") == 0) {
		*test = false;
	} else if (strcmp(info[0], "1") == 0) {
		*test = true;
	} else {
		ufcs_err("\"test_enable\" field format error");
		return -EINVAL;
	}

	/* parse vol_acc */
	if (strcmp(info[1], "0") == 0) {
		*vol_acc = false;
	} else if (strcmp(info[1], "1") == 0) {
		*vol_acc = true;
	} else {
		ufcs_err("\"vol_acc\" field format error");
		return -EINVAL;
	}

	/* parse addr */
	rc = kstrtol(info[2], 0, &num);
	if (rc != 0) {
		ufcs_err("\"addr\" field format error");
		return -EINVAL;
	}
	*addr = (u8)num;

	/* parse msg_type */
	rc = kstrtol(info[3], 0, &num);
	if (rc != 0) {
		ufcs_err("\"msg_type\" field format error");
		return -EINVAL;
	}
	*msg_type = (u8)num;

	/* parse cmd_type */
	rc = kstrtol(info[4], 0, &num);
	if (rc != 0) {
		ufcs_err("\"cmd_type\" field format error");
		return -EINVAL;
	}
	*cmd_type = (u8)num;

	return 0;
}

static int parse_refuse_data(char *buf, u8 *msg_type, u8 *cmd_type,
	u8 *reason, int *count)
{
	char *info[REFUSE_DATA_NUM] = { 0 };
	long int num;
	int rc;

	if (!split_string(buf, info, REFUSE_DATA_NUM)) {
		ufcs_err("data format error\n");
		return -EINVAL;
	}
	ufcs_info("msg_type[%s]:cmd_type[%s]:reason[%s]:count[%s]\n",
		  info[0], info[1], info[2], info[3]);

	/* parse msg_type */
	rc = kstrtol(info[0], 0, &num);
	if (rc != 0) {
		ufcs_err("\"msg_type\" field format error");
		return -EINVAL;
	}
	*msg_type = (u8)num;

	/* parse cmd_type */
	rc = kstrtol(info[1], 0, &num);
	if (rc != 0) {
		ufcs_err("\"cmd_type\" field format error");
		return -EINVAL;
	}
	*cmd_type = (u8)num;

	/* parse reason */
	rc = kstrtol(info[2], 0, &num);
	if (rc != 0) {
		ufcs_err("\"reason\" field format error");
		return -EINVAL;
	}
	*reason = (u8)num;

	/* parse count */
	rc = kstrtol(info[3], 0, &num);
	if (rc != 0) {
		ufcs_err("\"count\" field format error");
		return -EINVAL;
	}
	*count = (int)num;

	return 0;
}

static int parse_nck_data(char *buf, u8 *msg_type, u8 *cmd_type, int *count)
{
	char *info[NCK_DATA_NUM] = { 0 };
	long int num;
	int rc;

	if (!split_string(buf, info, NCK_DATA_NUM)) {
		ufcs_err("data format error\n");
		return -EINVAL;
	}
	ufcs_info("msg_type[%s]:cmd_type[%s]:count[%s]\n",
		  info[0], info[1], info[2]);

	/* parse msg_type */
	rc = kstrtol(info[0], 0, &num);
	if (rc != 0) {
		ufcs_err("\"msg_type\" field format error");
		return -EINVAL;
	}
	*msg_type = (u8)num;

	/* parse cmd_type */
	rc = kstrtol(info[1], 0, &num);
	if (rc != 0) {
		ufcs_err("\"cmd_type\" field format error");
		return -EINVAL;
	}
	*cmd_type = (u8)num;

	/* parse count */
	rc = kstrtol(info[2], 0, &num);
	if (rc != 0) {
		ufcs_err("\"count\" field format error");
		return -EINVAL;
	}
	*count = (int)num;

	return 0;
}

int ufcs_debug_set_test_request(struct ufcs_class *class, char *buf, size_t count)
{
	bool test;
	bool vol_acc;
	u8 addr;
	u8 msg_type;
	u8 cmd_type;
	struct ufcs_msg *msg;
	struct ufcs_event *event;
	int rc;

	if (class == NULL)
		return -EINVAL;
	if (buf == NULL)
		return -EINVAL;
	if (!class->handshake_success) {
		ufcs_err("ufcs no handshake\n");
		return -EINVAL;
	}

	rc = parse_test_request_data(buf, &test, &vol_acc, &addr, &msg_type, &cmd_type);
	if (rc < 0)
		return rc;

	msg = devm_kzalloc(&class->ufcs->dev, sizeof(struct ufcs_msg), GFP_KERNEL);
	if (msg == NULL) {
		ufcs_err("alloc ufcs msg buf error\n");
		return -ENOMEM;
	}

	msg->magic = UFCS_MSG_MAGIC;
	msg->head.type = UFCS_DATA_MSG;
	msg->head.version = UFCS_VER_CURR;
	msg->head.index = 0;
	msg->head.addr = UFCS_DEVICE_SINK;
	msg->crc_good = 1;
	msg->data_msg.command = DATA_MSG_TEST_REQUEST;
	msg->data_msg.length = sizeof(struct ufcs_data_msg_test_request);
	msg->data_msg.test_request.data =
		UFCS_TEST_REQUEST_DATA(test, vol_acc, addr, msg_type, cmd_type);

	event = devm_kzalloc(&class->ufcs->dev, sizeof(struct ufcs_event), GFP_KERNEL);
	if (event == NULL) {
		ufcs_err("alloc event buf error\n");
		devm_kfree(&class->ufcs->dev, msg);
		return -ENOMEM;
	}
	event->msg = msg;
	event->data = NULL;
	INIT_LIST_HEAD(&event->list);
	event->type = UFCS_EVENT_RECV_TEST_REQUEST;
	rc = ufcs_push_event(class, event);
	if (rc < 0) {
		ufcs_err("push test request event error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int ufcs_debug_set_recv_msg(struct ufcs_class *class, const char *buf, size_t count)
{
	struct ufcs_msg *msg;
	struct ufcs_event *event;
	int rc;

	if (class == NULL)
		return -EINVAL;
	if (buf == NULL)
		return -EINVAL;
	if (!class->handshake_success) {
		ufcs_err("ufcs no handshake\n");
		return -EINVAL;
	}

	msg = ufcs_unpack_msg(class, buf, count);
	if (msg == NULL) {
		ufcs_err("ufcs_unpack_msg error\n");
		return -EINVAL;
	}
	ufcs_dump_msg_info(msg, "debug recv");

	if (msg->head.type == UFCS_CTRL_MSG && ufcs_is_ack_nck_msg(&msg->ctrl_msg)) {
		/*
		 * ACK messages of non-message reply types need to be handed
		 * over to the state machine for processing, such as ACK
		 * messages when UFCS recognition is successful.
		 */
		if (ufcs_recv_ack_nck_msg(class, &msg->ctrl_msg)) {
			devm_kfree(&class->ufcs->dev, msg);
			return 0;
		}
	}

	event = devm_kzalloc(&class->ufcs->dev, sizeof(struct ufcs_event), GFP_KERNEL);
	if (event == NULL) {
		ufcs_err("alloc event buf error\n");
		devm_kfree(&class->ufcs->dev, msg);
		return -EINVAL;
	}
	event->msg = msg;
	event->data = NULL;
	INIT_LIST_HEAD(&event->list);
	rc = ufcs_process_event(class, event);
	if ((rc < 0) && (rc != -ENOTSUPP)) {
		devm_kfree(&class->ufcs->dev, msg);
		devm_kfree(&class->ufcs->dev, event);
		ufcs_err("ufcs event processing failed, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

int ufcs_debug_set_refuse_info(struct ufcs_class *class, char *buf, size_t count)
{
	struct ufcs_debug_refuse_info info;
	int rc;

	if (class == NULL)
		return -EINVAL;
	if (buf == NULL)
		return -EINVAL;

	rc = parse_refuse_data(buf, &info.msg_type, &info.cmd_type, &info.reason, &info.count);
	if (rc < 0)
		return rc;

	if (info.msg_type != UFCS_CTRL_MSG && info.msg_type != UFCS_DATA_MSG) {
		ufcs_err("msg_type error, just support ctrl msg and data msg\n");
		return -ENOTSUPP;
	}
	class->debug.refuse_info = info;

	return 0;
}

int ufcs_debug_set_nck_info(struct ufcs_class *class, char *buf, size_t count)
{
	struct ufcs_debug_nck_info info;
	int rc;

	if (class == NULL)
		return -EINVAL;
	if (buf == NULL)
		return -EINVAL;

	rc = parse_nck_data(buf, &info.msg_type, &info.cmd_type, &info.count);
	if (rc < 0)
		return rc;

	if (info.msg_type != UFCS_CTRL_MSG && info.msg_type != UFCS_DATA_MSG) {
		ufcs_err("msg_type error, just support ctrl msg and data msg\n");
		return -ENOTSUPP;
	}
	class->debug.nck_info = info;

	return 0;
}

int ufcs_debug_check_refuse_info(struct ufcs_class *class, struct ufcs_msg *msg)
{
	int rc;

	if (class == NULL)
		return -EINVAL;
	if (msg == NULL)
		return -EINVAL;

	if (class->debug.refuse_info.count == 0)
		return 0;
	if (class->debug.refuse_info.msg_type != msg->head.type)
		return 0;

	switch (msg->head.type) {
	case UFCS_CTRL_MSG:
		if (class->debug.refuse_info.cmd_type != msg->ctrl_msg.command)
			return 0;
		break;
	case UFCS_DATA_MSG:
		if (class->debug.refuse_info.cmd_type != msg->data_msg.command)
			return 0;
		break;
	default:
		return 0;
	}

	ufcs_dump_msg_info(msg, "debug refuse");
	rc = ufcs_send_data_msg_refuse(class, msg, class->debug.refuse_info.reason);
	if (rc < 0) {
		ufcs_err("send refuse msg error, rc=%d\n", rc);
		return rc;
	}

	if (class->debug.refuse_info.count > 0)
		class->debug.refuse_info.count--;

	/* Positive value indicates successful refuse */
	return 1;
}

int ufcs_debug_check_nck_info(struct ufcs_class *class, struct ufcs_msg *msg)
{
	if (class == NULL)
		return -EINVAL;
	if (msg == NULL)
		return -EINVAL;

	if (class->debug.nck_info.count == 0)
		return 0;
	if (class->debug.nck_info.msg_type != msg->head.type)
		return 0;

	switch (msg->head.type) {
	case UFCS_CTRL_MSG:
		if (class->debug.nck_info.cmd_type != msg->ctrl_msg.command)
			return 0;
		break;
	case UFCS_DATA_MSG:
		if (class->debug.nck_info.cmd_type != msg->data_msg.command)
			return 0;
		break;
	default:
		return 0;
	}

	ufcs_dump_msg_info(msg, "debug nck");

	if (class->debug.nck_info.count > 0)
		class->debug.nck_info.count--;

	/* Positive value indicates successful */
	return 1;
}

int ufcs_debug_set_recv_invalid_msg(struct ufcs_class *class, u8 msg_type)
{
	struct ufcs_msg *msg;
	struct ufcs_event *event;
	int rc;

	if (class == NULL)
		return -EINVAL;
	if (!class->handshake_success) {
		ufcs_err("ufcs no handshake\n");
		return -EINVAL;
	}

	msg = devm_kzalloc(&class->ufcs->dev, sizeof(struct ufcs_msg), GFP_KERNEL);
	if (msg == NULL) {
		ufcs_err("alloc ufcs msg buf error\n");
		return -ENOMEM;
	}

	msg->magic = UFCS_MSG_MAGIC;
	msg->head.type = msg_type;
	msg->head.version = UFCS_VER_CURR;
	msg->head.index = 0;
	msg->head.addr = UFCS_DEVICE_SINK;
	msg->crc_good = 1;
	if (msg_type == UFCS_CTRL_MSG) {
		msg->ctrl_msg.command = 0xff;
	} else if (msg_type == UFCS_DATA_MSG) {
		msg->data_msg.command = 0xfe;
		msg->data_msg.length = 0;
	} else {
		devm_kfree(&class->ufcs->dev, msg);
		return -EINVAL;
	}

	event = devm_kzalloc(&class->ufcs->dev, sizeof(struct ufcs_event), GFP_KERNEL);
	if (event == NULL) {
		ufcs_err("alloc event buf error\n");
		devm_kfree(&class->ufcs->dev, msg);
		return -ENOMEM;
	}
	event->msg = msg;
	event->data = NULL;
	INIT_LIST_HEAD(&event->list);
	event->type = UFCS_EVENT_RECV_TEST_REQUEST;
	rc = ufcs_process_event(class, event);
	if ((rc < 0) && (rc != -ENOTSUPP)) {
		devm_kfree(&class->ufcs->dev, event);
		ufcs_err("ufcs event processing failed, rc=%d\n", rc);
		devm_kfree(&class->ufcs->dev, msg);
		devm_kfree(&class->ufcs->dev, event);
		return rc;
	}

	return 0;
}
