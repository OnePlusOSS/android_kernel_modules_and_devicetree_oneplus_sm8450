/**************************************************************
 * Copyright (c)  2008- 2030  Oplus Mobile communication Corp.ltd.
 * File       : goodix_drivers_brl.c
 * Description: Source file for Goodix GT9897 driver
 * Version   : 1.0
 * Date        : 2019-08-27
 * Author    : bsp
 * TAG         : BSP.TP.Init
 * ---------------- Revision History: --------------------------
 *   <version>    <date>          < author >                            <desc>
 ****************************************************************/
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>
#include <linux/of_gpio.h>

#include "goodix_brl_core.h"

#define SPI_TRANS_PREFIX_LEN    1
#define REGISTER_WIDTH          4
#define SPI_READ_DUMMY_LEN      3
#define SPI_READ_PREFIX_LEN	\
		(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH + SPI_READ_DUMMY_LEN)
#define SPI_WRITE_PREFIX_LEN	(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH)

#define SPI_WRITE_FLAG		    0xF0
#define SPI_READ_FLAG		    0xF1

#define GOODIX_BUS_RETRY_TIMES  3

int gt8x_rawdiff_mode;

#define SPI_TRANS_PREFIX_LEN    1
#define REGISTER_WIDTH          4
#define SPI_READ_DUMMY_LEN      3
#define SPI_READ_PREFIX_LEN  	(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH + SPI_READ_DUMMY_LEN)
#define SPI_WRITE_PREFIX_LEN 	(SPI_TRANS_PREFIX_LEN + REGISTER_WIDTH)

#define SPI_WRITE_FLAG          0xF0
#define SPI_READ_FLAG           0xF1

/* shortcircuit info */
#define CHN_VDD					0xFF
#define CHN_GND					0x7F
#define DRV_CHANNEL_FLAG		0x80

#define MAX_TEST_TIME_MS        15000
#define DEFAULT_TEST_TIME_MS	7000

#define MAX_DRV_NUM_BRB				    	52
#define MAX_SEN_NUM_BRB				    	75
#define SHORT_TEST_TIME_REG_BRB				0x26AE0
#define DFT_ADC_DUMP_NUM_BRB				762
#define DFT_SHORT_THRESHOLD_BRB				100
#define DFT_DIFFCODE_SHORT_THRESHOLD_BRB	32
#define SHORT_TEST_STATUS_REG_BRB			0x20400
#define SHORT_TEST_RESULT_REG_BRB			0x20410
#define DRV_DRV_SELFCODE_REG_BRB			0x2049A
#define SEN_SEN_SELFCODE_REG_BRB 			0x21AF2
#define DRV_SEN_SELFCODE_REG_BRB			0x248A6
#define DIFF_CODE_DATA_REG_BRB				0x269E0

#define SHORT_TEST_FINISH_FLAG  			0x88
#define SHORT_TEST_THRESHOLD_REG			0x20402
#define SHORT_TEST_RUN_REG					0x10400
#define SHORT_TEST_RUN_FLAG					0xAA
#define INSPECT_FW_SWITCH_CMD				0x85

static u8 brl_b_drv_map[] = {
	75, 76, 77, 78, 79, 80, 81, 82,
	83, 84, 85, 86, 87, 88, 89, 90,
	91, 92, 93, 94, 95, 96, 97, 98,
	99, 100, 101, 102, 103, 104, 105,
	106, 107, 108, 109, 110, 111, 112,
	113, 114, 115, 116, 117, 118, 119,
	120, 121, 122, 123, 124, 125, 126
};

static u8 brl_b_sen_map[] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
	11, 12, 13, 14, 15, 16, 17, 18,
	19, 20, 21, 22, 23, 24, 25, 26,
	27, 28, 29, 30, 31, 32, 33, 34,
	35, 36, 37, 38, 39, 40, 41, 42,
	43, 44, 45, 46, 47, 48, 49, 50,
	51, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, 62, 63, 64, 65, 66,
	67, 68, 69, 70, 71, 72, 73, 74
};

typedef struct __attribute__((packed)) {
	u8 result;
	u8 drv_drv_num;
	u8 sen_sen_num;
	u8 drv_sen_num;
	u8 drv_gnd_avdd_num;
	u8 sen_gnd_avdd_num;
	u16 checksum;
} test_result_t;

static int cal_cha_to_cha_res(int v1, int v2)
{
	return (v1 - v2) * 74 / v2 + 20;
}

static int cal_cha_to_avdd_res(int v1, int v2)
{
	return 64 * (2 * v2 - 25) * 99 / v1 - 60;
}

static int cal_cha_to_gnd_res(int v)
{
	return 150500 / v - 60;
}

static int goodix_reset(void *chip_data);


int goodix_spi_read(struct chip_data_brl *chip_info, unsigned int addr,
		    unsigned char *data, unsigned int len)
{
	struct spi_device *spi = chip_info->s_client;
	u8 *rx_buf = NULL;
	u8 *tx_buf = NULL;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	int ret = 0;

	rx_buf = kzalloc(SPI_READ_PREFIX_LEN + len, GFP_KERNEL);
	if (!rx_buf) {
		TPD_INFO("rx_buf kzalloc error\n");
		ret = -ENOMEM;
		return ret;
	}
	tx_buf = kzalloc(SPI_READ_PREFIX_LEN + len, GFP_KERNEL);
	if (!tx_buf) {
		TPD_INFO("tx_buf kzalloc error\n");
		ret = -ENOMEM;
		goto err_tx_buf_alloc;
	}

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	/*spi_read tx_buf format: 0xF1 + addr(4bytes) + data*/
	tx_buf[0] = SPI_READ_FLAG;
	tx_buf[1] = (addr >> 24) & 0xFF;
	tx_buf[2] = (addr >> 16) & 0xFF;
	tx_buf[3] = (addr >> 8) & 0xFF;
	tx_buf[4] = addr & 0xFF;
	tx_buf[5] = 0xFF;
	tx_buf[6] = 0xFF;
	tx_buf[7] = 0xFF;

	xfers.tx_buf = tx_buf;
	xfers.rx_buf = rx_buf;
	xfers.len = SPI_READ_PREFIX_LEN + len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);
	ret = spi_sync(spi, &spi_msg);
	if (ret < 0) {
		TPD_INFO("spi transfer error:%d\n", ret);
		goto exit;
	}
	memcpy(data, &rx_buf[SPI_READ_PREFIX_LEN], len);

exit:
	kfree(tx_buf);
err_tx_buf_alloc:
	kfree(rx_buf);

	return ret;
}

int goodix_spi_write(struct chip_data_brl *chip_info, unsigned int addr,
		     unsigned char *data, unsigned int len)
{
	struct spi_device *spi = chip_info->s_client;
	u8 *tx_buf = NULL;
	struct spi_transfer xfers;
	struct spi_message spi_msg;
	int ret = 0;

	tx_buf = kzalloc(SPI_WRITE_PREFIX_LEN + len, GFP_KERNEL);
	if (!tx_buf) {
		TPD_INFO("alloc tx_buf failed, size:%d\n",
			 SPI_WRITE_PREFIX_LEN + len);
		return -ENOMEM;
	}

	spi_message_init(&spi_msg);
	memset(&xfers, 0, sizeof(xfers));

	tx_buf[0] = SPI_WRITE_FLAG;
	tx_buf[1] = (addr >> 24) & 0xFF;
	tx_buf[2] = (addr >> 16) & 0xFF;
	tx_buf[3] = (addr >> 8) & 0xFF;
	tx_buf[4] = addr & 0xFF;
	memcpy(&tx_buf[SPI_WRITE_PREFIX_LEN], data, len);
	xfers.tx_buf = tx_buf;
	xfers.len = SPI_WRITE_PREFIX_LEN + len;
	xfers.cs_change = 0;
	spi_message_add_tail(&xfers, &spi_msg);
	ret = spi_sync(spi, &spi_msg);
	if (ret < 0) {
		TPD_INFO("spi transfer error:%d\n", ret);
	}

	kfree(tx_buf);
	return ret;
}

static int goodix_reg_read(struct chip_data_brl *chip_info, u32 addr,
			   u8 *data, u32 len)
{
	return goodix_spi_read(chip_info, addr, data, len);
}

static int goodix_reg_write(struct chip_data_brl *chip_info, u32 addr,
			    u8 *data, u32 len)
{
	return goodix_spi_write(chip_info, addr, data, len);
}

static int goodix_reg_write_confirm(struct chip_data_brl *chip_info,
				    u32 addr, u8 *data, u32 len)
{
	u8 *cfm, cfm_buf[32];
	int r, i;

	if (len > sizeof(cfm_buf)) {
		cfm = kzalloc(len, GFP_KERNEL);
		if (!cfm) {
			TPD_INFO("Mem alloc failed\n");
			return -ENOMEM;
		}
	} else {
		cfm = &cfm_buf[0];
	}

	for (i = 0; i < 3; i++) {
		r = goodix_reg_write(chip_info, addr, data, len);
		if (r < 0) {
			goto exit;
		}
		r = goodix_reg_read(chip_info, addr, cfm, len);
		if (r < 0) {
			goto exit;
		}

		if (memcmp(data, cfm, len)) {
			r = -EMEMCMP;
			continue;
		} else {
			r = 0;
			break;
		}
	}

exit:
	if (cfm != &cfm_buf[0]) {
		kfree(cfm);
	}
	return r;
}

/*****************************************************************************
* goodix_append_checksum
* @summary
*    Calcualte data checksum with the specified mode.
*
* @param data
*   data need to be calculate
* @param len
*   data length
* @param mode
*   calculate for u8 or u16 checksum
* @return
*   return the data checksum value.
*
*****************************************************************************/
u32 goodix_append_checksum(u8 *data, int len, int mode)
{
	u32 checksum = 0;
	int i;

	checksum = 0;
	if (mode == CHECKSUM_MODE_U8_LE) {
		for (i = 0; i < len; i++) {
			checksum += data[i];
		}
	} else {
		for (i = 0; i < len; i+=2) {
			checksum += (data[i] + (data[i+1] << 8));
		}
	}

	if (mode == CHECKSUM_MODE_U8_LE) {
		data[len] = checksum & 0xff;
		data[len + 1] = (checksum >> 8) & 0xff;
		return 0xFFFF & checksum;
	}
	data[len] = checksum & 0xff;
	data[len + 1] = (checksum >> 8) & 0xff;
	data[len + 2] = (checksum >> 16) & 0xff;
	data[len + 3] = (checksum >> 24) & 0xff;
	return checksum;
}

/* checksum_cmp: check data valid or not
 * @data: data need to be check
 * @size: data length need to be check(include the checksum bytes)
 * @mode: compare with U8 or U16 mode
 * */
int checksum_cmp(const u8 *data, int size, int mode)
{
	u32 cal_checksum = 0;
	u32 r_checksum = 0;
	u32 i;

	if (mode == CHECKSUM_MODE_U8_LE) {
		if (size < 2) {
			return 1;
		}
		for (i = 0; i < size - 2; i++) {
			cal_checksum += data[i];
		}

		r_checksum = data[size - 2] + (data[size - 1] << 8);
		return (cal_checksum & 0xFFFF) == r_checksum ? 0 : 1;
	}

	if (size < 4) {
		return 1;
	}
	for (i = 0; i < size - 4; i += 2) {
		cal_checksum += data[i] + (data[i + 1] << 8);
	}
	r_checksum = data[size - 4] + (data[size - 3] << 8) +
		     (data[size - 2] << 16) + (data[size - 1] << 24);
	return cal_checksum == r_checksum ? 0 : 1;
}

static void goodix_rotate_abcd2cbad(int tx, int rx, s16 *data)
{
	s16 *temp_buf = NULL;
	int size = tx * rx;
	int i;
	int j;
	int col;

	temp_buf = kcalloc(size, sizeof(s16), GFP_KERNEL);
	if (!temp_buf)
		return;

	for (i = 0, j = 0, col = 0; i < size; i++) {
		temp_buf[i] = data[j++ * rx + col];
		if (j == tx) {
			j = 0;
			col++;
		}
	}

	memcpy(data, temp_buf, size * sizeof(s16));
	kfree(temp_buf);
}

/* command ack info */
#define CMD_ACK_IDLE             0x01
#define CMD_ACK_BUSY             0x02
#define CMD_ACK_BUFFER_OVERFLOW  0x03
#define CMD_ACK_CHECKSUM_ERROR   0x04
#define CMD_ACK_OK               0x80

#define GOODIX_CMD_RETRY 6
static int brl_send_cmd(struct chip_data_brl *chip_info,
			struct goodix_ts_cmd *cmd)
{
	int ret, retry, i;
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ic_info_misc *misc = &chip_info->ic_info.misc;

	if (!misc->cmd_addr) {
		TPD_INFO("%s: invalied cmd addr\n", __func__);
		return -1;
	}
	cmd->state = 0;
	cmd->ack = 0;
	goodix_append_checksum(&(cmd->buf[2]), cmd->len - 2,
			       CHECKSUM_MODE_U8_LE);
	TPD_DEBUG("cmd data %*ph\n", cmd->len, &(cmd->buf[2]));

	retry = 0;
	while (retry++ < GOODIX_CMD_RETRY) {
		ret = goodix_reg_write(chip_info,
				       misc->cmd_addr, cmd->buf, sizeof(*cmd));
		if (ret < 0) {
			TPD_INFO("%s: failed write command\n", __func__);
			return ret;
		}
		for (i = 0; i < GOODIX_CMD_RETRY; i++) {
			/* check command result */
			ret = goodix_reg_read(chip_info,
					      misc->cmd_addr, cmd_ack.buf, sizeof(cmd_ack));
			if (ret < 0) {
				TPD_INFO("%s: failed read command ack, %d\n",
					 __func__, ret);
				return ret;
			}
			TPD_DEBUG("cmd ack data %*ph\n",
				  (int)sizeof(cmd_ack), cmd_ack.buf);
			if (cmd_ack.ack == CMD_ACK_OK) {
				usleep_range(2000, 2100);
				return 0;
			}

			if (cmd_ack.ack == CMD_ACK_BUSY ||
			    cmd_ack.ack == 0x00) {
				usleep_range(1000, 1100);
				continue;
			}
			if (cmd_ack.ack == CMD_ACK_BUFFER_OVERFLOW) {
				usleep_range(10000, 11000);
			}
			usleep_range(1000, 1100);
			break;
		}
	}
	TPD_INFO("%s, failed get valid cmd ack\n", __func__);
	return -1;
}

/* this is for send cmd with one byte cmd_data parameters */
static int goodix_send_cmd_simple(struct chip_data_brl *chip_info,
				  u8 cmd_type, u8 cmd_data)
{
	struct goodix_ts_cmd cmd;

	if (cmd_type == GTP_CMD_NORMAL) {
		cmd.len = 4;
		TPD_INFO("%s, return to normal mode \n", __func__);
	} else {
		TPD_INFO("%s, return to default mode \n", __func__);
		cmd.len = 5;
	}

	cmd.cmd = cmd_type;
	cmd.data[0] = cmd_data;
	return brl_send_cmd(chip_info, &cmd);
}

/********* Start of function that work for oplus_touchpanel_operations callbacks***************/
static int goodix_clear_irq(void *chip_data)
{
	int ret = -1;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	u8 clean_flag = 0;

	if (!gt8x_rawdiff_mode) {
		ret = goodix_reg_write(chip_info,
				       chip_info->ic_info.misc.touch_data_addr, &clean_flag, 1);
		if (ret < 0) {
			TPD_INFO("I2C write end_cmd  error!\n");
		}
	}

	return ret;
}

/*
static void getSpecialCornerPoint(uint8_t *buf, int n, struct Coordinate *point)
{
    // TODO
}

static int clockWise(uint8_t *buf, int n)
{
    // TODO
    //TPD_INFO("ClockWise count = %d\n", count);
    return 1;
}
*/

static void goodix_esd_check_enable(struct chip_data_brl *chip_info, bool enable)
{
	TPD_INFO("%s %s\n", __func__, enable ? "enable" : "disable");
	/* enable/disable esd check flag */
	chip_info->esd_check_enabled = enable;
}

static int goodix_enter_sleep(struct chip_data_brl *chip_info, bool enable)
{
	u8 sleep_cmd[] = {0x00, 0x00, 0x04, 0x84, 0x88, 0x00};
	u32 cmd_reg = chip_info->ic_info.misc.cmd_addr;

	TPD_INFO("%s, sleep enable = %d\n", __func__, enable);

	if (enable) {
		goodix_spi_write(chip_info, cmd_reg, sleep_cmd, sizeof(sleep_cmd));
	} else {
		goodix_reset(chip_info);
	}

	return 0;
}

static int goodix_enable_gesture(struct chip_data_brl *chip_info, bool enable)
{
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	TPD_INFO("%s, gesture enable = %d\n", __func__, enable);

	if (enable) {
		/* if (chip_info->gesture_type == 0); */
		chip_info->gesture_type = 0xFFFFFFFF;

		goodix_enter_sleep(chip_info, false);

		TPD_INFO("%s, gesture_type:0x%08X, and disable sleep\n", __func__, chip_info->gesture_type);
		tmp_cmd.len = 8;
		tmp_cmd.cmd = 0xA6;
		tmp_cmd.data[0] = chip_info->gesture_type & 0xFF;
		tmp_cmd.data[1] = (chip_info->gesture_type >> 8) & 0xFF;
		tmp_cmd.data[2] = (chip_info->gesture_type >> 16) & 0xFF;
		tmp_cmd.data[3] = (chip_info->gesture_type >> 24) & 0xFF;
	} else {
		tmp_cmd.len = 4;
		tmp_cmd.cmd = 0xA7;
	}

	ret = brl_send_cmd(chip_info, &tmp_cmd);
	return ret;
}

static int goodix_enable_edge_limit(struct chip_data_brl *chip_info, int state)
{
	int ret = -1;

	TPD_INFO("%s, edge limit enable = %d\n", __func__, state);

	if (state == 1 || VERTICAL_SCREEN == chip_info->touch_direction) {
		ret = goodix_send_cmd_simple(chip_info, GTM_CMD_EDGE_LIMIT_VERTICAL, 0x00);
	} else {
		if (LANDSCAPE_SCREEN_90 == chip_info->touch_direction) {
			ret = goodix_send_cmd_simple(chip_info,
						     GTM_CMD_EDGE_LIMIT_LANDSCAPE, GTP_MASK_DISABLE);
		} else if (LANDSCAPE_SCREEN_270 == chip_info->touch_direction) {
			ret = goodix_send_cmd_simple(chip_info,
						     GTM_CMD_EDGE_LIMIT_LANDSCAPE, GTP_MASK_ENABLE);
		}
	}

	return ret;
}

static int goodix_enable_charge_mode(struct chip_data_brl *chip_info, bool enable)
{
	int ret = -1;

	TPD_INFO("%s, charge mode enable = %d\n", __func__, enable);

	if (enable) {
		ret = goodix_send_cmd_simple(chip_info, GTP_CMD_CHARGER_ON, GTP_MASK_ENABLE);
	} else {
		ret = goodix_send_cmd_simple(chip_info, GTP_CMD_CHARGER_OFF, GTP_MASK_DISABLE);
	}

	return ret;
}

static int goodix_enable_game_mode(struct chip_data_brl *chip_info, bool enable)
{
	int ret = 0;

	TPD_INFO("%s, game mode enable = %d\n", __func__, enable);
	if (enable) {
		ret = goodix_send_cmd_simple(chip_info, GTP_CMD_GAME_MODE, GTP_MASK_ENABLE);
		TPD_INFO("%s: GTP_CMD_ENTER_GAME_MODE\n", __func__);
	} else {
		ret = goodix_send_cmd_simple(chip_info, GTP_CMD_GAME_MODE, GTP_MASK_DISABLE);
		TPD_INFO("%s: GTP_CMD_EXIT_GAME_MODE\n", __func__);
	}

	return ret;
}

static int goodix_enable_pen_mode(struct chip_data_brl *chip_info, bool enable)
{
	int ret = 0;

	TPD_INFO("%s, now %s pen\n", __func__, enable > 0 ? "enable" : "disable");
	if (enable) {
		ret = goodix_send_cmd_simple(chip_info, GTP_PEN_ENABLE_MASK, GTP_MASK_ENABLE);
	} else {
		ret = goodix_send_cmd_simple(chip_info, GTP_PEN_ENABLE_MASK, GTP_MASK_DISABLE);
	}

	return ret;
}

static int goodix_pen_control(struct chip_data_brl *chip_info, int ctl_cmd)
{
	TPD_INFO("%s, pen control cmd is [%2d]-[0x%x]\n", __func__, ctl_cmd, ctl_cmd);

	if (ctl_cmd == PEN_CTL_FEEDBACK) {
		TPD_INFO("%s flag is feedback, return now control para\n", __func__);
		goto REPORT;
	}

	if (!ctl_cmd) {
		TPD_INFO("%s invaled data!!\n", __func__);
		goto ERR;
	}

	if (ctl_cmd & PEN_CTL_VIBRATOR) {
		TPD_INFO("%s find vibrator cmd and set cmd:%d\n", __func__, ctl_cmd & PEN_CTL_VIBRATOR_ENABLE);
		if (goodix_send_cmd_simple(chip_info, PEN_CMD_VIBRATOR, !(ctl_cmd & PEN_CTL_VIBRATOR_ENABLE)) < 0) {
			TPD_INFO("%s fail to set vibrator cmd:%d\n", __func__, ctl_cmd & PEN_CTL_VIBRATOR_ENABLE);
			goto ERR;
		}
	}

	chip_info->pen_ctl_para = ctl_cmd;

REPORT:
	return chip_info->pen_ctl_para;
ERR:
	return RESULT_ERR;
}

