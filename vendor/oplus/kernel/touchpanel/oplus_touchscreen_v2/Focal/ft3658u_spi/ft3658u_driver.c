// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/string.h>

#include "ft3658u_core.h"

struct chip_data_ft3658u *g_fts_data = NULL;

/*******Part0:LOG TAG Declear********************/

#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "focaltech,ft3658u_spi"
#else
#define TPD_DEVICE "focaltech,ft3658u_spi"
#endif

#define FTS_REG_UPGRADE                             0xFC
#define FTS_UPGRADE_AA                              0xAA
#define FTS_UPGRADE_55                              0x55
#define FTS_DELAY_UPGRADE_AA                        10
#define FTS_DELAY_UPGRADE_RESET                     80
#define FTS_UPGRADE_LOOP                            10

#define FTS_ROMBOOT_CMD_SET_PRAM_ADDR               0xAD
#define FTS_ROMBOOT_CMD_SET_PRAM_ADDR_LEN           4
#define FTS_ROMBOOT_CMD_WRITE                       0xAE
#define FTS_ROMBOOT_CMD_START_APP                   0x08
#define FTS_DELAY_PRAMBOOT_START                    100
#define FTS_ROMBOOT_CMD_ECC                         0xCC
#define FTS_ROMBOOT_CMD_ECC_NEW_LEN                 7
#define FTS_ECC_FINISH_TIMEOUT                      100
#define FTS_ROMBOOT_CMD_ECC_FINISH                  0xCE
#define FTS_ROMBOOT_CMD_ECC_READ                    0xCD
#define FTS_PRAM_SADDR                              0x000000
#define FTS_DRAM_SADDR                              0xD00000
#define FTS_DELAY_READ_ID                           20

#define FTS_CMD_RESET                               0x07
#define FTS_CMD_START                               0x55
#define FTS_CMD_START_DELAY                         12
#define FTS_CMD_READ_ID                             0x90
#define FTS_CMD_DATA_LEN                            0x7A
#define FTS_CMD_ERASE_APP                           0x61
#define FTS_RETRIES_REASE                           50
#define FTS_RETRIES_DELAY_REASE                     400
#define FTS_REASE_APP_DELAY                         1350
#define FTS_CMD_ECC_INIT                            0x64
#define FTS_CMD_ECC_CAL                             0x65
#define FTS_RETRIES_ECC_CAL                         10
#define FTS_RETRIES_DELAY_ECC_CAL                   50
#define FTS_CMD_ECC_READ                            0x66
#define FTS_CMD_FLASH_STATUS                        0x6A
#define FTS_CMD_WRITE                               0xBF
#define FTS_CMD_SET_WFLASH_ADDR                     0xAB
#define FTS_CMD_SET_RFLASH_ADDR                     0xAC
#define FTS_RETRIES_WRITE                           100
#define FTS_RETRIES_DELAY_WRITE                     1

#define FTS_CMD_FLASH_STATUS_NOP                    0x0000
#define FTS_CMD_FLASH_STATUS_ECC_OK                 0xF055
#define FTS_CMD_FLASH_STATUS_ERASE_OK               0xF0AA
#define FTS_CMD_FLASH_STATUS_WRITE_OK               0x1000

#define POINT_REPORT_CHECK_WAIT_TIME                200    /* unit:ms */
#define PRC_INTR_INTERVALS                          100    /* unit:ms */


#define AL2_FCS_COEF                ((1 << 15) + (1 << 10) + (1 << 3))

enum GESTURE_ID {
	GESTURE_RIGHT2LEFT_SWIP = 0x20,
	GESTURE_LEFT2RIGHT_SWIP = 0x21,
	GESTURE_DOWN2UP_SWIP = 0x22,
	GESTURE_UP2DOWN_SWIP = 0x23,
	GESTURE_DOUBLE_TAP = 0x24,
	GESTURE_DOUBLE_SWIP = 0x25,
	GESTURE_RIGHT_VEE = 0x51,
	GESTURE_LEFT_VEE = 0x52,
	GESTURE_DOWN_VEE = 0x53,
	GESTURE_UP_VEE = 0x54,
	GESTURE_O_CLOCKWISE = 0x57,
	GESTURE_O_ANTICLOCK = 0x30,
	GESTURE_W = 0x31,
	GESTURE_M = 0x32,
	GESTURE_FINGER_PRINT = 0x26,
	GESTURE_SINGLE_TAP = 0x27,
	GESTURE_HEART_ANTICLOCK = 0x55,
	GESTURE_HEART_CLOCKWISE = 0x59,
};

static void focal_esd_check_enable(void *chip_data, bool enable);
static int fts_hw_reset(struct chip_data_ft3658u *ts_data, u32 delayms);

/*************************************************************
 *******************FocalTech SPI protocols*******************
 *************************************************************/

#define SPI_RETRY_NUMBER            3
#define CS_HIGH_DELAY               150 /* unit: us */
#define SPI_BUF_LENGTH              4096

#define DATA_CRC_EN                 0x20
#define WRITE_CMD                   0x00
#define READ_CMD                    (0x80 | DATA_CRC_EN)

#define SPI_DUMMY_BYTE              3
#define SPI_HEADER_LENGTH           6   /*CRC*/

/* spi interface */
static int fts_spi_transfer(struct spi_device *spi, u8 *tx_buf, u8 *rx_buf, u32 len)
{
	int ret = 0;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.tx_buf = tx_buf,
		.rx_buf = rx_buf,
		.len    = len,
	};

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(spi, &msg);
	if (ret) {
		TPD_INFO("spi_sync fail,ret:%d", ret);
		return ret;
	}

	return ret;
}

static void fts_spi_buf_show(u8 *data, int datalen)
{
	int i = 0;
	int count = 0;
	int size = 0;
	char *tmpbuf = NULL;

	if (!data || (datalen <= 0)) {
		TPD_INFO("data/datalen is invalid");
		return;
	}

	size = (datalen > 256) ? 256 : datalen;
	tmpbuf = kzalloc(1024, GFP_KERNEL);
	if (!tmpbuf) {
		TPD_INFO("tmpbuf zalloc fail");
		return;
	}

	for (i = 0; i < size; i++)
		count += snprintf(tmpbuf + count, 1024 - count, "%02X ", data[i]);

	TPD_INFO("%s", tmpbuf);
	if (tmpbuf) {
		kfree(tmpbuf);
		tmpbuf = NULL;
	}
}

static void crckermit(u8 *data, u32 len, u16 *crc_out)
{
	u32 i = 0;
	u32 j = 0;
	u16 crc = 0xFFFF;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++) {
			if (crc & 0x01)
				crc = (crc >> 1) ^ 0x8408;
			else
				crc = (crc >> 1);
		}
	}

	*crc_out = crc;
}

static int rdata_check(u8 *rdata, u32 rlen)
{
	u16 crc_calc = 0;
	u16 crc_read = 0;

	crckermit(rdata, rlen - 2, &crc_calc);
	crc_read = (u16)(rdata[rlen - 1] << 8) + rdata[rlen - 2];
	if (crc_calc != crc_read) {
		fts_spi_buf_show(rdata, rlen);
		return -EIO;
	}

	return 0;
}

int fts_write(u8 *writebuf, u32 writelen)
{
	int ret = 0;
	int i = 0;
	struct chip_data_ft3658u *ts_data = g_fts_data;
	u8 *txbuf = NULL;
	u8 *rxbuf = NULL;
	u32 txlen = 0;
	u32 txlen_need = writelen + SPI_HEADER_LENGTH + SPI_DUMMY_BYTE;
	u32 datalen = writelen - 1;

	if (!ts_data || !ts_data->ft_spi) {
		TPD_INFO("ts_data/ft_spi is invalid");
		return -EINVAL;
	}

	if (!writebuf || !writelen) {
		TPD_INFO("writebuf/len is invalid");
		return -EINVAL;
	}

	mutex_lock(&ts_data->bus_lock);
	if (txlen_need > SPI_BUF_LENGTH) {
		txbuf = kzalloc(txlen_need, GFP_KERNEL);
		if (NULL == txbuf) {
			TPD_INFO("txbuf malloc fail");
			ret = -ENOMEM;
			goto err_write;
		}

		rxbuf = kzalloc(txlen_need, GFP_KERNEL);
		if (NULL == rxbuf) {
			TPD_INFO("rxbuf malloc fail");
			ret = -ENOMEM;
			goto err_write;
		}
	} else {
		txbuf = ts_data->bus_tx_buf;
		rxbuf = ts_data->bus_rx_buf;
		memset(txbuf, 0x0, SPI_BUF_LENGTH);
		memset(rxbuf, 0x0, SPI_BUF_LENGTH);
	}

	txbuf[txlen++] = writebuf[0];
	txbuf[txlen++] = WRITE_CMD;
	txbuf[txlen++] = (datalen >> 8) & 0xFF;
	txbuf[txlen++] = datalen & 0xFF;
	if (datalen > 0) {
		txlen = txlen + SPI_DUMMY_BYTE;
		memcpy(&txbuf[txlen], &writebuf[1], datalen);
		txlen = txlen + datalen;
	}

	for (i = 0; i < SPI_RETRY_NUMBER; i++) {
		ret = fts_spi_transfer(ts_data->ft_spi, txbuf, rxbuf, txlen);
		if ((0 == ret) && ((rxbuf[3] & 0xA0) == 0)) {
			break;
		} else {
			TPD_INFO("data write(addr:%x),status:%x,retry:%d,ret:%d",
			         writebuf[0], rxbuf[3], i, ret);
			ret = -EIO;
			udelay(CS_HIGH_DELAY);
		}
	}
	if (ret < 0) {
		TPD_INFO("data write(addr:%x) fail,status:%x,ret:%d",
		         writebuf[0], rxbuf[3], ret);
	}

err_write:
	if (txlen_need > SPI_BUF_LENGTH) {
		if (txbuf) {
			kfree(txbuf);
			txbuf = NULL;
		}

		if (rxbuf) {
			kfree(rxbuf);
			rxbuf = NULL;
		}
	}

	udelay(CS_HIGH_DELAY);
	mutex_unlock(&ts_data->bus_lock);
	return ret;
}

int fts_write_reg(u8 addr, u8 value)
{
	u8 writebuf[2] = { 0 };

	writebuf[0] = addr;
	writebuf[1] = value;
	return fts_write(writebuf, 2);
}

int fts_read(u8 *cmd, u32 cmdlen, u8 *data, u32 datalen)
{
	int ret = 0;
	int i = 0;
	struct chip_data_ft3658u *ts_data = g_fts_data;
	u8 *txbuf = NULL;
	u8 *rxbuf = NULL;
	u32 txlen = 0;
	u32 txlen_need = datalen + SPI_HEADER_LENGTH + SPI_DUMMY_BYTE;
	u8 ctrl = READ_CMD;
	u32 dp = 0;

	if (!ts_data || !ts_data->ft_spi) {
		TPD_INFO("ts_data/ft_spi is invalid");
		return -EINVAL;
	}

	if (!cmd || !cmdlen || !data || !datalen) {
		TPD_INFO("cmd/cmdlen/data/datalen is invalid");
		return -EINVAL;
	}

	mutex_lock(&ts_data->bus_lock);
	if (txlen_need > SPI_BUF_LENGTH) {
		txbuf = kzalloc(txlen_need, GFP_KERNEL);
		if (NULL == txbuf) {
			TPD_INFO("txbuf malloc fail");
			ret = -ENOMEM;
			goto err_read;
		}

		rxbuf = kzalloc(txlen_need, GFP_KERNEL);
		if (NULL == rxbuf) {
			TPD_INFO("rxbuf malloc fail");
			ret = -ENOMEM;
			goto err_read;
		}
	} else {
		txbuf = ts_data->bus_tx_buf;
		rxbuf = ts_data->bus_rx_buf;
		memset(txbuf, 0x0, SPI_BUF_LENGTH);
		memset(rxbuf, 0x0, SPI_BUF_LENGTH);
	}

	txbuf[txlen++] = cmd[0];
	txbuf[txlen++] = ctrl;
	txbuf[txlen++] = (datalen >> 8) & 0xFF;
	txbuf[txlen++] = datalen & 0xFF;
	dp = txlen + SPI_DUMMY_BYTE;
	txlen = dp + datalen;
	if (ctrl & DATA_CRC_EN) {
		txlen = txlen + 2;
	}

	for (i = 0; i < SPI_RETRY_NUMBER; i++) {
		ret = fts_spi_transfer(ts_data->ft_spi, txbuf, rxbuf, txlen);
		if ((0 == ret) && ((rxbuf[3] & 0xA0) == 0)) {
			memcpy(data, &rxbuf[dp], datalen);
			/* crc check */
			if (ctrl & DATA_CRC_EN) {
				ret = rdata_check(&rxbuf[dp], txlen - dp);
				if (ret < 0) {
					TPD_INFO("data read(addr:%x) crc abnormal,retry:%d",
					         cmd[0], i);
					udelay(CS_HIGH_DELAY);
					continue;
				}
			}
			break;
		} else {
			TPD_INFO("data read(addr:%x) status:%x,retry:%d,ret:%d",
			         cmd[0], rxbuf[3], i, ret);
			ret = -EIO;
			udelay(CS_HIGH_DELAY);
		}
	}

	if (ret < 0) {
		TPD_INFO("data read(addr:%x) %s,status:%x,ret:%d", cmd[0],
		         (i >= SPI_RETRY_NUMBER) ? "crc abnormal" : "fail",
		         rxbuf[3], ret);
	}

err_read:
	if (txlen_need > SPI_BUF_LENGTH) {
		if (txbuf) {
			kfree(txbuf);
			txbuf = NULL;
		}

		if (rxbuf) {
			kfree(rxbuf);
			rxbuf = NULL;
		}
	}

	udelay(CS_HIGH_DELAY);
	mutex_unlock(&ts_data->bus_lock);
	return ret;
}

