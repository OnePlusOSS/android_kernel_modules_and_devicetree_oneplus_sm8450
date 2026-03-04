// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2022 Oplus. All rights reserved.
 */


#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>

#include <trace/events/sched.h>
#include <linux/ktime.h>
#include <uapi/linux/sched/types.h>

#include "../../oplus_vooc.h"
#include "../../oplus_gauge.h"
#include "../../oplus_charger.h"
#include "../../oplus_ufcs.h"
#include "../../oplus_chg_module.h"
#include "../../voocphy/oplus_voocphy.h"
#include "../oplus_ufcs_protocol.h"
#include "oplus_sc6607.h"
#include "../../charger_ic/oplus_sc6607_reg.h"

static struct mutex i2c_rw_lock;
static struct oplus_sc6607 *g_sc6607 = NULL;

static int sc6607_read_byte(struct oplus_sc6607 *chip, u8 addr, u8 *data)
{
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_smbus_read_byte_data(chip->client, addr);
	if (rc < 0) {
		ufcs_err("read 0x%02x error, rc = %d \n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		oplus_ufcs_protocol_track_upload_i2c_err_info(rc, addr);
		goto error;
	}
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_clr();
	*data = rc;
	return 0;

error:
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_inc();
	return rc;
}

static int sc6607_read_data(struct oplus_sc6607 *chip, u8 addr, u8 *buf, int len)
{
	int rc = 0;

	mutex_lock(&i2c_rw_lock);
	rc = i2c_smbus_read_i2c_block_data(chip->client, addr, len, buf);
	if (rc < 0) {
		ufcs_err("read 0x%02x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		oplus_ufcs_protocol_track_upload_i2c_err_info(rc, addr);
		goto error;
	}
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_inc();
	return rc;
}

static int sc6607_write_byte(struct oplus_sc6607 *chip, u8 addr, u8 data)
{
	int rc = 0;
	u8 buf[2] = {addr & 0xff, data};

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, buf, 2);
	if (rc < 2) {
		ufcs_err("write 0x%02x error, rc = %d \n", addr, rc);
		oplus_ufcs_protocol_track_upload_i2c_err_info(rc, addr);
		mutex_unlock(&i2c_rw_lock);
		oplus_ufcs_protocol_i2c_err_inc();
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&i2c_rw_lock);
	oplus_ufcs_protocol_i2c_err_clr();
	return 0;
}

static int sc6607_write_data(struct oplus_sc6607 *chip, u8 addr, u16 length, u8 *data)
{
	u8 *buf;
	int rc = 0;

	buf = kzalloc(length + 1, GFP_KERNEL);
	if (!buf) {
		ufcs_err("alloc memorry for i2c buffer error\n");
		return -ENOMEM;
	}

	buf[0] = addr & 0xff;
	memcpy(&buf[1], data, length);

	mutex_lock(&i2c_rw_lock);
	rc = i2c_master_send(chip->client, buf, length + 1);
	if (rc < length + 1) {
		ufcs_err("write 0x%02x error, ret = %d \n", addr, rc);
		oplus_ufcs_protocol_track_upload_i2c_err_info(rc, addr);
		mutex_unlock(&i2c_rw_lock);
		kfree(buf);
		oplus_ufcs_protocol_i2c_err_inc();
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&i2c_rw_lock);
	kfree(buf);
	oplus_ufcs_protocol_i2c_err_clr();
	return 0;
}

static int sc6607_write_bit_mask(struct oplus_sc6607 *chip, u8 addr, u8 mask, u8 data)
{
	u8 temp = 0;
	int rc = 0;

	rc = sc6607_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;

	temp = (data & mask) | (temp & (~mask));

	rc = sc6607_write_byte(chip, addr, temp);
	if (rc < 0)
		return rc;

	return 0;
}

static int sc6607_write_tx_buffer(u8 *buf, u16 len)
{
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	sc6607_write_byte(chip, SC6607_ADDR_TX_LENGTH, len);
	sc6607_write_data(chip, SC6607_ADDR_TX_BUFFER0, len, buf);
	sc6607_write_bit_mask(chip, SC6607_ADDR_UFCS_CTRL0, SC6607_MASK_SND_CMP, SC6607_CMD_SND_CMP);
	return 0;
}

static int sc6607_rcv_msg(struct oplus_ufcs_protocol *protocol_chip)
{
	u8 rx_length = 0;
	int rc = 0;
	struct comm_msg *rcv;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip || !protocol_chip)
		return -ENODEV;

	rcv = &protocol_chip->rcv_msg;

	sc6607_read_byte(chip, SC6607_ADDR_RX_LENGTH, &rx_length);
	rcv->len = rx_length;
	rc = sc6607_read_data(chip, SC6607_ADDR_RX_BUFFER0, protocol_chip->rcv_buffer, rcv->len);
	sc6607_write_byte(chip, SC6607_ADDR_UFCS_CTRL1, 0x10);

	return rc;
}