#pragma  pack(1)
struct goodix_config_head {
	union {
		struct {
			u8 panel_name[8];
			u8 fw_pid[8];
			u8 fw_vid[4];
			u8 project_name[8];
			u8 file_ver[2];
			u32 cfg_id;
			u8 cfg_ver;
			u8 cfg_time[8];
			u8 reserved[15];
			u8 flag;
			u16 cfg_len;
			u8 cfg_num;
			u16 checksum;
		};
		u8 buf[64];
	};
};
#pragma pack()

#define CONFIG_CND_LEN 			4
#define CONFIG_CMD_START 		0x04
#define CONFIG_CMD_WRITE 		0x05
#define CONFIG_CMD_EXIT  		0x06
#define CONFIG_CMD_READ_START  		0x07
#define CONFIG_CMD_READ_EXIT   		0x08

#define CONFIG_CMD_STATUS_PASS 		0x80
#define CONFIG_CMD_WAIT_RETRY           20

static int wait_cmd_status(struct chip_data_brl *chip_info, u8 target_status, int retry)
{
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ic_info_misc *misc = &chip_info->ic_info.misc;
	int i, ret;

	for (i = 0; i < retry; i++) {
		ret = goodix_reg_read(chip_info,
				      misc->cmd_addr, cmd_ack.buf, sizeof(cmd_ack));
		if (!ret && cmd_ack.state == target_status) {
			TPD_DEBUG("status check pass\n");
			return 0;
		}

		TPD_INFO("%s: cmd status not ready, retry %d, ack 0x%x, status 0x%x, ret %d\n", __func__,
			 i, cmd_ack.ack, cmd_ack.state, ret);
		TPD_INFO("cmd buf %*ph\n", (int)sizeof(cmd_ack), cmd_ack.buf);
		usleep_range(15000, 15100);
	}
	return -1;
}

static int send_cfg_cmd(struct chip_data_brl *chip_info,
			struct goodix_ts_cmd *cfg_cmd)
{
	int ret;

	ret = brl_send_cmd(chip_info, cfg_cmd);
	if (ret) {
		TPD_INFO("%s: failed write cfg prepare cmd %d\n", __func__, ret);
		return ret;
	}
	ret = wait_cmd_status(chip_info, CONFIG_CMD_STATUS_PASS,
			      CONFIG_CMD_WAIT_RETRY);
	if (ret) {
		TPD_INFO("%s: failed wait for fw ready for config, %d\n",
			 __func__, ret);
		return ret;
	}
	return 0;
}

static s32 goodix_send_config(struct chip_data_brl *chip_info, u8 *cfg, int len)
{
	int ret;
	u8 *tmp_buf;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &chip_info->ic_info.misc;

	if (len > misc->fw_buffer_max_len) {
		TPD_INFO("%s: config len exceed limit %d > %d\n", __func__,
			 len, misc->fw_buffer_max_len);
		return -1;
	}

	tmp_buf = kzalloc(len, GFP_KERNEL);
	if (!tmp_buf) {
		TPD_INFO("%s: failed alloc malloc\n", __func__);
		return -ENOMEM;
	}

	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_START;
	ret = send_cfg_cmd(chip_info, &cfg_cmd);
	if (ret) {
		TPD_INFO("%s: failed write cfg prepare cmd %d\n",
			 __func__, ret);
		goto exit;
	}

	TPD_DEBUG("try send config to 0x%x, len %d\n",
		  misc->fw_buffer_addr, len);
	ret = goodix_reg_write(chip_info,
			       misc->fw_buffer_addr, cfg, len);
	if (ret) {
		TPD_INFO("%s: failed write config data, %d\n", __func__, ret);
		goto exit;
	}
	ret = goodix_reg_read(chip_info,
			      misc->fw_buffer_addr, tmp_buf, len);
	if (ret) {
		TPD_INFO("%s: failed read back config data\n", __func__);
		goto exit;
	}

	if (memcmp(cfg, tmp_buf, len)) {
		TPD_INFO("%s: config data read back compare file\n", __func__);
		ret = -1;
		goto exit;
	}
	/* notify fw for recive config */
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_WRITE;
	ret = send_cfg_cmd(chip_info, &cfg_cmd);
	if (ret) {
		TPD_INFO("%s: failed send config data ready cmd %d\n", __func__, ret);
	}

exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_EXIT;
	if (send_cfg_cmd(chip_info, &cfg_cmd)) {
		TPD_INFO("%s: failed send config write end command\n", __func__);
		ret = -1;
	}

	if (!ret) {
		TPD_INFO("success send config\n");
		msleep(100);
	}

	kfree(tmp_buf);
	return ret;
}

static int goodix_reset(void *chip_data)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	TPD_INFO("%s IN\n", __func__);

	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
		gpio_direction_output(chip_info->hw_res->reset_gpio, false);
		msleep(15);
		gpio_direction_output(chip_info->hw_res->reset_gpio, true);
		msleep(50);
		chip_info->halt_status = false; /*reset this flag when ic reset*/
	} else {
		TPD_INFO("reset gpio is invalid\n");
	}

	msleep(100);
	return 0;
}

/*********** End of function that work for oplus_touchpanel_operations callbacks***************/


/********* Start of implementation of oplus_touchpanel_operations callbacks********************/
static int goodix_ftm_process(void *chip_data)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	TPD_INFO("%s is called!\n", __func__);
	tp_powercontrol_avdd(chip_info->hw_res, false);
	tp_powercontrol_vddi(chip_info->hw_res, false);

	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
		gpio_direction_output(chip_info->hw_res->reset_gpio, false);
	}

	return 0;
}
/*
#define TS_DEFAULT_FIRMWARE         "GT9966.bin"
#define TS_DEFAULT_INSPECT_LIMIT    "gt9966_test_limit"
static void  goodix_util_get_vendor(struct hw_resource * hw_res,
		struct panel_info *panel_data)
{
	char manuf_version[MAX_DEVICE_VERSION_LENGTH] = "0202 ";
	char manuf_manufacture[MAX_DEVICE_MANU_LENGTH] = "GD_";
	char gt_fw_name[16] = TS_DEFAULT_FIRMWARE;
	char gt_test_limit_name[MAX_DEVICE_MANU_LENGTH] = TS_DEFAULT_INSPECT_LIMIT;

	memcpy (panel_data->manufacture_info.version,
		&manuf_version[0], MAX_DEVICE_VERSION_LENGTH);
	memcpy (panel_data->manufacture_info.manufacture,
		&manuf_manufacture[0], MAX_DEVICE_MANU_LENGTH);
	memcpy (panel_data->fw_name , &gt_fw_name [0],16);
	memcpy (panel_data->test_limit_name,
		&gt_test_limit_name[0], MAX_DEVICE_MANU_LENGTH);
}
*/
static int goodix_get_vendor(void *chip_data, struct panel_info *panel_data)
{
	int len = 0;
	char manu_temp[MAX_DEVICE_MANU_LENGTH] = "HD_";
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	len = strlen(panel_data->fw_name);
	chip_info->tp_type = panel_data->tp_type;
	chip_info->p_tp_fw = panel_data->manufacture_info.version;
	strlcat(manu_temp, panel_data->manufacture_info.manufacture,
		MAX_DEVICE_MANU_LENGTH);
	strncpy(panel_data->manufacture_info.manufacture, manu_temp,
		MAX_DEVICE_MANU_LENGTH);
	TPD_INFO("chip_info->tp_type = %d, panel_data->fw_name = %s\n",
		 chip_info->tp_type, panel_data->fw_name);

	return 0;
}


#define GOODIX_READ_VERSION_RETRY 	5
#define FW_VERSION_INFO_ADDR        0x10014
#define GOODIX_NORMAL_PID		    "9966"
/*
 * return: 0 for no error.
 * 	   -1 when encounter a bus error
 * 	   -EFAULT version checksum error
*/
static int brl_read_version(struct chip_data_brl *chip_info,
			    struct goodix_fw_version *version)
{
	int ret, i;
	u8 rom_pid[10] = {0};
	u8 patch_pid[10] = {0};
	u8 buf[sizeof(struct goodix_fw_version)] = {0};

	for (i = 0; i < GOODIX_READ_VERSION_RETRY; i++) {
		ret = goodix_reg_read(chip_info,
				      FW_VERSION_INFO_ADDR, buf, sizeof(buf));
		if (ret) {
			TPD_INFO("read fw version: %d, retry %d\n", ret, i);
			ret = -1;
			usleep_range(5000, 5100);
			continue;
		}

		if (!checksum_cmp(buf, sizeof(buf), CHECKSUM_MODE_U8_LE)) {
			break;
		}

		TPD_INFO("invalid fw version: checksum error!\n");
		TPD_INFO("fw version:%*ph\n", (int)sizeof(buf), buf);
		ret = -EFAULT;
		usleep_range(10000, 10100);
	}
	if (ret) {
		TPD_INFO("%s: failed get valied fw version, but continue\n", __func__);
		return 0;
	}
	memcpy(version, buf, sizeof(*version));
	memcpy(rom_pid, version->rom_pid, sizeof(version->rom_pid));
	memcpy(patch_pid, version->patch_pid, sizeof(version->patch_pid));
	TPD_INFO("rom_pid:%s\n", rom_pid);
	TPD_INFO("rom_vid:%*ph\n", (int)sizeof(version->rom_vid), version->rom_vid);
	TPD_INFO("pid:%s\n", patch_pid);
	TPD_INFO("vid:%*ph\n", (int)sizeof(version->patch_vid), version->patch_vid);
	TPD_INFO("sensor_id:%d\n", version->sensor_id);

	return 0;
}

#define LE16_TO_CPU(x)  (x = le16_to_cpu(x))
#define LE32_TO_CPU(x)  (x = le32_to_cpu(x))
static int convert_ic_info(struct goodix_ic_info *info, const u8 *data)
{
	int i;
	struct goodix_ic_info_version *version = &info->version;
	struct goodix_ic_info_feature *feature = &info->feature;
	struct goodix_ic_info_param *parm = &info->parm;
	struct goodix_ic_info_misc *misc = &info->misc;

	info->length = le16_to_cpup((__le16 *)data);

	data += 2;
	memcpy(version, data, sizeof(*version));
	version->config_id = le32_to_cpu(version->config_id);

	data += sizeof(struct goodix_ic_info_version);
	memcpy(feature, data, sizeof(*feature));
	feature->freqhop_feature = le16_to_cpu(feature->freqhop_feature);
	feature->calibration_feature = le16_to_cpu(feature->calibration_feature);
	feature->gesture_feature = le16_to_cpu(feature->gesture_feature);
	feature->side_touch_feature = le16_to_cpu(feature->side_touch_feature);
	feature->stylus_feature = le16_to_cpu(feature->stylus_feature);

	data += sizeof(struct goodix_ic_info_feature);
	parm->drv_num = *(data++);
	parm->sen_num = *(data++);
	parm->button_num = *(data++);
	parm->force_num = *(data++);
	parm->active_scan_rate_num = *(data++);
	if (parm->active_scan_rate_num > MAX_SCAN_RATE_NUM) {
		TPD_INFO("%s: invalid scan rate num %d > %d\n", __func__,
			 parm->active_scan_rate_num, MAX_SCAN_RATE_NUM);
		return -1;
	}
	for (i = 0; i < parm->active_scan_rate_num; i++) {
		parm->active_scan_rate[i] = le16_to_cpup((__le16 *)(data + i * 2));
	}

	data += parm->active_scan_rate_num * 2;
	parm->mutual_freq_num = *(data++);
	if (parm->mutual_freq_num > MAX_SCAN_FREQ_NUM) {
		TPD_INFO("%s: invalid mntual freq num %d > %d\n", __func__,
			 parm->mutual_freq_num, MAX_SCAN_FREQ_NUM);
		return -1;
	}
	for (i = 0; i < parm->mutual_freq_num; i++) {
		parm->mutual_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));
	}

	data += parm->mutual_freq_num * 2;
	parm->self_tx_freq_num = *(data++);
	if (parm->self_tx_freq_num > MAX_SCAN_FREQ_NUM) {
		TPD_INFO("%s: invalid tx freq num %d > %d\n", __func__,
			 parm->self_tx_freq_num, MAX_SCAN_FREQ_NUM);
		return -1;
	}
	for (i = 0; i < parm->self_tx_freq_num; i++) {
		parm->self_tx_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));
	}

	data += parm->self_tx_freq_num * 2;
	parm->self_rx_freq_num = *(data++);
	if (parm->self_rx_freq_num > MAX_SCAN_FREQ_NUM) {
		TPD_INFO("%s: invalid rx freq num %d > %d\n", __func__,
			 parm->self_rx_freq_num, MAX_SCAN_FREQ_NUM);
		return -1;
	}
	for (i = 0; i < parm->self_rx_freq_num; i++) {
		parm->self_rx_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));
	}

	data += parm->self_rx_freq_num * 2;
	parm->stylus_freq_num = *(data++);
	if (parm->stylus_freq_num > MAX_FREQ_NUM_STYLUS) {
		TPD_INFO("%s: invalid stylus freq num %d > %d\n", __func__,
			 parm->stylus_freq_num, MAX_FREQ_NUM_STYLUS);
		return -1;
	}
	for (i = 0; i < parm->stylus_freq_num; i++) {
		parm->stylus_freq[i] = le16_to_cpup((__le16 *)(data + i * 2));
	}

	data += parm->stylus_freq_num * 2;
	memcpy(misc, data, sizeof(*misc));
	misc->cmd_addr = le32_to_cpu(misc->cmd_addr);
	misc->cmd_max_len = le16_to_cpu(misc->cmd_max_len);
	misc->cmd_reply_addr = le32_to_cpu(misc->cmd_reply_addr);
	misc->cmd_reply_len = le16_to_cpu(misc->cmd_reply_len);
	misc->fw_state_addr = le32_to_cpu(misc->fw_state_addr);
	misc->fw_state_len = le16_to_cpu(misc->fw_state_len);
	misc->fw_buffer_addr = le32_to_cpu(misc->fw_buffer_addr);
	misc->fw_buffer_max_len = le16_to_cpu(misc->fw_buffer_max_len);
	misc->frame_data_addr = le32_to_cpu(misc->frame_data_addr);
	misc->frame_data_head_len = le16_to_cpu(misc->frame_data_head_len);

	misc->fw_attr_len = le16_to_cpu(misc->fw_attr_len);
	misc->fw_log_len = le16_to_cpu(misc->fw_log_len);
	misc->stylus_struct_len = le16_to_cpu(misc->stylus_struct_len);
	misc->mutual_struct_len = le16_to_cpu(misc->mutual_struct_len);
	misc->self_struct_len = le16_to_cpu(misc->self_struct_len);
	misc->noise_struct_len = le16_to_cpu(misc->noise_struct_len);
	misc->touch_data_addr = le32_to_cpu(misc->touch_data_addr);
	misc->touch_data_head_len = le16_to_cpu(misc->touch_data_head_len);
	misc->point_struct_len = le16_to_cpu(misc->point_struct_len);
	LE32_TO_CPU(misc->mutual_rawdata_addr);
	LE32_TO_CPU(misc->mutual_diffdata_addr);
	LE32_TO_CPU(misc->mutual_refdata_addr);
	LE32_TO_CPU(misc->self_rawdata_addr);
	LE32_TO_CPU(misc->self_diffdata_addr);
	LE32_TO_CPU(misc->self_refdata_addr);
	LE32_TO_CPU(misc->iq_rawdata_addr);
	LE32_TO_CPU(misc->iq_refdata_addr);
	LE32_TO_CPU(misc->im_rawdata_addr);
	LE16_TO_CPU(misc->im_readata_len);
	LE32_TO_CPU(misc->noise_rawdata_addr);
	LE16_TO_CPU(misc->noise_rawdata_len);
	LE32_TO_CPU(misc->stylus_rawdata_addr);
	LE16_TO_CPU(misc->stylus_rawdata_len);
	LE32_TO_CPU(misc->noise_data_addr);
	LE32_TO_CPU(misc->esd_addr);

	return 0;
}

static void print_ic_info(struct goodix_ic_info *ic_info)
{
	struct goodix_ic_info_version *version = &ic_info->version;
	struct goodix_ic_info_feature *feature = &ic_info->feature;
	struct goodix_ic_info_param *parm = &ic_info->parm;
	struct goodix_ic_info_misc *misc = &ic_info->misc;

	TPD_INFO("ic_info_length:                %d\n", ic_info->length);
	TPD_INFO("info_customer_id:              0x%01X\n", version->info_customer_id);
	TPD_INFO("info_version_id:               0x%01X\n", version->info_version_id);
	TPD_INFO("ic_die_id:                     0x%01X\n", version->ic_die_id);
	TPD_INFO("ic_version_id:                 0x%01X\n", version->ic_version_id);
	TPD_INFO("config_id:                     0x%4X\n", version->config_id);
	TPD_INFO("config_version:                0x%01X\n", version->config_version);
	TPD_INFO("frame_data_customer_id:        0x%01X\n", version->frame_data_customer_id);
	TPD_INFO("frame_data_version_id:         0x%01X\n", version->frame_data_version_id);
	TPD_INFO("touch_data_customer_id:        0x%01X\n", version->touch_data_customer_id);
	TPD_INFO("touch_data_version_id:         0x%01X\n", version->touch_data_version_id);

	TPD_INFO("freqhop_feature:               0x%04X\n", feature->freqhop_feature);
	TPD_INFO("calibration_feature:           0x%04X\n", feature->calibration_feature);
	TPD_INFO("gesture_feature:               0x%04X\n", feature->gesture_feature);
	TPD_INFO("side_touch_feature:            0x%04X\n", feature->side_touch_feature);
	TPD_INFO("stylus_feature:                0x%04X\n", feature->stylus_feature);

	TPD_INFO("Drv*Sen,Button,Force num:      %d x %d, %d, %d\n",
		 parm->drv_num, parm->sen_num, parm->button_num, parm->force_num);

	TPD_INFO("Cmd:                           0x%04X, %d\n",
		 misc->cmd_addr, misc->cmd_max_len);
	TPD_INFO("Cmd-Reply:                     0x%04X, %d\n",
		 misc->cmd_reply_addr, misc->cmd_reply_len);
	TPD_INFO("FW-State:                      0x%04X, %d\n",
		 misc->fw_state_addr, misc->fw_state_len);
	TPD_INFO("FW-Buffer:                     0x%04X, %d\n",
		 misc->fw_buffer_addr, misc->fw_buffer_max_len);
	TPD_INFO("Touch-Data:                    0x%04X, %d\n",
		 misc->touch_data_addr, misc->touch_data_head_len);
	TPD_INFO("point_struct_len:              %d\n", misc->point_struct_len);
	TPD_INFO("mutual_rawdata_addr:           0x%04X\n",
		 misc->mutual_rawdata_addr);
	TPD_INFO("mutual_diffdata_addr:          0x%04X\n",
		 misc->mutual_diffdata_addr);
	TPD_INFO("self_rawdata_addr:             0x%04X\n",
		 misc->self_rawdata_addr);
	TPD_INFO("self_rawdata_addr:             0x%04X\n",
		 misc->self_rawdata_addr);
	TPD_INFO("stylus_rawdata_addr:           0x%04X, %d\n",
		 misc->stylus_rawdata_addr, misc->stylus_rawdata_len);
	TPD_INFO("esd_addr:                      0x%04X\n", misc->esd_addr);
}

#define GOODIX_GET_IC_INFO_RETRY	3
#define GOODIX_IC_INFO_MAX_LEN		1024
#define GOODIX_IC_INFO_ADDR		    0x10070
static int brl_get_ic_info(struct chip_data_brl *chip_info,
			   struct goodix_ic_info *ic_info)
{
	int ret, i;
	u16 length = 0;
	u8 afe_data[GOODIX_IC_INFO_MAX_LEN] = {0};

	for (i = 0; i < GOODIX_GET_IC_INFO_RETRY; i++) {
		ret = goodix_reg_read(chip_info,
				      GOODIX_IC_INFO_ADDR,
				      (u8 *)&length, sizeof(length));
		if (ret) {
			TPD_INFO("failed get ic info length, %d\n", ret);
			usleep_range(5000, 5100);
			continue;
		}
		length = le16_to_cpu(length);
		if (length >= GOODIX_IC_INFO_MAX_LEN) {
			TPD_INFO("invalid ic info length %d, retry %d\n", length, i);
			continue;
		}

		ret = goodix_reg_read(chip_info,
				      GOODIX_IC_INFO_ADDR, afe_data, length);
		if (ret) {
			TPD_INFO("failed get ic info data, %d\n", ret);
			usleep_range(5000, 5100);
			continue;
		}
		/* judge whether the data is valid */
		/*if (is_risk_data((const uint8_t *)afe_data, length)) {
			TPD_INFO("fw info data invalid\n");
			usleep_range(5000, 5100);
			continue;
		}*/
		if (checksum_cmp((const uint8_t *)afe_data,
				 length, CHECKSUM_MODE_U8_LE)) {
			TPD_INFO("fw info checksum error!\n");
			usleep_range(5000, 5100);
			continue;
		}
		break;
	}
	if (i == GOODIX_GET_IC_INFO_RETRY) {
		TPD_INFO("%s: failed get ic info\n", __func__);
		/* set ic_info length =0 to indicate this is invalid */
		ic_info->length = 0;
		return -1;
	}

	ret = convert_ic_info(ic_info, afe_data);
	if (ret) {
		TPD_INFO("%s: convert ic info encounter error\n", __func__);
		ic_info->length = 0;
		return ret;
	}
	print_ic_info(ic_info);
	/* check some key info */
	if (!ic_info->misc.cmd_addr || !ic_info->misc.fw_buffer_addr ||
	    !ic_info->misc.touch_data_addr) {
		TPD_INFO("%s: cmd_addr fw_buf_addr and touch_data_addr is null\n", __func__);
		ic_info->length = 0;
		return -1;
	}
	TPD_INFO("success get ic info %d\n", ic_info->length);
	return 0;
}

