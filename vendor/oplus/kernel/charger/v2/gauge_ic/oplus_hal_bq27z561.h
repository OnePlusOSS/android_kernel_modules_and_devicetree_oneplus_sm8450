/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2024-2024 Oplus. All rights reserved.
 */

#ifndef __OPLUS_BQ27Z561_H__
#define __OPLUS_BQ27Z561_H__

#include <linux/regmap.h>
#include <oplus_chg_ic.h>
#include <oplus_mms_gauge.h>

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
#include <debug-kit.h>
#endif

#define DRIVER_VERSION			"1.1.0"

/* Bq27z561 standard data commands */
#define BQ27Z561_REG_CNTL		0x00
#define BQ27Z561_REG_AR			0x02
#define BQ27Z561_REG_ARTTE		0x04
#define BQ27Z561_REG_TEMP		0x06
#define BQ27Z561_REG_VOLT		0x08
#define BQ27Z561_REG_FLAGS		0x0A
#define BQ27Z561_REG_NAC		0x0C
#define BQ27Z561_REG_FAC		0x0e
#define BQ27Z561_REG_RM			0x10
#define BQ27Z561_REG_FCC		0x12
#define BQ27Z561_REG_AI			0x0C
#define BQ27Z561_REG_TTE		0x16
#define BQ27Z561_REG_TTF		0x18
#define BQ27Z561_REG_SI			0x1a
#define BQ27Z561_REG_STTE		0x1c
#define BQ27Z561_REG_MLI		0x1e
#define BQ27Z561_REG_MLTTE		0x20
#define BQ27Z561_REG_AE			0x22
#define BQ27Z561_REG_AP			0x24
#define BQ27Z561_REG_TTECP		0x26
#define BQ27Z561_REG_INTTEMP 		0x28
#define BQ27Z561_REG_CC			0x2a
#define BQ27Z561_REG_SOH		0x2E
#define BQ27Z561_REG_SOC		0x2c
#define BQ27Z561_REG_NIC		0x2e
#define BQ27Z561_REG_ICR		0x30
#define BQ27Z561_REG_LOGIDX		0x32
#define BQ27Z561_REG_LOGBUF		0x34
#define BQ27Z561_REG_DOD0		0x36
#define BQ27Z561_FLAG_DSC		BIT(0)
#define BQ27Z561_FLAG_FC		BIT(9)
#define BQ27Z561_CS_DLOGEN		BIT(15)
#define BQ27Z561_CS_SS			BIT(13)

#define BQ27Z561_CMD_INVALID		0xFF

/* Control subcommands */
#define BQ27Z561_SUBCMD_CTNL_STATUS		0x0000
#define BQ27Z561_SUBCMD_DEVICE_TYPE		0x0001
#define BQ27Z561_SUBCMD_FW_VER			0x0002
#define BQ27Z561_SUBCMD_HW_VER			0x0003
#define BQ27Z561_SUBCMD_DF_CSUM			0x0004
#define BQ27Z561_SUBCMD_CHEMID			0x0006
#define BQ27Z561_SUBCMD_PREV_MACW		0x0007
#define BQ27Z561_SUBCMD_CHEM_ID			0x0008
#define BQ27Z561_SUBCMD_BD_OFFSET		0x0009
#define BQ27Z561_SUBCMD_INT_OFFSET		0x000a
#define BQ27Z561_SUBCMD_CC_VER			0x000b
#define BQ27Z561_SUBCMD_OCV			0x000c
#define BQ27Z561_SUBCMD_BAT_INS			0x000d
#define BQ27Z561_SUBCMD_BAT_REM			0x000e
#define BQ27Z561_SUBCMD_SET_HIB			0x0011
#define BQ27Z561_SUBCMD_CLR_HIB			0x0012
#define BQ27Z561_SUBCMD_SET_SLP			0x0013
#define BQ27Z561_SUBCMD_CLR_SLP			0x0014
#define BQ27Z561_SUBCMD_FCT_RES			0x0015
#define BQ27Z561_SUBCMD_ENABLE_DLOG		0x0018
#define BQ27Z561_SUBCMD_DISABLE_DLOG		0x0019
#define BQ27Z561_SUBCMD_SEALED			0x0020
#define BQ27Z561_SUBCMD_ENABLE_IT		0x0021
#define BQ27Z561_SUBCMD_DISABLE_IT		0x0023
#define BQ27Z561_SUBCMD_CAL_MODE		0x0040
#define BQ27Z561_SUBCMD_RESET			0x0041
#define BQ27Z561_SUBCMD_GAUGE_STATUS		0X0056
#define BQ27Z561_SUBCMD_IT_STATUS1		0x0073
#define BQ27Z561_SUBCMD_IT_STATUS2		0X0074
#define BQ27Z561_SUBCMD_IT_STATUS3		0X0075
#define BQ27Z561_SUBCMD_FILTERED_CAP		0X0078

