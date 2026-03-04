// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2024 Oplus. All rights reserved.
 */

#ifndef __OPLUS_HAL_NU1669_H__
#define __OPLUS_HAL_NU1669_H__

#define NU1669_RX_PWR_15W		0x1E
#define NU1669_RX_PWR_11W		0x16
#define NU1669_RX_PWR_10W		0x14
#define NU1669_RX_PWR_5W		0x0A
#define NU1669_RX_MODE_BPP		0x04
#define NU1669_RX_MODE_EPP		0x31

#define READ_DATA_LENGTH		256
#define WRITE_DATA_LENGTH		15

/************** basic registers *************/
/**256 bytes reg to transfer chip infomation to AP**/
#define SRAM_BUF_START_ADDR		0x2100
#define SRAM_BUF_END_ADDR		0x21FF
/*16 bytes AP RW reg, AP can write these regs to send data to NU1669*/
#define GEN_RW_REG0			0x0000
#define GEN_RW_REG1			0x0001
#define GEN_RW_REG2			0x0002
#define GEN_RW_REG3			0x0003
#define GEN_RW_REG4			0x0004
#define GEN_RW_REG5			0x0005
#define GEN_RW_REG6			0x0006
#define GEN_RW_REG7			0x0007
#define GEN_RW_REG8			0x0008
#define GEN_RW_REG9			0x0009
#define GEN_RW_REGA			0x000A
#define GEN_RW_REGB			0x000B
#define GEN_RW_REGC			0x000C
#define GEN_RW_REGD			0x000D
#define GEN_RW_REGE			0x000E
#define GEN_RW_REGF			0x000F
/*************16 bytes AP RO reg************/
#define GEN_RO_REG20			0x0020
#define GEN_RO_REG21			0x0021
#define GEN_RO_REG22			0x0022
#define GEN_RO_REG23			0x0023
#define GEN_RO_REG24			0x0024
#define GEN_RO_REG25			0x0025
#define GEN_RO_REG26			0x0026
#define GEN_RO_REG27			0x0027
#define GEN_RO_REG28			0x0028
#define GEN_RO_REG29			0x0029
#define GEN_RO_REG2A			0x002A
#define GEN_RO_REG2B			0x002B
#define GEN_RO_REG2C			0x002C
#define GEN_RO_REG2D			0x002D
#define GEN_RO_REG2E			0x002E
#define GEN_RO_REG2F			0x002F
/*4 bytes AP RW reg, these regs is used for AP to trig interrupt to NU1669*/
#define INT_REG60			0x0060
#define INT_REG61			0x0061
#define INT_REG62			0x0062
#define INT_REG63			0x0063

/*AP program registers*/
#define TM_CUST				0x1F23
#define TM_GEN_DIG			0x1002
#define SP_CTRL0			0x0090
#define RECT_CTRL0			0x4A28
#define MLDO_CTRL0			0x4A30
#define MCU_MTP_CTRL			0x4918
#define MTP_LOCK			0x008C
#define MTP_SECTOR			0x0012
#define MTP_ADDR_H			0x0010
#define MTP_ADDR_L			0x0011
#define MTP_WDATA0			0x001C
#define MTP_WDATA1			0x001D
#define MTP_WDATA2			0x001E
#define MTP_WDATA3			0x001F
#define MTP_CTRL0			0x0017
#define MTP_CTRL2			0x0019
#define MTP_STAT			0x001B
#define MTP_CMD				0x001A
#define WDOG_LOAD			0x5000
#define WDOG_CTRL			0x5008

/*Program registers val*/
#define TM_CUST_RST			0x00
#define TM_CUST_CODE0			0x2D
#define TM_CUST_CODE1			0xD2
#define TM_CUST_CODE2			0x22
#define TM_CUST_CODE3			0xDD

#define DISABLE_MCU			0x88
#define GET_APB_AUTH			0x40
#define RECT_CONFIG			0x01
#define MLDO_CFG			0x61
#define GET_MTP_AUTH			0x80
#define SEL_SECTOR_ALL			0xFF
#define SET_PCS				0x01
#define SET_WRT_PULSE			0x01
#define CLR_WRT_PULSE			0x00
#define START_WRITE			0x5A
#define DISABLE_STANDBY			0x80

#define CLOSE_OPT			0x00
#define CLR_PCS				0x00
#define RELEASE_MTP_AUTH		0x00
#define ENABLE_MCU			0x08
#define RST_IC0				0x01
#define RST_IC1				0x03
#define RELEASE_APB_AUTH		0x00
#define RELEASE_MCU			0x00
#define EXIT_TEST_MODE			0x00

#define MTP_LOCK_BITS_MASK		0x03
#define MTP_UNLOCK_GOOD			0x02
#define MTP_UNLOCK_TIMES		10
#define MTP_BUSY_POS			7
#define MTP_BUSY_WAIT			120