static int goodix_get_chip_info(void *chip_data)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	int ret;

	/* get ic info */
	ret = brl_get_ic_info(chip_info, &chip_info->ic_info);
	if (ret < 0) {
		TPD_INFO("faile to get ic info, but continue to probe common!!\n");
		return 0;
	}

	/* get version info */
	ret = brl_read_version(chip_info, &chip_info->ver_info);
	if (ret < 0) {
		TPD_INFO("failed to get version\n");
	}

	return ret;
}

static int goodix_power_control(void *chip_data, bool enable)
{
	int ret = 0;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	TPD_INFO("%s: enable:%d\n", __func__, enable);

	if (true == enable) {
		ret = tp_powercontrol_vddi(chip_info->hw_res, true);
		if (ret) {
			return -1;
		}

		usleep_range(10000, 11000);

		ret = tp_powercontrol_avdd(chip_info->hw_res, true);
		if (ret) {
			return -1;
		}

		usleep_range(10000, 11000);
		goodix_reset(chip_info);
	} else {
		if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
			gpio_direction_output(chip_info->hw_res->reset_gpio, false);
		}

		disable_irq(chip_info->irq);

		usleep_range(10000, 11000);

		ret = tp_powercontrol_vddi(chip_info->hw_res, false);
		if (ret) {
			return -1;
		}

		usleep_range(50000, 51000);

		ret = tp_powercontrol_avdd(chip_info->hw_res, false);
		if (ret) {
			return -1;
		}
	}

	return ret;
}

static fw_check_state goodix_fw_check(void *chip_data,
				      struct resolution_info *resolution_info,
				      struct panel_info *panel_data)
{
	u32 fw_ver_num = 0;
	u8 cfg_ver = 0;
	char dev_version[MAX_DEVICE_VERSION_LENGTH] = {0};
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	struct goodix_fw_version *fw_ver;

	/* TODO need confirm fw state check method*/
	fw_ver = &chip_info->ver_info;
	if (!isdigit(fw_ver->patch_pid[0]) || !isdigit(fw_ver->patch_pid[1]) ||
	    !isdigit(fw_ver->patch_pid[2]) || !isdigit(fw_ver->patch_pid[3])) {
		TPD_INFO("%s: goodix abnormal patch id found: %s\n", __func__,
			 fw_ver->patch_pid);
		return FW_ABNORMAL;
	}
	fw_ver_num = le32_to_cpup((__le32 *)&fw_ver->patch_vid[0]);
	cfg_ver = chip_info->ic_info.version.config_version;

	if (panel_data->manufacture_info.version) {
		panel_data->tp_fw = (fw_ver_num >> 24);
		sprintf(dev_version, "%04x", panel_data->tp_fw);
		strlcpy(&(panel_data->manufacture_info.version[5]), dev_version, 5);
	}

	TPD_INFO("%s: panel_data->tp_fw = 0x%x , fw_ver_num = 0x%x, fw_ver_num >> 24 = 0x%x\n",
		__func__, panel_data->tp_fw, fw_ver_num, fw_ver_num >> 24);
	return FW_NORMAL;
}

/*****************start of GT9886's update function********************/

#define FW_HEADER_SIZE				512
#define FW_SUBSYS_INFO_SIZE			10
#define FW_SUBSYS_INFO_OFFSET       42
#define FW_SUBSYS_MAX_NUM			47

#define ISP_MAX_BUFFERSIZE			(1024 * 4)

#define NO_NEED_UPDATE              99

#define FW_PID_LEN		    		8
#define FW_VID_LEN       			4
#define FLASH_CMD_LEN 				11

#define FW_FILE_CHECKSUM_OFFSET 	8
#define CONFIG_DATA_TYPE 			4

#define ISP_RAM_ADDR				0x57000
#define HW_REG_CPU_RUN_FROM			0x10000
#define FLASH_CMD_REG				0x13400
#define HW_REG_ISP_BUFFER			0x13410
#define CONFIG_DATA_ADDR			0x40000

#define HOLD_CPU_REG_W 				0x0002
#define HOLD_CPU_REG_R 				0x2000
#define MISCTL_REG				    0xD80B
#define WATCH_DOG_REG				0xD054
#define ENABLE_MISCTL				0x04
#define DISABLE_WATCH_DOG		    0x00

#define FLASH_CMD_TYPE_READ  			    0xAA
#define FLASH_CMD_TYPE_WRITE 			    0xBB
#define FLASH_CMD_ACK_CHK_PASS	    		0xEE
#define FLASH_CMD_ACK_CHK_ERROR     		0x33
#define FLASH_CMD_ACK_IDLE      		    0x11
#define FLASH_CMD_W_STATUS_CHK_PASS 		0x22
#define FLASH_CMD_W_STATUS_CHK_FAIL 		0x33
#define FLASH_CMD_W_STATUS_ADDR_ERR 		0x44
#define FLASH_CMD_W_STATUS_WRITE_ERR 		0x55
#define FLASH_CMD_W_STATUS_WRITE_OK 		0xEE


/**
 * fw_subsys_info - subsytem firmware infomation
 * @type: sybsystem type
 * @size: firmware size
 * @flash_addr: flash address
 * @data: firmware data
 */
struct fw_subsys_info {
	u8 type;
	u32 size;
	u32 flash_addr;
	const u8 *data;
};

/**
 *  firmware_summary
 * @size: fw total length
 * @checksum: checksum of fw
 * @hw_pid: mask pid string
 * @hw_pid: mask vid code
 * @fw_pid: fw pid string
 * @fw_vid: fw vid code
 * @subsys_num: number of fw subsystem
 * @chip_type: chip type
 * @protocol_ver: firmware packing
 *   protocol version
 * @bus_type: 0 represent I2C, 1 for SPI
 * @subsys: sybsystem info
 */
#pragma pack(1)
struct  firmware_summary {
	u32 size;
	u32 checksum;
	u8 hw_pid[6];
	u8 hw_vid[3];
	u8 fw_pid[FW_PID_LEN];
	u8 fw_vid[FW_VID_LEN];
	u8 subsys_num;
	u8 chip_type;
	u8 protocol_ver;
	u8 bus_type;
	u8 flash_protect;
	u8 reserved[8];
	struct fw_subsys_info subsys[FW_SUBSYS_MAX_NUM];
};
#pragma pack()

/**
 * firmware_data - firmware data structure
 * @fw_summary: firmware infomation
 * @firmware: firmware data structure
 */
struct firmware_data {
	struct  firmware_summary fw_summary;
	const struct firmware *firmware;
};

struct config_data {
	u8 *data;
	int size;
};

#pragma pack(1)
struct goodix_flash_cmd {
	union {
		struct {
			u8 status;
			u8 ack;
			u8 len;
			u8 cmd;
			u8 fw_type;
			u16 fw_len;
			u32 fw_addr;
			/*u16 checksum;*/
		};
		u8 buf[16];
	};
};
#pragma pack()

struct fw_update_ctrl {
	int mode;
	struct goodix_ts_config *ic_config;
	struct chip_data_brl *chip_info;
	struct firmware_data fw_data;
};

/**
 * goodix_parse_firmware - parse firmware header infomation
 *	and subsystem infomation from firmware data buffer
 *
 * @fw_data: firmware struct, contains firmware header info
 *	and firmware data.
 * return: 0 - OK, < 0 - error
 */
/* sizeof(length) + sizeof(checksum) */

static int goodix_parse_firmware(struct firmware_data *fw_data)
{
	const struct firmware *firmware;
	struct  firmware_summary *fw_summary;
	unsigned int i, fw_offset, info_offset;
	u32 checksum;
	int r = 0;

	fw_summary = &fw_data->fw_summary;

	/* copy firmware head info */
	firmware = fw_data->firmware;
	if (firmware->size < FW_SUBSYS_INFO_OFFSET) {
		TPD_INFO("%s: Invalid firmware size:%zu\n",
			 __func__, firmware->size);
		r = -1;
		goto err_size;
	}
	memcpy(fw_summary, firmware->data, sizeof(*fw_summary));

	/* check firmware size */
	fw_summary->size = le32_to_cpu(fw_summary->size);
	if (firmware->size != fw_summary->size + FW_FILE_CHECKSUM_OFFSET) {
		TPD_INFO("%s: Bad firmware, size not match, %zu != %d\n",
			 __func__, firmware->size, fw_summary->size + 6);
		r = -1;
		goto err_size;
	}

	for (i = FW_FILE_CHECKSUM_OFFSET, checksum = 0;
	     i < firmware->size; i+=2) {
		checksum += firmware->data[i] + (firmware->data[i+1] << 8);
	}

	/* byte order change, and check */
	fw_summary->checksum = le32_to_cpu(fw_summary->checksum);
	if (checksum != fw_summary->checksum) {
		TPD_INFO("%s: Bad firmware, cheksum error\n", __func__);
		r = -1;
		goto err_size;
	}

	if (fw_summary->subsys_num > FW_SUBSYS_MAX_NUM) {
		TPD_INFO("%s: Bad firmware, invalid subsys num: %d\n",
			 __func__, fw_summary->subsys_num);
		r = -1;
		goto err_size;
	}

	/* parse subsystem info */
	fw_offset = FW_HEADER_SIZE;
	for (i = 0; i < fw_summary->subsys_num; i++) {
		info_offset = FW_SUBSYS_INFO_OFFSET +
			      i * FW_SUBSYS_INFO_SIZE;

		fw_summary->subsys[i].type = firmware->data[info_offset];
		fw_summary->subsys[i].size =
			le32_to_cpup((__le32 *)&firmware->data[info_offset + 1]);

		fw_summary->subsys[i].flash_addr =
			le32_to_cpup((__le32 *)&firmware->data[info_offset + 5]);
		if (fw_offset > firmware->size) {
			TPD_INFO("%s: Sybsys offset exceed Firmware size\n",
				 __func__);
			goto err_size;
		}

		fw_summary->subsys[i].data = firmware->data + fw_offset;
		fw_offset += fw_summary->subsys[i].size;
	}

	TPD_INFO("Firmware package protocol: V%u\n", fw_summary->protocol_ver);
	TPD_INFO("Fimware PID:GT%s\n", fw_summary->fw_pid);
	TPD_INFO("Fimware VID:%*ph\n", 4, fw_summary->fw_vid);
	TPD_INFO("Firmware chip type:%02X\n", fw_summary->chip_type);
	TPD_INFO("Firmware bus type:%d\n", fw_summary->bus_type);
	TPD_INFO("Firmware size:%u\n", fw_summary->size);
	TPD_INFO("Firmware subsystem num:%u\n", fw_summary->subsys_num);

	for (i = 0; i < fw_summary->subsys_num; i++) {
		TPD_DEBUG("------------------------------------------\n");
		TPD_DEBUG("Index:%d\n", i);
		TPD_DEBUG("Subsystem type:%02X\n", fw_summary->subsys[i].type);
		TPD_DEBUG("Subsystem size:%u\n", fw_summary->subsys[i].size);
		TPD_DEBUG("Subsystem flash_addr:%08X\n",
			  fw_summary->subsys[i].flash_addr);
		TPD_DEBUG("Subsystem Ptr:%p\n", fw_summary->subsys[i].data);
	}

err_size:
	return r;
}

static int goodix_fw_update_reset(struct fw_update_ctrl *fwu_ctrl, int delay_ms)
{
	struct chip_data_brl *chip_info = fwu_ctrl->chip_info;

	if (gpio_is_valid(chip_info->hw_res->reset_gpio)) {
		gpio_direction_output(chip_info->hw_res->reset_gpio, 0);
		udelay(2000);
		gpio_direction_output(chip_info->hw_res->reset_gpio, 1);
		if (delay_ms < 20) {
			usleep_range(delay_ms * 1000, delay_ms * 1000 + 100);
		} else {
			msleep(delay_ms);
		}
	}
	return 0;
}

/* get config id form config file */
#define CONFIG_ID_OFFSET	    30
u32 goodix_get_file_config_id(u8 *ic_config)
{
	if (!ic_config) {
		return 0;
	}
	return le32_to_cpup((__le32 *)&ic_config[CONFIG_ID_OFFSET]);
}

/**
 * goodix_fw_version_compare - compare the active version with
 * firmware file version.
 * @fwu_ctrl: firmware infomation to be compared
 * return: 0 equal, < 0 unequal
 */
static int goodix_fw_version_compare(struct fw_update_ctrl *fwu_ctrl)
{
	int ret;
	struct goodix_fw_version fw_version;
	struct firmware_summary *fw_summary = &fwu_ctrl->fw_data.fw_summary;
	u32 file_cfg_id;
	u32 ic_cfg_id;

	/* compare fw_version */
	fw_version = fwu_ctrl->chip_info->ver_info;
	if (memcmp(fw_version.patch_pid, fw_summary->fw_pid, FW_PID_LEN)) {
		TPD_INFO("%s: Product ID mismatch:%s:%s\n", __func__,
			 fw_version.patch_pid, fw_summary->fw_pid);
		return -1;
	}

	ret = memcmp(fw_version.patch_vid, fw_summary->fw_vid, FW_VID_LEN);
	if (ret) {
		TPD_INFO("active firmware version:%*ph\n", FW_VID_LEN,
			 fw_version.patch_vid);
		TPD_INFO("firmware file version: %*ph\n", FW_VID_LEN,
			 fw_summary->fw_vid);
		return -1;
	}
	TPD_INFO("Firmware version equal\n");

	/* compare config id */
	if (fwu_ctrl->ic_config && fwu_ctrl->ic_config->length > 0) {
		file_cfg_id = goodix_get_file_config_id(fwu_ctrl->ic_config->data);
		ic_cfg_id = fwu_ctrl->chip_info->ic_info.version.config_id;
		if (ic_cfg_id != file_cfg_id) {
			TPD_INFO("ic_cfg_id:0x%x != file_cfg_id:0x%x\n",
				 ic_cfg_id, file_cfg_id);
			return -1;
		}
		TPD_INFO("Config_id equal\n");
	}

	return 0;
}

/**
 * goodix_load_isp - load ISP program to deivce ram
 * @dev: pointer to touch device
 * @fw_data: firmware data
 * return 0 ok, <0 error
 */
static int goodix_load_isp(struct fw_update_ctrl *fwu_ctrl)
{
	struct firmware_data *fw_data = &fwu_ctrl->fw_data;
	struct goodix_fw_version isp_fw_version = {{0}};
	struct fw_subsys_info *fw_isp;
	u8 reg_val[8] = {0x00};
	int r;

	fw_isp = &fw_data->fw_summary.subsys[0];

	TPD_INFO("Loading ISP start\n");
	r = goodix_reg_write_confirm(fwu_ctrl->chip_info, ISP_RAM_ADDR,
				     (u8 *)fw_isp->data, fw_isp->size);
	if (r < 0) {
		TPD_INFO("%s: Loading ISP error\n", __func__);
		return r;
	}

	TPD_INFO("Success send ISP data\n");

	/* SET BOOT OPTION TO 0X55 */
	memset(reg_val, 0x55, 8);
	r = goodix_reg_write_confirm(fwu_ctrl->chip_info,
				     HW_REG_CPU_RUN_FROM, reg_val, 8);
	if (r < 0) {
		TPD_INFO("%s: Failed set REG_CPU_RUN_FROM flag\n", __func__);
		return r;
	}
	TPD_INFO("Success write [8]0x55 to 0x%x\n", HW_REG_CPU_RUN_FROM);

	if (goodix_fw_update_reset(fwu_ctrl, 100)) {
		TPD_INFO("%s: reset abnormal\n", __func__);
	}
	/*check isp state */
	if (brl_read_version(fwu_ctrl->chip_info, &isp_fw_version)) {
		TPD_INFO("%s: failed read isp version\n", __func__);
		return -2;
	}
	if (memcmp(&isp_fw_version.patch_pid[3], "ISP", 3)) {
		TPD_INFO("%s: patch id error %c%c%c != %s\n", __func__,
			 isp_fw_version.patch_pid[3], isp_fw_version.patch_pid[4],
			 isp_fw_version.patch_pid[5], "ISP");
		return -3;
	}
	TPD_INFO("ISP running successfully\n");
	return 0;
}

/**
 * goodix_update_prepare - update prepare, loading ISP program
 *  and make sure the ISP is running.
 * @fwu_ctrl: pointer to fimrware control structure
 * return: 0 ok, <0 error
 */
static int goodix_update_prepare(struct fw_update_ctrl *fwu_ctrl)
{
	u8 reg_val[4] = {0};
	u8 temp_buf[64] = {0};
	int retry = 20;
	int r;

	/*reset IC*/
	TPD_INFO("firmware update, reset\n");
	if (goodix_fw_update_reset(fwu_ctrl, 5)) {
		TPD_INFO("%s: reset abnormal\n", __func__);
	}

	retry = 100;
	/* Hold cpu*/
	do {
		reg_val[0] = 0x01;
		reg_val[1] = 0x00;
		r = goodix_reg_write(fwu_ctrl->chip_info,
				     HOLD_CPU_REG_W, reg_val, 2);
		r |= goodix_reg_read(fwu_ctrl->chip_info,
				     HOLD_CPU_REG_R, &temp_buf[0], 4);
		r |= goodix_reg_read(fwu_ctrl->chip_info,
				     HOLD_CPU_REG_R, &temp_buf[4], 4);
		r |= goodix_reg_read(fwu_ctrl->chip_info,
				     HOLD_CPU_REG_R, &temp_buf[8], 4);
		if (!r && !memcmp(&temp_buf[0], &temp_buf[4], 4) &&
		    !memcmp(&temp_buf[4], &temp_buf[8], 4) &&
		    !memcmp(&temp_buf[0], &temp_buf[8], 4)) {
			break;
		}
		usleep_range(1000, 1100);
		TPD_INFO("retry hold cpu %d\n", retry);
		TPD_DEBUG("data:%*ph\n", 12, temp_buf);
	} while (--retry);
	if (!retry) {
		TPD_INFO("%s: Failed to hold CPU, return =%d\n", __func__, r);
		return -1;
	}
	TPD_INFO("Success hold CPU\n");

	/* enable misctl clock */
	r = goodix_reg_read(fwu_ctrl->chip_info, MISCTL_REG, reg_val, 1);
	reg_val[0] |= ENABLE_MISCTL;
	r = goodix_reg_write(fwu_ctrl->chip_info, MISCTL_REG, reg_val, 1);
	TPD_INFO("enbale misctl clock\n");

	/* disable watch dog */
	reg_val[0] = DISABLE_WATCH_DOG;
	r = goodix_reg_write(fwu_ctrl->chip_info, WATCH_DOG_REG, reg_val, 1);
	TPD_INFO("disable watch dog\n");

	/* load ISP code and run form isp */
	r = goodix_load_isp(fwu_ctrl);
	if (r < 0) {
		TPD_INFO("%s: Failed lode and run isp\n", __func__);
	}

	return r;
}

/* goodix_send_flash_cmd: send command to read or write flash data
 * @flash_cmd: command need to send.
 * */
static int goodix_send_flash_cmd(struct fw_update_ctrl *fwu_ctrl,
				 struct goodix_flash_cmd *flash_cmd)
{
	int i, ret, retry;
	struct goodix_flash_cmd tmp_cmd;

	TPD_DEBUG("try send flash cmd:%*ph\n", (int)sizeof(flash_cmd->buf),
		  flash_cmd->buf);
	memset(tmp_cmd.buf, 0, sizeof(tmp_cmd));
	ret = goodix_reg_write(fwu_ctrl->chip_info, FLASH_CMD_REG,
			       flash_cmd->buf, sizeof(flash_cmd->buf));
	if (ret) {
		TPD_INFO("%s: failed send flash cmd %d\n", __func__, ret);
		return ret;
	}

	retry = 5;
	for (i = 0; i < retry; i++) {
		ret = goodix_reg_read(fwu_ctrl->chip_info, FLASH_CMD_REG,
				      tmp_cmd.buf, sizeof(tmp_cmd.buf));
		if (!ret && tmp_cmd.ack == FLASH_CMD_ACK_CHK_PASS) {
			break;
		}
		usleep_range(5000, 5100);
		TPD_INFO("flash cmd ack error retry %d, ack 0x%x, ret %d\n",
			 i, tmp_cmd.ack, ret);
	}
	if (tmp_cmd.ack != FLASH_CMD_ACK_CHK_PASS) {
		TPD_INFO("%s: flash cmd ack error, ack 0x%x, ret %d\n",
			 __func__, tmp_cmd.ack, ret);
		TPD_INFO("%s: data:%*ph\n", __func__,
			 (int)sizeof(tmp_cmd.buf), tmp_cmd.buf);
		return -1;
	}
	TPD_INFO("flash cmd ack check pass\n");

	msleep(80);
	retry = 20;
	for (i = 0; i < retry; i++) {
		ret = goodix_reg_read(fwu_ctrl->chip_info, FLASH_CMD_REG,
				      tmp_cmd.buf, sizeof(tmp_cmd.buf));
		if (!ret && tmp_cmd.ack == FLASH_CMD_ACK_CHK_PASS &&
		    tmp_cmd.status == FLASH_CMD_W_STATUS_WRITE_OK) {
			TPD_INFO("flash status check pass\n");
			return 0;
		}

		TPD_INFO("flash cmd status not ready, retry %d, ack 0x%x, status 0x%x, ret %d\n",
			 i, tmp_cmd.ack, tmp_cmd.status, ret);
		usleep_range(15000, 15100);
	}

	TPD_INFO("%s: flash cmd status error %d, ack 0x%x, status 0x%x, ret %d\n", __func__,
		 i, tmp_cmd.ack, tmp_cmd.status, ret);
	if (ret) {
		TPD_INFO("reason: bus or paltform error\n");
		return -1;
	}

	switch (tmp_cmd.status) {
	case FLASH_CMD_W_STATUS_CHK_PASS:
		TPD_INFO("%s: data check pass, but failed get follow-up results\n", __func__);
		return -EFAULT;
	case FLASH_CMD_W_STATUS_CHK_FAIL:
		TPD_INFO("%s: data check failed, please retry\n", __func__);
		return -EAGAIN;
	case FLASH_CMD_W_STATUS_ADDR_ERR:
		TPD_INFO("%s: flash target addr error, please check\n", __func__);
		return -EFAULT;
	case FLASH_CMD_W_STATUS_WRITE_ERR:
		TPD_INFO("%s: flash data write err, please retry\n", __func__);
		return -EAGAIN;
	default:
		TPD_INFO("%s: unknown status\n", __func__);
		return -EFAULT;
	}
}