int fts_read_reg(u8 addr, u8 *value)
{
	return fts_read(&addr, 1, value, 1);
}

static int fts_spi_transfer_direct(u8 *writebuf, u32 writelen, u8 *readbuf, u32 readlen)
{
	int ret = 0;
	struct chip_data_ft3658u *ts_data = g_fts_data;
	u8 *txbuf = NULL;
	u8 *rxbuf = NULL;
	bool read_cmd = (readbuf && readlen) ? 1 : 0;
	u32 txlen = (read_cmd) ? readlen : writelen;

	if (!writebuf || !writelen) {
		TPD_INFO("writebuf/len is invalid");
		return -EINVAL;
	}

	mutex_lock(&ts_data->bus_lock);
	if (txlen > SPI_BUF_LENGTH) {
		txbuf = kzalloc(txlen, GFP_KERNEL);
		if (NULL == txbuf) {
			TPD_INFO("txbuf malloc fail");
			ret = -ENOMEM;
			goto err_spi_dir;
		}

		rxbuf = kzalloc(txlen, GFP_KERNEL);
		if (NULL == rxbuf) {
			TPD_INFO("rxbuf malloc fail");
			ret = -ENOMEM;
			goto err_spi_dir;
		}
	} else {
		txbuf = ts_data->bus_tx_buf;
		rxbuf = ts_data->bus_rx_buf;
		memset(txbuf, 0x0, SPI_BUF_LENGTH);
		memset(rxbuf, 0x0, SPI_BUF_LENGTH);
	}

	memcpy(txbuf, writebuf, writelen);
	ret = fts_spi_transfer(ts_data->ft_spi, txbuf, rxbuf, txlen);
	if (ret < 0) {
		TPD_INFO("data read(addr:%x) fail,status:%x,ret:%d", txbuf[0], rxbuf[3], ret);
		goto err_spi_dir;
	}

	if (read_cmd) {
		memcpy(readbuf, rxbuf, txlen);
	}

	ret = 0;
err_spi_dir:
	if (txlen > SPI_BUF_LENGTH) {
		if (txbuf) {
			kfree(txbuf);
			txbuf = NULL;
		}

		if (rxbuf) {
			kfree(rxbuf);
			rxbuf = NULL;
		}
	}

	udelay(CS_HIGH_DELAY);
	mutex_unlock(&ts_data->bus_lock);
	return ret;
}

int fts_spi_write_direct(u8 *writebuf, u32 writelen)
{
	int ret = 0;
	u8 *readbuf = NULL;

	ret = fts_spi_transfer_direct(writebuf, writelen, readbuf, 0);
	if (ret < 0)
		return ret;
	else
		return 0;
}

int fts_spi_read_direct(u8 *writebuf, u32 writelen, u8 *readbuf, u32 readlen)
{
	int ret = 0;

	ret = fts_spi_transfer_direct(writebuf, writelen, readbuf, readlen);
	if (ret < 0)
		return ret;
	else
		return 0;
}

static int fts_bus_init(struct chip_data_ft3658u *ts_data)
{
	ts_data->bus_tx_buf = kzalloc(SPI_BUF_LENGTH, GFP_KERNEL);
	if (NULL == ts_data->bus_tx_buf) {
		TPD_INFO("failed to allocate memory for bus_tx_buf");
		return -ENOMEM;
	}

	ts_data->bus_rx_buf = kzalloc(SPI_BUF_LENGTH, GFP_KERNEL);
	if (NULL == ts_data->bus_rx_buf) {
		kfree(ts_data->bus_tx_buf);
		ts_data->bus_tx_buf = NULL;
		TPD_INFO("failed to allocate memory for bus_rx_buf");
		return -ENOMEM;
	}

	mutex_init(&ts_data->bus_lock);
	return 0;
}

static int fts_bus_exit(struct chip_data_ft3658u *ts_data)
{
	if (ts_data && ts_data->bus_tx_buf) {
		kfree(ts_data->bus_tx_buf);
		ts_data->bus_tx_buf = NULL;
	}

	if (ts_data && ts_data->bus_rx_buf) {
		kfree(ts_data->bus_rx_buf);
		ts_data->bus_rx_buf = NULL;
	}
	return 0;
}

/*********************************************************
 *              proc/ftxxxx-debug                        *
 *********************************************************/
#define PROC_READ_REGISTER                      1
#define PROC_WRITE_REGISTER                     2
#define PROC_WRITE_DATA                         6
#define PROC_READ_DATA                          7
#define PROC_SET_TEST_FLAG                      8
#define PROC_HW_RESET                           11
#define PROC_READ_STATUS                        12
#define PROC_SET_BOOT_MODE                      13
#define PROC_ENTER_TEST_ENVIRONMENT             14
#define PROC_WRITE_DATA_DIRECT                  16
#define PROC_READ_DATA_DIRECT                   17
#define PROC_CONFIGURE                          18
#define PROC_CONFIGURE_INTR                     20
#define PROC_GET_DRIVER_INFO                    21
#define PROC_NAME                               "ftxxxx-debug"
#define PROC_BUF_SIZE                           256

static ssize_t fts_debug_write(struct file *filp, const char __user *buff, size_t count, loff_t *ppos)
{
	u8 *writebuf = NULL;
	u8 tmpbuf[PROC_BUF_SIZE] = { 0 };
	int buflen = count;
	int writelen = 0;
	int ret = 0;
	char tmp[PROC_BUF_SIZE];
	struct chip_data_ft3658u *ts_data = PDE_DATA(file_inode(filp));
	struct ftxxxx_proc *proc = &ts_data->proc;

	if (buflen < 1) {
		TPD_INFO("apk proc wirte count(%d) fail", buflen);
		return -EINVAL;
	}

	if (buflen > PROC_BUF_SIZE) {
		writebuf = (u8 *)kzalloc(buflen * sizeof(u8), GFP_KERNEL);
		if (NULL == writebuf) {
			TPD_INFO("apk proc wirte buf zalloc fail");
			return -ENOMEM;
		}
	} else {
		writebuf = tmpbuf;
	}

	if (copy_from_user(writebuf, buff, buflen)) {
		TPD_INFO("[APK]: copy from user error!!");
		ret = -EFAULT;
		goto proc_write_err;
	}

	proc->opmode = writebuf[0];
	if (buflen == 1) {
		ret = buflen;
		goto proc_write_err;
	}

	switch (proc->opmode) {
	case PROC_SET_TEST_FLAG:
		TPD_INFO("[APK]: PROC_SET_TEST_FLAG = %x", writebuf[1]);
		if (writebuf[1] == 0) {
			focal_esd_check_enable(ts_data, true);
		} else {
			focal_esd_check_enable(ts_data, false);
		}
		break;

	case PROC_READ_REGISTER:
		proc->cmd[0] = writebuf[1];
		break;

	case PROC_WRITE_REGISTER:
		ret = fts_write_reg(writebuf[1], writebuf[2]);
		if (ret < 0) {
			TPD_INFO("PROC_WRITE_REGISTER write error");
			goto proc_write_err;
		}
		break;

	case PROC_READ_DATA:
		writelen = buflen - 1;
		if (writelen >= FTS_MAX_COMMMAND_LENGTH) {
			TPD_INFO("cmd(PROC_READ_DATA) length(%d) fail", writelen);
			goto proc_write_err;
		}
		memcpy(proc->cmd, writebuf + 1, writelen);
		proc->cmd_len = writelen;
		break;

	case PROC_WRITE_DATA:
		writelen = buflen - 1;
		ret = fts_write(writebuf + 1, writelen);
		if (ret < 0) {
			TPD_INFO("PROC_WRITE_DATA write error");
			goto proc_write_err;
		}
		break;

	case PROC_HW_RESET:
		if (buflen < PROC_BUF_SIZE) {
			snprintf(tmp, PROC_BUF_SIZE, "%s", writebuf + 1);
			tmp[buflen - 1] = '\0';
			if (strncmp(tmp, "focal_driver", 12) == 0) {
				TPD_INFO("APK execute HW Reset");
				fts_hw_reset(ts_data, 0);
			}
		}
		break;

	case PROC_READ_DATA_DIRECT:
		writelen = buflen - 1;
		if (writelen >= FTS_MAX_COMMMAND_LENGTH) {
			TPD_INFO("cmd(PROC_READ_DATA_DIRECT) length(%d) fail", writelen);
			goto proc_write_err;
		}
		memcpy(proc->cmd, writebuf + 1, writelen);
		proc->cmd_len = writelen;
		break;

	case PROC_WRITE_DATA_DIRECT:
		writelen = buflen - 1;
		ret = fts_spi_transfer_direct(writebuf + 1, writelen, NULL, 0);
		if (ret < 0) {
			TPD_INFO("PROC_WRITE_DATA_DIRECT write error");
			goto proc_write_err;
		}
		break;

	case PROC_CONFIGURE:
		ts_data->ft_spi->mode = writebuf[1];
		ts_data->ft_spi->bits_per_word = writebuf[2];
		ts_data->ft_spi->max_speed_hz = *(u32 *)(writebuf + 4);
		TPD_INFO("spi,mode=%d,bits=%d,speed=%d", ts_data->ft_spi->mode,
		         ts_data->ft_spi->bits_per_word, ts_data->ft_spi->max_speed_hz);
		ret = spi_setup(ts_data->ft_spi);
		if (ret) {
			TPD_INFO("spi setup fail");
			goto proc_write_err;
		}
		break;

	case PROC_CONFIGURE_INTR:
		if (writebuf[1] == 0)
			disable_irq_nosync(ts_data->ts->irq);
		else
			enable_irq(ts_data->ts->irq);
		break;

	default:
		break;
	}

	ret = buflen;
proc_write_err:
	if ((buflen > PROC_BUF_SIZE) && writebuf) {
		kfree(writebuf);
		writebuf = NULL;
	}

	return ret;
}

static ssize_t fts_debug_read(struct file *filp, char __user *buff, size_t count, loff_t *ppos)
{
	int ret = 0;
	int num_read_chars = 0;
	int buflen = count;
	u8 *readbuf = NULL;
	u8 tmpbuf[PROC_BUF_SIZE] = { 0 };
	struct chip_data_ft3658u *ts_data = PDE_DATA(file_inode(filp));
	struct ftxxxx_proc *proc = &ts_data->proc;

	if (buflen <= 0) {
		TPD_INFO("apk proc read count(%d) fail", buflen);
		return -EINVAL;
	}

	if (buflen > PROC_BUF_SIZE) {
		readbuf = (u8 *)kzalloc(buflen * sizeof(u8), GFP_KERNEL);
		if (NULL == readbuf) {
			TPD_INFO("apk proc wirte buf zalloc fail");
			return -ENOMEM;
		}
	} else {
		readbuf = tmpbuf;
	}

	switch (proc->opmode) {
	case PROC_READ_REGISTER:
		num_read_chars = 1;
		ret = fts_read_reg(proc->cmd[0], &readbuf[0]);
		if (ret < 0) {
			TPD_INFO("PROC_READ_REGISTER read error");
			goto proc_read_err;
		}
		break;

	case PROC_READ_DATA:
		num_read_chars = buflen;
		ret = fts_read(proc->cmd, proc->cmd_len, readbuf, num_read_chars);
		if (ret < 0) {
			TPD_INFO("PROC_READ_DATA read error");
			goto proc_read_err;
		}
		break;

	case PROC_READ_DATA_DIRECT:
		num_read_chars = buflen;
		ret = fts_spi_transfer_direct(proc->cmd, proc->cmd_len, readbuf, num_read_chars);
		if (ret < 0) {
			TPD_INFO("PROC_READ_DATA_DIRECT read error");
			goto proc_read_err;
		}
		break;

	case PROC_GET_DRIVER_INFO:
		if (buflen >= 64) {
			num_read_chars = buflen;
			readbuf[0] = 3;
			snprintf(&readbuf[32], buflen - 32, "Focaltech V3.4 20211214");
		}
		break;

	default:
		break;
	}

	ret = num_read_chars;
proc_read_err:
	if ((num_read_chars > 0) && copy_to_user(buff, readbuf, num_read_chars)) {
		TPD_INFO("copy to user error");
		ret = -EFAULT;
	}

	if ((buflen > PROC_BUF_SIZE) && readbuf) {
		kfree(readbuf);
		readbuf = NULL;
	}

	return ret;
}