#define BQ27Z561_AUTHENDATA_1ST			0x40
#define BQ27Z561_AUTHENDATA_2ND			0x50
#define BQ27Z561_AUTHENCHECKSUM			0x60
#define BQ27Z561_AUTHENLEN 			0x61
#define BQ27Z561_OPERATION_STATUS		0x0054
#define BQ27Z561_I2C_TRY_COUNT			7

#define ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN	(-2731)
#define BQ27Z561_INIT_DELAY			((HZ)*1)

#define BQ27Z561_SUBCMD_TRY_COUNT 		3
#define BQ27Z561_DATA_CLASS_ACCESS 		0x003e
#define BQ27Z561_REG_CNTL1			0x3e
#define GAUGE_EXTERN_DATAFLASHBLOCK	0x3e

#define DEVICE_TYPE_BQ27541			0x0541
#define DEVICE_TYPE_BQ27411			0x0421
#define DEVICE_TYPE_BQ28Z610			0xFFA5
#define DEVICE_TYPE_ZY0602			0x0602
#define DEVICE_TYPE_ZY0603			0xA5FF
#define DEVICE_TYPE_BQ27Z561			0x1561

#define DEVICE_BQ27541				0
#define DEVICE_BQ27411				1
#define DEVICE_BQ28Z610				2
#define DEVICE_ZY0602				3
#define DEVICE_ZY0603				4
#define DEVICE_NFG8011B				5
#define DEVICE_BQ27Z561				6