static int goodix_flash_package(struct fw_update_ctrl *fwu_ctrl,
				u8 subsys_type, u8 *pkg, u32 flash_addr, u16 pkg_len)
{
	int ret, retry;
	struct goodix_flash_cmd flash_cmd;

	retry = 2;
	do {
		ret = goodix_reg_write(fwu_ctrl->chip_info,
				       HW_REG_ISP_BUFFER, pkg, pkg_len);
		if (ret < 0) {
			TPD_INFO("%s: Failed to write firmware packet\n", __func__);
			return ret;
		}

		flash_cmd.status = 0;
		flash_cmd.ack = 0;
		flash_cmd.len = FLASH_CMD_LEN;
		flash_cmd.cmd = FLASH_CMD_TYPE_WRITE;
		flash_cmd.fw_type = subsys_type;
		flash_cmd.fw_len = cpu_to_le16(pkg_len);
		flash_cmd.fw_addr = cpu_to_le32(flash_addr);

		goodix_append_checksum(&(flash_cmd.buf[2]),
				       9, CHECKSUM_MODE_U8_LE);

		ret = goodix_send_flash_cmd(fwu_ctrl, &flash_cmd);
		if (!ret) {
			TPD_INFO("success write package to 0x%x, len %d\n",
				 flash_addr, pkg_len - 4);
			return 0;
		}
	} while (ret == -EAGAIN && --retry);

	return ret;
}

/**
 * goodix_flash_subsystem - flash subsystem firmware,
 *  Main flow of flashing firmware.
 *	Each firmware subsystem is divided into several
 *	packets, the max size of packet is limited to
 *	@{ISP_MAX_BUFFERSIZE}
 * @dev: pointer to touch device
 * @subsys: subsystem infomation
 * return: 0 ok, < 0 error
 */
static int goodix_flash_subsystem(struct fw_update_ctrl *fwu_ctrl,
				  struct fw_subsys_info *subsys)
{
	u32 data_size, offset;
	u32 total_size;
	/*TODO: confirm flash addr ,<< 8??*/
	u32 subsys_base_addr = subsys->flash_addr;
	u8 *fw_packet = NULL;
	int r = 0;

	/*
	 * if bus(i2c/spi) error occued, then exit, we will do
	 * hardware reset and re-prepare ISP and then retry
	 * flashing
	 */
	total_size = subsys->size;
	fw_packet = kzalloc(ISP_MAX_BUFFERSIZE + 4, GFP_KERNEL);
	if (!fw_packet) {
		TPD_INFO("%s: Failed alloc memory\n", __func__);
		return -ENOMEM;
	}

	offset = 0;
	while (total_size > 0) {
		data_size = total_size > ISP_MAX_BUFFERSIZE ?
			    ISP_MAX_BUFFERSIZE : total_size;
		TPD_INFO("Flash firmware to %08x,size:%u bytes\n",
			 subsys_base_addr + offset, data_size);

		memcpy(fw_packet, &subsys->data[offset], data_size);
		/* set checksum for package data */
		goodix_append_checksum(fw_packet,
				       data_size, CHECKSUM_MODE_U16_LE);

		r = goodix_flash_package(fwu_ctrl, subsys->type, fw_packet,
					 subsys_base_addr + offset, data_size + 4);
		if (r) {
			TPD_INFO("%s: failed flash to %08x,size:%u bytes\n",
				 __func__, subsys_base_addr + offset, data_size);
			break;
		}
		offset += data_size;
		total_size -= data_size;
	} /* end while */

	kfree(fw_packet);
	return r;
}

/**
 * goodix_flash_firmware - flash firmware
 * @dev: pointer to touch device
 * @fw_data: firmware data
 * return: 0 ok, < 0 error
 */
static int goodix_flash_firmware(struct fw_update_ctrl *fw_ctrl)
{
	struct firmware_data *fw_data = &fw_ctrl->fw_data;
	struct  firmware_summary  *fw_summary;
	struct fw_subsys_info *fw_x;
	struct fw_subsys_info subsys_cfg = {0};
	int retry = GOODIX_BUS_RETRY_TIMES;
	int i, r = 0, fw_num;

	/* start from subsystem 1,
	 * subsystem 0 is the ISP program */

	fw_summary = &fw_data->fw_summary;
	fw_num = fw_summary->subsys_num;

	/* flash config data first if we have */
	if (fw_ctrl->ic_config && fw_ctrl->ic_config->length) {
		subsys_cfg.data = fw_ctrl->ic_config->data;
		subsys_cfg.size = fw_ctrl->ic_config->length;
		subsys_cfg.flash_addr = CONFIG_DATA_ADDR;
		subsys_cfg.type = CONFIG_DATA_TYPE;
		r = goodix_flash_subsystem(fw_ctrl, &subsys_cfg);
		if (r) {
			TPD_INFO("%s: failed flash config with ISP, %d\n",
				 __func__, r);
			return r;
		}
		TPD_INFO("success flash config with ISP\n");
	}

	for (i = 1; i < fw_num && retry;) {
		TPD_INFO("--- Start to flash subsystem[%d] ---\n", i);
		fw_x = &fw_summary->subsys[i];
		r = goodix_flash_subsystem(fw_ctrl, fw_x);
		if (r == 0) {
			TPD_INFO("--- End flash subsystem[%d]: OK ---\n", i);
			i++;
		} else if (r == -EAGAIN) {
			retry--;
			TPD_INFO("%s: --- End flash subsystem%d: Fail, errno:%d, retry:%d ---\n", __func__,
				 i, r, GOODIX_BUS_RETRY_TIMES - retry);
		} else if (r < 0) { /* bus error */
			TPD_INFO("%s: --- End flash subsystem%d: Fatal error:%d exit ---\n", __func__,
				 i, r);
			goto exit_flash;
		}
	}

exit_flash:
	return r;
}

/**
 * goodix_update_finish - update finished, FREE resource
 *  and reset flags---
 * @fwu_ctrl: pointer to fw_update_ctrl structrue
 * return: 0 ok, < 0 error
 */
static int goodix_update_finish(struct fw_update_ctrl *fwu_ctrl)
{
	if (goodix_fw_update_reset(fwu_ctrl, 100)) {
		TPD_INFO("%s: reset abnormal\n", __func__);
	}

	/* refresh chip_info */
	if (goodix_get_chip_info(fwu_ctrl->chip_info)) {
		return -1;
	}
	/* compare version */
	if (goodix_fw_version_compare(fwu_ctrl)) {
		return -1;
	}

	return 0;
}

u32 getUint(u8 *buffer, int len)
{
	u32 num = 0;
	int i = 0;
	for (i = 0; i < len; i++) {
		num <<= 8;
		num += buffer[i];
	}
	return num;
}

static int goodix_parse_cfg_data(const struct firmware *cfg_bin,
				 char *cfg_type, u8 *cfg, int *cfg_len, u8 sid)
{
	int i = 0, config_status = 0, one_cfg_count = 0;
	int cfg_pkg_len = 0;

	u8 bin_group_num = 0, bin_cfg_num = 0;
	u16 cfg_checksum = 0, checksum = 0;
	u8 sid_is_exist = GOODIX_NOT_EXIST;
	u16 cfg_offset = 0;
	u8 cfg_sid = 0;

	TPD_DEBUG("%s run,sensor id:%d\n", __func__, sid);

	cfg_pkg_len = getU32(cfg_bin->data) + BIN_CFG_START_LOCAL;
	if (cfg_pkg_len > cfg_bin->size) {
		TPD_INFO("%s:Bad firmware!,cfg package len:%d,firmware size:%d\n",
			 __func__, cfg_pkg_len, (int)cfg_bin->size);
		goto exit;
	}

	/* check firmware's checksum */
	cfg_checksum = getU16(&cfg_bin->data[4]);

	for (i = BIN_CFG_START_LOCAL; i < (cfg_pkg_len) ; i++) {
		checksum += cfg_bin->data[i];
	}

	if ((checksum) != cfg_checksum) {
		TPD_INFO("%s:Bad firmware!(checksum: 0x%04X, header define: 0x%04X)\n",
			 __func__, checksum, cfg_checksum);
		goto exit;
	}
	/* check head end  */

	bin_group_num = cfg_bin->data[MODULE_NUM];
	bin_cfg_num = cfg_bin->data[CFG_NUM];
	TPD_DEBUG("%s:bin_group_num = %d, bin_cfg_num = %d\n",
		  __func__, bin_group_num, bin_cfg_num);

	if (!strncmp(cfg_type, GOODIX_TEST_CONFIG, strlen(GOODIX_TEST_CONFIG))) {
		config_status = 0;
	} else if (!strncmp(cfg_type, GOODIX_NORMAL_CONFIG, strlen(GOODIX_NORMAL_CONFIG))) {
		config_status = 1;
	} else if (!strncmp(cfg_type, GOODIX_NORMAL_NOISE_CONFIG, strlen(GOODIX_NORMAL_NOISE_CONFIG))) {
		config_status = 2;
	} else if (!strncmp(cfg_type, GOODIX_GLOVE_CONFIG, strlen(GOODIX_GLOVE_CONFIG))) {
		config_status = 3;
	} else if (!strncmp(cfg_type, GOODIX_GLOVE_NOISE_CONFIG, strlen(GOODIX_GLOVE_NOISE_CONFIG))) {
		config_status = 4;
	} else if (!strncmp(cfg_type, GOODIX_HOLSTER_CONFIG, strlen(GOODIX_HOLSTER_CONFIG))) {
		config_status = 5;
	} else if (!strncmp(cfg_type, GOODIX_HOLSTER_NOISE_CONFIG, strlen(GOODIX_HOLSTER_NOISE_CONFIG))) {
		config_status = 6;
	} else if (!strncmp(cfg_type, GOODIX_NOISE_TEST_CONFIG, strlen(GOODIX_NOISE_TEST_CONFIG))) {
		config_status = 7;
	} else {
		TPD_INFO("%s: invalid config text field\n", __func__);
		goto exit;
	}

	cfg_offset = CFG_HEAD_BYTES + bin_group_num * bin_cfg_num * CFG_INFO_BLOCK_BYTES;
	for (i = 0 ; i < bin_group_num * bin_cfg_num; i++) {
		/* find cfg's sid in cfg.bin */
		one_cfg_count = getU16(&cfg_bin->data[CFG_HEAD_BYTES + 2 + i * CFG_INFO_BLOCK_BYTES]);
		cfg_sid = cfg_bin->data[CFG_HEAD_BYTES + i * CFG_INFO_BLOCK_BYTES];
		if (sid == cfg_sid) {
			sid_is_exist = GOODIX_EXIST;
			if (config_status == (cfg_bin->data[CFG_HEAD_BYTES + 1 + i * CFG_INFO_BLOCK_BYTES])) {
				memcpy(cfg, &cfg_bin->data[cfg_offset], one_cfg_count);
				*cfg_len = one_cfg_count;
				TPD_DEBUG("%s:one_cfg_count = %d, cfg_data1 = 0x%02x, cfg_data2 = 0x%02x\n",
					  __func__, one_cfg_count, cfg[0], cfg[1]);
				break;
			}
		}
		cfg_offset += one_cfg_count;
	}

	if (i >= bin_group_num * bin_cfg_num) {
		TPD_INFO("%s:(not find config ,config_status: %d)\n", __func__, config_status);
		goto exit;
	}

	TPD_DEBUG("%s exit\n", __func__);
	return NO_ERR;
exit:
	return RESULT_ERR;
}

static int goodix_get_cfg_data(void *chip_data_info, const struct firmware *cfg_bin,
			       char *config_name, struct goodix_ts_config *config)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data_info;
	u8 *cfg_data = NULL;
	int cfg_len = 0;
	int ret = NO_ERR;

	TPD_DEBUG("%s run\n", __func__);

	cfg_data = kzalloc(GOODIX_CFG_MAX_SIZE, GFP_KERNEL);
	if (cfg_data == NULL) {
		TPD_INFO("Memory allco err\n");
		goto exit;
	}

	config->length = 0;
	/* parse config data */
	ret = goodix_parse_cfg_data(cfg_bin, config_name, cfg_data,
				    &cfg_len, chip_info->ver_info.sensor_id);
	if (ret < 0) {
		TPD_INFO("%s: parse %s data failed\n", __func__, config_name);
		ret = -1;
		goto exit;
	}

	TPD_INFO("%s: %s  version:%d , size:%d\n", __func__,
		 config_name, cfg_data[0], cfg_len);
	memcpy(config->data, cfg_data, cfg_len);
	config->length = cfg_len;

	strncpy(config->name, config_name, MAX_STR_LEN);

exit:
	if (cfg_data) {
		kfree(cfg_data);
		cfg_data = NULL;
	}
	TPD_DEBUG("%s exit\n", __func__);
	return ret;
}


static int goodix_get_cfg_parms(void *chip_data_info,
				const struct firmware *firmware)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data_info;
	int ret = 0;

	TPD_DEBUG("%s run\n", __func__);
	if (firmware == NULL) {
		TPD_INFO("%s: firmware is null\n", __func__);
		ret = -1;
		goto exit;
	}

	if (firmware->data == NULL) {
		TPD_INFO("%s:Bad firmware!(config firmware data is null: )\n", __func__);
		ret = -1;
		goto exit;
	}

	TPD_INFO("%s: cfg_bin_size:%d\n", __func__, (int)firmware->size);
	if (firmware->size > 56) {
		TPD_DEBUG("cfg_bin head info:%*ph\n", 32, firmware->data);
		TPD_DEBUG("cfg_bin head info:%*ph\n", 24, firmware->data + 32);
	}

	/* parse normal config data */
	ret = goodix_get_cfg_data(chip_info, firmware,
				  GOODIX_NORMAL_CONFIG, &chip_info->normal_cfg);
	if (ret < 0) {
		TPD_INFO("%s: Failed to parse normal_config data:%d\n", __func__, ret);
	} else {
		TPD_INFO("%s: parse normal_config data success\n", __func__);
	}

	ret = goodix_get_cfg_data(chip_info, firmware,
				  GOODIX_TEST_CONFIG, &chip_info->test_cfg);
	if (ret < 0) {
		TPD_INFO("%s: Failed to parse test_config data:%d\n", __func__, ret);
	} else {
		TPD_INFO("%s: parse test_config data success\n", __func__);
	}

	/* parse normal noise config data */
	ret = goodix_get_cfg_data(chip_info, firmware,
				  GOODIX_NORMAL_NOISE_CONFIG, &chip_info->normal_noise_cfg);
	if (ret < 0) {
		TPD_INFO("%s: Failed to parse normal_noise_config data\n", __func__);
	} else {
		TPD_INFO("%s: parse normal_noise_config data success\n", __func__);
	}

	/* parse noise test config data */
	ret = goodix_get_cfg_data(chip_info, firmware,
				  GOODIX_NOISE_TEST_CONFIG, &chip_info->noise_test_cfg);
	if (ret < 0) {
		memcpy(&chip_info->noise_test_cfg, &chip_info->normal_cfg,
		       sizeof(chip_info->noise_test_cfg));
		TPD_INFO("%s: Failed to parse noise_test_config data,use normal_config data\n", __func__);
	} else {
		TPD_INFO("%s: parse noise_test_config data success\n", __func__);
	}
exit:
	TPD_DEBUG("%s exit:%d\n", __func__, ret);
	return ret;
}

/*	get fw firmware from firmware
	return value:
	0: operate success
	other: failed*/
static int goodix_get_fw_parms(void *chip_data_info,
			       const struct firmware *firmware, struct firmware *fw_firmware)
{
	/*struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data_info;*/
	int ret = 0;
	int cfg_pkg_len = 0;
	int fw_pkg_len = 0;

	TPD_DEBUG("%s run\n", __func__);
	if (firmware == NULL) {
		TPD_INFO("%s: firmware is null\n", __func__);
		ret = -1;
		goto exit;
	}

	if (firmware->data == NULL) {
		TPD_INFO("%s:Bad firmware!(config firmware data si null)\n", __func__);
		ret = -1;
		goto exit;
	}

	if (fw_firmware == NULL) {
		TPD_INFO("%s:fw_firmware is null\n", __func__);
		ret = -1;
		goto exit;
	}

	TPD_DEBUG("clear fw_firmware\n");
	memset(fw_firmware, 0, sizeof(struct firmware));

	cfg_pkg_len = getU32(firmware->data) + BIN_CFG_START_LOCAL;
	TPD_DEBUG("%s cfg package len:%d\n", __func__, cfg_pkg_len);

	if (firmware->size <= (cfg_pkg_len + 16)) {
		TPD_INFO("%s current firmware does not contain goodix fw\n", __func__);
		TPD_INFO("%s cfg package len:%d,firmware size:%d\n", __func__,
			 cfg_pkg_len, (int)firmware->size);
		ret = -1;
		goto exit;
	}

	if (!((firmware->data[cfg_pkg_len + 0] == 'G') &&
			(firmware->data[cfg_pkg_len + 1] == 'X') &&
			(firmware->data[cfg_pkg_len + 2] == 'F') &&
			(firmware->data[cfg_pkg_len + 3] == 'W'))) {
		TPD_INFO("%s can't find fw package\n", __func__);
		TPD_INFO("Data type:%c %c %c %c,dest type is:GXFW\n", firmware->data[cfg_pkg_len + 0],
			 firmware->data[cfg_pkg_len + 1], firmware->data[cfg_pkg_len + 2],
			 firmware->data[cfg_pkg_len + 3]);
		ret = -1;
		goto exit;
	}

	if (firmware->data[cfg_pkg_len + 4] != 1) {
		TPD_INFO("%s can't support this ver:%d\n", __func__,
			 firmware->data[cfg_pkg_len + 4]);
		ret = -1;
		goto exit;
	}

	fw_pkg_len =  getU32(firmware->data + cfg_pkg_len + 8);

	TPD_DEBUG("%s fw package len:%d\n", __func__, fw_pkg_len);
	if ((fw_pkg_len + 16 + cfg_pkg_len) > firmware->size) {
		TPD_INFO("%s bad firmware,need len:%d,actual firmware size:%d\n",
			 __func__, fw_pkg_len + 16 + cfg_pkg_len, (int)firmware->size);
		ret = -1;
		goto exit;
	}

	fw_firmware->size = fw_pkg_len;
	fw_firmware->data = firmware->data + cfg_pkg_len + 16;

	TPD_DEBUG("success get fw,len:%d\n", fw_pkg_len);
	TPD_DEBUG("fw head info:%*ph\n", 32, fw_firmware->data);
	TPD_DEBUG("fw tail info:%*ph\n", 4, &fw_firmware->data[fw_pkg_len - 4 - 1]);
	ret = 0;

exit:
	TPD_DEBUG("%s exit:%d\n", __func__, ret);
	return ret;
}

static fw_update_state goodix_fw_update(void *chip_data,
					const struct firmware *cfg_fw_firmware, bool force)
{
#define FW_UPDATE_RETRY   2
	int retry0 = FW_UPDATE_RETRY;
	int retry1 = FW_UPDATE_RETRY;
	int r, ret;
	struct chip_data_brl *chip_info;
	struct fw_update_ctrl *fwu_ctrl = NULL;
	struct firmware fw_firmware;

	fwu_ctrl = kzalloc(sizeof(struct fw_update_ctrl), GFP_KERNEL);
	if (!fwu_ctrl) {
		TPD_INFO("Failed to alloc memory for fwu_ctrl\n");
		return -ENOMEM;
	}
	chip_info = (struct chip_data_brl *)chip_data;
	fwu_ctrl->chip_info = chip_info;

	r = goodix_get_cfg_parms(chip_data, cfg_fw_firmware);
	if (r < 0) {
		TPD_INFO("%s Failed get cfg from firmware\n", __func__);
	} else {
		TPD_INFO("%s success get ic cfg from firmware\n", __func__);
	}

	r = goodix_get_fw_parms(chip_data, cfg_fw_firmware, &fw_firmware);
	if (r < 0) {
		TPD_INFO("%s Failed get ic fw from firmware\n", __func__);
		goto err_parse_fw;
	} else {
		TPD_INFO("%s success get ic fw from firmware\n", __func__);
	}

	fwu_ctrl->fw_data.firmware = &fw_firmware;
	fwu_ctrl->ic_config = &fwu_ctrl->chip_info->normal_cfg;
	r = goodix_parse_firmware(&fwu_ctrl->fw_data);
	if (r < 0) {
		goto err_parse_fw;
	}

	/* TODO: set force update flag*/
	if (force == false) {
		r = goodix_fw_version_compare(fwu_ctrl);
		if (!r) {
			TPD_INFO("firmware upgraded\n");
			r = FW_NO_NEED_UPDATE;
			goto err_check_update;
		}
	}

start_update:
	do {
		ret = goodix_update_prepare(fwu_ctrl);
		if (ret) {
			TPD_INFO("%s: failed prepare ISP, retry %d\n", __func__,
				 FW_UPDATE_RETRY - retry0);
		}
	} while (ret && --retry0 > 0);
	if (ret) {
		TPD_INFO("%s: Failed to prepare ISP, exit update:%d\n",
			 __func__, ret);
		goto err_fw_prepare;
	}

	/* progress: 20%~100% */
	ret = goodix_flash_firmware(fwu_ctrl);
	if (ret < 0 && --retry1 > 0) {
		TPD_INFO("%s: Bus error, retry firmware update:%d\n", __func__,
			 FW_UPDATE_RETRY - retry1);
		goto start_update;
	}
	if (ret) {
		TPD_INFO("flash fw data enter error\n");
	} else {
		TPD_INFO("flash fw data success, need check version\n");
	}

err_fw_prepare:
	ret = goodix_update_finish(fwu_ctrl);
	if (!ret) {
		TPD_INFO("Firmware update successfully\n");
		r = FW_UPDATE_SUCCESS;
	} else {
		TPD_INFO("%s: Firmware update failed\n", __func__);
		r = FW_UPDATE_ERROR;
	}
err_check_update:
err_parse_fw:
	if (fwu_ctrl) {
		kfree(fwu_ctrl);
		fwu_ctrl = NULL;
	}

	return r;
}

