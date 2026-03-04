// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2024 Oplus. All rights reserved.
 */

#ifndef _OPLUS_I2C_RST_NOTIFY_H
#define _OPLUS_I2C_RST_NOTIFY_H

enum i2c_reset_notifier {
	I2C_NONE,
	I2C_RST_START,
	I2C_RST_END,
};
int i2c_rst_register_notifier(struct notifier_block *nb);
int i2c_rst_unregister_notifier(struct notifier_block *nb);
void i2c_rst_call_notifier(unsigned long action, void *data);

#endif /* _OPLUS_I2C_RST_NOTIFY_H */
