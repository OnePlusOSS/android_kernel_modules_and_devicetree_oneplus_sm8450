// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */
#ifndef _OPLUS_REGION_CHECK_
#define _OPLUS_REGION_CHECK_
#include "oplus_charger.h"

bool third_pps_supported_from_nvid(void);
bool third_pps_supported_comm_chg_nvid(void);
bool eco_design_supported_comm_chg_nvid(void);
bool third_pps_priority_than_svooc(void);
void oplus_chg_region_check_init(struct oplus_chg_chip *chip);
bool oplus_limit_svooc_current(void);

#endif /*_OPLUS_REGION_CHECK_*/