static u32 goodix_u32_trigger_reason(void *chip_data,
				     int gesture_enable, int is_suspended)
{
	int ret = -1;
	u8 touch_num = 0;
	u8 point_type = 0;
	u32 result_event = 0;
	int pre_read_len;
	u8 event_status;
	struct goodix_ic_info_misc *misc;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	memset(chip_info->touch_data, 0, MAX_GT_IRQ_DATA_LENGTH);
	if (chip_info->kernel_grip_support) {
		memset(chip_info->edge_data, 0, MAX_GT_EDGE_DATA_LENGTH);
	}

	misc = &chip_info->ic_info.misc;
	if (!misc->touch_data_addr) {
		TPD_DEBUG("%s: invalied touch data address\n", __func__);
		return IRQ_IGNORE;
	}
	/* touch head + 2 fingers + checksum */
	pre_read_len = IRQ_EVENT_HEAD_LEN +
		       BYTES_PER_POINT * 2 + COOR_DATA_CHECKSUM_SIZE;

	ret = goodix_reg_read(chip_info, misc->touch_data_addr,
			      chip_info->touch_data, pre_read_len);
	if (ret < 0) {
		TPD_DEBUG("%s: spi transfer error!\n", __func__);
		return IRQ_IGNORE;
	}

	if (chip_info->touch_data[0] == 0x00) {
		TPD_DEBUG("invalid touch head");
		return IRQ_IGNORE;
	}

	if (checksum_cmp(chip_info->touch_data,
			 IRQ_EVENT_HEAD_LEN, CHECKSUM_MODE_U8_LE)) {
		TPD_DEBUG("%s: [checksum err !!]touch_head %*ph\n", __func__, IRQ_EVENT_HEAD_LEN,
			chip_info->touch_data);
		goto exit;
	}

	TPD_DEBUG("%s:check->flag:[%*ph]-data:[%*ph]\n", __func__,
		8, chip_info->touch_data, 8, chip_info->touch_data+8);

	event_status = chip_info->touch_data[IRQ_EVENT_TYPE_OFFSET];
	if (event_status & GOODIX_TOUCH_EVENT) {
		touch_num = chip_info->touch_data[POINT_NUM_OFFSET] & 0x0F;
		if (touch_num > MAX_POINT_NUM) {
			TPD_DEBUG("invalid touch num %d\n", touch_num);
			goto exit;
		}
		if (chip_info->kernel_grip_support == true) {
			chip_info->abnormal_grip_coor = false;
			if (event_status & GRIP_COOR_SUPPORT_FLAG) {
				chip_info->get_grip_coor = true;
				ret = goodix_reg_read(chip_info,
					EDGE_INPUT_COORD, chip_info->edge_data,
					EDGE_INPUT_OFFSET * touch_num + 2);
				if (ret < 0) {
					TPD_INFO("%s:spi transfer error!\n", __func__);
					goto exit;
				}
				TPD_DEBUG("grip bit->[0x%x]->girp[%*ph]\n",
					event_status & GRIP_COOR_SUPPORT_FLAG, 16, chip_info->edge_data);
				if (checksum_cmp(chip_info->edge_data,
						EDGE_INPUT_OFFSET * touch_num + 2,
						CHECKSUM_MODE_U8_LE)) {
					chip_info->abnormal_grip_coor = true;
					TPD_INFO("%s: [checksum err!!] girp:[%*ph]\n", __func__,
						16, chip_info->edge_data);
				}
			} else {
				chip_info->get_grip_coor = false;
			}
		}

		if (unlikely(touch_num > 2)) {
			ret = goodix_reg_read(chip_info,
					misc->touch_data_addr + pre_read_len,
					&chip_info->touch_data[pre_read_len],
					(touch_num - 2) * BYTES_PER_POINT);
			if (ret < 0) {
				TPD_DEBUG("read touch point data from coor_addr failed!\n");
				goto exit;
			}
		}

		point_type = chip_info->touch_data[IRQ_EVENT_HEAD_LEN] & 0x0F;
		if (point_type == POINT_TYPE_STYLUS ||
				point_type == POINT_TYPE_STYLUS_HOVER) {
			ret = checksum_cmp(&chip_info->touch_data[IRQ_EVENT_HEAD_LEN],
					BYTES_PER_POINT * 2 + 2, CHECKSUM_MODE_U8_LE);
		} else {
			ret = checksum_cmp(&chip_info->touch_data[IRQ_EVENT_HEAD_LEN],
					BYTES_PER_POINT * touch_num + 2, CHECKSUM_MODE_U8_LE);
		}
		if (ret < 0) {
			TPD_DEBUG("touch data checksum error\n");
			goto exit;
		}
	} else if (!(event_status & (GOODIX_GESTURE_EVENT | GOODIX_FINGER_IDLE_EVENT))) {
		/*TODO check this event*/
	}

	if (gesture_enable && is_suspended &&
		(event_status & GOODIX_GESTURE_EVENT)) {
		result_event = IRQ_GESTURE;
		goto exit;
	} else if (is_suspended) {
		goto exit;
	}
	if (event_status & GOODIX_FINGER_IDLE_EVENT) {
		goto exit;
	}
	if (event_status & GOODIX_REQUEST_EVENT) {/*int request*/
		result_event = IRQ_FW_CONFIG;
		goto exit;
	}
	if (event_status & GOODIX_FINGER_STATUS_EVENT) {
		SET_BIT(result_event, IRQ_FW_HEALTH);
	}

	if (event_status & GOODIX_TOUCH_EVENT) {
		if (point_type == POINT_TYPE_STYLUS ||
				point_type == POINT_TYPE_STYLUS_HOVER) {
			SET_BIT(result_event, IRQ_PEN);
		} else {
			SET_BIT(result_event, IRQ_TOUCH);
		}

		if ((event_status & GOODIX_FINGER_PRINT_EVENT) &&
		    !is_suspended && (chip_info->fp_down_flag == false)) {
			chip_info->fp_down_flag = true;
			SET_BIT(result_event, IRQ_FINGERPRINT);
		} else if (!is_suspended && (event_status & GOODIX_FINGER_PRINT_EVENT) &&
			   (chip_info->fp_down_flag == true)) {
			chip_info->fp_down_flag = false;
			SET_BIT(result_event, IRQ_FINGERPRINT);
		}
	}

exit:
	/* read done */
	goodix_clear_irq(chip_info);
	return result_event;
}

/* Xa=O-K*n; n:0~370 (O:992,k:3) */

#define LEFT_MIN_X_AXIS     12000
#define LEFT_MAX_X_AXIS    5000
static void check_dynamic_limit(struct chip_data_brl *chip_info,
		struct point_info *points, s32 id)
{
	int now_x_pos = points[id].x;
	int dyna_pos = chip_info->motor_get_coord;
	int vir_ori = chip_info->virtual_origin;
	int new_x_axis = 0;
	int k = chip_info->dynamic_k_value;
	int prevent = chip_info->motor_prevent;
	int offect = chip_info->motor_offect;

	switch (chip_info->motor_runing) {
	case true:
		new_x_axis = vir_ori - (k * dyna_pos);
		TPD_DEBUG("[ori:%d,dyna:%d,k:%d]-->new:[Xposi:%d %s Xaxis:%d]\n",
			vir_ori, dyna_pos, k, now_x_pos, (now_x_pos > (new_x_axis + offect)) ? ">" : "<=", new_x_axis);
		if (now_x_pos <= new_x_axis + offect) {
			TPD_DEBUG(" ----->!!!include limit, not to touch!!!<-----\n");
			points[id].tx_press = 0;
			points[id].rx_press = prevent;
			points[id].tx_er =    0;
			points[id].rx_er =    prevent;
		}
		chip_info->runing_x_coord = new_x_axis;
		chip_info->runing_x_offect = offect;
	break;
	case false:
	break;
	default: break;
	}
}

/*#define DATA_CHANGE(grip_data) (grip_data == 255 ? 0 : (grip_data == 0 ? 0 :12))*/
#define DATA_CHANGE(grip_data) (grip_data == 255 ? 0 : grip_data)
static int goodix_get_touch_points(void *chip_data,
			struct point_info *points, int max_num)
{
	int i;
	int touch_map = 0;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	u8 touch_num = 0;
	u8 *coor_data = NULL;
	u8 *ew_data = NULL;
	s32 id = 0;

	/* TPD_DEBUG("%s:check points data = [%*ph]\n", __func__,
		IRQ_EVENT_HEAD_LEN, chip_info->touch_data); */

	touch_num = chip_info->touch_data[POINT_NUM_OFFSET] & 0x0F;
	if (touch_num == 0 || chip_info->abnormal_grip_coor == true) { /*Up event*/
		TPD_DEBUG(" touch_num = %d, abnormal_grip_coor = %d\n",
			touch_num, chip_info->abnormal_grip_coor);
		goto END_TOUCH;
	}

	coor_data = &chip_info->touch_data[IRQ_EVENT_HEAD_LEN];
	ew_data = &chip_info->edge_data[0];

	for (i = 0; i < touch_num; i++) {
		id = (coor_data[0] >> 4) & 0x0F;
		points[id].x = le16_to_cpup((__le16 *)(coor_data + 2));
		points[id].y = le16_to_cpup((__le16 *)(coor_data + 4));
		points[id].z = 0;
		points[id].width_major = 30;
		points[id].status = 1;
		points[id].touch_major = le16_to_cpup((__le16 *)(coor_data + 6));

		points[id].tx_press = DATA_CHANGE(ew_data[1]); /* 0X102F6 */
		points[id].rx_press = DATA_CHANGE(ew_data[0]); /* 0X102F7 */
		points[id].tx_er =    DATA_CHANGE(ew_data[2]); /* 0X102F8 */
		points[id].rx_er =    DATA_CHANGE(ew_data[3]); /* 0X102F9 */

		if (chip_info->motor_coord_support)
			check_dynamic_limit(chip_info, points, id);

		TPD_DEBUG("[TP]->[%s]->points[%d]->(%6d,%6d).tx_press[%2d-%2d].rx_press[%2d-%2d].tx_er[%2d-%2d].rx_er[%2d-%2d]",
				points[id].x > 17000 ? "right" : "left",
				id, points[id].x, points[id].y,
				points[id].tx_press, ew_data[0], points[id].rx_press, ew_data[1],
				points[id].tx_er, ew_data[3], points[id].rx_er, ew_data[2]);

		ew_data += BYTES_PER_EDGE;
		coor_data += BYTES_PER_POINT;
		touch_map |= 0x01 << id;
	}

END_TOUCH:
	return touch_map;
}

static void goodix_get_pen_points(void *chip_data, struct pen_info *pen_info)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	int touch_num;
	u8 *coor_data;
	u8 cur_key_map;
	int16_t x_angle, y_angle;

	TPD_DEBUG("%s:check pen data = [%*ph]\n", __func__,
		IRQ_EVENT_HEAD_LEN, chip_info->touch_data);

	touch_num = chip_info->touch_data[POINT_NUM_OFFSET] & 0x0F;
	if (touch_num == 0) /*Up event*/
		return;

	coor_data = &chip_info->touch_data[IRQ_EVENT_HEAD_LEN];
	pen_info->status = 1;
	pen_info->x = le16_to_cpup((__le16 *)(coor_data + 2));
	pen_info->y = le16_to_cpup((__le16 *)(coor_data + 4));
	pen_info->z = le16_to_cpup((__le16 *)(coor_data + 6));
	if (pen_info->z == 0)
		pen_info->d = 1;
	else
		pen_info->d = 0;
	x_angle = le16_to_cpup((__le16 *)(coor_data + 8));
	y_angle = le16_to_cpup((__le16 *)(coor_data + 10));
	pen_info->tilt_x = x_angle / 100;
	pen_info->tilt_y = y_angle / 100;

	cur_key_map = (chip_info->touch_data[3] & 0x0F) >> 1;
	if (cur_key_map & 0x01)
		pen_info->btn1 = 1;
	if (cur_key_map & 0x02)
		pen_info->btn2 = 1;
}

static int goodix_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	u8 gesture_type;

	TPD_DEBUG("%s:check gesture data = [%*ph]\n", __func__,
		IRQ_EVENT_HEAD_LEN, chip_info->touch_data);

	gesture_type = chip_info->touch_data[4];
	TPD_INFO("%s: get gesture type:0x%x\n", __func__, gesture_type);

	switch (gesture_type) {
	case GOODIX_LEFT2RIGHT_SWIP:
		gesture->gesture_type = LEFT2RIGHT_SWIP;
	break;
	case GOODIX_RIGHT2LEFT_SWIP:
		gesture->gesture_type = RIGHT2LEFT_SWIP;
	break;
	case GOODIX_UP2DOWN_SWIP:
		gesture->gesture_type = UP2DOWN_SWIP;
	break;
	case GOODIX_DOWN2UP_SWIP:
		gesture->gesture_type = DOWN2UP_SWIP;
	break;
	case GOODIX_DOU_SWIP:
		gesture->gesture_type = DOU_SWIP;
	break;
	case GOODIX_DOU_TAP:
		gesture->gesture_type = DOU_TAP;
	break;
	case GOODIX_SINGLE_TAP:
		gesture->gesture_type = SINGLE_TAP;
	break;
	case GOODIX_PENDETECT:
		gesture->gesture_type = PENDETECT;
	break;
	case GOODIX_UP_VEE:
		gesture->gesture_type = UP_VEE;
	break;
	case GOODIX_DOWN_VEE:
		gesture->gesture_type = DOWN_VEE;
	break;
	case GOODIX_LEFT_VEE:
		gesture->gesture_type = LEFT_VEE;
	break;
	case GOODIX_RIGHT_VEE:
		gesture->gesture_type = RIGHT_VEE;
	break;
	case GOODIX_CIRCLE_GESTURE:
		gesture->gesture_type = CIRCLE_GESTURE;
	break;
	case GOODIX_M_GESTRUE:
		gesture->gesture_type = M_GESTRUE;
	break;
	case GOODIX_W_GESTURE:
		gesture->gesture_type = W_GESTURE;
	break;
	default:
		TPD_INFO("%s: unknown gesture type[0x%x]\n", __func__, gesture_type);
	break;
	}
	return 0;
}

static void goodix_get_health_info(void *chip_data, struct monitor_data *mon_data)
{
	/* struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;*/
	return;
}
/*
static int goodix_read_coord_position(void *chip_data, char *page, size_t str)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	TPD_INFO("%s: start to goodix_read_coord_position, str:%d\n", __func__, str);

	snprintf(page, str-1, "Xnow:%d,Xoffect:%d,Xk:%d,Mnow:%d,Moffect:%d,Mtotal:%d-%d~%d-%d\n",
			chip_info->runing_x_coord,
			chip_info->runing_x_offect,
			chip_info->dynamic_k_value,
			chip_info->motor_get_coord,
			chip_info->motor_max_max - chip_info->motor_max_min,
			chip_info->motor_min_min, chip_info->motor_min_max,
			chip_info->motor_max_min, chip_info->motor_max_max);

	return 0;
}
*/
static void set_new_offect(struct chip_data_brl *chip_info, int new_offect)
{
	chip_info->motor_max_min = chip_info->motor_max_max + new_offect;
	chip_info->motor_min_min = chip_info->motor_min_max + new_offect;
	TPD_INFO("%s:new motor para:[%d,%d]~[%d,%d][offect:%d]\n", __func__,
		chip_info->motor_min_min, chip_info->motor_min_max,
		chip_info->motor_max_min, chip_info->motor_max_max,
		new_offect);
}

static void goodix_check_reg(struct chip_data_brl *chip_info, int addr, int lens)
{
	u8 buf[32] = {0};
	goodix_reg_read(chip_info, addr, buf, lens);
	TPD_INFO("%s->[%*ph]\n", __func__, lens, buf);
	return;
}

static int goodix_set_coord_position(void *chip_data, int position)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	int ret = 0;


	if (position < 0) {
		set_new_offect(chip_info, position);
	}

	chip_info->motor_get_coord = position;

	if (position >= chip_info->motor_min_min &&
		position <= chip_info->motor_min_max &&
		chip_info->motor_max_limit == true) {
		ret = goodix_send_cmd_simple(chip_info, GTP_MOTOR_POSITON_MASK, GTP_MASK_DISABLE);
		goodix_check_reg(chip_info, GOODIX_CMD_REG, 8);
		ret = goodix_send_cmd_simple(chip_info, GTP_PLAM_MASK, GTP_MASK_ENABLE);
		goodix_check_reg(chip_info, GOODIX_CMD_REG, 8);
		chip_info->motor_max_limit = false;
		chip_info->motor_runing = false;

		TPD_INFO("%s: MIN position:%d\n", __func__, chip_info->motor_get_coord);
	} else if (position > chip_info->motor_min_max &&
		position <= chip_info->motor_max_min) {
		chip_info->motor_runing = true;
		if (chip_info->motor_max_limit == false) {
			ret = goodix_send_cmd_simple(chip_info, GTP_MOTOR_POSITON_MASK, GTP_MASK_ENABLE);
			goodix_check_reg(chip_info, GOODIX_CMD_REG, 8);
			ret = goodix_send_cmd_simple(chip_info, GTP_PLAM_MASK, GTP_MASK_DISABLE);
			goodix_check_reg(chip_info, GOODIX_CMD_REG, 8);
			chip_info->motor_max_limit = true;
		TPD_INFO("%s: RUNING position:%d\n", __func__, chip_info->motor_get_coord);
		}
	} else if (position > chip_info->motor_max_min &&
		position <= chip_info->motor_max_max &&
		chip_info->motor_runing == true) {
		chip_info->motor_runing = false;
		ret = goodix_send_cmd_simple(chip_info, GTP_PLAM_MASK, GTP_MASK_ENABLE);
		goodix_check_reg(chip_info, GOODIX_CMD_REG, 8);
		TPD_INFO("%s: MAX position:%d\n", __func__, chip_info->motor_get_coord);
	}

	return ret;
}

static int goodix_mode_switch(void *chip_data, work_mode mode, int flag)
{
	int ret = -1;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	if (!chip_info->ic_info.length) {
		if ((mode == MODE_NORMAL) && (flag == true)) {
			TPD_INFO("%s: goodix ic info invalid, but probe, continue\n", __func__);
			return 0;
		}
		TPD_INFO("%s: goodix ic info invalid\n", __func__);
		return ret;
	}
	if (chip_info->halt_status && (mode != MODE_NORMAL)) {
		goodix_reset(chip_info);
	}

	switch (mode) {
	case MODE_NORMAL:
		ret = 0;
		break;

	case MODE_SLEEP:
		ret = goodix_enter_sleep(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: goodix enter sleep failed\n", __func__);
		}
		break;

	case MODE_GESTURE:
		ret = goodix_enable_gesture(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: goodix enable:(%d) gesture failed.\n", __func__, flag);
			return ret;
		}
		break;

	case MODE_EDGE:
		ret = goodix_enable_edge_limit(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: goodix enable:(%d) edge limit failed.\n", __func__, flag);
			return ret;
		}
		break;

	case MODE_CHARGE:
		ret = goodix_enable_charge_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable charge mode : %d failed\n", __func__, flag);
		}
		break;

	case MODE_GAME:
		ret = goodix_enable_game_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable game mode : %d failed\n", __func__, flag);
		}
		break;

	case MODE_PEN_SCAN:
		ret = goodix_enable_pen_mode(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable pen mode : %d failed\n", __func__, flag);
		}
		break;
	case MODE_PEN_CTL:
		ret = goodix_pen_control(chip_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: enable pen control : %d failed\n", __func__, flag);
		}
		break;
	default:
		TPD_INFO("%s: mode %d not support.\n", __func__, mode);
	}

	return ret;
}

static int goodix_esd_handle(void *chip_data)
{
	s32 ret = -1;
	u8 esd_buf = 0;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	struct goodix_ic_info_misc *misc = &chip_info->ic_info.misc;

	if (!chip_info->esd_check_enabled || !misc->esd_addr) {
		TPD_DEBUG("%s: close\n", __func__);
		return 0;
	}

	ret = goodix_reg_read(chip_info, misc->esd_addr, &esd_buf, 1);
	if ((ret < 0) || esd_buf == 0xAA) {
		TPD_INFO("%s: esd dynamic esd occur, ret = %d, esd_buf = %d.\n",
			 __func__, ret, esd_buf);
		TPD_INFO("%s: IC works abnormally! Process esd reset.\n", __func__);
		disable_irq_nosync(chip_info->irq);

		goodix_power_control(chip_info, false);
		msleep(30);
		goodix_power_control(chip_info, true);
		usleep_range(10000, 10100);

		goodix_reset(chip_data);

		tp_touch_btnkey_release(chip_info->tp_index);

		enable_irq(chip_info->irq);
		TPD_INFO("%s: Goodix esd reset over.\n", __func__);
		chip_info->esd_err_count++;
		return -1;
	} else {
		esd_buf = 0xAA;
		ret = goodix_reg_write(chip_info,
				       chip_info->ic_info.misc.esd_addr, &esd_buf, 1);
		if (ret < 0) {
			TPD_INFO("%s: Failed to reset esd reg.\n", __func__);
		}
	}

	return 0;
}

static void goodix_enable_fingerprint_underscreen(void *chip_data, uint32_t enable)
{
	int ret = 0;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	TPD_INFO("%s, enable = %d\n", __func__, enable);
	if (enable) {
		ret = goodix_send_cmd_simple(chip_info, GTP_CMD_FOD_FINGER_PRINT, GTP_MASK_DISABLE);
	} else {
		ret = goodix_send_cmd_simple(chip_info, GTP_CMD_FOD_FINGER_PRINT, GTP_MASK_ENABLE);
	}

	return;
}

