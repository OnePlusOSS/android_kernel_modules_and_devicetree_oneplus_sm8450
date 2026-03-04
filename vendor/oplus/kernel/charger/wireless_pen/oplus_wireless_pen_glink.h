// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */
#ifndef __OPLUS_WIRELESS_GLINK__
#define __OPLUS_WIRELESS_GLINK__

#ifndef CONFIG_OPLUS_CHARGER_MTK
#if (KERNEL_VERSION(6, 6, 0) <= LINUX_VERSION_CODE)
#include <linux/soc/qcom/qti_pmic_glink.h>
#else
#include <linux/soc/qcom/pmic_glink.h>
#endif

#define OEM_OPCODE_SET_HBOOST_VOLT	0x10007
#define MSG_OWNER_HBOOST	32785
#define MSG_TYPE_REQ_RESP	1
#define BC_WAIT_TIME_MS	2500

struct wireless_pen_set_hboost_vout_req {
	struct pmic_glink_hdr hdr;
	uint8_t reg_vout;
};

struct wireless_pen_set_hboost_vout_resp {
	struct pmic_glink_hdr hdr;
	uint32_t ret_code;
};
#endif /*CONFIG_OPLUS_CHARGER_MTK*/

int wireless_pen_send_hboost_volt_req(uint8_t value);
void wireless_pen_glink_init(void);
void wireless_pen_glink_exit(void);

#endif /* __OPLUS_WIRELESS_GLINK__ */