static int sc6607_retrieve_flags(struct oplus_ufcs_protocol *protocol_chip)
{
	int rc = 0;
	int err_type = TRACK_UFCS_ERR_DEFAULT;

	struct ufcs_error_flag *reg;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip || !protocol_chip)
		return -ENODEV;

	reg = &protocol_chip->flag;
	if (reg->hd_error.dp_ovp)
		err_type = TRACK_UFCS_ERR_DP_OVP;
	else if (reg->hd_error.dm_ovp)
		err_type = TRACK_UFCS_ERR_DM_OVP;
	else if (reg->hd_error.temp_shutdown)
		err_type = TRACK_UFCS_ERR_TEMP_SHUTDOWN;
	else if (reg->hd_error.wtd_timeout)
		err_type = TRACK_UFCS_ERR_WDT_TIMEOUT;
	else if (reg->rcv_error.msg_trans_fail)
		err_type = TRACK_UFCS_ERR_MSG_TRANS_FAIL;
	else if (reg->rcv_error.ack_rcv_timeout)
		err_type = TRACK_UFCS_ERR_ACK_RCV_TIMEOUT;
	else if (reg->commu_error.baud_error)
		err_type = TRACK_UFCS_ERR_BAUD_RARE_ERROR;
	else if (reg->commu_error.training_error)
		err_type = TRACK_UFCS_ERR_TRAINNING_BYTE_ERROR;
	else if (reg->commu_error.byte_timeout)
		err_type = TRACK_UFCS_ERR_DATA_BYTE_TIMEOUT;
	else if (reg->commu_error.rx_len_error)
		err_type = TRACK_UFCS_ERR_LEN_ERROR;
	else if (reg->commu_error.rx_overflow)
		err_type = TRACK_UFCS_ERR_RX_OVERFLOW;
	else if (reg->commu_error.crc_error)
		err_type = TRACK_UFCS_ERR_CRC_ERROR;
	else if (reg->commu_error.baud_change)
		err_type = TRACK_UFCS_ERR_BAUD_RATE_CHANGE;
	else if (reg->commu_error.bus_conflict)
		err_type = TRACK_UFCS_ERR_BUS_CONFLICT;

	if (protocol_chip->debug_force_cp_err)
		err_type = protocol_chip->debug_force_cp_err;
	if (err_type != TRACK_UFCS_ERR_DEFAULT)
		oplus_ufcs_protoco_track_upload_cp_err_info(err_type);
	return rc;
}