static void goodix_enable_gesture_mask(void *chip_data, uint32_t enable)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	TPD_INFO("%s, enable = %d\n", __func__, enable);
	TPD_INFO("%s: gesture_type:0x%08X\n", __func__, chip_info->gesture_type);
	/* if (enable) {
		enable all gesture type
		chip_info->gesture_type = 0xFFFFFFFF;
	} else {
		chip_info->gesture_type = 0x00000000;
	} */
}

static void goodix_set_gesture_state(void *chip_data, int state)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	SET_GESTURE_BIT(state, DOU_TAP, chip_info->gesture_type, 7);
	SET_GESTURE_BIT(state, UP_VEE, chip_info->gesture_type, 17);
	SET_GESTURE_BIT(state, DOWN_VEE, chip_info->gesture_type, 16);
	SET_GESTURE_BIT(state, LEFT_VEE, chip_info->gesture_type, 19);
	SET_GESTURE_BIT(state, RIGHT_VEE, chip_info->gesture_type, 18);
	SET_GESTURE_BIT(state, CIRCLE_GESTURE, chip_info->gesture_type, 4);
	SET_GESTURE_BIT(state, DOU_SWIP, chip_info->gesture_type, 20);
	SET_GESTURE_BIT(state, LEFT2RIGHT_SWIP, chip_info->gesture_type, 10);
	SET_GESTURE_BIT(state, RIGHT2LEFT_SWIP, chip_info->gesture_type, 11);
	SET_GESTURE_BIT(state, UP2DOWN_SWIP, chip_info->gesture_type, 24);
	SET_GESTURE_BIT(state, DOWN2UP_SWIP, chip_info->gesture_type, 25);
	SET_GESTURE_BIT(state, M_GESTRUE, chip_info->gesture_type, 3);
	SET_GESTURE_BIT(state, W_GESTURE, chip_info->gesture_type, 5);
	SET_GESTURE_BIT(state, SINGLE_TAP, chip_info->gesture_type, 12);
	/* SET_GESTURE_BIT(state, HEART, chip_info->gesture_type, 17); */
	/* SET_GESTURE_BIT(state, S_GESTURE, chip_info->gesture_type, 18); */
	SET_GESTURE_BIT(state, PENDETECT, chip_info->gesture_type, 23);

	TPD_INFO("%s: gesture_type:0x%08X\n", __func__, chip_info->gesture_type);
}

static void goodix_screenon_fingerprint_info(void *chip_data,
		struct fp_underscreen_info *fp_tpinfo)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	if (chip_info->fp_down_flag) {
		fp_tpinfo->x = chip_info->fp_coor_report.fp_x_coor;
		fp_tpinfo->y = chip_info->fp_coor_report.fp_y_coor;
		fp_tpinfo->area_rate = chip_info->fp_coor_report.fp_area;
		fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;
	} else {
		fp_tpinfo->x = chip_info->fp_coor_report.fp_x_coor;
		fp_tpinfo->y = chip_info->fp_coor_report.fp_y_coor;
		fp_tpinfo->area_rate = chip_info->fp_coor_report.fp_area;
		fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;
	}
}

static int goodix_request_event_handler(struct chip_data_brl *chip_info)
{
	int ret = -1;
	u8 rqst_code = 0;

	rqst_code = chip_info->touch_data[REQUEST_EVENT_TYPE_OFFSET];
	TPD_INFO("%s: request state:0x%02x.\n", __func__, rqst_code);

	switch (rqst_code) {
	case GTP_RQST_CONFIG:
		TPD_INFO("HW request config.\n");
		ret = goodix_send_config(chip_info, chip_info->normal_cfg.data,
					 chip_info->normal_cfg.length);
		if (ret) {
			TPD_INFO("request config, send config faild.\n");
		}
		break;
	case GTP_RQST_RESET:
		TPD_INFO("%s: HW requset reset.\n", __func__);
		goodix_reset(chip_info);
		break;
	default:
		TPD_INFO("%s: Unknown hw request:%d.\n", __func__, rqst_code);
		break;
	}

	return 0;
}

static int goodix_fw_handle(void *chip_data)
{
	int ret = 0;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	ret = goodix_request_event_handler(chip_info);

	return ret;
}

static void goodix_register_info_read(void *chip_data,
			uint16_t register_addr, uint8_t *result, uint8_t length)
{
	/*struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	TODO need change oplus framework to support u32 address*/
}

static void goodix_set_touch_direction(void *chip_data, uint8_t dir)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	chip_info->touch_direction = dir;
}

static uint8_t goodix_get_touch_direction(void *chip_data)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	return chip_info->touch_direction;
}

static int goodix_specific_resume_operate(void *chip_data,
		struct specific_resume_data *p_resume_data)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	TPD_DEBUG("%s call\n", __func__);
	goodix_esd_check_enable(chip_info, true);
	return 0;
}

/* high frame default enable 60s */
static int goodix_set_high_frame_rate(void *chip_data, int level, int time)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	int ret = 0;

	TPD_INFO("%s, %s high frame rate, %d\n", __func__, !!level > 0 ? "enable" : "disable", time);
	if (!!level) {
		ret = goodix_send_cmd_simple(chip_info, GTP_GAME_HIGH_FRAME, GTP_MASK_ENABLE);
	} else {
		ret = goodix_send_cmd_simple(chip_info, GTP_GAME_HIGH_FRAME, GTP_MASK_DISABLE);
	}

	return 0;
}

struct oplus_touchpanel_operations goodix_ops = {
	.ftm_process                 = goodix_ftm_process,
	.get_vendor                  = goodix_get_vendor,
	.get_chip_info               = goodix_get_chip_info,
	.reset                       = goodix_reset,
	.power_control               = goodix_power_control,
	.fw_check                    = goodix_fw_check,
	.fw_update                   = goodix_fw_update,
	.trigger_reason              = goodix_u32_trigger_reason,
	.get_touch_points            = goodix_get_touch_points,
	.get_pen_points              = goodix_get_pen_points,
	.get_gesture_info            = goodix_get_gesture_info,
	.mode_switch                 = goodix_mode_switch,
	.esd_handle                  = goodix_esd_handle,
	.fw_handle                   = goodix_fw_handle,
	.register_info_read          = goodix_register_info_read,
	.enable_fingerprint          = goodix_enable_fingerprint_underscreen,
	.enable_gesture_mask         = goodix_enable_gesture_mask,
	.screenon_fingerprint_info   = goodix_screenon_fingerprint_info,
	.set_gesture_state         	 = goodix_set_gesture_state,
	.set_touch_direction         = goodix_set_touch_direction,
	.get_touch_direction         = goodix_get_touch_direction,
	.specific_resume_operate     = goodix_specific_resume_operate,
	.health_report               = goodix_get_health_info,
	.set_high_frame_rate         = goodix_set_high_frame_rate,
};
/********* End of implementation of oplus_touchpanel_operations callbacks**********************/

/******** Start of implementation of debug_info_proc_operations callbacks*********************/
static void goodix_debug_info_read(struct seq_file *s,
				   void *chip_data, debug_type debug_type)
{
	int ret = -1, i = 0, j = 0;
	u8 *kernel_buf = NULL;
	int addr = 0;
	u8 clear_state = 0;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	struct goodix_ic_info *ic_info;
	int tx_num = 0;
	int rx_num = 0;
	s16 data = 0;

	/*keep in active mode
	goodix_send_cmd_simple(chip_info, GTP_CMD_ENTER_DOZE_TIME, 0xFF);*/

	ic_info = &chip_info->ic_info;
	rx_num = ic_info->parm.sen_num;
	tx_num = ic_info->parm.drv_num;
	if (!tx_num || !rx_num) {
		TPD_INFO("%s: error invalid tx %d or rx %d num\n",
			 __func__, tx_num, rx_num);
		return;
	}

	kernel_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (kernel_buf == NULL) {
		TPD_INFO("%s kmalloc error\n", __func__);
		return;
	}
	switch (debug_type) {
	case GTP_RAWDATA:
		addr = ic_info->misc.mutual_rawdata_addr;
		break;
	case GTP_DIFFDATA:
		addr = ic_info->misc.mutual_diffdata_addr;
		break;
	default:
		addr = ic_info->misc.mutual_refdata_addr;
		break;
	}
	gt8x_rawdiff_mode = 1;
	goodix_send_cmd_simple(chip_info, GTP_CMD_RAWDATA, 0);
	msleep(20);
	goodix_reg_write(chip_info, ic_info->misc.touch_data_addr, &clear_state, 1);

	while (i++ < 10) {
		ret = goodix_reg_read(chip_info, ic_info->misc.touch_data_addr, kernel_buf, 1);
		TPD_INFO("ret = %d  kernel_buf = %d\n", ret, kernel_buf[0]);
		if (!ret && (kernel_buf[0] & 0x80)) {
			TPD_INFO("Data ready OK\n");
			break;
		}
		msleep(20);
	}
	if (i >= 10) {
		TPD_INFO("data not ready, quit!\n");
		goto read_data_exit;
	}

	ret = goodix_reg_read(chip_info, addr, kernel_buf, tx_num * rx_num * 2);
	usleep_range(5000, 5100);

	for (i = 0; i < rx_num; i++) {
		seq_printf(s, "[%2d] ", i);
		for (j = 0; j < tx_num; j++) {
			data = kernel_buf[j * rx_num * 2 + i * 2] + (kernel_buf[j * rx_num * 2 + i * 2 + 1] << 8);
			seq_printf(s, "%4d ", data);
		}
		seq_printf(s, "\n");
	}
read_data_exit:
	goodix_send_cmd_simple(chip_info, GTP_CMD_NORMAL, 0);
	gt8x_rawdiff_mode = 0;
	goodix_reg_write(chip_info, ic_info->misc.touch_data_addr, &clear_state, 1);
	/*be normal in idle
	goodix_send_cmd_simple(chip_info, GTP_CMD_DEFULT_DOZE_TIME, 0x00);*/

	kfree(kernel_buf);
	return;
}

static void goodix_delta_read(struct seq_file *s, void *chip_data)
{
	goodix_debug_info_read(s, chip_data, GTP_DIFFDATA);
}

static void goodix_baseline_read(struct seq_file *s, void *chip_data)
{
	goodix_debug_info_read(s, chip_data, GTP_RAWDATA);
}

static void goodix_main_register_read(struct seq_file *s, void *chip_data)
{
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	struct goodix_ic_info_misc *ic_info_misc;
	int ret = 0;
	u8 touch_data[IRQ_EVENT_HEAD_LEN + BYTES_PER_POINT + COOR_DATA_CHECKSUM_SIZE] = {0};

	ic_info_misc = &chip_info->ic_info.misc;
	seq_printf(s, "====================================================\n");
	if (chip_info->p_tp_fw) {
		seq_printf(s, "tp fw = 0x%s\n", chip_info->p_tp_fw);
	}
	ret = goodix_reg_read(chip_info, ic_info_misc->touch_data_addr,
			      touch_data, sizeof(touch_data));
	if (ret < 0) {
		TPD_INFO("%s: i2c transfer error!\n", __func__);
		goto out;
	}
	seq_printf(s, "cached touch_data: %*ph\n", (int)sizeof(touch_data), chip_info->touch_data);
	TPD_INFO("%s: cached touch_data: %*ph\n", __func__, (int)sizeof(touch_data), chip_info->touch_data);
	seq_printf(s, "current touch_data: %*ph\n", (int)sizeof(touch_data), touch_data);
	TPD_INFO("%s: current touch_data: %*ph\n", __func__, (int)sizeof(touch_data), touch_data);
	seq_printf(s, "====================================================\n");
out:
	return;
}

static struct debug_info_proc_operations debug_info_proc_ops = {
	/*    .limit_read         = goodix_limit_read,*/
	.delta_read         = goodix_delta_read,
	.baseline_read      = goodix_baseline_read,
	.main_register_read = goodix_main_register_read,
};
/********* End of implementation of debug_info_proc_operations callbacks**********************/

/************** Start of callback of proc/Goodix/config_version node**************************/

/* success return config_len, <= 0 failed */
int goodix_read_config(struct chip_data_brl *chip_info, u8 *cfg, int size)
{
	int ret;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &chip_info->ic_info.misc;
	struct goodix_config_head cfg_head;

	if (!cfg) {
		return -1;
	}

	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_START;
	ret = send_cfg_cmd(chip_info, &cfg_cmd);
	if (ret) {
		TPD_INFO("%s: failed send config read prepare command\n", __func__);
		return ret;
	}

	ret = goodix_reg_read(chip_info, misc->fw_buffer_addr,
			      cfg_head.buf, sizeof(cfg_head));
	if (ret) {
		TPD_INFO("%s: failed read config head %d\n", __func__, ret);
		goto exit;
	}

	if (checksum_cmp(cfg_head.buf, sizeof(cfg_head), CHECKSUM_MODE_U8_LE)) {
		TPD_INFO("%s: config head checksum error\n", __func__);
		ret = -1;
		goto exit;
	}

	cfg_head.cfg_len = le16_to_cpu(cfg_head.cfg_len);
	if (cfg_head.cfg_len > misc->fw_buffer_max_len ||
	    cfg_head.cfg_len > size) {
		TPD_INFO("%s: cfg len exceed buffer size %d > %d\n", __func__, cfg_head.cfg_len,
			 misc->fw_buffer_max_len);
		ret = -1;
		goto exit;
	}

	memcpy(cfg, cfg_head.buf, sizeof(cfg_head));
	ret = goodix_reg_read(chip_info, misc->fw_buffer_addr + sizeof(cfg_head),
			      cfg + sizeof(cfg_head), cfg_head.cfg_len);
	if (ret) {
		TPD_INFO("%s: failed read cfg pack, %d\n", __func__, ret);
		goto exit;
	}

	TPD_INFO("config len %d\n", cfg_head.cfg_len);
	if (checksum_cmp(cfg + sizeof(cfg_head),
			 cfg_head.cfg_len, CHECKSUM_MODE_U16_LE)) {
		TPD_INFO("%s: config body checksum error\n", __func__);
		ret = -1;
		goto exit;
	}
	TPD_INFO("success read config data: len %zu\n",
		 cfg_head.cfg_len + sizeof(cfg_head));
exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CND_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_EXIT;
	if (send_cfg_cmd(chip_info, &cfg_cmd)) {
		TPD_INFO("%s: failed send config read finish command\n", __func__);
		ret = -1;
	}
	if (ret) {
		return -1;
	}
	return cfg_head.cfg_len + sizeof(cfg_head);
}

static void goodix_config_info_read(struct seq_file *s, void *chip_data)
{
	int ret = 0, i = 0;
	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;
	struct goodix_fw_version *fw_version = &chip_info->ver_info;
	struct goodix_ic_info *ic_info = &chip_info->ic_info;

	seq_printf(s, "==== Goodix default config setting in driver====\n");
	for (i = 0; i < TS_CFG_MAX_LEN && i < chip_info->normal_cfg.length; i++) {
		seq_printf(s, "0x%02X, ", chip_info->normal_cfg.data[i]);
		if ((i + 1) % 32 == 0) {
			seq_printf(s, "\n");
		}
	}
	seq_printf(s, "\n");
	seq_printf(s, "==== Goodix test cfg in driver====\n");
	for (i = 0; i < TS_CFG_MAX_LEN && i < chip_info->test_cfg.length; i++) {
		seq_printf(s, "0x%02X, ", chip_info->test_cfg.data[i]);
		if ((i + 1) % 32 == 0) {
			seq_printf(s, "\n");
		}
	}
	seq_printf(s, "\n");
	seq_printf(s, "==== Goodix noise test cfg in driver====\n");
	for (i = 0; i < TS_CFG_MAX_LEN && i < chip_info->noise_test_cfg.length; i++) {
		seq_printf(s, "0x%02X, ", chip_info->noise_test_cfg.data[i]);
		if ((i + 1) % 32 == 0) {
			seq_printf(s, "\n");
		}
	}
	seq_printf(s, "\n");

	seq_printf(s, "==== Goodix config read from chip====\n");

	ret = brl_get_ic_info(chip_info, ic_info);
	if (ret) {
		TPD_INFO("%s: failed get ic info, ret %d\n", __func__, ret);
		goto exit;
	}
	ret = brl_read_version(chip_info, fw_version);
	if (ret) {
		TPD_INFO("goodix_config_info_read goodix_read_config error:%d\n", ret);
		goto exit;
	}

	seq_printf(s, "\n");
	seq_printf(s, "==== Goodix Version Info ====\n");
	seq_printf(s, "ConfigVer: 0x%02X\n", ic_info->version.config_version);
	seq_printf(s, "ProductID: GT%s\n", fw_version->patch_pid);
	seq_printf(s, "PatchID: %*ph\n", (int)sizeof(fw_version->patch_pid), fw_version->patch_pid);
	seq_printf(s, "MaskID: %*ph\n", (int)sizeof(fw_version->rom_vid), fw_version->rom_vid);
	seq_printf(s, "SensorID: %d\n", fw_version->sensor_id);

exit:
	return;
}
/*************** End of callback of proc/Goodix/config_version node***************************/

