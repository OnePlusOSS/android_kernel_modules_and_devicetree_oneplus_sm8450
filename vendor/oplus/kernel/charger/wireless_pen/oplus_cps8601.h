// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CPS8601_H__
#define __OPLUS_CPS8601_H__

#include <linux/version.h>

#define CPS_WLS_FAIL	-1
#define CPS_WLS_SUCCESS	0

#define ADDR_BUFFER0	0x20000A00
#define ADDR_BUFFER1	0x20000B00
#define ADDR_CMD	0x200009FC
#define ADDR_FLAG	0x200009F8
#define ADDR_BUF_SIZE	0x20001808
#define ADDR_FW_VER	0x2000180C

#define ADDR_EN_32BIT_IIC	0xFFFFFF00
#define CMD_EN_32BIT_IIC	0x0000000E

#define ADDR_IIC_ENDIAN	0x40040030
#define CMD_IIC_LITTLE_ENDIAN	0xFFFFFFFF
#define CMD_IIC_BIG_ENDIAN	0x00000000

#define ADDR_PASSWORD	0x40040070
#define PASSWORD	0x0000A061

#define ADDR_SYSTEM_CMD	0x40040008
#define CMD_RESET_HALT_MCU	0x00000008
#define CMD_DIS_TRIMMING_LOAD	0x00000020
#define CMD_RESET_ALL_SYS	0x00000001

#define ADDR_DISABLE_DMA	0x4000E000
#define CMD_DISABLE_DMA	0x00000000

#define ADDR_WDOG_COUNT_OVER	0x40040048
#define CMD_WDOG_COUNT_OVER	0x00000FFF

#define ADDR_BOOTLOADER	0x20000000
#define BOOTLOADER_LEN	0x00000990
#define FIRMWARE_LEN	0x00003C00

#define ADDR_TRIMMING_LOAD	0x40040014
#define CMD_DIS_TRIM_LOAD	0x00000001

#define ADDR_ERASE_MTP	0x200009FC
#define CMD_ERASE_MTP	0x00000070

#define PGM_BUFFER0	0x10
#define PGM_BUFFER1	0x20
#define PGM_BUFFER2	0x30
#define PGM_BUFFER3	0x40
#define PGM_BUFFER0_1	0x50
#define PGM_ERASER_0	0x60
#define PGM_ERASER_1	0x70
#define PGM_WR_FLAG	0x80

#define CACL_CRC_APP	0x90
#define CACL_CRC_TEST	0xB0

#define PGM_ADDR_SET	0xC0

#define RUNNING	0x10
#define PASS	0x20
#define FAIL	0x30
#define ILLEGAL	0x40

#define CONFIG_BUFF_SIZE	256

#define UNKNOWN	0
#define SYS_MODE_TX	1

#define ENTER_CALI_MODE	0xAA
#define EXIT_CALI_MODE	0xFF

/*tx irq type*/
#define TX_INT_WAKEUP	(0x01 << 0)
#define TX_INT_PING	(0x01 << 1)
#define TX_INT_SSP	(0x01 << 2)
#define TX_INT_IDP	(0x01 << 3)
#define TX_INT_CFGP	(0x01 << 4)
#define TX_INT_ASK_PKT	(0x01 << 5)
#define TX_INT_EPT	(0x01 << 6)
#define TX_INT_RPP_TO	(0x01 << 7)
#define TX_INT_CEP_TO	(0x01 << 8)
#define TX_INT_RX_ATTACH	(0x01 << 9)
#define TX_INT_RX_REMOVED	(0x01 << 10)
#define TX_INT_Q_CALI	(0x01 << 12)

/*tx command*/
#define ENTER_DEEP_LOW_POWER_MODE	(0x01 << 0)
#define TX_CMD_ENTER_TX_MODE	(0x01 << 1)
#define TX_CMD_CRC_CHERCK	(0x01 << 2)
#define TX_CMD_EXIT_TX_MODE	(0x01 << 3)
#define TX_CMD_SEND_FSK	(0x01 << 4)
#define TX_CMD_RESET_SYS	(0x01 << 5)
#define TX_CMD_START_Q_CAIL	(0x01 << 6)

