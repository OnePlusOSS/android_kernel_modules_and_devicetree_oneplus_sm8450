/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */


#ifndef _OPLUS_LOCKING_MAIN_H_
#define _OPLUS_LOCKING_MAIN_H_

#define LK_MUTEX_ENABLE (1 << 0)
#define LK_RWSEM_ENABLE (1 << 1)

extern unsigned int g_opt_enable;
extern unsigned int g_opt_debug;

static inline bool locking_opt_enable(unsigned int enable)
{
	return g_opt_enable & enable;
}

static inline bool locking_opt_debug(void)
{
	return g_opt_debug;
}

void register_rwsem_vendor_hooks(void);
void register_mutex_vendor_hooks(void);
void unregister_rwsem_vendor_hooks(void);
void unregister_mutex_vendor_hooks(void);


#endif /* _OPLUS_LOCKING_MAIN_H_ */