/************** Start of auto test func**************************/
#define ABS(val)			((val < 0)? -(val) : val)
#define MAX(a, b)			((a > b)? a : b)
static void goodix_cache_deltadata(struct chip_data_brl *chip_data)
{
	u32 data_size;
	struct goodix_ts_test *ts_test = chip_data->brl_test;
	int tx = chip_data->ic_info.parm.drv_num;
	int j;
	int max_val;
	int raw;
	int temp;

	data_size = ts_test->rawdata.size;

		for (j = 0; j < data_size; j++) {
			raw = ts_test->rawdata.data[j];
			max_val = 0;
			/* calcu delta with above node */
			if (j - tx >= 0) {
				temp = ts_test->rawdata.data[j - tx];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			/* calcu delta with bellow node */
			if (j + tx < data_size) {
				temp = ts_test->rawdata.data[j + tx];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			/* calcu delta with left node */
			if (j % tx) {
				temp = ts_test->rawdata.data[j - 1];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			/* calcu delta with right node */
			if ((j + 1) % tx) {
				temp = ts_test->rawdata.data[j + 1];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			ts_test->deltadata.data[j] = max_val * 1000 / raw;
		}
}

static int brl_auto_test_preoperation(struct seq_file *s,
				       void *chip_data,
				       struct auto_testdata *goodix_testdata,
				       struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;
	s16 *buf;
	int size;
	struct goodix_ts_cmd temp_cmd;
	int ret;
	int discard_cnt = 5;
	u32 coor_addr = cd->ic_info.misc.touch_data_addr;
	u32 rawdata_addr = cd->ic_info.misc.mutual_rawdata_addr;
	u32 noisedata_addr = cd->ic_info.misc.mutual_diffdata_addr;
	u32 self_rawdata_addr = cd->ic_info.misc.self_rawdata_addr;
	u8 status;
	int retry;

	TPD_INFO("%s IN\n", __func__);

	cd->brl_test = kzalloc(sizeof(struct goodix_ts_test), GFP_KERNEL);
	cd->brl_test->rawdata.data = kzalloc(tx * rx * 2, GFP_KERNEL);
	cd->brl_test->rawdata.size = tx * rx;
	cd->brl_test->noisedata.data = kzalloc(tx * rx * 2, GFP_KERNEL);
	cd->brl_test->noisedata.size = tx * rx;

	cd->brl_test->deltadata.data = kzalloc(tx * rx * 2, GFP_KERNEL);
	cd->brl_test->deltadata.size = tx * rx;

	cd->brl_test->self_rawdata.data = kzalloc((tx + rx) * 2, GFP_KERNEL);
	cd->brl_test->self_rawdata.size = tx + rx;

	/* disabled irq */
	disable_irq(cd->irq);

	if (cd->test_cfg.length > 0) {
		ret = goodix_send_config(cd, cd->test_cfg.data, cd->test_cfg.length);
		if (ret < 0) {
			TPD_INFO("send test config failed\n");
			return -EINVAL;
		}
	}

	temp_cmd.cmd = 2;
	temp_cmd.len = 4;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		TPD_INFO("%s Enter rawdata mode failed\n", __func__);
		return ret;
	}

	status = 0;
	while (--discard_cnt) {
		msleep(20);
		goodix_reg_write(cd, coor_addr, &status, 1);
	}

	retry = 20;
	while (retry--) {
		usleep_range(5000, 5100);
		goodix_reg_read(cd, coor_addr, &status, 1);
		if (status == 0x80)
			break;
	}
	if (retry < 0) {
		TPD_INFO("rawdata is not ready val:0x%02x exit", status);
		return -EINVAL;
	}

	/* cache rawdata */
	buf = cd->brl_test->rawdata.data;
	size = cd->brl_test->rawdata.size;
	goodix_reg_read(cd, rawdata_addr, (u8 *)buf, size * 2);
	goodix_rotate_abcd2cbad(tx, rx, buf);

	/* cache noisedata */
	buf = cd->brl_test->noisedata.data;
	size = cd->brl_test->noisedata.size;
	goodix_reg_read(cd, noisedata_addr, (u8 *)buf, size * 2);
	goodix_rotate_abcd2cbad(tx, rx, buf);

	/* cache self rawdata */
	buf = cd->brl_test->self_rawdata.data;
	size = cd->brl_test->self_rawdata.size;
	goodix_reg_read(cd, self_rawdata_addr, (u8 *)buf, size * 2);

	TPD_INFO("%s OUT\n", __func__);

	return 0;
}

/*
#define EXT_OSC_FREQ	64000000
#define INTER_CLK_FREQ	144000000
static int brl_clk_test(struct seq_file *s,
				void *chip_data,
			    struct auto_testdata *goodix_testdata,
				struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	u8 prepare_cmd[] = {0x00, 0x00, 0x04, 0x0D, 0x11, 0x00};
	u8 start_cmd[] = {0x00, 0x00, 0x04, 0x0E, 0x12, 0x00};
	u32 cmd_addr = cd->ic_info.misc.cmd_addr;
	u32 frame_buf_addr = cd->ic_info.misc.fw_buffer_addr;
	u8 rcv_buf[8];
	struct clk_test_parm clk_parm;
	u64 cal_freq;
	u64 clk_in_cnt;
	u64 clk_osc_cnt;
	u64 freq_delta;
	int retry;
	int ret = RESULT_ERR;

	TPD_INFO("%s IN\n", __func__);
	goodix_reset(cd);

	goodix_spi_write(cd, cmd_addr, prepare_cmd, sizeof(prepare_cmd));

	retry = 10;
	while (retry--) {
		msleep(20);
		goodix_spi_read(cd, cmd_addr, rcv_buf, 2);
		if (rcv_buf[0] == 0x08 && rcv_buf[1] == 0x80)
			break;
	}
	if (retry < 0) {
		TPD_INFO("%s: switch clk test mode failed, sta[%x] ack[%x]\n",
				__func__, rcv_buf[0], rcv_buf[1]);
		goto exit;
	}

	clk_parm.gio = 14;
	clk_parm.div = 1;
	clk_parm.gio_set = 0;
	clk_parm.en = 1;
	clk_parm.osc_en_io = 9;
	clk_parm.trigger_mode = 0;
	clk_parm.clk_in_num = 1000;
	goodix_append_checksum(clk_parm.buf, sizeof(clk_parm) - 2, CHECKSUM_MODE_U8_LE);
	goodix_spi_write(cd, frame_buf_addr, clk_parm.buf, sizeof(clk_parm));

	goodix_spi_write(cd, cmd_addr, start_cmd, sizeof(start_cmd));
	retry = 20;
	while (retry--) {
		msleep(50);
		goodix_spi_read(cd, cmd_addr, rcv_buf, 2);
		if (rcv_buf[0] == 0x0B && rcv_buf[1] == 0x80)
			break;
	}
	if (retry < 0) {
		TPD_INFO("%s: wait clk test result failed, sta[%x] ack[%x]\n",
				__func__, rcv_buf[0], rcv_buf[1]);
		goto exit;
	}

	goodix_spi_read(cd, frame_buf_addr + sizeof(clk_parm), rcv_buf, sizeof(rcv_buf));
	if (checksum_cmp(rcv_buf, sizeof(rcv_buf), CHECKSUM_MODE_U8_LE)) {
		TPD_INFO("%s: clk test result checksum error, [%*ph]\n",
				__func__, (int)sizeof(rcv_buf), rcv_buf);
		goto exit;
	}

	clk_in_cnt = le16_to_cpup((__le16 *)rcv_buf);
	clk_osc_cnt = le32_to_cpup((__le32 *)&rcv_buf[2]);
	cal_freq = clk_in_cnt * 8 * INTER_CLK_FREQ / clk_osc_cnt;
	TPD_INFO("%s: clk_in_cnt:%lld clk_osc_cnt:%lld cal_freq:%lld\n",
			__func__, clk_in_cnt, clk_osc_cnt, cal_freq);

	if (EXT_OSC_FREQ > cal_freq)
		freq_delta = EXT_OSC_FREQ - cal_freq;
	else
		freq_delta = cal_freq - EXT_OSC_FREQ;
	if (freq_delta * 100 / EXT_OSC_FREQ > 2) {
		TPD_INFO("%s: osc clk test failed\n", __func__);
	} else {
		TPD_INFO("%s: osc clk test pass\n", __func__);
		ret = 0;
	}

exit:
	return ret;
}
*/
static int brl_noisedata_test(struct seq_file *s,
				void *chip_data,
			    struct auto_testdata *goodix_testdata,
				struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	struct goodix_ts_test *ts_test = cd->brl_test;
	int val;
	int noise_threshold;
	int err_cnt = 0;
	int i;

	TPD_INFO("%s IN\n", __func__);

	if (p_test_item_info->para_num && p_test_item_info->p_buffer[0]) {
		ts_test->is_item_support[TYPE_TEST1] = 1;
		noise_threshold = p_test_item_info->p_buffer[1];
		TPD_INFO("noise_threshold:%d\n", noise_threshold);
	} else {
		TPD_INFO("skip noisedata test\n");
		return 0;
	}

	ts_test->test_result[TYPE_TEST1] = GTP_TEST_OK;
	for (i = 0; i < ts_test->noisedata.size; i++) {
		val = ts_test->noisedata.data[i];
		val = ABS(val);
		if (val > noise_threshold) {
			TPD_INFO("noisedata[%d]:%d > noise_threshold[%d]\n", i, val, noise_threshold);
			err_cnt++;
		}
	}

	if (err_cnt > 0) {
		ts_test->test_result[TYPE_TEST1] = GTP_TEST_NG;
		return RESULT_ERR;
	}

	return 0;
}

static int brl_capacitance_test(struct seq_file *s,
				void *chip_data,
			    struct auto_testdata *goodix_testdata,
				struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	struct goodix_ts_test *ts_test = cd->brl_test;
	int i;
	int val;
	int err_cnt = 0;
	int32_t *max_limit;
	int32_t *min_limit;

	TPD_INFO("%s IN\n", __func__);

	if (p_test_item_info->para_num && p_test_item_info->p_buffer[0]) {
		ts_test->is_item_support[TYPE_TEST2] = 1;
		if (p_test_item_info->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
				max_limit = (int32_t *)(goodix_testdata->fw->data +
								       p_test_item_info->top_limit_offset);
				min_limit = (int32_t *)(goodix_testdata->fw->data +
								       p_test_item_info->floor_limit_offset);
		} else {
			TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
			return RESULT_ERR;
		}
	} else {
		TPD_INFO("skip rawdata test\n");
		return 0;
	}

	ts_test->test_result[TYPE_TEST2] = GTP_TEST_OK;
	for (i = 0; i < ts_test->rawdata.size; i++) {
		val = ts_test->rawdata.data[i];
		if (val > max_limit[i] || val < min_limit[i]) {
			TPD_INFO("rawdata[%d] out of threshold[%d,%d]\n", val, min_limit[i], max_limit[i]);
			err_cnt++;
		}
	}

	if (err_cnt > 0) {
		ts_test->test_result[TYPE_TEST2] = GTP_TEST_NG;
		return RESULT_ERR;
	}

	return 0;
}

static int brl_deltacapacitance(struct seq_file *s,
				void *chip_data,
			    struct auto_testdata *goodix_testdata,
				struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	struct goodix_ts_test *ts_test = cd->brl_test;
	int i;
	int val;
	int err_cnt = 0;
	int32_t *delta_threshold;

	TPD_INFO("%s IN\n", __func__);

	if (p_test_item_info->para_num && p_test_item_info->p_buffer[0]) {
		ts_test->is_item_support[TYPE_TEST3] = 1;
		if (p_test_item_info->item_limit_type == LIMIT_TYPE_TX_RX_DATA) {
			delta_threshold = (int32_t *)(goodix_testdata->fw->data +
								p_test_item_info->top_limit_offset);
		} else {
			TPD_INFO("item: %d get_test_item_info fail\n", LIMIT_TYPE_TX_RX_DATA);
			return RESULT_ERR;
		}
	} else {
		TPD_INFO("skip deltadata test\n");
		return 0;
	}

	goodix_cache_deltadata(cd);

	ts_test->test_result[TYPE_TEST3] = GTP_TEST_OK;
	for (i = 0; i < ts_test->deltadata.size; i++) {
		val = ts_test->deltadata.data[i];
		if (val > delta_threshold[i]) {
			TPD_INFO("deltadata[%d] out of threshold[%d]\n", val, delta_threshold[i]);
			err_cnt++;
		}
	}

	if (err_cnt > 0) {
		ts_test->test_result[TYPE_TEST3] = GTP_TEST_NG;
		return RESULT_ERR;
	}

	return 0;
}

static int brl_self_rawcapacitance(struct seq_file *s,
				void *chip_data,
			    struct auto_testdata *goodix_testdata,
				struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	struct goodix_ts_test *ts_test = cd->brl_test;
	int i;
	int val;
	int err_cnt = 0;
	int32_t *max_limit;
	int32_t *min_limit;

	TPD_INFO("%s IN\n", __func__);

	if (p_test_item_info->para_num && p_test_item_info->p_buffer[0]) {
		ts_test->is_item_support[TYPE_TEST4] = 1;

		if (p_test_item_info->item_limit_type == LIMIT_TYPE_SLEF_TX_RX_DATA) {
			max_limit = (int32_t *)(goodix_testdata->fw->data +
							    p_test_item_info->top_limit_offset);
			min_limit = (int32_t *)(goodix_testdata->fw->data +
							    p_test_item_info->floor_limit_offset);
		} else {
			TPD_INFO("item: %d get_test_item_info fail\n", TYPE_TEST4);
			return RESULT_ERR;
		}
	} else {
		TPD_INFO("skip self_rawdata test\n");
		return 0;
	}

	ts_test->test_result[TYPE_TEST4] = GTP_TEST_OK;
	for (i = 0; i < ts_test->self_rawdata.size; i++) {
		val = ts_test->self_rawdata.data[i];
		if (val > max_limit[i] || val < min_limit[i]) {
			TPD_INFO("self_rawdata[%d] out of threshold[%d,%d]\n", val, min_limit[i], max_limit[i]);
			err_cnt++;
		}
	}

	if (err_cnt > 0) {
		ts_test->test_result[TYPE_TEST4] = GTP_TEST_NG;
		return RESULT_ERR;
	}

	return 0;
}

static int brl_short_test_prepare(struct chip_data_brl *cd)
{
	struct goodix_ts_cmd tmp_cmd;
	int ret;
	int retry;
	int resend = 3;
	u8 status;

	TPD_INFO("short test prepare IN\n");

	tmp_cmd.len = 4;
	tmp_cmd.cmd = INSPECT_FW_SWITCH_CMD;

resend_cmd:
	goodix_reset(cd);
	ret = brl_send_cmd(cd, &tmp_cmd);
	if (ret < 0) {
		TPD_INFO("send test mode failed\n");
		return ret;
	}

	retry = 3;
	while (retry--) {
		msleep(40);
		ret = goodix_reg_read(cd, SHORT_TEST_RUN_REG, &status, 1);
		if (!ret && status == SHORT_TEST_RUN_FLAG)
			return 0;
		TPD_INFO("short_mode_status=0x%02x ret=%d\n", status, ret);
	}

	if (resend--)
		goto resend_cmd;

	return -EINVAL;
}

static u32 map_die2pin(struct ts_test_params *test_params, u32 chn_num)
{
	int i = 0;
	u32 res = 255;

	if (chn_num & DRV_CHANNEL_FLAG)
		chn_num = (chn_num & ~DRV_CHANNEL_FLAG) + test_params->max_sen_num;

	for (i = 0; i < test_params->max_sen_num; i++) {
		if (test_params->sen_map[i] == chn_num) {
			res = i;
			break;
		}
	}
	/* res != 255 mean found the corresponding channel num */
	if (res != 255)
		return res;
	/* if cannot find in SenMap try find in DrvMap */
	for (i = 0; i < test_params->max_drv_num; i++) {
		if (test_params->drv_map[i] == chn_num) {
			res = i;
			break;
		}
	}
	if (i >= test_params->max_drv_num)
		TPD_INFO("Faild found corrresponding channel num:%d\n", chn_num);
	else
		res |= DRV_CHANNEL_FLAG;

	return res;
}

static int gdix_check_tx_tx_shortcircut(struct chip_data_brl *cd,
        u8 short_ch_num)
{
	int ret = 0, err = 0;
	u32 r_threshold = 0, short_r = 0;
	int size = 0, i = 0, j = 0;
	u16 adc_signal = 0;
	u8 master_pin_num, slave_pin_num;
	u8 *data_buf;
	u32 data_reg = DRV_DRV_SELFCODE_REG_BRB;
	struct goodix_ts_test *ts_test = cd->brl_test;
	struct ts_test_params *test_params = &ts_test->test_params;
	int max_drv_num = test_params->max_drv_num;
	int max_sen_num = test_params->max_sen_num;
	u16 self_capdata, short_die_num = 0;

	size = 4 + max_drv_num * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		return -ENOMEM;
	}
	/* drv&drv shortcircut check */
	for (i = 0; i < short_ch_num; i++) {
		ret = goodix_reg_read(cd, data_reg, data_buf, size);
		if (ret < 0) {
			TPD_INFO("Failed read Drv-to-Drv short rawdata\n");
			err = -EINVAL;
			break;
		}

		if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
			TPD_INFO("Drv-to-Drv adc data checksum error\n");
			err = -EINVAL;
			break;
		}

		r_threshold = test_params->r_drv_drv_threshold;
		short_die_num = le16_to_cpup((__le16 *)&data_buf[0]);
		short_die_num -= max_sen_num;
		if (short_die_num >= max_drv_num) {
			TPD_INFO("invalid short pad num:%d\n",
				short_die_num + max_sen_num);
			continue;
		}

		/* TODO: j start position need recheck */
		self_capdata = le16_to_cpup((__le16 *)&data_buf[2]);
		if (self_capdata == 0xffff || self_capdata == 0) {
			TPD_INFO("invalid self_capdata:0x%x\n", self_capdata);
			continue;
		}

		for (j = short_die_num + 1; j < max_drv_num; j++) {
			adc_signal = le16_to_cpup((__le16 *)&data_buf[4 + j * 2]);

			if (adc_signal < test_params->short_threshold)
				continue;

			short_r = (u32)cal_cha_to_cha_res(self_capdata, adc_signal);
			if (short_r < r_threshold) {
				master_pin_num =
					map_die2pin(test_params, short_die_num + max_sen_num);
				slave_pin_num =
					map_die2pin(test_params, j + max_sen_num);
				if (master_pin_num == 0xFF || slave_pin_num == 0xFF) {
					TPD_INFO("WARNNING invalid pin\n");
					continue;
				}
				TPD_INFO("short circut:R=%dK,R_Threshold=%dK",
							short_r, r_threshold);
				TPD_INFO("%s%d--%s%d shortcircut\n",
					(master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
					(master_pin_num & ~DRV_CHANNEL_FLAG),
					(slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
					(slave_pin_num & ~DRV_CHANNEL_FLAG));
				err = -EINVAL;
			}
		}
		data_reg += size;
	}

	kfree(data_buf);
	return err;
}

static int gdix_check_rx_rx_shortcircut(struct chip_data_brl *cd,
        u8 short_ch_num)
{
	int ret = 0, err = 0;
	u32 r_threshold = 0, short_r = 0;
	int size = 0, i = 0, j = 0;
	u16 adc_signal = 0;
	u8 master_pin_num, slave_pin_num;
	u8 *data_buf;
	u32 data_reg = SEN_SEN_SELFCODE_REG_BRB;
	struct goodix_ts_test *ts_test = cd->brl_test;
	struct ts_test_params *test_params = &ts_test->test_params;
	int max_sen_num = test_params->max_sen_num;
	u16 self_capdata, short_die_num = 0;

	size = 4 + max_sen_num * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		return -ENOMEM;
	}
	/* drv&drv shortcircut check */
	for (i = 0; i < short_ch_num; i++) {
		ret = goodix_reg_read(cd, data_reg, data_buf, size);
		if (ret) {
			TPD_INFO("Failed read Sen-to-Sen short rawdata\n");
			err = -EINVAL;
			break;
		}

		if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
			TPD_INFO("Sen-to-Sen adc data checksum error\n");
			err = -EINVAL;
			break;
		}

		r_threshold = test_params->r_sen_sen_threshold;
		short_die_num = le16_to_cpup((__le16 *)&data_buf[0]);
		if (short_die_num >= max_sen_num) {
			TPD_INFO("invalid short pad num:%d\n", short_die_num);
			continue;
		}

		/* TODO: j start position need recheck */
		self_capdata = le16_to_cpup((__le16 *)&data_buf[2]);
		if (self_capdata == 0xffff || self_capdata == 0) {
			TPD_INFO("invalid self_capdata:0x%x\n", self_capdata);
			continue;
		}

		for (j = short_die_num + 1; j < max_sen_num; j++) {
			adc_signal = le16_to_cpup((__le16 *)&data_buf[4 + j * 2]);

			if (adc_signal < test_params->short_threshold)
				continue;

			short_r = (u32)cal_cha_to_cha_res(self_capdata, adc_signal);
			if (short_r < r_threshold) {
				master_pin_num = map_die2pin(test_params, short_die_num);
				slave_pin_num = map_die2pin(test_params, j);
				if (master_pin_num == 0xFF || slave_pin_num == 0xFF) {
					TPD_INFO("WARNNING invalid pin\n");
					continue;
				}
				TPD_INFO("short circut:R=%dK,R_Threshold=%dK\n",
							short_r, r_threshold);
				TPD_INFO("%s%d--%s%d shortcircut\n",
					(master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
					(master_pin_num & ~DRV_CHANNEL_FLAG),
					(slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
					(slave_pin_num & ~DRV_CHANNEL_FLAG));
				err = -EINVAL;
			}
		}
		data_reg += size;
	}

	kfree(data_buf);
	return err;
}

static int gdix_check_tx_rx_shortcircut(struct chip_data_brl *cd,
        u8 short_ch_num)
{
	int ret = 0, err = 0;
	u32 r_threshold = 0, short_r = 0;
	int size = 0, i = 0, j = 0;
	u16 adc_signal = 0;
	u8 master_pin_num, slave_pin_num;
	u8 *data_buf = NULL;
	u32 data_reg = DRV_SEN_SELFCODE_REG_BRB;
	struct goodix_ts_test *ts_test = cd->brl_test;
	struct ts_test_params *test_params = &ts_test->test_params;
	int max_drv_num = test_params->max_drv_num;
	int max_sen_num = test_params->max_sen_num;
	u16 self_capdata, short_die_num = 0;

	size = 4 + max_drv_num * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		return -ENOMEM;
	}
	/* drv&sen shortcircut check */
	for (i = 0; i < short_ch_num; i++) {
		ret = goodix_reg_read(cd, data_reg, data_buf, size);
		if (ret) {
			TPD_INFO("Failed read Drv-to-Sen short rawdata\n");
			err = -EINVAL;
			break;
		}

		if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
			TPD_INFO("Drv-to-Sen adc data checksum error\n");
			err = -EINVAL;
			break;
		}

		r_threshold = test_params->r_drv_sen_threshold;
		short_die_num = le16_to_cpup((__le16 *)&data_buf[0]);
		if (short_die_num >= max_sen_num) {
			TPD_INFO("invalid short pad num:%d\n", short_die_num);
			continue;
		}

		/* TODO: j start position need recheck */
		self_capdata = le16_to_cpup((__le16 *)&data_buf[2]);
		if (self_capdata == 0xffff || self_capdata == 0) {
			TPD_INFO("invalid self_capdata:0x%x\n", self_capdata);
			continue;
		}

		for (j = 0; j < max_drv_num; j++) {
			adc_signal = le16_to_cpup((__le16 *)&data_buf[4 + j * 2]);

			if (adc_signal < test_params->short_threshold)
				continue;

			short_r = (u32)cal_cha_to_cha_res(self_capdata, adc_signal);
			if (short_r < r_threshold) {
				master_pin_num = map_die2pin(test_params, short_die_num);
				slave_pin_num = map_die2pin(test_params, j + max_sen_num);
				if (master_pin_num == 0xFF || slave_pin_num == 0xFF) {
					TPD_INFO("WARNNING invalid pin\n");
					continue;
				}
				TPD_INFO("short circut:R=%dK,R_Threshold=%dK\n",
							short_r, r_threshold);
				TPD_INFO("%s%d--%s%d shortcircut\n",
					(master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
					(master_pin_num & ~DRV_CHANNEL_FLAG),
					(slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
					(slave_pin_num & ~DRV_CHANNEL_FLAG));
				err = -EINVAL;
			}
		}
		data_reg += size;
	}

	kfree(data_buf);
	return err;
}

static int gdix_check_resistance_to_gnd(struct chip_data_brl *cd,
        u16 adc_signal, u32 pos)
{
	long r = 0;
	u16 r_th = 0, avdd_value = 0;
	u16 chn_id_tmp = 0;
	u8 pin_num = 0;
	unsigned short short_type;
	struct goodix_ts_test *ts_test = cd->brl_test;
	struct ts_test_params *test_params = &ts_test->test_params;
	int max_drv_num = test_params->max_drv_num;
	int max_sen_num = test_params->max_sen_num;

	avdd_value = test_params->avdd_value;
	short_type = adc_signal & 0x8000;
	adc_signal &= ~0x8000;
	if (adc_signal == 0)
		adc_signal = 1;

	if (short_type == 0) {
		/* short to GND */
		r = cal_cha_to_gnd_res(adc_signal);
	} else {
		/* short to VDD */
		r = cal_cha_to_avdd_res(adc_signal, avdd_value);
	}

	if (pos < max_drv_num)
		r_th = test_params->r_drv_gnd_threshold;
	else
		r_th = test_params->r_sen_gnd_threshold;

	chn_id_tmp = pos;
	if (chn_id_tmp < max_drv_num)
		chn_id_tmp += max_sen_num;
	else
		chn_id_tmp -= max_drv_num;

	if (r < r_th) {
		pin_num = map_die2pin(test_params, chn_id_tmp);
		TPD_INFO("%s%d shortcircut to %s,R=%ldK,R_Threshold=%dK\n",
				(pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN",
				(pin_num & ~DRV_CHANNEL_FLAG),
				short_type ? "VDD" : "GND",
				r, r_th);
		return -EINVAL;
	}

	return 0;
}

static int gdix_check_gndvdd_shortcircut(struct chip_data_brl *cd)
{
	int ret = 0, err = 0;
	int size = 0, i = 0;
	u16 adc_signal = 0;
	u32 data_reg = DIFF_CODE_DATA_REG_BRB;
	u8 *data_buf = NULL;
	struct goodix_ts_test *ts_test = cd->brl_test;
	int max_drv_num = ts_test->test_params.max_drv_num;
	int max_sen_num = ts_test->test_params.max_sen_num;

	size = (max_drv_num + max_sen_num) * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		return -ENOMEM;
	}
	/* read diff code, diff code will be used to calculate
		* resistance between channel and GND */
	ret = goodix_reg_read(cd, data_reg, data_buf, size);
	if (ret < 0) {
		TPD_INFO("Failed read to-gnd rawdata\n");
		err = -EINVAL;
		goto err_out;
	}

	if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
		TPD_INFO("diff code checksum error\n");
		err = -EINVAL;
		goto err_out;
	}

	for (i = 0; i < max_drv_num + max_sen_num; i++) {
		adc_signal = le16_to_cpup((__le16 *)&data_buf[i * 2]);
		ret = gdix_check_resistance_to_gnd(cd,
					adc_signal, i);
		if (ret != 0) {
			TPD_INFO("Resistance to-gnd/vdd short\n");
			err = ret;
		}
	}

err_out:
	kfree(data_buf);
	return err;
}

static int brl_shortcircut_analysis(struct chip_data_brl *cd)
{
	int ret;
	int err = 0;
	test_result_t test_result;

	ret = goodix_reg_read(cd, SHORT_TEST_RESULT_REG_BRB,
		(u8 *)&test_result, sizeof(test_result));
	if (ret < 0) {
		TPD_INFO("Read TEST_RESULT_REG failed\n");
		return ret;
	}

	if (checksum_cmp((u8 *)&test_result, sizeof(test_result),
		CHECKSUM_MODE_U8_LE)) {
		TPD_INFO("shrot result checksum err\n");
		return -EINVAL;
	}

	if (!(test_result.result & 0x0F)) {
		TPD_INFO(">>>>> No shortcircut\n");
		return 0;
	}
	TPD_INFO("short flag 0x%02x, drv&drv:%d, sen&sen:%d, drv&sen:%d, drv/GNDVDD:%d, sen/GNDVDD:%d\n",
		test_result.result, test_result.drv_drv_num, test_result.sen_sen_num,
		test_result.drv_sen_num, test_result.drv_gnd_avdd_num, test_result.sen_gnd_avdd_num);

	if (test_result.drv_drv_num)
		err |= gdix_check_tx_tx_shortcircut(cd, test_result.drv_drv_num);
	if (test_result.sen_sen_num)
		err |= gdix_check_rx_rx_shortcircut(cd, test_result.sen_sen_num);
	if (test_result.drv_sen_num)
		err |= gdix_check_tx_rx_shortcircut(cd, test_result.drv_sen_num);
	if (test_result.drv_gnd_avdd_num || test_result.sen_gnd_avdd_num)
		err |= gdix_check_gndvdd_shortcircut(cd);

	TPD_INFO(">>>>> short check return 0x%x\n", err);

	return err;
}

static int brl_shortcircut_test(struct seq_file *s,
				void *chip_data,
			    struct auto_testdata *goodix_testdata,
				struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	struct goodix_ts_test *ts_test = cd->brl_test;
	struct ts_test_params *test_params = &ts_test->test_params;
	u16 test_time;
	u8 status;
	int retry;
	int ret;

	TPD_INFO("%s IN\n", __func__);

	if (p_test_item_info->para_num && p_test_item_info->p_buffer[0]) {
		ts_test->is_item_support[TYPE_TEST5] = 1;
		/* store data to test_parms */
		test_params->short_threshold = p_test_item_info->p_buffer[1];
		test_params->r_drv_drv_threshold = p_test_item_info->p_buffer[2];
		test_params->r_drv_sen_threshold = p_test_item_info->p_buffer[3];
		test_params->r_sen_sen_threshold = p_test_item_info->p_buffer[4];
		test_params->r_drv_gnd_threshold = p_test_item_info->p_buffer[5];
		test_params->r_sen_gnd_threshold = p_test_item_info->p_buffer[6];
		test_params->avdd_value = p_test_item_info->p_buffer[7];
		test_params->max_drv_num = MAX_DRV_NUM_BRB;
		test_params->max_sen_num = MAX_SEN_NUM_BRB;
		test_params->drv_map = brl_b_drv_map;
		test_params->sen_map = brl_b_sen_map;
	} else {
		TPD_INFO("skip shortcircuit test\n");
		return 0;
	}

	ret = brl_short_test_prepare(cd);
	if (ret < 0) {
		TPD_INFO("Failed enter short test mode\n");
		return RESULT_ERR;
	}

	msleep(300);

	/* get short test time */
	ret = goodix_reg_read(cd, SHORT_TEST_TIME_REG_BRB, (u8 *)&test_time, 2);
	if (ret < 0) {
		TPD_INFO("Failed to get test_time, default %dms\n", DEFAULT_TEST_TIME_MS);
		test_time = DEFAULT_TEST_TIME_MS;
	} else {
		if (test_time > MAX_TEST_TIME_MS) {
			TPD_INFO("test time too long %d > %d\n",
				test_time, MAX_TEST_TIME_MS);
			test_time = MAX_TEST_TIME_MS;
		}
		TPD_INFO("get test time %dms\n", test_time);
	}

	/* start short circuit test */
	status = 0;
	goodix_reg_write(cd, SHORT_TEST_RUN_REG, &status, 1);

	/* wait short test finish */
	if (test_time > 0)
		msleep(test_time);

	retry = 50;
	while (retry--) {
		ret = goodix_reg_read(cd, SHORT_TEST_STATUS_REG_BRB, &status, 1);
		if (!ret && status == SHORT_TEST_FINISH_FLAG)
			break;
		msleep(50);
	}
	if (retry < 0) {
		TPD_INFO("short test failed, status:0x%02x\n", status);
		return RESULT_ERR;
	}

	/* start analysis short result */
	TPD_INFO("short_test finished, start analysis\n");
	ret = brl_shortcircut_analysis(cd);
	if (ret < 0)
		return RESULT_ERR;

	ts_test->test_result[TYPE_TEST5] = GTP_TEST_OK;
	return 0;
}

static void brl_put_test_result(struct seq_file *s, void *chip_data,
				 struct auto_testdata *p_testdata,
				 struct test_item_info *p_test_item_info)
{
	uint8_t  data_buf[64];
	int i;
	/*save test fail result*/
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	struct goodix_ts_test *ts_test = cd->brl_test;

	memset(data_buf, 0, sizeof(data_buf));

	if (ts_test->rawdata.size && ts_test->is_item_support[TYPE_TEST2]) {
		if (!IS_ERR_OR_NULL(p_testdata->fp)) {
			snprintf(data_buf, 64, "%s\n", "[RAW DATA]");
			tp_test_write(p_testdata->fp, p_testdata->length,
				      data_buf, strlen(data_buf), p_testdata->pos);
		}

		for (i = 0; i < ts_test->rawdata.size; i++) {
			if (!IS_ERR_OR_NULL(p_testdata->fp)) {
				snprintf(data_buf, 64, "%d,", ts_test->rawdata.data[i]);
				tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
					      p_testdata->pos);

				if (!((i + 1) % p_testdata->rx_num) && (i != 0)) {
					snprintf(data_buf, 64, "\n");
					tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
						      p_testdata->pos);
				}
			}
		}
	}

	if (ts_test->noisedata.size && ts_test->is_item_support[TYPE_TEST1]) {
		if (!IS_ERR_OR_NULL(p_testdata->fp)) {
			snprintf(data_buf, 64, "\n%s\n", "[NOISE DATA]");
			tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
				      p_testdata->pos);
		}

		for (i = 0; i < ts_test->noisedata.size; i++) {
			if (!IS_ERR_OR_NULL(p_testdata->fp)) {
				sprintf(data_buf, "%d,", ts_test->noisedata.data[i]);
				tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
					      p_testdata->pos);

				if (!((i + 1) % p_testdata->rx_num) && (i != 0)) {
					snprintf(data_buf, 64, "\n");
					tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
						      p_testdata->pos);
				}
			}
		}
	}

	if (ts_test->deltadata.size && ts_test->is_item_support[TYPE_TEST3]) {
		if (!IS_ERR_OR_NULL(p_testdata->fp)) {
			snprintf(data_buf, 64, "\n%s\n", "[DELTA DATA]");
			tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
				      p_testdata->pos);
		}

		for (i = 0; i < ts_test->deltadata.size; i++) {
			if (!IS_ERR_OR_NULL(p_testdata->fp)) {
				sprintf(data_buf, "%d,", ts_test->deltadata.data[i]);
				tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
					      p_testdata->pos);

				if (!((i + 1) % p_testdata->rx_num) && (i != 0)) {
					snprintf(data_buf, 64, "\n");
					tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
						      p_testdata->pos);
				}
			}
		}
	}

	if (ts_test->self_rawdata.size && ts_test->is_item_support[TYPE_TEST4]) {
		if (!IS_ERR_OR_NULL(p_testdata->fp)) {
			snprintf(data_buf, 64, "\n%s\n", "[SELF RAW DATA]");
			tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
				      p_testdata->pos);
		}

		for (i = 0; i < ts_test->self_rawdata.size; i++) {
			if (!IS_ERR_OR_NULL(p_testdata->fp)) {
				sprintf(data_buf, "%d,", ts_test->self_rawdata.data[i]);
				tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
					      p_testdata->pos);
			}
		}

		if (!IS_ERR_OR_NULL(p_testdata->fp)) {
			sprintf(data_buf, "\n");
			tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
				      p_testdata->pos);
		}
	}

	if (!IS_ERR_OR_NULL(p_testdata->fp)) {
		snprintf(data_buf, 64, "\nTX:%d,RX:%d\n", p_testdata->tx_num, p_testdata->rx_num);
		tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
			      p_testdata->pos);
	}

	if (!IS_ERR_OR_NULL(p_testdata->fp)) {
		snprintf(data_buf, 64, "Img version:%x, device version:%x\n\n",
			 p_testdata->tp_fw, p_testdata->dev_tp_fw);
		tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
			      p_testdata->pos);
	}

	for (i = 0; i < MAX_TEST_ITEMS; i++) {
		/* if have tested, show result */
		if (!ts_test->is_item_support[i])
			continue;

		TPD_INFO("test_result_info %s: %s\n", test_item_name[i],
				ts_test->test_result[i] ? "pass" : "fail");
		if (!IS_ERR_OR_NULL(p_testdata->fp)) {
			snprintf(data_buf, 64, "%s: %s\n", test_item_name[i],
					ts_test->test_result[i] ? "pass" : "fail");
			tp_test_write(p_testdata->fp, p_testdata->length, data_buf, strlen(data_buf),
						p_testdata->pos);
		}
	}

	TPD_INFO("%s exit\n", __func__);
	return;
}