DECLARE_PROC_OPS(fts_proc_fops, simple_open, fts_debug_read, fts_debug_write, NULL);

static int fts_ta_open(struct inode *inode, struct file *file)
{
	struct chip_data_ft3658u *ts_data = PDE_DATA(inode);

	if (ts_data->touch_analysis_support) {
		TPD_INFO("fts_ta open");
		ts_data->ta_buf = kzalloc(FTS_MAX_TOUCH_BUF, GFP_KERNEL);
		if (!ts_data->ta_buf) {
			TPD_INFO("kzalloc for ta_buf fails");
			return -ENOMEM;
		}
	}
	return 0;
}

static int fts_ta_release(struct inode *inode, struct file *file)
{
	struct chip_data_ft3658u *ts_data = PDE_DATA(inode);

	if (ts_data->touch_analysis_support) {
		TPD_INFO("fts_ta close");
		ts_data->ta_flag = 0;
		if (ts_data->ta_buf) {
			kfree(ts_data->ta_buf);
			ts_data->ta_buf = NULL;
		}
	}
	return 0;
}

static ssize_t fts_ta_read(struct file *filp, char __user *buff, size_t count, loff_t *ppos)
{
	int read_num = (int)count;
	struct chip_data_ft3658u *ts_data = PDE_DATA(file_inode(filp));

	if (!ts_data->touch_analysis_support || !ts_data->ta_buf) {
		TPD_INFO("touch_analysis is disabled, or ta_buf is NULL");
		return -EINVAL;
	}

	if (!(filp->f_flags & O_NONBLOCK)) {
		ts_data->ta_flag = 1;
		wait_event_interruptible(ts_data->ts_waitqueue, !ts_data->ta_flag);
	}

	read_num = (ts_data->ta_size < read_num) ? ts_data->ta_size : read_num;
	if ((read_num > 0) && (copy_to_user(buff, ts_data->ta_buf, read_num))) {
		TPD_INFO("copy to user error");
		return -EFAULT;
	}

	return read_num;
}

DECLARE_PROC_OPS(fts_procta_fops, fts_ta_open, fts_ta_read, NULL, fts_ta_release);

static int fts_create_apk_debug_channel(struct chip_data_ft3658u *ts_data)
{
	struct ftxxxx_proc *proc = &ts_data->proc;

	proc->proc_entry = proc_create_data(PROC_NAME, 0777, NULL, &fts_proc_fops, ts_data);
	if (NULL == proc->proc_entry) {
		TPD_INFO("create proc entry fail");
		return -ENOMEM;
	}

	ts_data->proc_ta.proc_entry = proc_create_data("fts_ta", 0777, NULL, \
	                              &fts_procta_fops, ts_data);
	if (!ts_data->proc_ta.proc_entry) {
		TPD_INFO("create proc_ta entry fail");
		return -ENOMEM;
	}
	TPD_INFO("Create proc entry success!");
	return 0;
}

static void fts_release_apk_debug_channel(struct chip_data_ft3658u *ts_data)
{
	struct ftxxxx_proc *proc = &ts_data->proc;

	if (proc->proc_entry) {
		proc_remove(proc->proc_entry);
	}

	if (ts_data->proc_ta.proc_entry)
		proc_remove(ts_data->proc_ta.proc_entry);
}


static ssize_t fts_prc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)ts->chip_data;

	mutex_lock(&ts->mutex);
	if (buf[0] == '1') {
		TPD_INFO("enable prc");
		ts_data->prc_support = 1;
	} else if (buf[0] == '0') {
		TPD_INFO("disable prc");
		cancel_delayed_work_sync(&ts_data->prc_work);
		ts_data->prc_support = 0;
	}
	mutex_unlock(&ts->mutex);

	return count;
}

static ssize_t fts_prc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count;
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)ts->chip_data;

	count = snprintf(buf, PAGE_SIZE, "PRC: %s\n", \
	                 ts_data->prc_support ? "Enable" : "Disable");

	return count;
}


/* fts_touch_size node */
static ssize_t fts_touchsize_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)ts->chip_data;

	count += snprintf(buf + count, PAGE_SIZE, "touch size:%d\n", ts_data->touch_size);

	return count;
}

static ssize_t fts_touchsize_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value = 0;
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)ts->chip_data;

	sscanf(buf, "%d", &value);
	if ((value > 2) && (value < FTS_MAX_TOUCH_BUF)) {
		TPD_INFO("touch size:%d->%d", ts_data->touch_size, value);
		ts_data->touch_size = value;
	} else
		TPD_INFO("touch size:%d invalid", value);

	return count;
}

/* fts_ta_mode node */
static ssize_t fts_tamode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int count = 0;
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)ts->chip_data;

	count += snprintf(buf + count, PAGE_SIZE, "touch analysis:%s\n", \
	                  ts_data->touch_analysis_support ? "Enable" : "Disable");

	return count;
}

static ssize_t fts_tamode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value = 0;
	struct touchpanel_data *ts = dev_get_drvdata(dev);
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)ts->chip_data;

	sscanf(buf, "%d", &value);
	ts_data->touch_analysis_support = !!value;
	TPD_INFO("set touch analysis:%d", ts_data->touch_analysis_support);

	return count;
}

static DEVICE_ATTR(fts_prc, S_IRUGO | S_IWUSR, fts_prc_show, fts_prc_store);
static DEVICE_ATTR(fts_touch_size, S_IRUGO | S_IWUSR, fts_touchsize_show, fts_touchsize_store);
static DEVICE_ATTR(fts_ta_mode, S_IRUGO | S_IWUSR, fts_tamode_show, fts_tamode_store);

/* add your attr in here*/
static struct attribute *fts_attributes[] = {
	&dev_attr_fts_prc.attr,
	&dev_attr_fts_touch_size.attr,
	&dev_attr_fts_ta_mode.attr,
	NULL
};

static struct attribute_group fts_attribute_group = {
	.attrs = fts_attributes
};

static int fts_create_sysfs(struct chip_data_ft3658u *ts_data)
{
	int ret = 0;

	ret = sysfs_create_group(&ts_data->ts->dev->kobj, &fts_attribute_group);
	if (ret) {
		TPD_INFO("[EX]: sysfs_create_group() failed!!");
		sysfs_remove_group(&ts_data->ts->dev->kobj, &fts_attribute_group);
		return -ENOMEM;
	} else {
		TPD_INFO("[EX]: sysfs_create_group() succeeded!!");
	}

	return ret;
}

static int fts_remove_sysfs(struct chip_data_ft3658u *ts_data)
{
	sysfs_remove_group(&ts_data->ts->dev->kobj, &fts_attribute_group);
	return 0;
}

/*******Part1:Call Back Function implement*******/

static int fts_rstgpio_set(struct hw_resource *hw_res, bool on)
{
	if (gpio_is_valid(hw_res->reset_gpio)) {
		TPD_INFO("Set the reset_gpio \n");
		gpio_direction_output(hw_res->reset_gpio, on);

	} else {
		TPD_INFO("reset is invalid!!\n");
	}

	return 0;
}

/*
 * return success: 0; fail : negative
 */
static int fts_hw_reset(struct chip_data_ft3658u *ts_data, u32 delayms)
{
	TPD_INFO("%s.\n", __func__);
	fts_rstgpio_set(ts_data->hw_res, false); /* reset gpio*/
	msleep(5);
	fts_rstgpio_set(ts_data->hw_res, true); /* reset gpio*/

	if (delayms) {
		msleep(delayms);
	}

	return 0;
}
static int fts_power_control(void *chip_data, bool enable)
{
	int ret = 0;

	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;

	if (true == enable) {
		fts_rstgpio_set(ts_data->hw_res, false);
		msleep(1);
		ret = tp_powercontrol_avdd(ts_data->hw_res, true);

		if (ret) {
			return -1;
		}
		ret = tp_powercontrol_vddi(ts_data->hw_res, true);

		if (ret) {
			return -1;
		}
		msleep(POWEWRUP_TO_RESET_TIME);
		fts_rstgpio_set(ts_data->hw_res, true);
		msleep(RESET_TO_NORMAL_TIME);

	} else {
		fts_rstgpio_set(ts_data->hw_res, false);
		ret = tp_powercontrol_avdd(ts_data->hw_res, false);

		if (ret) {
			return -1;
		}
		ret = tp_powercontrol_vddi(ts_data->hw_res, false);

		if (ret) {
			return -1;
		}
	}

	return ret;
}

static int focal_dump_reg_state(void *chip_data, char *buf)
{
	int count = 0;
	u8 regvalue = 0;

	/* power mode 0:active 1:monitor 3:sleep */
	fts_read_reg(FTS_REG_POWER_MODE, &regvalue);
	count += sprintf(buf + count, "Power Mode:0x%02x\n", regvalue);

	/* FW version */
	fts_read_reg(FTS_REG_FW_VER, &regvalue);
	count += sprintf(buf + count, "FW Ver:0x%02x\n", regvalue);

	/* Vendor ID */
	fts_read_reg(FTS_REG_VENDOR_ID, &regvalue);
	count += sprintf(buf + count, "Vendor ID:0x%02x\n", regvalue);

	/*  1 Gesture mode,0 Normal mode */
	fts_read_reg(FTS_REG_GESTURE_EN, &regvalue);
	count += sprintf(buf + count, "Gesture Mode:0x%02x\n", regvalue);

	/*  3 charge in */
	fts_read_reg(FTS_REG_CHARGER_MODE_EN, &regvalue);
	count += sprintf(buf + count, "charge stat:0x%02x\n", regvalue);

	/* Interrupt counter */
	fts_read_reg(FTS_REG_INT_CNT, &regvalue);
	count += sprintf(buf + count, "INT count:0x%02x\n", regvalue);

	/* Flow work counter */
	fts_read_reg(FTS_REG_FLOW_WORK_CNT, &regvalue);
	count += sprintf(buf + count, "ESD count:0x%02x\n", regvalue);

	return count;
}

static int focal_get_fw_version(void *chip_data)
{
	u8 fw_ver = 0;

	fts_read_reg(FTS_REG_FW_VER, &fw_ver);
	return (int)fw_ver;
}

static void focal_esd_check_enable(void *chip_data, bool enable)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	ts_data->esd_check_enabled = enable;
}

static bool focal_get_esd_check_flag(void *chip_data)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	return ts_data->esd_check_need_stop;
}

static int fts_esd_handle(void *chip_data)
{
	int ret = -1;
	int i = 0;
	static int flow_work_cnt_last = 0;
	static int err_cnt = 0;
	static int i2c_err = 0;
	u8 val = 0xFF;
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;

	if (!ts_data->esd_check_enabled) {
		goto NORMAL_END;
	}

	ret = fts_read_reg(0x00, &val);

	if ((ret & 0x70) == 0x40) { /*work in factory mode*/
		goto NORMAL_END;
	}

	for (i = 0; i < 3; i++) {
		ret = fts_read_reg(FTS_REG_CHIP_ID, &val);

		if (val != FTS_VAL_CHIP_ID) {
			TPD_INFO("%s: read chip_id failed!(ret:%x)\n", __func__, ret);
			msleep(10);
			i2c_err++;

		} else {
			i2c_err = 0;
			break;
		}
	}

	ret = fts_read_reg(FTS_REG_FLOW_WORK_CNT, &val);

	if (ret < 0) {
		TPD_INFO("%s: read FTS_REG_FLOW_WORK_CNT failed!\n", __func__);
		i2c_err++;
	}

	if (flow_work_cnt_last == val) {
		err_cnt++;

	} else {
		err_cnt = 0;
	}

	flow_work_cnt_last = ret;

	if ((err_cnt >= 5) || (i2c_err >= 3)) {
		TPD_INFO("esd check failed, start reset!\n");
		disable_irq_nosync(ts_data->ts->irq);
		tp_touch_btnkey_release(ts_data->ts->tp_index);
		fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);
		enable_irq(ts_data->ts->irq);
		flow_work_cnt_last = 0;
		err_cnt = 0;
		i2c_err = 0;
	}

