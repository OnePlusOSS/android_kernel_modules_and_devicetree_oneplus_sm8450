/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef _OPLUS_BOOT_H
#define _OPLUS_BOOT_H
#if IS_ENABLED(CONFIG_OPLUS_SYSTEM_KERNEL_QCOM)
enum { MSM_BOOT_MODE__NORMAL,
       MSM_BOOT_MODE__FASTBOOT,
       MSM_BOOT_MODE__RECOVERY,
       MSM_BOOT_MODE__FACTORY,
       MSM_BOOT_MODE__RF,
       MSM_BOOT_MODE__WLAN,
       MSM_BOOT_MODE__MOS,
       MSM_BOOT_MODE__CHARGE,
       MSM_BOOT_MODE__SILENCE,
       MSM_BOOT_MODE__SAU,
       MSM_BOOT_MODE__AGING = 998,
       MSM_BOOT_MODE__SAFE = 999,
};

#ifdef OPLUS_BUG_STABILITY
/*add for charge*/
extern bool qpnp_is_power_off_charging(void);
/*add for detect charger when reboot */
extern bool qpnp_is_charger_reboot(void);
/*Add for kernel monitor whole bootup*/
#ifdef PHOENIX_PROJECT
extern bool op_is_monitorable_boot(void);
#endif
#endif
#else
enum { UNKOWN_MODE = 0x00,
       RECOVERY_MODE = 0x1,
       FASTBOOT_MODE = 0x2,
       ALARM_BOOT = 0x3,
       DM_VERITY_LOGGING = 0x4,
       DM_VERITY_ENFORCING = 0x5,
       DM_VERITY_KEYSCLEAR = 0x6,
       SILENCE_MODE = 0x21,
       SAU_MODE = 0x22,
       RF_MODE = 0x23,
       WLAN_MODE = 0x24,
       MOS_MODE = 0x25,
       FACTORY_MODE = 0x26,
       REBOOT_KERNEL = 0x27,
       REBOOT_MODEM = 0x28,
       REBOOT_ANDROID = 0x29,
       REBOOT_SBL_DDRTEST = 0x2B,
       REBOOT_SBL_DDR_CUS = 0x2C,
       REBOOT_AGINGTEST = 0x2D,
       REBOOT_SBLTEST_FAIL = 0x2E,
       NORMAL_MODE = 0x3E,
       REBOOT_NORMAL = 0x3F,
       EMERGENCY_DLOAD = 0xFF,
};
#endif /* end CONFIG_OPLUS_SYSTEM_KERNEL_QCOM */
extern int get_boot_mode(void);
#endif /*_OPLUS_BOOT_H*/
