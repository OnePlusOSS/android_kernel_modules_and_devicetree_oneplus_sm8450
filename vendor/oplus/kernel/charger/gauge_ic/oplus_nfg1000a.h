// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2026 Oplus. All rights reserved.
 */
#ifndef __OPLUS_NFG1000A_H__
#define __OPLUS_NFG1000A_H__

#define DEVICE_TYPE_NFG1000A		0x4131

#define NFG1000A_SUBCMD_DA_STATUS1	0X0071
#define NFG1000A_SUBCMD_DA_STATUS2	0X0072
#define NFG1000A_SUBCMD_IT_STATUS1	0X0073
#define NFG1000A_DATAFLASHBLOCK		0x3e
#define NFG1000A_REG_CIS_ALERT_LEVEL	0x72
#define NFG1000A_SUBCMD_CHEMID		0X0006
#define NFG1000A_SUBCMD_GAUGE_STATUS	0X0056
#define NFG1000A_SUBCMD_TRY_COUNT	3

#define NFG1000A_AUTHENDATA_1ST		0x40
#define NFG1000A_AUTHENDATA_2ND		0x50
#define NFG1000A_AUTHENCHECKSUM		0x60
#define NFG1000A_AUTHENLEN		0x61
#define NFG1000A_OPERATION_STATUS	0x0054
#define NFG1000A_I2C_TRY_COUNT		7

int nfg1000a_get_qmax_parameters(struct chip_bq27541 *chip, int *cell_qmax);
int nfg1000a_get_rsoc_parameters(struct chip_bq27541 *chip, int *rsoc);
int nfg1000a_get_calib_time(struct chip_bq27541 *chip, int *dod_time, int *qmax_time);
void nfg1000a_get_info(struct chip_bq27541 *chip, u8 *info, int len);
bool nfg1000a_sha256_hmac_authenticate(struct chip_bq27541 *chip);
void nfg1000a_bcc_set_buffer(struct chip_bq27541 *chip, int *buffer);
void nfg1000a_sub_bcc_set_buffer(struct chip_bq27541 *chip, int *buffer);
#endif  /* __OPLUS_NFG1000A_H__ */