static int sc6607_read_flags(struct oplus_ufcs_protocol *protocol_chip)
{
	struct ufcs_error_flag *reg;
	int rc = 0;
	u8 flag_buf[SC6607_FLAG_NUM] = { 0 };
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip || !protocol_chip)
		return -ENODEV;

	reg = &protocol_chip->flag;

	rc = sc6607_read_data(chip, SC6607_ADDR_GENERAL_INT_FLAG1, flag_buf, SC6607_FLAG_NUM);
	if (rc < 0) {
		ufcs_err("failed to read flag register\n");
		protocol_chip->get_flag_failed = 1;
		return -EBUSY;
	}
	memcpy(protocol_chip->reg_dump, flag_buf, sizeof(flag_buf));

	reg->hd_error.dp_ovp = 0;
	reg->hd_error.dm_ovp = 0;
	reg->hd_error.temp_shutdown = 0;
	reg->hd_error.wtd_timeout = 0;

	reg->rcv_error.sent_cmp = flag_buf[0] & SC6607_FLAG_SENT_PACKET_COMPLETE;
	reg->rcv_error.msg_trans_fail = flag_buf[0] & SC6607_FLAG_MSG_TRANS_FAIL;
	reg->rcv_error.ack_rcv_timeout = flag_buf[0] & SC6607_FLAG_ACK_RECEIVE_TIMEOUT;
	reg->rcv_error.data_rdy = flag_buf[0] & SC6607_FLAG_DATA_READY;
	reg->commu_error.rx_overflow = flag_buf[0] & SC6607_FLAG_RX_OVERFLOW;

	reg->commu_error.baud_error = flag_buf[1] & SC6607_FLAG_BAUD_RATE_ERROR;
	reg->commu_error.training_error = flag_buf[1] & SC6607_FLAG_TRAINING_BYTE_ERROR;
	reg->commu_error.start_fail = flag_buf[1] & SC6607_FLAG_START_FAIL;
	reg->commu_error.byte_timeout = flag_buf[1] & SC6607_FLAG_DATA_BYTE_TIMEOUT;
	reg->commu_error.rx_len_error = flag_buf[1] & SC6607_FLAG_LENGTH_ERROR;
	reg->hd_error.hardreset = flag_buf[1] & SC6607_FLAG_HARD_RESET;
	reg->commu_error.crc_error = flag_buf[1] & SC6607_FLAG_CRC_ERROR;
	reg->commu_error.stop_error = flag_buf[1] & SC6607_FLAG_STOP_ERROR;

	reg->commu_error.baud_change = flag_buf[2] & SC6607_FLAG_BAUD_RATE_CHANGE;
	reg->commu_error.bus_conflict = flag_buf[2] & SC6607_FLAG_BUS_CONFLICT;

	if (protocol_chip->state == STATE_HANDSHAKE) {
		protocol_chip->handshake_success = (flag_buf[0] & SC6607_FLAG_HANDSHAKE_SUCCESS) &&
			!(flag_buf[0] & SC6607_FLAG_HANDSHAKE_FAIL);
	}
	ufcs_debug("[0x%x, 0x%x, 0x%x]\n", flag_buf[0], flag_buf[1], flag_buf[2]);

	protocol_chip->get_flag_failed = 0;
	return 0;
}

static int sc6607_dump_registers(void)
{
	int rc = 0;
	u16 addr = 0x0;
	u8 val_buf[6] = { 0x0 };
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	if (atomic_read(&chip->suspended) == 1) {
		ufcs_err("sc6607 is suspend!\n");
		return -ENODEV;
	}

	for (addr = 0x0; addr <= 0x05; addr++) {
		rc = sc6607_read_byte(chip, addr, &val_buf[addr]);
		if (rc < 0) {
			ufcs_err("sc6607_dump_registers Couldn't read 0x%02x rc = %d\n", addr, rc);
			break;
		}
	}

	ufcs_debug(":[0~5][0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x]\n", val_buf[0],
		 val_buf[1], val_buf[2], val_buf[3], val_buf[4], val_buf[5]);

	return 0;
}

static int sc6607_chip_enable(void)
{
	u8 addr_buf[SC6607_ENABLE_REG_NUM] = { SC6607_ADDR_UFCS_CTRL0,
						SC6607_ADDR_UFCS_CTRL1,
						SC6607_ADDR_UFCS_INT_MASK1,
						SC6607_ADDR_UFCS_INT_MASK2 };
	u8 cmd_buf[SC6607_ENABLE_REG_NUM] = {
		SC6607_CMD_EN_CHIP, SC6607_CMD_CLR_TX_RX,
		SC6607_CMD_MASK_ACK_TIMEOUT, SC6607_MASK_TRANING_BYTE_ERROR
	};
	int i = 0, rc = 0;
	struct oplus_sc6607 *chip = g_sc6607;

	if (IS_ERR_OR_NULL(chip)) {
		ufcs_err("global pointer sc6607_ufcs error\n");
		return -ENODEV;
	}

	if (atomic_read(&chip->suspended) == 1) {
		ufcs_err("sc6607 is suspend!\n");
		return -ENODEV;
	}

	for (i = 0; i < SC6607_ENABLE_REG_NUM; i++) {
		rc = sc6607_write_byte(chip, addr_buf[i], cmd_buf[i]);
		if (rc < 0) {
			ufcs_err("write i2c failed!\n");
			return rc;
		}
	}
	chip->ufcs_enable = true;

	return 0;
}

