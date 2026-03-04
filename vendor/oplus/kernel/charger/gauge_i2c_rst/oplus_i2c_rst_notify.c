// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2024 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/export.h>
#include <linux/notifier.h>

BLOCKING_NOTIFIER_HEAD(i2c_reset_notifier_list);

int i2c_rst_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&i2c_reset_notifier_list, nb);
}
EXPORT_SYMBOL(i2c_rst_register_notifier);

int i2c_rst_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&i2c_reset_notifier_list, nb);
}
EXPORT_SYMBOL(i2c_rst_unregister_notifier);

void i2c_rst_call_notifier(unsigned long action, void *data)
{
	blocking_notifier_call_chain(&i2c_reset_notifier_list, action, data);
}
EXPORT_SYMBOL(i2c_rst_call_notifier);

MODULE_DESCRIPTION("I2C reset Notify Driver");
MODULE_LICENSE("GPL");