NORMAL_END:
	return 0;
}

static void fts_release_all_finger(struct touchpanel_data *ts)
{
#ifdef TYPE_B_PROTOCOL
	int i = 0;

	if (!ts->touch_count || !ts->irq_slot)
		return;

	mutex_lock(&ts->report_mutex);
	for (i = 0; i < ts->max_num; i++) {
		input_mt_slot(ts->input_dev, i);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
	}
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
	input_sync(ts->input_dev);
	mutex_unlock(&ts->report_mutex);
	TPD_INFO("fts_release_all_finger");
	ts->view_area_touched = 0; /* realse all touch point,must clear this flag */
	ts->touch_count = 0;
	ts->irq_slot = 0;
#endif
}

static void fts_prc_func(struct work_struct *work)
{
	struct chip_data_ft3658u *ts_data = container_of(work,
	                                    struct chip_data_ft3658u, prc_work.work);
	unsigned long cur_jiffies = jiffies;
	unsigned long intr_timeout = msecs_to_jiffies(PRC_INTR_INTERVALS);

	if (ts_data->prc_support && !ts_data->ts->is_suspended) {
		intr_timeout += ts_data->intr_jiffies;
		if (time_after(cur_jiffies, intr_timeout)) {
			if (ts_data->ts->touch_count && ts_data->ts->irq_slot) {
				fts_release_all_finger(ts_data->ts);
				TPD_INFO("prc trigger interval:%dms",
				         jiffies_to_msecs(cur_jiffies - ts_data->intr_jiffies));
			}
			ts_data->prc_mode = 0;
		} else {
			queue_delayed_work(ts_data->ts_workqueue, &ts_data->prc_work,
			                   msecs_to_jiffies(POINT_REPORT_CHECK_WAIT_TIME));
			ts_data->prc_mode = 1;
		}
	} else {
		ts_data->prc_mode = 0;
	}
}

static void fts_prc_queue_work(struct chip_data_ft3658u *ts_data)
{
	ts_data->intr_jiffies = jiffies;
	if (ts_data->prc_support && !ts_data->prc_mode && !ts_data->ts->is_suspended) {
		queue_delayed_work(ts_data->ts_workqueue, &ts_data->prc_work,
		                   msecs_to_jiffies(POINT_REPORT_CHECK_WAIT_TIME));
		ts_data->prc_mode = 1;
	}
}

static int fts_point_report_check_init(struct chip_data_ft3658u *ts_data)
{
	TPD_INFO("point check init");

	if (ts_data->ts_workqueue) {
		INIT_DELAYED_WORK(&ts_data->prc_work, fts_prc_func);
	} else {
		TPD_INFO("fts workqueue is NULL, can't run point report check function");
		return -EINVAL;
	}

	ts_data->prc_support = 1;
	return 0;
}

static int fts_point_report_check_exit(struct chip_data_ft3658u *ts_data)
{
	TPD_INFO("point check exit");
	cancel_delayed_work_sync(&ts_data->prc_work);
	return 0;
}


static bool fts_fwupg_check_flash_status(struct chip_data_ft3658u *ts_data,
        u16 flash_status, int retries, int retries_delay)
{
	int ret = 0;
	int i = 0;
	u8 cmd = 0;
	u8 val[2] = { 0 };
	u16 read_status = 0;

	for (i = 0; i < retries; i++) {
		cmd = FTS_CMD_FLASH_STATUS;
		ret = fts_read(&cmd, 1, val, 2);
		read_status = (((u16)val[0]) << 8) + val[1];

		if (flash_status == read_status) {
			return true;
		}

		TPD_DEBUG("flash status fail,ok:%04x read:%04x, retries:%d", flash_status,
		          read_status, i);
		msleep(retries_delay);
	}

	TPD_INFO("flash status fail,ok:%04x read:%04x, retries:%d", flash_status,
	         read_status, i);
	return false;
}

static int fts_fwupg_enter_into_boot(struct chip_data_ft3658u *ts_data)
{
	int ret = 0;
	int i = 0;
	u8 cmd = 0;
	u8 id[2] = { 0 };

	do {
		/*reset to boot*/
		ret = fts_write_reg(FTS_REG_UPGRADE, FTS_UPGRADE_AA);

		if (ret < 0) {
			TPD_INFO("write FC=0xAA fail");
			return ret;
		}

		msleep(FTS_DELAY_UPGRADE_AA);

		ret = fts_write_reg(FTS_REG_UPGRADE, FTS_UPGRADE_55);

		if (ret < 0) {
			TPD_INFO("write FC=0x55 fail");
			return ret;
		}

		msleep(FTS_DELAY_UPGRADE_RESET);

		/*read boot id*/
		cmd = FTS_CMD_START;
		ret = fts_write(&cmd, 1);

		if (ret < 0) {
			TPD_INFO("write 0x55 fail");
			return ret;
		}

		cmd = FTS_CMD_READ_ID;
		ret = fts_read(&cmd, 1, id, 2);

		if (ret < 0) {
			TPD_INFO("read boot id fail");
			return ret;
		}

		TPD_INFO("read boot id:0x%02x%02x", id[0], id[1]);

		if ((id[0] == FTS_VAL_BT_ID) && (id[1] == FTS_VAL_BT_ID2)) {
			break;
		}
	} while (i++ < FTS_UPGRADE_LOOP);

	return 0;
}

static int fts_fwupg_erase(struct chip_data_ft3658u *ts_data, u32 delay)
{
	int ret = 0;
	u8 cmd = 0;
	bool flag = false;

	TPD_INFO("**********erase now**********");

	/*send to erase flash*/
	cmd = FTS_CMD_ERASE_APP;
	ret = fts_write(&cmd, 1);

	if (ret < 0) {
		TPD_INFO("send erase cmd fail");
		return ret;
	}

	msleep(delay);

	/* read status 0xF0AA: success */
	flag = fts_fwupg_check_flash_status(ts_data, FTS_CMD_FLASH_STATUS_ERASE_OK,
	                                    FTS_RETRIES_REASE, FTS_RETRIES_DELAY_REASE);

	if (!flag) {
		TPD_INFO("check ecc flash status fail");
		return -EIO;
	}

	return 0;
}

static int fts_flash_write_buf(struct chip_data_ft3658u *ts_data, u32 saddr,
                               u8 *buf, u32 len, u32 delay)
{
	int ret = 0;
	u32 i = 0;
	u32 j = 0;
	u32 packet_number = 0;
	u32 packet_len = 0;
	u32 addr = 0;
	u32 offset = 0;
	u32 remainder = 0;
	u32 cmdlen = 0;
	u8 packet_buf[BYTES_PER_TIME + 6] = { 0 };
	u8 cmd = 0;
	u8 val[2] = { 0 };
	u16 read_status = 0;
	u16 wr_ok = 0;

	TPD_INFO("**********write data to flash**********");
	TPD_INFO("data buf start addr=0x%x, len=0x%x", saddr, len);
	packet_number = len / BYTES_PER_TIME;
	remainder = len % BYTES_PER_TIME;

	if (remainder > 0) {
		packet_number++;
	}

	packet_len = BYTES_PER_TIME;
	TPD_INFO("write data, num:%d remainder:%d", packet_number, remainder);

	for (i = 0; i < packet_number; i++) {
		offset = i * BYTES_PER_TIME;
		addr = saddr + offset;

		/* last packet */
		if ((i == (packet_number - 1)) && remainder) {
			packet_len = remainder;
		}

		packet_buf[0] = FTS_CMD_SET_WFLASH_ADDR;
		packet_buf[1] = (addr >> 16) & 0xFF;
		packet_buf[2] = (addr >> 8) & 0xFF;
		packet_buf[3] = (addr) & 0xFF;
		ret = fts_write(packet_buf, 4);
		if (ret < 0) {
			TPD_INFO("set flash address fail");
			return ret;
		}

		packet_buf[0] = FTS_CMD_WRITE;
		cmdlen = 1;

		memcpy(&packet_buf[cmdlen], &buf[offset], packet_len);
		ret = fts_write(&packet_buf[0], packet_len + cmdlen);

		if (ret < 0) {
			TPD_INFO("app write fail");
			return ret;
		}

		mdelay(delay);

		/* read status */
		wr_ok = FTS_CMD_FLASH_STATUS_WRITE_OK + addr / packet_len;

		for (j = 0; j < FTS_RETRIES_WRITE; j++) {
			cmd = FTS_CMD_FLASH_STATUS;
			ret = fts_read(&cmd, 1, val, 2);
			read_status = (((u16)val[0]) << 8) + val[1];

			/* TPD_DEBUG("%x %x", wr_ok, read_status); */
			if (wr_ok == read_status) {
				break;
			}

			mdelay(FTS_RETRIES_DELAY_WRITE);
		}
	}

	return 0;
}

static int fts_fwupg_ecc_cal_host(u8 *buf, u32 len)
{
	u16 ecc = 0;
	u32 i = 0;
	u32 j = 0;

	for (i = 0; i < len; i += 2) {
		ecc ^= ((buf[i] << 8) | (buf[i + 1]));
		for (j = 0; j < 16; j ++) {
			if (ecc & 0x01)
				ecc = (u16)((ecc >> 1) ^ AL2_FCS_COEF);
			else
				ecc >>= 1;
		}
	}

	return (int)ecc;
}

int fts_fwupg_ecc_cal_tp(struct chip_data_ft3658u *ts_data, u32 saddr, u32 len)
{
	int ret = 0;
	u8 wbuf[7] = { 0 };
	u8 val[2] = { 0 };
	int ecc = 0;
	bool bflag = false;

	TPD_INFO("**********read out checksum**********");
	/* check sum init */
	wbuf[0] = FTS_CMD_ECC_INIT;
	ret = fts_write(&wbuf[0], 1);

	if (ret < 0) {
		TPD_INFO("ecc init cmd write fail");
		return ret;
	}

	/* send commond to start checksum */
	wbuf[0] = FTS_CMD_ECC_CAL;
	wbuf[1] = (saddr >> 16) & 0xFF;
	wbuf[2] = (saddr >> 8) & 0xFF;
	wbuf[3] = (saddr);
	wbuf[4] = (len >> 16) & 0xFF;
	wbuf[5] = (len >> 8) & 0xFF;
	wbuf[6] = (len);
	TPD_INFO("ecc calc startaddr:0x%04x, len:%d", saddr, len);
	ret = fts_write(&wbuf[0], 7);

	if (ret < 0) {
		TPD_INFO("ecc calc cmd write fail");
		return ret;
	}

	msleep(len / 256);

	/* read status if check sum is finished */
	bflag = fts_fwupg_check_flash_status(ts_data, FTS_CMD_FLASH_STATUS_ECC_OK,
	                                     FTS_RETRIES_ECC_CAL,
	                                     FTS_RETRIES_DELAY_ECC_CAL);

	if (!bflag) {
		TPD_INFO("ecc flash status read fail");
		return -EIO;
	}

	/* read out check sum */
	wbuf[0] = FTS_CMD_ECC_READ;
	ret = fts_read(&wbuf[0], 1, val, 2);

	if (ret < 0) {
		TPD_INFO("ecc read cmd write fail");
		return ret;
	}

	ecc = (int)((u16)(val[0] << 8) + val[1]);

	return ecc;
}