static int brl_auto_test_endoperation(struct seq_file *s,
				void *chip_data,
				struct auto_testdata *p_testdata,
				struct test_item_info *p_test_item_info)
{
	struct chip_data_brl *cd = (struct chip_data_brl *)chip_data;
	struct goodix_fw_version ic_ver;
	struct goodix_ic_info ic_info;
	u32 fw_ver = 0;
	u8 cfg_ver = 0;
	int ret;

	TPD_INFO("%s IN\n", __func__);

	goodix_reset(cd);
	if (cd->normal_cfg.length > 0) {
		ret = goodix_send_config(cd, cd->normal_cfg.data, cd->normal_cfg.length);
		if (ret < 0)
			TPD_INFO("send normal config failed\n");
	}

	/*read device fw version*/
	brl_read_version(cd, &ic_ver);
	fw_ver = le32_to_cpup((__le32 *)&ic_ver.patch_vid[0]);
	brl_get_ic_info(cd, &ic_info);
	cfg_ver = ic_info.version.config_version;
	p_testdata->dev_tp_fw = (fw_ver << 8) + cfg_ver;

	enable_irq(cd->irq);

	brl_put_test_result(s, chip_data, p_testdata, p_test_item_info);

	kfree(cd->brl_test->rawdata.data);
	kfree(cd->brl_test->noisedata.data);
	kfree(cd->brl_test->self_rawdata.data);
	kfree(cd->brl_test->deltadata.data);
	kfree(cd->brl_test);

	return 0;
}

/*************** End of atuo test func***************************/
static struct goodix_auto_test_operations goodix_test_ops = {
	.auto_test_preoperation = brl_auto_test_preoperation,
	.test1 = brl_noisedata_test,
	.test2 = brl_capacitance_test,
	.test3 = brl_deltacapacitance,
	.test4 = brl_self_rawcapacitance,
	.test5 = brl_shortcircut_test,
	/*.test6 = brl_clk_test,*/
	.auto_test_endoperation = brl_auto_test_endoperation,
};

static struct engineer_test_operations goodix_engineer_test_ops = {
	.auto_test                  = goodix_auto_test,
};

struct goodix_proc_operations goodix_brl_proc_ops = {
	.goodix_config_info_read    = goodix_config_info_read,
};

static void init_motor_para(struct chip_data_brl *chip_info)
{
	chip_info->motor_max_max = chip_info->motor_coord_para[1];
	chip_info->motor_max_min = chip_info->motor_coord_para[1] - chip_info->motor_coord_para[2];
	chip_info->motor_min_max = chip_info->motor_coord_para[0] + chip_info->motor_coord_para[2];
	chip_info->motor_min_min = chip_info->motor_coord_para[0];

	chip_info->virtual_origin = chip_info->motor_dynamic_limit[0];
	chip_info->motor_offect = chip_info->motor_dynamic_limit[1];
	chip_info->motor_prevent = chip_info->motor_dynamic_limit[2];
	chip_info->dynamic_k_value = chip_info->virtual_origin / chip_info->motor_coord_para[1];

	TPD_INFO("motor parse OK:[%3d~%3d,%3d~%3d],virtual_origin:%d,k:%d,offect:%d,prevent:%d",
			chip_info->motor_min_min, chip_info->motor_min_max,
			chip_info->motor_max_min, chip_info->motor_max_max,
			chip_info->virtual_origin, chip_info->dynamic_k_value,
			chip_info->motor_offect, chip_info->motor_prevent);

	chip_info->motor_max_limit = false;
	chip_info->motor_runing = false;
	chip_info->get_grip_coor = false;
	return;
}

static void init_goodix_chip_dts(struct device *dev, void *chip_data)
{
	int rc;
	int i = 0, cnt = 0;
	struct device_node *np;
	struct device_node *chip_np;
	int temp_array[MAX_SIZE];

	struct chip_data_brl *chip_info = (struct chip_data_brl *)chip_data;

	np = dev->of_node;

	chip_info->snr_read_support = of_property_read_bool(np, "snr_read_support");

	chip_np = of_get_child_by_name(np, "GT9966");
	if (!chip_np) {
		TPD_INFO(" fail get child node for gt9966");
		return;
	} else
		TPD_INFO(" success get child node for gt9966");

	rc = of_property_read_u32_array(chip_np, "gt9966_get_motor_coord", temp_array, MOTOR_COORD_PARA);
	if (rc) {
		TPD_INFO("fail get gt9966_get_motor_coord %d\n", rc);
	} else {
		chip_info->motor_coord_support = true;
		for (i = 0; i < MOTOR_COORD_PARA; i++) {
			chip_info->motor_coord_para[i] = temp_array[i];
			cnt += chip_info->motor_coord_para[i];
		}
		if (0 == cnt) {
			TPD_INFO("invalid data!! stop to parse\n");
			goto OUT_ERR;
		}
		goodix_set_coord_position(chip_info, 1);
	}

	rc = of_property_read_u32_array(chip_np, "gt9966_dynamic_limit", temp_array, MOTOR_DYNA_LIMIT);
	if (rc) {
		TPD_INFO("fail get gt9966_dynamic_limit %d\n", rc);
	} else {
		for (i = 0; i < MOTOR_DYNA_LIMIT; i++) {
			chip_info->motor_dynamic_limit[i] = temp_array[i];
			cnt += chip_info->motor_dynamic_limit[i];
		}
		if (0 == cnt) {
			TPD_INFO("invalid data!! stop to parse\n");
			goto OUT_ERR;
		}
	}

	if (chip_info->motor_coord_support == true)
		init_motor_para(chip_info);

	return;
OUT_ERR:
	chip_info->motor_coord_support = false;
	return;
}


static int goodix_gt9966_ts_probe(struct spi_device *spi)
{
	struct chip_data_brl *chip_info = NULL;
	struct touchpanel_data *ts = NULL;
	int ret = -1;

	TPD_INFO("Goodix driver version: %s\n", GOODIX_DRIVER_VERSION);

	/* 2. Alloc chip_info */
	chip_info = kzalloc(sizeof(struct chip_data_brl), GFP_KERNEL);
	if (chip_info == NULL) {
		TPD_INFO("chip info kzalloc error\n");
		ret = -ENOMEM;
		return ret;
	}

	/* 3. Alloc common ts */
	ts = common_touch_data_alloc();
	if (ts == NULL) {
		TPD_INFO("ts kzalloc error\n");
		goto ts_malloc_failed;
	}

	/* 4. alloc touch data space */
	chip_info->touch_data = kzalloc(MAX_GT_IRQ_DATA_LENGTH, GFP_KERNEL);
	if (chip_info->touch_data == NULL) {
		TPD_INFO("touch_data kzalloc error\n");
		goto err_register_driver;
	}

	chip_info->edge_data = kzalloc(MAX_GT_EDGE_DATA_LENGTH, GFP_KERNEL);
	if (chip_info->edge_data == NULL) {
		TPD_INFO("edge_data kzalloc error\n");
		goto err_touch_data_alloc;
	}

	/* init spi_device */
	spi->mode          = SPI_MODE_0;
	spi->bits_per_word = 8;

	ret = spi_setup(spi);
	if (ret) {
		TPD_INFO("failed set spi mode, %d\n", ret);
		goto err_edge_data_alloc;
	}

	ts->dev = &spi->dev;
	ts->s_client = spi;
	ts->irq = spi->irq;
	ts->chip_data = chip_info;
	spi_set_drvdata(spi, ts);

	chip_info->hw_res = &ts->hw_res;
	chip_info->s_client = spi;
	chip_info->goodix_ops = &goodix_brl_proc_ops;
	ts->com_test_data.chip_test_ops = &goodix_test_ops;
	ts->engineer_ops = &goodix_engineer_test_ops;
	ts->debug_info_ops = &debug_info_proc_ops;
	/* 6. file_operations callbacks binding */
	ts->ts_ops = &goodix_ops;

	init_goodix_chip_dts(ts->dev, chip_info);
	ts->bus_type = TP_BUS_SPI;

	/* 8. register common touch device */
	ret = register_common_touch_device(ts);
	if (ret < 0) {
		goto err_edge_data_alloc;
	}

	/* 9. create goodix tool node */
	gtx8_init_tool_node(ts, &gt8x_rawdiff_mode);

	chip_info->kernel_grip_support = ts->kernel_grip_support;
	chip_info->tp_index = ts->tp_index;

	/* 10. create goodix debug files
	Goodix_create_proc(ts, chip_info->goodix_ops);
	*/

	goodix_esd_check_enable(chip_info, true);

	TPD_INFO("%s, probe normal end\n", __func__);

	return 0;

err_edge_data_alloc:
	if (chip_info->edge_data) {
		kfree(chip_info->edge_data);
	}
	chip_info->edge_data = NULL;

err_touch_data_alloc:
	if (chip_info->touch_data) {
		kfree(chip_info->touch_data);
	}
	chip_info->touch_data = NULL;

err_register_driver:
	common_touch_data_free(ts);
	ts = NULL;

ts_malloc_failed:
	kfree(chip_info);
	chip_info = NULL;

	TPD_INFO("%s, probe error\n", __func__);
	return ret;
}

static int goodix_ts_pm_suspend(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called\n", __func__);
	tp_pm_suspend(ts);

	return 0;
}

static int goodix_ts_pm_resume(struct device *dev)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s is called\n", __func__);
	tp_pm_resume(ts);

	return 0;
}

static int __maybe_unused goodix_gt9966_ts_remove(struct spi_device *spi)
{
	struct touchpanel_data *ts = spi_get_drvdata(spi);

	TPD_INFO("%s is called\n", __func__);
	kfree(ts);

	return 0;
}

static const struct dev_pm_ops dev_pm_ops = {
	.suspend = goodix_ts_pm_suspend,
	.resume = goodix_ts_pm_resume,
};

static struct of_device_id gt9966_match_table[] = {
	{ .compatible = "goodix-gt9966", }
};

static struct spi_driver gt9966_ts_driver = {
	.driver = {
		.name = GOODIX_CORE_DRIVER_NAME,
		.of_match_table = gt9966_match_table,
		.pm = &dev_pm_ops,
	},
	.probe = goodix_gt9966_ts_probe,
	.remove = goodix_gt9966_ts_remove,
};

/***********************Start of module init and exit****************************/
int __init tp_driver_init_gt9966(void)
{
	TPD_INFO("%s is called\n", __func__);

	if (!tp_judge_ic_match(GOODIX_CORE_DRIVER_NAME)) {
		TPD_INFO("%s not match\n", __func__);
		goto OUT;
	}

	if (spi_register_driver(&gt9966_ts_driver) != 0) {
		TPD_INFO("%s : unable to add spi driver.\n", __func__);
		goto OUT;
	}

OUT:
	return 0;
}

void __exit tp_driver_exit_gt9966(void)
{
	TPD_INFO("%s : Core layer exit", __func__);
	spi_unregister_driver(&gt9966_ts_driver);
}

#ifdef CONFIG_TOUCHPANEL_LATE_INIT
late_initcall(tp_driver_init_gt9966);
#else
module_init(tp_driver_init_gt9966);
#endif

module_exit(tp_driver_exit_gt9966);
/***********************End of module init and exit*******************************/
MODULE_AUTHOR("Goodix, Inc.");
MODULE_DESCRIPTION("GTP Touchpanel Driver");
MODULE_LICENSE("GPL v2");