static int sc6607_chip_disable(void)
{
	int rc = 0;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	if (IS_ERR_OR_NULL(chip)) {
		ufcs_err("sleep pinctrl error \n");
		return -ENODEV;
	}

	rc = sc6607_write_byte(chip, SC6607_ADDR_UFCS_CTRL0,
		SC6607_CMD_DIS_CHIP);
	if (rc < 0) {
		ufcs_err("write i2c failed\n");
		return rc;
	}
	chip->ufcs_enable = false;

	return 0;
}

static int sc6607_wdt_enable(bool en)
{
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	return 0;
}

static int sc6607_handshake(void)
{
	int rc = 0;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	rc = sc6607_write_bit_mask(chip, SC6607_ADDR_UFCS_CTRL0, SC6607_MASK_EN_HANDSHAKE, SC6607_CMD_EN_HANDSHAKE);
	return rc;
}

static int sc6607_ping(int baud)
{
	int rc = 0;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	rc = sc6607_write_bit_mask(chip, SC6607_ADDR_UFCS_CTRL0, SC6607_FLAG_BAUD_RATE_VALUE,
				   (baud << SC6607_FLAG_BAUD_NUM_SHIFT));

	return rc;
}

static int sc6607_source_hard_reset(void)
{
	int rc = 0;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	rc = sc6607_write_bit_mask(chip, SC6607_ADDR_UFCS_CTRL0, SC6607_SEND_SOURCE_HARDRESET,
				   SC6607_SEND_SOURCE_HARDRESET);

	return rc;
}

static int sc6607_cable_hard_reset(void)
{
	int rc = 0;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return -ENODEV;

	rc = sc6607_write_bit_mask(chip, SC6607_ADDR_UFCS_CTRL0, SC6607_SEND_CABLE_HARDRESET,
				   SC6607_SEND_CABLE_HARDRESET);

	return rc;
}

static void sc6607_notify_baudrate_change(void)
{
	u8 baud_rate = 0;
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip)
		return;

	sc6607_read_byte(chip, SC6607_ADDR_UFCS_CTRL0, &baud_rate);
	baud_rate = baud_rate & SC6607_FLAG_BAUD_RATE_VALUE;
	ufcs_debug("baud_rate change! new value = %d\n", baud_rate);
}

int sc6607_ufcs_event_handler(void)
{
	struct oplus_ufcs_protocol *chip_protocol = oplus_ufcs_get_protocol_struct();
	struct oplus_sc6607 *chip = g_sc6607;

	if (!chip_protocol || !chip)
		return -1;

	ufcs_debug("--------------ufcs_enable = %d\n", chip->ufcs_enable);
	if (chip->ufcs_enable)
		kthread_queue_work(chip_protocol->wq, &chip_protocol->rcv_work);

	return 0;
}

static int sc6607_charger_choose(struct oplus_sc6607 *chip)
{
	int rc = 0;
	u16 addr = 0x0;
	u8 val_buf = 0x0;

	rc = sc6607_read_byte(chip, addr, &val_buf);
	if (rc < 0) {
		ufcs_err("sc6607_dump_registers Couldn't read 0x%02x rc = %d\n", addr, rc);
		return -EPROBE_DEFER;
	} else
		return 1;
}

static int sc6607_hardware_init(struct oplus_sc6607 *chip)
{
	int rc = 0;

	rc = sc6607_charger_choose(chip);
	if (rc <= 0)
		return rc;

	sc6607_dump_registers();

	return rc;
}

static int sc6607_parse_dt(struct oplus_sc6607 *chip)
{
	struct device_node *node = NULL;

	if (!chip) {
		return -1;
	}
	node = chip->dev->of_node;
	return 0;
}

static void register_ufcs_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int rc = 0;
	char *version;
	char *manufacture;

	version = "sc6607";
	manufacture = "SouthChip";

	rc = register_device_proc("sc6607", version, manufacture);
	if (rc)
		ufcs_err("register_ufcs_devinfo fail\n");
#endif
}

static bool sc6607_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static struct regmap_config sc6607_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = SC6607_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = sc6607_is_volatile_reg,
};