static int fts_upgrade(struct chip_data_ft3658u *ts_data, u8 *buf, u32 len)
{
	int ret = 0;
	u32 start_addr = 0;
	u8 cmd[4] = { 0 };
	int ecc_in_host = 0;
	int ecc_in_tp = 0;

	if (!buf) {
		TPD_INFO("fw_buf is invalid");
		return -EINVAL;
	}

	/* enter into upgrade environment */
	ret = fts_fwupg_enter_into_boot(ts_data);

	if (ret < 0) {
		TPD_INFO("enter into pramboot/bootloader fail,ret=%d", ret);
		goto fw_reset;
	}

	cmd[0] = FTS_CMD_DATA_LEN;
	cmd[1] = (len >> 16) & 0xFF;
	cmd[2] = (len >> 8) & 0xFF;
	cmd[3] = (len) & 0xFF;
	ret = fts_write(&cmd[0], 4);

	if (ret < 0) {
		TPD_INFO("data len cmd write fail");
		goto fw_reset;
	}

	/*erase*/
	ret = fts_fwupg_erase(ts_data, FTS_REASE_APP_DELAY);

	if (ret < 0) {
		TPD_INFO("erase cmd write fail");
		goto fw_reset;
	}

	/* write app */
	start_addr = 0;
	ret = fts_flash_write_buf(ts_data, start_addr, buf, len, 1);

	if (ret < 0) {
		TPD_INFO("flash write fail");
		goto fw_reset;
	}

	ecc_in_host = fts_fwupg_ecc_cal_host(buf, len);
	ecc_in_tp = fts_fwupg_ecc_cal_tp(ts_data, start_addr, len);

	if (ecc_in_tp < 0) {
		TPD_INFO("ecc read fail");
		goto fw_reset;
	}

	TPD_INFO("ecc in tp:%x, host:%x", ecc_in_tp, ecc_in_host);

	if (ecc_in_tp != ecc_in_host) {
		TPD_INFO("ecc check fail");
		goto fw_reset;
	}

	TPD_INFO("upgrade success, reset to normal boot");
	cmd[0] = FTS_CMD_RESET;
	ret = fts_write(&cmd[0], 1);

	if (ret < 0) {
		TPD_INFO("reset to normal boot fail");
	}

	msleep(200);
	return 0;

fw_reset:
	TPD_INFO("upgrade fail, reset to normal boot");
	cmd[0] = FTS_CMD_RESET;
	ret = fts_write(&cmd[0], 1);

	if (ret < 0) {
		TPD_INFO("reset to normal boot fail");
	}

	return -EIO;
}


static fw_check_state fts_fw_check(void *chip_data,
                                   struct resolution_info *resolution_info, struct panel_info *panel_data)
{
	u8 cmd = 0;
	u8 id[2] = { 0 };
	char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;

	fts_read_reg(FTS_REG_CHIP_ID, &id[0]);
	fts_read_reg(FTS_REG_CHIP_ID2, &id[1]);

	if ((id[0] != FTS_VAL_CHIP_ID) || (id[1] != FTS_VAL_CHIP_ID2)) {
		cmd = 0x55;
		fts_write(&cmd, 1);
		msleep(12);
		cmd = 0x90;
		fts_read(&cmd, 1, id, 2);
		TPD_INFO("boot id:0x%02x%02x, fw abnormal", id[0], id[1]);
		return FW_ABNORMAL;
	}

	/*fw check normal need update tp_fw  && device info*/
	fts_read_reg(FTS_REG_FW_VER, &ts_data->fwver);
	panel_data->tp_fw = ts_data->fwver;
	TPD_INFO("FW VER:%d", panel_data->tp_fw);

	if (panel_data->manufacture_info.version) {
		sprintf(dev_version, "%04x", panel_data->tp_fw);
		strlcpy(&(panel_data->manufacture_info.version[7]), dev_version, 5);
	}

	return FW_NORMAL;
}

int fts_reset_proc(int hdelayms)
{
	TPD_INFO("tp reset");
	fts_rstgpio_set(g_fts_data->hw_res, false); /* reset gpio*/
	msleep(5);
	fts_rstgpio_set(g_fts_data->hw_res, true); /* reset gpio*/

	if (hdelayms) {
		msleep(hdelayms);
	}

	return 0;
}

#define OFFSET_FW_DATA_FW_VER 0x010E
static fw_update_state fts_fw_update(void *chip_data, const struct firmware *fw,
                                     bool force)
{
	int ret = 0;
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	u8 *buf;
	u32 len = 0;

	if (!fw) {
		TPD_INFO("fw is null");
		return FW_UPDATE_ERROR;
	}

	buf = (u8 *)fw->data;
	len = (int)fw->size;

	if ((len < 0x120) || (len > (120 * 1024))) {
		TPD_INFO("fw_len(%d) is invalid", len);
		return FW_UPDATE_ERROR;
	}

	if (force || (buf[OFFSET_FW_DATA_FW_VER] != ts_data->fwver)) {
		TPD_INFO("Need update, force(%d)/fwver:Host(0x%02x),TP(0x%02x)", force,
		         buf[OFFSET_FW_DATA_FW_VER], ts_data->fwver);
		focal_esd_check_enable(ts_data, false);
		ret = fts_upgrade(ts_data, buf, len);
		focal_esd_check_enable(ts_data, true);

		if (ret < 0) {
			TPD_INFO("fw update fail");
			return FW_UPDATE_ERROR;
		}

		return FW_UPDATE_SUCCESS;
	}

	return FW_NO_NEED_UPDATE;
}


static int fts_enter_factory_work_mode(struct chip_data_ft3658u *ts_data,
                                       u8 mode_val)
{
	int ret = 0;
	int retry = 20;
	u8 regval = 0;

	TPD_INFO("%s:enter %s mode", __func__, (mode_val == 0x40) ? "factory" : "work");
	ret = fts_write_reg(DEVIDE_MODE_ADDR, mode_val);

	if (ret < 0) {
		TPD_INFO("%s:write mode(val:0x%x) fail", __func__, mode_val);
		return ret;
	}

	while (--retry) {
		fts_read_reg(DEVIDE_MODE_ADDR, &regval);

		if (regval == mode_val) {
			break;
		}

		msleep(20);
	}

	if (!retry) {
		TPD_INFO("%s:enter mode(val:0x%x) timeout", __func__, mode_val);
		return -EIO;
	}

	msleep(FACTORY_TEST_DELAY);
	return 0;
}

static int fts_start_scan(struct chip_data_ft3658u *ts_data)
{
	int ret = 0;
	int retry = 50;
	u8 regval = 0;
	u8 scanval = FTS_FACTORY_MODE_VALUE | (1 << 7);

	TPD_INFO("%s: start to scan a frame", __func__);
	ret = fts_write_reg(DEVIDE_MODE_ADDR, scanval);

	if (ret < 0) {
		TPD_INFO("%s:start to scan a frame fail", __func__);
		return ret;
	}

	while (--retry) {
		fts_read_reg(DEVIDE_MODE_ADDR, &regval);

		if (regval == FTS_FACTORY_MODE_VALUE) {
			break;
		}

		msleep(20);
	}

	if (!retry) {
		TPD_INFO("%s:scan a frame timeout", __func__);
		return -EIO;
	}

	return 0;
}

static int fts_get_rawdata(struct chip_data_ft3658u *ts_data, int *raw,
                           bool is_diff)
{
	int ret = 0;
	int i = 0;
	int byte_num = ts_data->hw_res->tx_num * ts_data->hw_res->rx_num * 2;
	int size = 0;
	int packet_len = 0;
	int offset = 0;
	u8 raw_addr = 0;
	u8 regval = 0;
	u8 *buf = NULL;

	TPD_INFO("%s:call", __func__);
	/*kzalloc buffer*/
	buf = kzalloc(byte_num, GFP_KERNEL);

	if (!buf) {
		TPD_INFO("%s:kzalloc for raw byte buf fail", __func__);
		return -ENOMEM;
	}

	ret = fts_enter_factory_work_mode(ts_data, FTS_FACTORY_MODE_VALUE);

	if (ret < 0) {
		TPD_INFO("%s:enter factory mode fail", __func__);
		goto raw_err;
	}

	if (is_diff) {
		fts_read_reg(FACTORY_REG_DATA_SELECT, &regval);
		ret = fts_write_reg(FACTORY_REG_DATA_SELECT, 0x01);

		if (ret < 0) {
			TPD_INFO("%s:write 0x01 to reg0x06 fail", __func__);
			goto reg_restore;
		}
	}

	ret = fts_start_scan(ts_data);

	if (ret < 0) {
		TPD_INFO("%s:scan a frame fail", __func__);
		goto reg_restore;
	}

	ret = fts_write_reg(FACTORY_REG_LINE_ADDR, 0xAA);

	if (ret < 0) {
		TPD_INFO("%s:write 0xAA to reg0x01 fail", __func__);
		goto reg_restore;
	}

	raw_addr = FACTORY_REG_RAWDATA_ADDR_MC_SC;
	ret = fts_read(&raw_addr, 1, buf, MAX_PACKET_SIZE);
	size = byte_num - MAX_PACKET_SIZE;
	offset = MAX_PACKET_SIZE;

	while (size > 0) {
		if (size >= MAX_PACKET_SIZE) {
			packet_len = MAX_PACKET_SIZE;

		} else {
			packet_len = size;
		}

		ret = fts_read(&raw_addr, 1, buf + offset, packet_len);

		if (ret < 0) {
			TPD_INFO("%s:read raw data(packet:%d) fail", __func__,
			         offset / MAX_PACKET_SIZE);
			goto reg_restore;
		}

		size -= packet_len;
		offset += packet_len;
	}

	for (i = 0; i < byte_num; i = i + 2) {
		raw[i >> 1] = (int)(short)((buf[i] << 8) + buf[i + 1]);
	}

reg_restore:

	if (is_diff) {
		ret = fts_write_reg(FACTORY_REG_DATA_SELECT, regval);

		if (ret < 0) {
			TPD_INFO("%s:restore reg0x06 fail", __func__);
		}
	}

raw_err:
	kfree(buf);
	ret = fts_enter_factory_work_mode(ts_data, FTS_WORK_MODE_VALUE);

	if (ret < 0) {
		TPD_INFO("%s:enter work mode fail", __func__);
	}

	return ret;
}

static void fts_delta_read(struct seq_file *s, void *chip_data)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	int *raw = NULL;
	int tx_num = ts_data->hw_res->tx_num;
	int rx_num = ts_data->hw_res->rx_num;

	TPD_INFO("%s:start to read diff data", __func__);
	focal_esd_check_enable(ts_data, false);   /*no allowed esd check*/

	raw = kzalloc(tx_num * rx_num * sizeof(int), GFP_KERNEL);

	if (!raw) {
		seq_printf(s, "kzalloc for raw fail\n");
		goto raw_fail;
	}

	ret = fts_write_reg(FTS_REG_AUTOCLB_ADDR, 0x01);

	if (ret < 0) {
		TPD_INFO("%s, write 0x01 to reg 0xee failed \n", __func__);
	}

	ret = fts_get_rawdata(ts_data, raw, true);

	if (ret < 0) {
		seq_printf(s, "get diff data fail\n");
		goto raw_fail;
	}

	for (i = 0; i < tx_num; i++) {
		seq_printf(s, "\n[%2d]", i + 1);

		for (j = 0; j < rx_num; j++) {
			seq_printf(s, " %5d,", raw[i * rx_num + j]);
		}
	}

	seq_printf(s, "\n");

raw_fail:
	fts_write_reg(FTS_REG_AUTOCLB_ADDR, 0x00);
	focal_esd_check_enable(ts_data, true);
	kfree(raw);
}

static void fts_baseline_read(struct seq_file *s, void *chip_data)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	int *raw = NULL;
	int tx_num = ts_data->hw_res->tx_num;
	int rx_num = ts_data->hw_res->rx_num;

	TPD_INFO("%s:start to read raw data", __func__);
	focal_esd_check_enable(ts_data, false);

	raw = kzalloc(tx_num * rx_num * sizeof(int), GFP_KERNEL);

	if (!raw) {
		seq_printf(s, "kzalloc for raw fail\n");
		goto raw_fail;
	}

	ret = fts_write_reg(FTS_REG_AUTOCLB_ADDR, 0x01);

	if (ret < 0) {
		TPD_INFO("%s, write 0x01 to reg 0xee failed \n", __func__);
	}

	ret = fts_get_rawdata(ts_data, raw, false);

	if (ret < 0) {
		seq_printf(s, "get raw data fail\n");
		goto raw_fail;
	}

	for (i = 0; i < tx_num; i++) {
		seq_printf(s, "\n[%2d]", i + 1);

		for (j = 0; j < rx_num; j++) {
			seq_printf(s, " %5d,", raw[i * rx_num + j]);
		}
	}

	seq_printf(s, "\n");

raw_fail:
	fts_write_reg(FTS_REG_AUTOCLB_ADDR, 0x00);
	focal_esd_check_enable(ts_data, true);
	kfree(raw);
}