#define BQ27Z561_BATT_SN_EN_ADDR		0x3E
#define BQ27Z561_BATT_SN_CMD			0x004C
#define BQ27Z561_BATT_SN_READ_BUF_LEN		22
#define BQ27Z561_BATTINFO_NO_CHECKSUM		0x00
#define BQ27Z561_BATT_SN_RETRY_MAX		3
#define BQ28Z610_BATT_FIRST_USAGE_DATE_WLEN	5
#define BQ28Z610_BATT_FIRST_USAGE_DATE_WADDR	0x404E
#define BQ28Z610_BATTINFO_DEFAULT_CHECKSUM	0xFF
#define BQ28Z610_REG_CNTL1_SIZE			4
#define BQ28Z610_REG_CNTL1			0x3e
#define BQ28Z610_SEAL_STATUS			0x0054
#define BQ28Z610_SEAL_BIT			(BIT(0) | BIT(1))
#define BQ28Z610_SEAL_VALUE			3
#define BQ28Z610_UNSEAL_SUBCMD1			0x0414
#define BQ28Z610_UNSEAL_SUBCMD2			0x3672
#define BQ28Z610_BATT_WRITE_CHECK_SUM_ADDR	0x60
#define BQ28Z610_SEAL_SUBCMD			0x0030
#define BQ28Z610_SEAL_POLLING_RETRY_LIMIT	20
#define BQ28Z610_BATT_UI_CC_WLEN		5
#define BQ28Z610_BATT_UI_CC_WADDR		0x4053
#define BQ28Z610_BATT_UI_SOH_WLEN		4
#define BQ28Z610_BATT_UI_SOH_WADDR		0x4051
#define BQ28Z610_BATT_USED_FLAG_WLEN		4
#define BQ28Z610_BATT_USED_FLAG_WADDR		0x4056
#define BQ28Z610_BATT_MANU_DATE_READ_BUF_LEN	4
#define BQ28Z610_BATTINFO_EN_ADDR		0x3E
#define BQ28Z610_BATTINFO_MANUDATE_CMD		0x004D
#define BQ28Z610_BATT_VDM_DATA_READ_BUF_LEN	34
#define BQ28Z610_BATT_INFO_RETRY_MAX		3
#define BQ28Z610_BATTINFO_VDMDATA_CMD		0x0070
#define BQ28Z610_BATT_FIRST_USAGE_DATE_L	15
#define BQ28Z610_BATT_FIRST_USAGE_DATE_H	16
#define BQ28Z610_BATT_UI_SOH			18
#define BQ28Z610_BATT_UI_CYCLE_COUNT_L		20
#define BQ28Z610_BATT_UI_CYCLE_COUNT_H		21
#define BQ28Z610_BATT_FIRST_USAGE_DATE_CHECK	17
#define BQ28Z610_BATTINFO_NO_CHECKSUM		0x00
#define BQ28Z610_BATT_UI_SOH_CHECK		19
#define BQ28Z610_BATT_UI_CYCLE_COUNT_CHECK	22
#define BQ28Z610_BATT_USED_FLAG_CHECK		24
#define BQ28Z610_BATT_USED_FLAG			23

#define BQ27Z561_AUTHENTICATE_OK		0x56
#define AUTHEN_MESSAGE_MAX_COUNT		30
struct bq27z561_authenticate_data {
	uint8_t result;
	uint8_t message_offset;
	uint8_t message_len;
	uint8_t message[AUTHEN_MESSAGE_MAX_COUNT]; /* 25, larger than 20 bytes */
};

#define BQ27Z561_AUTHENTICATE_DATA_COUNT		sizeof(struct bq27z561_authenticate_data)
#define SMEM_RESERVED_BOOT_INFO_FOR_APPS			418
#define GAUGE_AUTH_MSG_LEN					20
#define WLS_AUTH_RANDOM_LEN					8
#define WLS_AUTH_ENCODE_LEN					8
#define GAUGE_SHA256_AUTH_MSG_LEN				32
#define UFCS_AUTH_MSG_LEN					16

#define BCC_PARMS_COUNT						19
#define BCC_PARMS_COUNT_LEN					69
#define CALIB_TIME_CHECK_ARGS					6
#define DEVICE_TYPE_FOR_VOOC_BQ27Z561				0
#define BQ27Z561_DATAFLASHBLOCK					0x3e
#define DEVICE_NAME_LEN 					12

#define BQ27Z561_BLOCK_SIZE			32
#define BQ27Z561_EXTEND_DATA_SIZE		34
#define BQ27Z561_REG_TRUE_FCC			0x0073
#define BQ27Z561_TRUE_FCC_NUM_SIZE		2
#define BQ27Z561_TRUE_FCC_OFFSET		8
#define BQ27Z561_FCC_SYNC_CMD			0x0043
#define GAUGE_SUBCMD_TRY_COUNT			3
struct cmd_address {
	/* bq27z561 standard cmds */
	u8 reg_cntl;
	u8 reg_temp;
	u8 reg_volt;
	u8 reg_flags;
	u8 reg_nac;
	u8 reg_fac;
	u8 reg_rm;
	u8 reg_fcc;
	u8 reg_ai;
	u8 reg_si;
	u8 reg_mli;
	u8 reg_ap;
	u8 reg_soc;
	u8 reg_inttemp;
	u8 reg_soh;
	u8 reg_fc; /* add gauge reg print log start */
	u8 reg_qm;
	u8 reg_pd;
	u8 reg_rcu;
	u8 reg_rcf;
	u8 reg_fcu;
	u8 reg_fcf;
	u8 reg_sou;
	u8 reg_do0;
	u8 reg_doe;
	u8 reg_trm;
	u8 reg_pc;
	u8 reg_qs; /* add gauge reg print log end */
	u16 flag_dsc;
	u16 flag_fc;
	u16 cs_dlogen;
	u16 cs_ss;