/*tx EPT*/

#define EPT_WRONG_PACKET	(0x01 << 0)
#define EPT_SSP	(0x01 << 2)
#define EPT_RCV_EPT	(0x01 << 3)
#define EPT_CEP_TO	(0x01 << 4)
#define EPT_RPP_TO	(0x01 << 5)

#define EPT_OCP	(0x01 << 6)
#define EPT_OVP	(0x01 << 7)
#define EPT_UVP	(0x01 << 8)
#define EPT_FOD	(0x01 << 9)
#define EPT_OTP	(0x01 << 10)
#define EPT_POCP	(0x01 << 11)

#define EPT_SR_OCP	(0x01 << 12)
#define EPT_VRECT_OVP	(0x01 << 13)


/*gpio operator define*/
#define GP_0	0
#define GP_1	1
#define GP_2	2
#define GP_3	3
#define GP_4	4

#define MAC_PKG_HEAD	0x48
#define MAC_PKG_CMD_CKECK	0xC1
#define MAC_PKG_CMD_ADD1	0xB6
#define MAC_PKG_CMD_ADD2	0xB7

#define POWER_PKG_HEAD	0x28
#define POWER_PKG_CMD	0x17

#define CPS_LOG_NONE	0
#define CPS_LOG_ERR	1
#define CPS_LOG_DEBG	2
#define CPS_LOG_FULL	3

#define ENABLE_CPS_LOG CPS_LOG_ERR

#define cps_wls_log(num, fmt, ...) \
	do { \
		if (ENABLE_CPS_LOG >= (int)num) \
			printk(KERN_ERR "[cps8601]:" pr_fmt(fmt), ##__VA_ARGS__); \
	} while (0)

#define FW_VER_LEN	2
#define MIN_TO_MS	(60000)
#define P9418_WAIT_TIME	50

enum PEN_STATUS {
	PEN_STATUS_UNDEFINED,
	PEN_STATUS_NEAR,
	PEN_STATUS_FAR,
};

enum POWER_ENABLE_REASON {
	PEN_REASON_UNDEFINED,
	PEN_REASON_NEAR,
	PEN_REASON_RECHARGE,
};

enum POWER_DISABLE_REASON {
	PEN_REASON_UNKNOWN,
	PEN_REASON_FAR,
	PEN_REASON_CHARGE_FULL,
	PEN_REASON_CHARGE_TIMEOUT,
	PEN_REASON_CHARGE_STOP,
	PEN_REASON_CHARGE_EPT,
	PEN_REASON_CHARGE_OCP,
	PEN_OFF_REASON_MAX,
};

enum CALIBRATE_STATUS {
	Q_CALI_UNKNOWN,
	Q_CALI_IN_PROGRESS,
	Q_CALI_SUCCESS,
	Q_CALI_FAIL,
	Q_CALI_CALIBRATED,
	Q_CALI_UNCALIBRATED,
};

struct q_cali_result {
	int q_cali_times;
	int q_cali_cnt;
	int q_cali_width;
	int q_cali_cnt_var;
	int q_cali_width_var;
};

enum HBOOST_STATUS {
	HBOOST_UNKNOWN,
	HBOOST_SET_FAIL,
	HBOOST_SET_SUCCESS,
	HBOOST_IS_SETTING_BOOST,
};

enum FW_UPDATE_STATUS {
	FW_UPDATE_UNKNOWN,
	FW_UPDATE_FAIL,
	FW_UPDATE_SUCCESS,
	FW_UPDATE_ON_GOING,
};

#if (defined(CONFIG_OPLUS_CHARGER_MTK) || LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <uapi/linux/rtc.h>
struct timeval {
	__kernel_old_time_t tv_sec; /* seconds */
	__kernel_suseconds_t tv_usec; /* microseconds */
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)) */
#endif /* defined(CONFIG_OPLUS_CHARGER_MTK)
	|| LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) */

#endif /* __OPLUS_CPS8601_H__ */