static void fts_main_register_read(struct seq_file *s, void *chip_data)
{
	u8 regvalue = 0;

	/*TP FW version*/
	fts_read_reg(FTS_REG_FW_VER, &regvalue);
	seq_printf(s, "TP FW Ver:0x%02x\n", regvalue);

	/*Vendor ID*/
	fts_read_reg(FTS_REG_VENDOR_ID, &regvalue);
	seq_printf(s, "Vendor ID:0x%02x\n", regvalue);

	/*Gesture enable*/
	fts_read_reg(FTS_REG_GESTURE_EN, &regvalue);
	seq_printf(s, "Gesture Mode:0x%02x\n", regvalue);

	/*charge in*/
	fts_read_reg(FTS_REG_CHARGER_MODE_EN, &regvalue);
	seq_printf(s, "charge state:0x%02x\n", regvalue);

	/*edge limit*/
	fts_read_reg(FTS_REG_EDGE_LIMIT, &regvalue);
	seq_printf(s, "edge Mode:0x%02x\n", regvalue);

	/*game mode*/
	fts_read_reg(FTS_REG_GAME_MODE_EN, &regvalue);
	seq_printf(s, "Game Mode:0x%02x\n", regvalue);

	/*FOD mode*/
	fts_read_reg(FTS_REG_FOD_EN, &regvalue);
	seq_printf(s, "FOD Mode:0x%02x\n", regvalue);

	/*Interrupt counter*/
	fts_read_reg(FTS_REG_INT_CNT, &regvalue);
	seq_printf(s, "INT count:0x%02x\n", regvalue);

	/*Flow work counter*/
	fts_read_reg(FTS_REG_FLOW_WORK_CNT, &regvalue);
	seq_printf(s, "ESD count:0x%02x\n", regvalue);

	/*Panel ID*/
	fts_read_reg(FTS_REG_MODULE_ID, &regvalue);
	seq_printf(s, "PANEL ID:0x%02x\n", regvalue);

	return;
}

#define SET_FTS_GESTURE(state, state_flag, config, config_flag)\
	if (CHK_BIT(state, (1 << state_flag))) {\
		SET_BIT(config, (1 << config_flag));\
	} else {\
		CLR_BIT(config, (1 << config_flag));\
	}

static int fts_enable_black_gesture(struct chip_data_ft3658u *ts_data,
                                    bool enable)
{
	u8 config1 = 0xff;
	int state = ts_data->gesture_state;

	SET_FTS_GESTURE(state, DOU_TAP, config1, 4);
	SET_FTS_GESTURE(state, SINGLE_TAP, config1, 7);
	TPD_INFO("MODE_GESTURE, write 0xD0=%d 0xD1=0x%x", enable, config1);

	if (enable && ts_data->black_gesture_indep) {
		fts_write_reg(FTS_REG_GESTURE_CONFIG1, config1);
		fts_write_reg(FTS_REG_GESTURE_EN, enable);
	} else {
		fts_write_reg(FTS_REG_GESTURE_EN, enable);
	}

	return 0;
}

static int fts_enable_edge_limit(struct chip_data_ft3658u *ts_data, int enable)
{
	u8 edge_mode = 0;

	/*0:Horizontal, 1:Vertical*/
	if (enable == VERTICAL_SCREEN) {
		edge_mode = 0;

	} else if (enable == LANDSCAPE_SCREEN_90) {
		edge_mode = 1;

	} else if (enable == LANDSCAPE_SCREEN_270) {
		edge_mode = 2;
	}

	TPD_INFO("MODE_EDGE, write 0x8C=%d", edge_mode);
	return fts_write_reg(FTS_REG_EDGE_LIMIT, edge_mode);
}

static int fts_enable_charge_mode(struct chip_data_ft3658u *ts_data, bool enable)
{
	TPD_INFO("MODE_CHARGE, write 0x8B=%d", enable);
	return fts_write_reg(FTS_REG_CHARGER_MODE_EN, enable);
}

static int fts_enable_game_mode(struct chip_data_ft3658u *ts_data, bool enable)
{
	TPD_INFO("MODE_GAME, write 0xC3=%d", enable);
	return fts_write_reg(FTS_REG_GAME_MODE_EN, enable);
}

static int fts_enable_headset_mode(struct chip_data_ft3658u *ts_data,
                                   bool enable)
{
	TPD_INFO("MODE_HEADSET, write 0xC3=%d \n", enable);
	return fts_write_reg(FTS_REG_HEADSET_MODE_EN, enable);
}

static int fts_mode_switch(void *chip_data, work_mode mode, int flag)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	int ret = 0;

	switch (mode) {
	case MODE_NORMAL:
		TPD_INFO("MODE_NORMAL");
		break;

	case MODE_SLEEP:
		TPD_INFO("MODE_SLEEP, write 0xA5=3");
		ret = fts_write_reg(FTS_REG_POWER_MODE, 0x03);

		if (ret < 0) {
			TPD_INFO("%s: enter into sleep failed.\n", __func__);
			goto mode_err;
		}

		break;

	case MODE_GESTURE:
		TPD_INFO("MODE_GESTURE, Melo, ts->is_suspended = %d \n",
		         ts_data->ts->is_suspended);

		if (ts_data->ts->is_suspended) {                             /* do not pull up reset when doing resume*/
			if (ts_data->last_mode == MODE_SLEEP) {
				fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);
			}
		}

		ret = fts_enable_black_gesture(ts_data, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable gesture failed.\n", __func__);
			goto mode_err;
		}

		break;

	/*    case MODE_GLOVE:*/
	/*        break;*/

	case MODE_EDGE:
		ret = fts_enable_edge_limit(ts_data, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable edg limit failed.\n", __func__);
			goto mode_err;
		}

		break;

	case MODE_FACE_DETECT:
		break;

	case MODE_CHARGE:
		ret = fts_enable_charge_mode(ts_data, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable charge mode failed.\n", __func__);
			goto mode_err;
		}

		break;

	case MODE_GAME:
		ret = fts_enable_game_mode(ts_data, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable game mode failed.\n", __func__);
			goto mode_err;
		}

		break;

	case MODE_HEADSET:
		ret = fts_enable_headset_mode(ts_data, flag);

		if (ret < 0) {
			TPD_INFO("%s: enable headset mode failed.\n", __func__);
			goto mode_err;
		}

		break;

	default:
		TPD_INFO("%s: Wrong mode.\n", __func__);
		goto mode_err;
	}

	ts_data->last_mode = mode;
	return 0;
mode_err:
	return ret;
}

/*
 * return success: 0; fail : negative
 */
static int fts_reset(void *chip_data)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;

	TPD_INFO("%s:call\n", __func__);
	fts_hw_reset(ts_data, RESET_TO_NORMAL_TIME);

	return 0;
}

static int  fts_reset_gpio_control(void *chip_data, bool enable)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	return fts_rstgpio_set(ts_data->hw_res, enable);
}

static int fts_get_vendor(void *chip_data, struct panel_info *panel_data)
{
	int len = 0;

	len = strlen(panel_data->fw_name);

	if ((len > 3) && (panel_data->fw_name[len - 3] == 'i') && \
	    (panel_data->fw_name[len - 2] == 'm')
	    && (panel_data->fw_name[len - 1] == 'g')) {
		TPD_INFO("tp_type = %d, panel_data->fw_name = %s\n", panel_data->tp_type,
		         panel_data->fw_name);
	}

	TPD_INFO("tp_type = %d, panel_data->fw_name = %s\n", panel_data->tp_type,
	         panel_data->fw_name);

	return 0;
}

static int fts_get_chip_info(void *chip_data)
{
	u8 cmd = 0;
	u8 id[2] = { 0 };

	fts_read_reg(FTS_REG_CHIP_ID, &id[0]);
	fts_read_reg(FTS_REG_CHIP_ID2, &id[1]);
	TPD_INFO("read chip id:0x%02x%02x", id[0], id[1]);

	if ((id[0] == FTS_VAL_CHIP_ID) && (id[1] == FTS_VAL_CHIP_ID2)) {
		return 0;
	}

	TPD_INFO("fw is invalid, need read boot id");
	cmd = 0x55;
	fts_write(&cmd, 1);
	msleep(12);
	cmd = 0x90;
	fts_read(&cmd, 1, id, 2);
	TPD_INFO("read boot id:0x%02x%02x", id[0], id[1]);

	if ((id[0] == FTS_VAL_BT_ID) && (id[1] == FTS_VAL_BT_ID2)) {
		return 0;
	}

	return 0;
}

static int fts_ftm_process(void *chip_data)
{
	int ret = 0;

	ret = fts_mode_switch(chip_data, MODE_SLEEP, true);

	if (ret < 0) {
		TPD_INFO("%s:switch mode to MODE_SLEEP fail", __func__);
		return ret;
	}

	ret = fts_power_control(chip_data, false);

	if (ret < 0) {
		TPD_INFO("%s:power on fail", __func__);
		return ret;
	}

	return 0;
}

static void fts_read_fod_info(struct chip_data_ft3658u *ts_data)
{
	int ret = 0;
	u8 cmd = FTS_REG_FOD_INFO;
	u8 val[FTS_REG_FOD_INFO_LEN] = { 0 };

	ret = fts_read(&cmd, 1, val, FTS_REG_FOD_INFO_LEN);

	if (ret < 0) {
		TPD_INFO("%s:read FOD info fail", __func__);
		return;
	}

	TPD_DEBUG("%s:FOD info buffer:%x %x %x %x %x %x %x %x %x", __func__, val[0],
	          val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8]);
	ts_data->fod_info.fp_id = val[0];
	ts_data->fod_info.event_type = val[1];

	if (val[8] == 0) {
		ts_data->fod_info.fp_down = 1;

	} else if (val[8] == 1) {
		ts_data->fod_info.fp_down = 0;
	}

	ts_data->fod_info.fp_area_rate = val[2];
	ts_data->fod_info.fp_x = (val[4] << 8) + val[5];
	ts_data->fod_info.fp_y = (val[6] << 8) + val[7];
}

static u32 fts_u32_trigger_reason(void *chip_data, int gesture_enable,
                                  int is_suspended)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	int ret = 0;
	u8 cmd = FTS_REG_POINTS;
	u32 result_event = 0;
	u8 *touch_buf = ts_data->touch_buf;
	u8 val = 0xFF;

	fts_prc_queue_work(ts_data);

	memset(touch_buf, 0xFF, FTS_MAX_POINTS_LENGTH);

	if (gesture_enable && is_suspended) {
		ret = fts_read_reg(FTS_REG_GESTURE_EN, &val);
		if (val == 0x01) {
			return IRQ_GESTURE;
		}
	}

	ret = fts_read(&cmd, 1, &touch_buf[0], ts_data->touch_size);
	if (ret < 0) {
		TPD_INFO("read touch point one fail");
		return IRQ_IGNORE;
	}

	if ((touch_buf[1] == 0xFF) && (touch_buf[2] == 0xFF) && (touch_buf[3] == 0xFF)) {
		TPD_INFO("Need recovery TP state");
		return IRQ_FW_AUTO_RESET;
	}

	/*confirm need print debug info*/
	if (touch_buf[0] != ts_data->irq_type) {
		SET_BIT(result_event, IRQ_FW_HEALTH);
	}

	ts_data->irq_type = touch_buf[0];

	/*normal touch*/
	SET_BIT(result_event, IRQ_TOUCH);
	TPD_DEBUG("%s, fgerprint, is_suspended = %d, fp_en = %d, ", __func__,
	          is_suspended, ts_data->fp_en);
	TPD_DEBUG("%s, fgerprint, touched = %d, event_type = %d, fp_down = %d, fp_down_report = %d, ",
	          __func__, ts_data->ts->view_area_touched, ts_data->fod_info.event_type,
	          ts_data->fod_info.fp_down, ts_data->fod_info.fp_down_report);

	if (!is_suspended && ts_data->fp_en) {
		fts_read_fod_info(ts_data);

		if ((ts_data->fod_info.event_type == FTS_EVENT_FOD)
		    && (ts_data->fod_info.fp_down)) {
			if (!ts_data->fod_info.fp_down_report) {    /* 38, 1, 0*/
				ts_data->fod_info.fp_down_report = 1;
				SET_BIT(result_event, IRQ_FINGERPRINT);
				TPD_DEBUG("%s, fgerprint, set IRQ_FINGERPRINT when fger down but not reported! \n",
				          __func__);
			}

			/*            if (ts_data->fod_info.fp_down_report) {      38, 1, 1*/
			/*            }*/

		} else if ((ts_data->fod_info.event_type == FTS_EVENT_FOD)
		           && (!ts_data->fod_info.fp_down)) {
			if (ts_data->fod_info.fp_down_report) {     /* 38, 0, 1*/
				ts_data->fod_info.fp_down_report = 0;
				SET_BIT(result_event, IRQ_FINGERPRINT);
				TPD_DEBUG("%s, fgerprint, set IRQ_FINGERPRINT when fger up but still reported! \n",
				          __func__);
			}

			/*                if (!ts_data->fod_info.fp_down_report) {     38, 0, 0*/
			/*                }*/
		}
	}

	return result_event;
}