	/*  bq27z561 external standard cmds */
	u8 reg_ar;
	u8 reg_artte;
	u8 reg_tte;
	u8 reg_ttf;
	u8 reg_stte;
	u8 reg_mltte;
	u8 reg_ae;
	u8 reg_ttecp;
	u8 reg_cc;
	u8 reg_nic;
	u8 reg_icr;
	u8 reg_logidx;
	u8 reg_logbuf;
	u8 reg_dod0;

	/* bq27z561 sub cmds */
	u16 subcmd_cntl_status;
	u16 subcmd_device_type;
	u16 subcmd_fw_ver;
	u16 subcmd_dm_code;
	u16 subcmd_prev_macw;
	u16 subcmd_chem_id;
	u16 subcmd_set_hib;
	u16 subcmd_clr_hib;
	u16 subcmd_set_cfg;
	u16 subcmd_sealed;
	u16 subcmd_reset;
	u16 subcmd_softreset;
	u16 subcmd_exit_cfg;
	u16 subcmd_enable_dlog;
	u16 subcmd_disable_dlog;
	u16 subcmd_enable_it;
	u16 subcmd_disable_it;

	/* bq27z561 external sub cmds */
	u16 subcmd_hw_ver;
	u16 subcmd_df_csum;
	u16 subcmd_bd_offset;
	u16 subcmd_int_offset;
	u16 subcmd_cc_ver;
	u16 subcmd_ocv;
	u16 subcmd_bat_ins;
	u16 subcmd_bat_rem;
	u16 subcmd_set_slp;
	u16 subcmd_clr_slp;
	u16 subcmd_fct_res;
	u16 subcmd_cal_mode;
};

struct wls_chg_auth_result {
	unsigned char random_num[WLS_AUTH_RANDOM_LEN];
	unsigned char encode_num[WLS_AUTH_ENCODE_LEN];
};

typedef struct {
	unsigned char msg[UFCS_AUTH_MSG_LEN];
} oplus_ufcs_auth_result;

typedef struct {
	int result;
	unsigned char msg[GAUGE_AUTH_MSG_LEN];
	unsigned char rcv_msg[GAUGE_AUTH_MSG_LEN];
} oplus_gauge_auth_result;

struct oplus_gauge_sha256_auth_result {
	unsigned char msg[GAUGE_SHA256_AUTH_MSG_LEN];
	unsigned char rcv_msg[GAUGE_SHA256_AUTH_MSG_LEN];
};

typedef struct {
	oplus_gauge_auth_result rst_k0;
	oplus_gauge_auth_result rst_k1;
	struct wls_chg_auth_result wls_auth_data;
	oplus_gauge_auth_result rst_k2;
	oplus_ufcs_auth_result ufcs_k0;
	struct oplus_gauge_sha256_auth_result sha256_rst_k0;
} oplus_gauge_auth_info_type;

struct oplus_gauge_sha256_auth{
	unsigned char random[GAUGE_SHA256_AUTH_MSG_LEN];
	unsigned char ap_encode[GAUGE_SHA256_AUTH_MSG_LEN];
	unsigned char gauge_encode[GAUGE_SHA256_AUTH_MSG_LEN];
};

struct chip_bq27z561 {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	u8 device_name[DEVICE_NAME_LEN];
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	struct regmap *regmap;
	struct oplus_device_bus *odb;
#endif

	atomic_t locked;