/************* 1. AP Write Data to NU1669 **************
 * 1. AP write data length to gen-rw-reg0
 * 2. AP write data to gen-rw-reg1~gen-rw-reg15
 * 3. AP write data offset to int-reg0 to notify MCU excu write cmd.
 *************************************************/
#define WRITE_DATA_LENGTH_REG		GEN_RW_REG0
#define WRITE_DATA_REG			GEN_RW_REG1
#define WRITE_DATA_OFFSET_REG		INT_REG60

/************ 2. AP Read Data From NU1669 ************
 * The MCU stores info in sram-buf regs, start from reg[0x2100]
 * to reg[0x21FF], AP can read sram-buf regs directly.
 ************************************************/
#define READ_DATA_REG			SRAM_BUF_START_ADDR

#define I2C_BUSY_REG			0x212C
#define I2C_BUSY_WAIT			30
#define I2C_TEST_DATA			0x88
#define FW_VERSION_OFFSET		12
#define INVALID_FW_VERSION0		0x00
#define INVALID_FW_VERSION1		0xFF
#define FW_CHECK_EN			BIT(0)
#define TX_ENABLE			BIT(0)
#define TX_START_PING			BIT(1)
#define B2B_ON_EN			BIT(0)
#define VAC_ACDRV_OK			0x5A

#define NU1669_CHIP_ID			0x1669
#define NU1669_HW_VERSION		0x0
#define NU1669_CUSTOMER_ID		0x04

#define NU1669_REG_PWR_CTRL		0x00d0
#define NU1669_DCDC_EN			BIT(0)

#define NU1669_REG_TRX_CTRL		0x0076
#define NU1669_TRX_EN			BIT(0)

#define NU1669_REG_STATUS		0x20/*~0x23*/
#define NU1669_ERR_IRQ_VALUE		0xFFFFFFFF
#define NU1669_RX_ERR_OCP		BIT(0)
#define NU1669_RX_ERR_CLAMPOVP		BIT(1)
#define NU1669_RX_ERR_HARDOVP		BIT(2)
#define NU1669_RX_ERR_VOUTOVP		BIT(3)
#define NU1669_RX_ERR_SOFTOTP		BIT(4)
#define NU1669_RX_ERR_OTP		BIT(5)
#define NU1669_VOUT2V2X_OVP		BIT(6)
#define NU1669_LDO_ON			BIT(7)
#define NU1669_LDO_OFF			BIT(8)
#define NU1669_EPP_CAP			BIT(9)
#define NU1669_TX_DATA_RCVD		BIT(10)
#define NU1669_SEND_PKT_TIMEOUT		BIT(11)
#define NU1669_SEND_PKT_SUCCESS		BIT(12)
#define NU1669_VAC_PRESENT		BIT(13)
#define NU1669_VOUT2V2X_UVP		BIT(14)
#define NU1669_V2X_OVP			BIT(15)
#define NU1669_VOUT_PRESENT_LDO_OFF	BIT(16)
#define NU1669_LDO_OPP			BIT(17)
#define NU1669_VOUT_SC			BIT(18)
#define NU1669_EPP_TX_MANU_ID		BIT(19)
#define NU1669_V2X_UCP			BIT(20)

#define NU1669_EPP_NEGO_FAIL		BIT(29)

#define NU1669_TX_STATUS_MASK		(BIT(21) | BIT(22) | BIT(23))
#define NU1669_TX_STATUS_TRANSFER	BIT(21)
#define NU1669_TX_STATUS_DIGITALPING	BIT(22)
#define NU1669_TX_STATUS_READY		BIT(23)

#define NU1669_TX_ERR_MASK		(BIT(1) | BIT(24) | BIT(25) | BIT(26) | BIT(27) | BIT(28) | BIT(29) | BIT(30) | BIT(31))
#define NU1669_TX_ERR_VRECTOVP		BIT(1)
#define NU1669_TX_ERR_RXAC		BIT(24)
#define NU1669_TX_ERR_OCP		BIT(25)
#define NU1669_TX_ERR_OVP		BIT(26)
#define NU1669_TX_ERR_LVP		BIT(27)
#define NU1669_TX_ERR_FOD		BIT(28)
#define NU1669_TX_ERR_OTP		BIT(29)
#define NU1669_TX_ERR_CEPTIMEOUT	BIT(30)
#define NU1669_TX_ERR_RXEPT		BIT(31)

#define NU1669_MTP_VOL_MV		5500

#define NU1669_FOD_PARM_LEN_MAX		40
#define NU1669_RXAC_STATE_ON		1

#endif /* __OPLUS_HAL_NU1669_H__ */