static int fts_get_touch_points(void *chip_data, struct point_info *points,
                                int max_num)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	int i = 0;
	int obj_attention = 0;
	int base = 0;
	int event_num = 0;
	u8 finger_num = 0;
	u8 pointid = 0;
	u8 event_flag = 0;
	u8 *touch_buf = ts_data->touch_buf;

	finger_num = touch_buf[1] & 0xFF;

	if (finger_num > max_num) {
		TPD_INFO("invalid point_num(%d),max_num(%d)", finger_num, max_num);
		return -EIO;
	}

	for (i = 0; i < max_num; i++) {
		base = 6 * i;
		pointid = (touch_buf[4 + base]) >> 4;

		if (pointid >= FTS_MAX_ID) {
			break;

		} else if (pointid >= max_num) {
			TPD_INFO("ID(%d) beyond max_num(%d)", pointid, max_num);
			return -EINVAL;
		}

		event_num++;
		if (!ts_data->high_resolution_support && !ts_data->high_resolution_support_x8) {
			points[pointid].x = ((touch_buf[2 + base] & 0x0F) << 8) + (touch_buf[3 + base] & 0xFF);
			points[pointid].y = ((touch_buf[4 + base] & 0x0F) << 8) + (touch_buf[5 + base] & 0xFF);
			points[pointid].touch_major = touch_buf[7 + base];
			points[pointid].width_major = touch_buf[7 + base];
			points[pointid].z =  touch_buf[7 + base];
			event_flag = (touch_buf[2 + base] >> 6);
		} else if (ts_data->high_resolution_support_x8) {
			points[pointid].x = (((touch_buf[2 + base] & 0x0F) << 11) +
			                     ((touch_buf[3 + base] & 0xFF) << 3) +
			                     ((touch_buf[6 + base] >> 5) & 0x07));
			points[pointid].y = (((touch_buf[4 + base] & 0x0F) << 11) +
			                     ((touch_buf[5 + base] & 0xFF) << 3) +
			                     ((touch_buf[6 + base] >> 2) & 0x07));
			points[pointid].touch_major = touch_buf[7 + base];
			points[pointid].width_major = touch_buf[7 + base];
			points[pointid].z =  touch_buf[7 + base];
			event_flag = (touch_buf[2 + base] >> 6);
		}

		points[pointid].status = 0;

		if ((event_flag == 0) || (event_flag == 2)) {
			points[pointid].status = 1;
			obj_attention |= (1 << pointid);

			if (finger_num == 0) {
				TPD_INFO("abnormal touch data from fw");
				return -EIO;
			}
		}
	}

	if (event_num == 0) {
		TPD_INFO("no touch point information");
		return -EIO;
	}

	if (ts_data->touch_analysis_support && ts_data->ta_flag) {
		ts_data->ta_flag = 0;
		ts_data->ta_size = ts_data->touch_size;
		if (ts_data->ta_buf && ts_data->ta_size)
			memcpy(ts_data->ta_buf, ts_data->touch_buf, ts_data->ta_size);
		wake_up_interruptible(&ts_data->ts_waitqueue);
	}

	return obj_attention;
}

static void fts_health_report(void *chip_data, struct monitor_data *mon_data)
{
	int ret = 0;
	u8 val = 0;

	ret = fts_read_reg(0x01, &val);
	TPD_INFO("Health register(0x01):0x%x", val);
	ret = fts_read_reg(FTS_REG_HEALTH_1, &val);
	TPD_INFO("Health register(0xFD):0x%x", val);
	ret = fts_read_reg(FTS_REG_HEALTH_2, &val);
	TPD_INFO("Health register(0xFE):0x%x", val);
}

static int fts_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	int ret = 0;
	u8 cmd = FTS_REG_GESTURE_OUTPUT_ADDRESS;
	u8 buf[FTS_GESTURE_DATA_LEN] = { 0 };
	u8 gesture_id = 0;
	u8 point_num = 0;

	ret = fts_read(&cmd, 1, &buf[2], FTS_GESTURE_DATA_LEN - 2);

	if (ret < 0) {
		TPD_INFO("read gesture data fail");
		return ret;
	}

	gesture_id = buf[2];
	point_num = buf[3];
	TPD_INFO("gesture_id=%d, point_num=%d", gesture_id, point_num);

	switch (gesture_id) {
	case GESTURE_DOUBLE_TAP:
		gesture->gesture_type = DOU_TAP;
		break;

	case GESTURE_UP_VEE:
		gesture->gesture_type = UP_VEE;
		break;

	case GESTURE_DOWN_VEE:
		gesture->gesture_type = DOWN_VEE;
		break;

	case GESTURE_LEFT_VEE:
		gesture->gesture_type = LEFT_VEE;
		break;

	case GESTURE_RIGHT_VEE:
		gesture->gesture_type = RIGHT_VEE;
		break;

	case GESTURE_O_CLOCKWISE:
		gesture->clockwise = 1;
		gesture->gesture_type = CIRCLE_GESTURE;
		break;

	case GESTURE_O_ANTICLOCK:
		gesture->clockwise = 0;
		gesture->gesture_type = CIRCLE_GESTURE;
		break;

	case GESTURE_DOUBLE_SWIP:
		gesture->gesture_type = DOU_SWIP;
		break;

	case GESTURE_LEFT2RIGHT_SWIP:
		gesture->gesture_type = LEFT2RIGHT_SWIP;
		break;

	case GESTURE_RIGHT2LEFT_SWIP:
		gesture->gesture_type = RIGHT2LEFT_SWIP;
		break;

	case GESTURE_UP2DOWN_SWIP:
		gesture->gesture_type = UP2DOWN_SWIP;
		break;

	case GESTURE_DOWN2UP_SWIP:
		gesture->gesture_type = DOWN2UP_SWIP;
		break;

	case GESTURE_M:
		gesture->gesture_type = M_GESTRUE;
		break;

	case GESTURE_W:
		gesture->gesture_type = W_GESTURE;
		break;

	case GESTURE_HEART_CLOCKWISE:
		gesture->clockwise = 1;
		gesture->gesture_type = HEART;
		break;
	case GESTURE_HEART_ANTICLOCK:
		gesture->clockwise = 0;
		gesture->gesture_type = HEART;
		break;

	case GESTURE_FINGER_PRINT:
		fts_read_fod_info(ts_data);
		TPD_INFO("FOD event type:0x%x", ts_data->fod_info.event_type);
		TPD_DEBUG("%s, fgerprint, touched = %d, fp_down = %d, fp_down_report = %d, \n",
		          __func__, ts_data->ts->view_area_touched, ts_data->fod_info.fp_down,
		          ts_data->fod_info.fp_down_report);

		if (ts_data->fod_info.event_type == FTS_EVENT_FOD) {
			if (ts_data->fod_info.fp_down && !ts_data->fod_info.fp_down_report) {
				gesture->gesture_type = FINGER_PRINTDOWN;
				ts_data->fod_info.fp_down_report = 1;

			} else if (!ts_data->fod_info.fp_down && ts_data->fod_info.fp_down_report) {
				gesture->gesture_type = FRINGER_PRINTUP;
				ts_data->fod_info.fp_down_report = 0;
			}

			gesture->Point_start.x = ts_data->fod_info.fp_x;
			gesture->Point_start.y = ts_data->fod_info.fp_y;
			gesture->Point_end.x = ts_data->fod_info.fp_area_rate;
			gesture->Point_end.y = 0;
		}

		break;

	case GESTURE_SINGLE_TAP:
		gesture->gesture_type = SINGLE_TAP;
		break;

	default:
		gesture->gesture_type = UNKOWN_GESTURE;
	}

	if ((gesture->gesture_type != FINGER_PRINTDOWN)
	    && (gesture->gesture_type != FRINGER_PRINTUP)
	    && (gesture->gesture_type != UNKOWN_GESTURE)) {
		gesture->Point_start.x = (u16)((buf[4] << 8) + buf[5]);
		gesture->Point_start.y = (u16)((buf[6] << 8) + buf[7]);
		gesture->Point_end.x = (u16)((buf[8] << 8) + buf[9]);
		gesture->Point_end.y = (u16)((buf[10] << 8) + buf[11]);
		gesture->Point_1st.x = (u16)((buf[12] << 8) + buf[13]);
		gesture->Point_1st.y = (u16)((buf[14] << 8) + buf[15]);
		gesture->Point_2nd.x = (u16)((buf[16] << 8) + buf[17]);
		gesture->Point_2nd.y = (u16)((buf[18] << 8) + buf[19]);
		gesture->Point_3rd.x = (u16)((buf[20] << 8) + buf[21]);
		gesture->Point_3rd.y = (u16)((buf[22] << 8) + buf[23]);
		gesture->Point_4th.x = (u16)((buf[24] << 8) + buf[25]);
		gesture->Point_4th.y = (u16)((buf[26] << 8) + buf[27]);
	}

	return 0;
}

static void fts_enable_fingerprint_underscreen(void *chip_data, uint32_t enable)
{
	int ret = 0;
	u8 val = 0xFF;
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;


	TPD_INFO("%s:enable=%d", __func__, enable);
	ret = fts_read_reg(FTS_REG_FOD_EN, &val);

	if (ret < 0) {
		TPD_INFO("%s: read FOD enable(%x) fail", __func__, FTS_REG_FOD_EN);
		return;
	}

	TPD_DEBUG("%s, fgerprint, touched = %d, event_type = %d, fp_down = %d. fp_down_report = %d \n",
	          __func__, ts_data->ts->view_area_touched, ts_data->fod_info.event_type,
	          ts_data->fod_info.fp_down, ts_data->fod_info.fp_down_report);

	if (enable) {
		val |= 0x02;
		ts_data->fp_en = 1;

		if ((!ts_data->ts->view_area_touched)
		    && (ts_data->fod_info.event_type != FTS_EVENT_FOD)
		    && (!ts_data->fod_info.fp_down)
		    && (ts_data->fod_info.fp_down_report)) {   /* notouch, !38, 0, 1*/
			ts_data->fod_info.fp_down_report = 0;
			TPD_DEBUG("%s, fgerprint, fp_down_report status abnormal (notouch, 38!, 0, 1), needed to be reseted! \n",
			          __func__);
		}

	} else {
		val &= 0xFD;
		ts_data->fp_en = 0;
		ts_data->fod_info.fp_down = 0;
		ts_data->fod_info.event_type = 0;
		/*        ts_data->fod_info.fp_down_report = 0;*/
	}

	TPD_INFO("%s:write %x=%x.", __func__, FTS_REG_FOD_EN, val);
	ret = fts_write_reg(FTS_REG_FOD_EN, val);

	if (ret < 0) {
		TPD_INFO("%s: write FOD enable(%x=%x) fail", __func__, FTS_REG_FOD_EN, val);
	}
}

static void fts_screenon_fingerprint_info(void *chip_data,
        struct fp_underscreen_info *fp_tpinfo)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;

	memset(fp_tpinfo, 0, sizeof(struct fp_underscreen_info));
	TPD_INFO("FOD event type:0x%x", ts_data->fod_info.event_type);

	if (ts_data->fod_info.fp_down) {
		fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;

	} else {
		fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;
	}

	fp_tpinfo->area_rate = ts_data->fod_info.fp_area_rate;
	fp_tpinfo->x = ts_data->fod_info.fp_x;
	fp_tpinfo->y = ts_data->fod_info.fp_y;

	TPD_INFO("FOD Info:touch_state:%d,area_rate:%d,x:%d,y:%d[fp_down:%d]",
	         fp_tpinfo->touch_state, fp_tpinfo->area_rate, fp_tpinfo->x,
	         fp_tpinfo->y, ts_data->fod_info.fp_down);
}

static void fts_register_info_read(void *chip_data, uint16_t register_addr,
                                   uint8_t *result, uint8_t length)
{
	u8 addr = (u8)register_addr;

	fts_read(&addr, 1, result, length);
}

static void fts_set_touch_direction(void *chip_data, uint8_t dir)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	ts_data->touch_direction = dir;
}

static uint8_t fts_get_touch_direction(void *chip_data)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;
	return ts_data->touch_direction;
}

static int fts_smooth_lv_set(void *chip_data, int level)
{
	TPD_INFO("set smooth lv to %d", level);

	return fts_write_reg(FTS_REG_SMOOTH_LEVEL, level);
}