struct oplus_ufcs_protocol_operations oplus_ufcs_sc6607_ops = {
	.ufcs_ic_enable = sc6607_chip_enable,
	.ufcs_ic_disable = sc6607_chip_disable,
	.ufcs_ic_wdt_enable = sc6607_wdt_enable,
	.ufcs_ic_handshake = sc6607_handshake,
	.ufcs_ic_ping = sc6607_ping,
	.ufcs_ic_source_hard_reset = sc6607_source_hard_reset,
	.ufcs_ic_cable_hard_reset = sc6607_cable_hard_reset,
	.ufcs_ic_baudrate_change = sc6607_notify_baudrate_change,
	.ufcs_ic_write_msg = sc6607_write_tx_buffer,
	.ufcs_ic_rcv_msg = sc6607_rcv_msg,
	.ufcs_ic_read_flags = sc6607_read_flags,
	.ufcs_ic_retrieve_flags = sc6607_retrieve_flags,
	.ufcs_ic_dump_registers = sc6607_dump_registers,
	.ufcs_ic_get_master_vbus = oplus_sc6607_read_vbus,
	.ufcs_ic_get_master_ibus = oplus_sc6607_read_ibus,
	.ufcs_ic_get_master_vac = oplus_sc6607_read_vac,
	.ufcs_ic_get_master_vout = oplus_sc6607_read_vsys,
	.ufcs_ic_get_master_vbat = oplus_sc6607_read_vbat,
	.ufcs_ic_event_handle = sc6607_ufcs_event_handler,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
static int sc6607_driver_probe(struct i2c_client *client)
#else
static int sc6607_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct oplus_sc6607 *chip_ic;

	int rc;

	chip_ic = devm_kzalloc(&client->dev, sizeof(struct oplus_sc6607), GFP_KERNEL);
	if (!chip_ic) {
		ufcs_err("failed to allocate oplus_sc6607\n");
		return -ENOMEM;
	}

	chip_ic->regmap = devm_regmap_init_i2c(client, &sc6607_regmap_config);
	if (!chip_ic->regmap) {
		rc = -ENODEV;
		goto regmap_init_err;
	}

	chip_ic->dev = &client->dev;
	chip_ic->client = client;
	i2c_set_clientdata(client, chip_ic);
	g_sc6607 = chip_ic;
	mutex_init(&i2c_rw_lock);

	rc = sc6607_hardware_init(chip_ic);
	if (rc < 0) {
		ufcs_err("sc6607 ic init failed, rc = %d!\n", rc);
		goto regmap_init_err;
	}
	sc6607_parse_dt(chip_ic);
	oplus_ufcs_ops_register(&oplus_ufcs_sc6607_ops, UFCS_IC_NAME);

	register_ufcs_devinfo();

	ufcs_debug("call end!\n");

	return 0;

regmap_init_err:
	devm_kfree(&client->dev, chip_ic);
	return rc;
}

static int sc6607_pm_resume(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sc6607 *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 0);

	return 0;
}

static int sc6607_pm_suspend(struct device *dev_chip)
{
	struct i2c_client *client = container_of(dev_chip, struct i2c_client, dev);
	struct oplus_sc6607 *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 1);

	return 0;
}

static const struct dev_pm_ops sc6607_pm_ops = {
	.resume = sc6607_pm_resume,
	.suspend = sc6607_pm_suspend,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
static void sc6607_driver_remove(struct i2c_client *client)
#else
static int sc6607_driver_remove(struct i2c_client *client)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
	return;
#else
	return 0;
#endif
}

static void sc6607_shutdown(struct i2c_client *chip_client)
{
	oplus_chg_set_chargerid_switch_val(0);
}

static const struct of_device_id sc6607_match[] = {
	{.compatible = "oplus,sc6607-ufcs" },
	{},
};

static const struct i2c_device_id sc6607_id[] = {
	{ "oplus,sc6607-ufcs", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sc6607_id);

static struct i2c_driver sc6607_i2c_driver = {
	.driver =
		{
			.name = "sc6607-ufcs",
			.owner = THIS_MODULE,
			.of_match_table = sc6607_match,
			.pm = &sc6607_pm_ops,
		},
	.probe = sc6607_driver_probe,
	.remove = sc6607_driver_remove,
	.id_table = sc6607_id,
	.shutdown = sc6607_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(sc6607_i2c_driver);
#else
static __init int sc6607_i2c_driver_init(void)
{
	return i2c_add_driver(&sc6607_i2c_driver);
}

static __exit void sc6607_i2c_driver_exit(void)
{
	i2c_del_driver(&sc6607_i2c_driver);
}

oplus_chg_module_register(sc6607_i2c_driver);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("SC UFCS Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");