	int soc_pre;
	int temp_pre;
	int batt_vol_pre;
	int current_pre;
	int cc_pre;
	int soh_pre;
	int fcc_pre;
	int rm_pre;
	int fc_pre; /* add gauge reg print log start */
	int qm_pre;
	int pd_pre;
	int rcu_pre;
	int rcf_pre;
	int fcu_pre;
	int fcf_pre;
	int sou_pre;
	int do0_pre;
	int passed_q_pre;
	int doe_pre;
	int trm_pre;
	int pc_pre;
	int qs_pre; /* add gauge reg print log end */
	int volt_1_pre;
	int volt_2_pre;
	int batt_dod0_pre;

	int dod_passed_q_pre;
	int qmax_pre;
	int qmax_passed_q_pre;
	int device_type;
	int device_type_for_vooc;
	struct cmd_address cmd_addr;
	atomic_t suspended;
	int batt_cell_1_vol;
	int batt_cell_2_vol;
	int batt_cell_max_vol;
	int batt_cell_min_vol;
	int max_vol_pre;
	int min_vol_pre;
	int batt_num;

	bool fcc_too_small_checking;
	struct work_struct fcc_too_small_check_work;

	bool modify_soc_smooth;
	bool modify_soc_calibration;
	bool batt_bq27z561;

	bool battery_full_param; /* only for wite battery full param in guage dirver probe on 7250 platform */
	int sha1_key_index;
	struct delayed_work afi_update;
	bool afi_update_done;
	bool protect_check_done;
	bool disabled;
	bool error_occured;
	bool need_check;
	unsigned int afi_count;
	unsigned int zy_dts_qmax_min;
	unsigned int zy_dts_qmax_max;
	const u8 *static_df_checksum_3e;
	const u8 *static_df_checksum_60;
	const u8 **afi_buf;
	unsigned int *afi_buf_len;
	bool enable_sleep_mode;
	int fg_soft_version;
	int gauge_num;
	struct mutex chip_mutex;
	struct mutex calib_time_mutex;
	struct mutex bq27z561_alt_manufacturer_access;
	atomic_t i2c_err_count;
	bool i2c_err;
	oplus_gauge_auth_result auth_data;
	struct bq27z561_authenticate_data *authenticate_data;
	struct oplus_gauge_sha256_auth *sha256_authenticate_data;
	struct file_operations *authenticate_ops;

	int batt_dod0;
	int dod0_1_pre;
	int dod0_2_pre;
	int batt_dod0_passed_q;

	int batt_qmax_1;
	int batt_qmax_2;
	int batt_qmax_passed_q;
	int bcc_buf[BCC_PARMS_COUNT];

	bool calib_info_init;
	bool calib_info_save_support;
	int dod_time;
	int qmax_time;
	int dod_time_pre;
	int qmax_time_pre;
	int calib_check_args_pre[CALIB_TIME_CHECK_ARGS];

	bool support_sha256_hmac;
	bool support_extern_cmd;
	bool support_eco_design;
	struct delayed_work check_iic_recover;
	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	bool i2c_rst_ext;
	bool err_status;
	/* end workaround 230504153935012779 */

	struct mutex pinctrl_lock;
	struct pinctrl *pinctrl;
	struct pinctrl_state *id_not_pull;
	struct pinctrl_state *id_pull_up;
	struct pinctrl_state *id_pull_down;
	int id_gpio;
	int id_match_status;
	int id_value;
	bool fpga_test_support;
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	struct test_feature *battery_id_gpio_test;
	struct test_feature *fpga_fg_test;
#endif
	struct battery_manufacture_info battinfo;
	struct delayed_work get_manu_battinfo_work;
	int deep_dischg_count_pre;
	int deep_term_volt_pre;
	bool dsg_enable;
	u8 chem_id[CHEM_ID_LENGTH + 1];
	int last_cc_pre;
	int gauge_type;
	int bq27z561_seal_flag;
};

struct gauge_track_info_reg {
	int addr;
	int len;
	int start_index;
	int end_index;
};

int oplus_vooc_get_fastchg_started(void);
int oplus_vooc_get_fastchg_ing(void);
#endif /* __OPLUS_BQ27Z561_H__ */