static int fts_sensitive_lv_set(void *chip_data, int level)
{
	int ret = 0;

	TPD_INFO("set sensitive lv to %d", level);

	ret = fts_write_reg(FTS_REG_STABLE_DISTANCE_AFTER_N, level);
	if (ret < 0) {
		TPD_INFO("write FTS_REG_STABLE_DISTANCE_AFTER_N fail");
		return ret;
	}

	ret = fts_write_reg(FTS_REG_STABLE_DISTANCE, level);
	if (ret < 0) {
		TPD_INFO("write FTS_REG_STABLE_DISTANCE fail");
		return ret;
	}

	return 0;
}

static void fts_set_gesture_state(void *chip_data, int state)
{
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)chip_data;

	ts_data->gesture_state = state;
}

static int fts_set_high_frame_rate(void *chip_data, int level, int time)
{
	int ret = 0;

	TPD_INFO("set high_frame_rate to %d, keep %ds", level, time);
	if (level > 0) {
		TPD_INFO("Enter high_frame mode, MODE_GAME, write 0xC3=%d, MODE_HIGH_FRAME write 0x8E=%d, HIGH_FRAME_TIME write 0x8A=%d", true, true, time);
		ret = fts_write_reg(FTS_REG_GAME_MODE_EN, true);
		if (ret < 0) {
			return ret;
		}
		ret = fts_write_reg(FTS_REG_HIGH_FRAME_EN, true);
		if (ret < 0) {
			return ret;
		}
		ret = fts_write_reg(FTS_REG_HIGH_FRAME_TIME, time);
		if (ret < 0) {
			return ret;
		}
	} else {
		TPD_INFO("Exit high_frame mode write 0x8E=%d", false);
		ret = fts_write_reg(FTS_REG_HIGH_FRAME_EN, false);
		if (ret < 0) {
			return ret;
		}
	}
	return ret;
}

static int fts_refresh_switch(void *chip_data, int fps)
{
	TPD_INFO("lcd fps =%d", fps);
	/*  FTS_REG_GAME_MODE_EN also config as report rate reg */
	return fts_write_reg(FTS_REG_GAME_MODE_EN,
		fps == 60 ? FTS_120HZ_REPORT_RATE : FTS_180HZ_REPORT_RATE);
}

static int ft3658u_parse_dts(struct chip_data_ft3658u *ts_data, struct spi_device *spi)
{
	struct device *dev;
	struct device_node *np;

	dev = &spi->dev;
	np = dev->of_node;

	ts_data->high_resolution_support = of_property_read_bool(np, "high_resolution_support");
	ts_data->high_resolution_support_x8 = of_property_read_bool(np, "high_resolution_support_x8");
	TPD_INFO("%s:high_resolution_support is:%d %d\n", __func__, ts_data->high_resolution_support,
	         ts_data->high_resolution_support_x8);

	return 0;
}

int fts_set_spi_max_speed(unsigned int speed, char mode)
{
	int rc;
	struct spi_device *spi = g_fts_data->ft_spi;

	if (mode) {
		spi->max_speed_hz = speed;
	} else {
		spi->max_speed_hz = g_fts_data->spi_speed;
	}

	rc = spi_setup(spi);
	if (rc) {
		TPD_INFO("%s: spi setup fail\n", __func__);
		return rc;
	}
	return rc;
}

static struct oplus_touchpanel_operations fts_ops = {
	.power_control              = fts_power_control,
	.get_vendor                 = fts_get_vendor,
	.get_chip_info              = fts_get_chip_info,
	.fw_check                   = fts_fw_check,
	.mode_switch                = fts_mode_switch,
	.reset                      = fts_reset,
	.reset_gpio_control         = fts_reset_gpio_control,
	.fw_update                  = fts_fw_update,
	.trigger_reason             = fts_u32_trigger_reason,
	.get_touch_points           = fts_get_touch_points,
	.health_report              = fts_health_report,
	.get_gesture_info           = fts_get_gesture_info,
	.ftm_process                = fts_ftm_process,
	.enable_fingerprint         = fts_enable_fingerprint_underscreen,
	.screenon_fingerprint_info  = fts_screenon_fingerprint_info,
	.register_info_read         = fts_register_info_read,
	.set_touch_direction        = fts_set_touch_direction,
	.get_touch_direction        = fts_get_touch_direction,
	.esd_handle                 = fts_esd_handle,
	.smooth_lv_set              = fts_smooth_lv_set,
	.sensitive_lv_set           = fts_sensitive_lv_set,
	.set_gesture_state          = fts_set_gesture_state,
	.tp_refresh_switch          = fts_refresh_switch,
	.set_high_frame_rate        = fts_set_high_frame_rate,
};

static struct focal_auto_test_operations ft3658u_test_ops = {
	.auto_test_preoperation = ft3658u_auto_preoperation,
	.test1 = ft3658u_rawdata_autotest,
	.test2 = ft3658u_uniformity_autotest,
	.test3 = ft3658u_scap_cb_autotest,
	.test4 = ft3658u_scap_rawdata_autotest,
	.test5 = ft3658u_short_test,
	.test6 = ft3658u_noise_autotest,
	.auto_test_endoperation = ft3658u_auto_endoperation,
};


static struct engineer_test_operations ft3658u_engineer_test_ops = {
	.auto_test              = focal_auto_test,
};

static struct debug_info_proc_operations fts_debug_info_proc_ops = {
	.delta_read             = fts_delta_read,
	.baseline_read          = fts_baseline_read,
	.main_register_read     = fts_main_register_read,
};

struct focal_debug_func focal_debug_ops = {
	.esd_check_enable       = focal_esd_check_enable,
	.get_esd_check_flag     = focal_get_esd_check_flag,
	.get_fw_version         = focal_get_fw_version,
	.dump_reg_sate          = focal_dump_reg_state,
};

static int fts_tp_probe(struct spi_device *spi)
{
	struct chip_data_ft3658u *ts_data = NULL;
	struct touchpanel_data *ts = NULL;
	u64 time_counter = 0;
	int ret = -1;

	TPD_INFO("%s  is called\n", __func__);

	reset_healthinfo_time_counter(&time_counter);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	spi->chip_select = 0; /*modify reg=0 for more tp vendor share same spi interface*/
	ret = spi_setup(spi);
	if (ret) {
		TPD_INFO("spi setup fail");
		return ret;
	}

	/*step1:Alloc chip_info*/
	ts_data = kzalloc(sizeof(struct chip_data_ft3658u), GFP_KERNEL);

	if (ts_data == NULL) {
		TPD_INFO("ts_data kzalloc error\n");
		ret = -ENOMEM;
		return ret;
	}

	memset(ts_data, 0, sizeof(*ts_data));
	ts_data->spi_speed = spi->max_speed_hz;
	g_fts_data = ts_data;

	ts_data->ts_workqueue = create_singlethread_workqueue("fts_wq");
	if (!ts_data->ts_workqueue) {
		TPD_INFO("create fts workqueue fail");
	}
	init_waitqueue_head(&ts_data->ts_waitqueue);

	ret = fts_bus_init(ts_data);
	if (ret < 0) {
		TPD_INFO("bus init error\n");
		goto ts_malloc_failed;
	}

	ts_data->touch_buf = (u8 *)kzalloc(FTS_MAX_TOUCH_BUF, GFP_KERNEL);
	if (!ts_data->touch_buf) {
		TPD_INFO("failed to alloc memory for touch buf");
		ret = -ENOMEM;
		goto err_bus_init;
	}
	ts_data->touch_size = FTS_MAX_POINTS_LENGTH;
	ts_data->touch_analysis_support = 0;
	ts_data->ta_flag = 0;
	ts_data->ta_size = 0;

	fts_point_report_check_init(ts_data);

	/*step2:Alloc common ts*/
	ts = common_touch_data_alloc();

	if (ts == NULL) {
		TPD_INFO("ts kzalloc error\n");
		ret = -ENOMEM;
		goto err_report_buf;
	}

	memset(ts, 0, sizeof(*ts));

	/*step3:binding client && dev for easy operate*/
	ts_data->ft_spi = spi;
	ts_data->hw_res = &ts->hw_res;
	ts_data->ts = ts;
	ts->debug_info_ops = &fts_debug_info_proc_ops;
	ts->s_client = spi;
	ts->bus_type = TP_BUS_SPI;
	ts->irq = spi->irq;
	ts->dev = &spi->dev;
	ts->chip_data = ts_data;
	spi_set_drvdata(spi, ts);

	/*step4:file_operations callback binding*/
	ts->ts_ops = &fts_ops;
	ts->engineer_ops = &ft3658u_engineer_test_ops;
	ts->com_test_data.chip_test_ops = &ft3658u_test_ops;

	ts->private_data = &focal_debug_ops;
	ft3658u_parse_dts(ts_data, spi);

	/*step5:register common touch*/
	ret = register_common_touch_device(ts);

	if (ret < 0) {
		goto err_register_driver;
	}

	ts_data->black_gesture_indep = ts->black_gesture_indep_support;
	ts_data->monitor_data = &ts->monitor_data;
	/*step6:create ftxxxx-debug related proc files*/
	fts_create_apk_debug_channel(ts_data);
	fts_create_sysfs(ts_data);

	/*step7:Chip Related function*/
	focal_create_sysfs_spi(spi);

	if (ts->health_monitor_support) {
		tp_healthinfo_report(&ts->monitor_data, HEALTH_PROBE, &time_counter);
	}
	ts_data->probe_done = 1;
	TPD_INFO("%s, probe normal end\n", __func__);

	return 0;

err_register_driver:
	common_touch_data_free(ts);
	ts = NULL;

err_report_buf:
	kfree(ts_data->touch_buf);
	ts_data->touch_buf = NULL;

err_bus_init:
	kfree(ts_data->bus_tx_buf);
	ts_data->bus_tx_buf = NULL;
	kfree(ts_data->bus_rx_buf);
	ts_data->bus_rx_buf = NULL;

ts_malloc_failed:
	kfree(ts_data);
	ts_data = NULL;

	TPD_INFO("%s, probe error\n", __func__);

	return ret;
}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
static void fts_tp_remove(struct spi_device *spi)
#else
static int fts_tp_remove(struct spi_device *spi)
#endif
{
	struct touchpanel_data *ts = spi_get_drvdata(spi);
	struct chip_data_ft3658u *ts_data = (struct chip_data_ft3658u *)ts->chip_data;

	TPD_INFO("%s is called\n", __func__);
	fts_point_report_check_exit(ts_data);
	fts_release_apk_debug_channel(ts_data);
	fts_remove_sysfs(ts_data);
	fts_bus_exit(ts_data);
	kfree(ts_data->touch_buf);
	ts_data->touch_buf = NULL;

	kfree(ts_data);
	ts_data = NULL;

	kfree(ts);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
#else
	return 0;
#endif
}

static int fts_spi_suspend(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called\n", __func__);
	tp_pm_suspend(ts);

	return 0;
}

static int fts_spi_resume(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s is called\n", __func__);
	tp_pm_resume(ts);

	return 0;
}

static const struct spi_device_id tp_id[] = {
	{ TPD_DEVICE, 0 },
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
	{ "oplus,tp_noflash", 0 },
#endif
	{ }
};

static struct of_device_id tp_match_table[] = {
	{ .compatible = TPD_DEVICE, },
#ifdef CONFIG_TOUCHPANEL_MTK_PLATFORM
	{ .compatible = "oplus,tp_noflash", },
#endif
	{ }
};

static const struct dev_pm_ops tp_pm_ops = {
	.suspend = fts_spi_suspend,
	.resume = fts_spi_resume,
};

static struct spi_driver fts_ts_driver = {
	.probe          = fts_tp_probe,
	.remove         = fts_tp_remove,
	.id_table   = tp_id,
	.driver         = {
		.name   = TPD_DEVICE,
		.of_match_table =  tp_match_table,
		.pm = &tp_pm_ops,
	},
};

static int __init tp_driver_init_ft3658u(void)
{
	TPD_INFO("%s is called\n", __func__);

	if (!tp_judge_ic_match(TPD_DEVICE)) {
		TPD_INFO("%s not match\n", __func__);
		return 0;
	}

	if (spi_register_driver(&fts_ts_driver) != 0) {
		TPD_INFO("unable to add spi driver.\n");
		return 0;
	}

	return 0;
}

/* should never be called */
static void __exit tp_driver_exit_ft3658u(void)
{
	spi_unregister_driver(&fts_ts_driver);
	return;
}

#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(tp_driver_init_ft3658u);
#else
module_init(tp_driver_init_ft3658u);
#endif
module_exit(tp_driver_exit_ft3658u);

MODULE_DESCRIPTION("Touchscreen ft3658u Driver");
MODULE_LICENSE("GPL");
