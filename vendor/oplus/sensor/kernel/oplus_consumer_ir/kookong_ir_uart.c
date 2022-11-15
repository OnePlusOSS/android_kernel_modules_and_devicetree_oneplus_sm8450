// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/miscdevice.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>

#include "kookong_ir_def.h"
#include "kookong_ir_uart.h"


#define IR_SENDDATA_START    0
#define IR_SENDDATA_END      1

struct hw_config_t {
	int ir_power_ctrl;
	struct pinctrl *pinctrl;
	struct pinctrl_state *ir_send_data_enable;
	struct pinctrl_state *ir_send_data_disable;
};

struct kookong_ir_t {
	dev_t devt;
	struct hw_config_t hw_config;
	struct platform_device *kookong_ir_uart_dev;
	struct miscdevice misc_dev;
	struct mutex kookong_tx_mutex;
	struct list_head device_entry;
};

static struct kookong_ir_t *kookong_ir_uart;

static int parse_hw_config(struct device *dev)
{
	int retval = -1;
	struct device_node *np = dev->of_node;

	if (!kookong_ir_uart) {
		pr_err("kookong_ir: parse_hw_config error, kookong_ir_uart is null!\n");
	} else {
		kookong_ir_uart->hw_config.pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR_OR_NULL(kookong_ir_uart->hw_config.pinctrl)) {
			pr_info("kookong_ir: falied to get pinctrl!\n");
			return -EINVAL;
		} else {
			kookong_ir_uart->hw_config.ir_send_data_enable = pinctrl_lookup_state(kookong_ir_uart->hw_config.pinctrl, "ir_data_enable");
			if (IS_ERR_OR_NULL(kookong_ir_uart->hw_config.ir_send_data_enable)) {
				pr_err("%s, failed to request ir_data_enable.\n", __func__);
				return -EINVAL;
			}

			kookong_ir_uart->hw_config.ir_send_data_disable = pinctrl_lookup_state(kookong_ir_uart->hw_config.pinctrl, "ir_data_disable");
			if (IS_ERR_OR_NULL(kookong_ir_uart->hw_config.ir_send_data_disable)) {
				pr_err("%s, failed to request ir_data_disable.\n", __func__);
				return -EINVAL;
			}
			pinctrl_select_state(kookong_ir_uart->hw_config.pinctrl, kookong_ir_uart->hw_config.ir_send_data_disable);
		}

		retval = of_get_named_gpio(np, "ir-power-ctrl", 0);
		if (retval < 0) {
			pr_info("kookong_ir: falied to get ir-power-ctrl\n");
			return retval;
		} else {
			kookong_ir_uart->hw_config.ir_power_ctrl = retval;
			retval = devm_gpio_request(dev, kookong_ir_uart->hw_config.ir_power_ctrl, "ir-power-ctrl");
			if (retval) {
				pr_err("%s, failed to request ir-power-ctrl, ret = %d.\n", __func__, retval);
			}
		}
		pr_info("kookong_ir: kookong_ir_uart->hw_config.ir_power_ctrl = %d\n", kookong_ir_uart->hw_config.ir_power_ctrl);
	}

	return retval;
}

static ssize_t kookong_ir_uart_file_write(struct file *file, const char __user *ubuff, size_t count, loff_t *offset)
{
	char *user_wBuff;

	pr_info("kookong_ir: kookong_ir_uart_file_write call\n");
	if (*offset != 0) {
		return 0;
	}
	user_wBuff = kzalloc(count, GFP_KERNEL);
	if (!user_wBuff) {
		return -ENOMEM;
	}
	if (copy_from_user(user_wBuff, ubuff, count)) {
		kfree(user_wBuff);
		return -EFAULT;
	}

	if (user_wBuff[0] == IR_SENDDATA_START) {
		pr_info("kookong_ir: kookong_ir_uart_open enable power and sendData!\n");
		gpio_direction_output(kookong_ir_uart->hw_config.ir_power_ctrl, 1);
		pinctrl_select_state(kookong_ir_uart->hw_config.pinctrl, kookong_ir_uart->hw_config.ir_send_data_enable);
		mdelay(2);
	} else if (user_wBuff[0] == IR_SENDDATA_END) {
		pr_info("kookong_ir: kookong_ir_uart_open disable power and sendData!\n");
		mdelay(2);
		gpio_direction_output(kookong_ir_uart->hw_config.ir_power_ctrl, 0);
		pinctrl_select_state(kookong_ir_uart->hw_config.pinctrl, kookong_ir_uart->hw_config.ir_send_data_disable);
	}

	return 0;
}

static int kookong_ir_uart_file_open(struct inode *inode, struct file *file)
{
	pr_info("kookong_ir: kookong_ir_uart_file_open call\n");
	return 0;
}

static int kookong_ir_uart_file_close(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t kookong_ir_uart_file_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	enum  ir_hardware_interface ir_interface = IR_HW_UART;

	if (copy_to_user(buf, &ir_interface, sizeof(ir_interface))) {
		return -EFAULT;
	}
	pr_info("kookong_ir: kookong_ir_uart_file_read call\n");
	return 0;
}

loff_t kookong_iruart_uart_file_llseek(struct file *file, loff_t offset, int whence)
{
	return 0;
}

static struct file_operations fops = {
	.open = kookong_ir_uart_file_open,
	.release = kookong_ir_uart_file_close,
	.read = kookong_ir_uart_file_read,
	.write = kookong_ir_uart_file_write,
	.llseek = kookong_iruart_uart_file_llseek,
};

static int kookong_ir_uart_probe(struct platform_device *pdev)
{
	pr_info("kookong_ir: spi_probe call\n");

	kookong_ir_uart = kzalloc(sizeof(*kookong_ir_uart), GFP_KERNEL);
	if (!kookong_ir_uart)
		return -ENOMEM;
	kookong_ir_uart->kookong_ir_uart_dev = pdev;
	mutex_init(&kookong_ir_uart->kookong_tx_mutex);
	INIT_LIST_HEAD(&kookong_ir_uart->device_entry);
	kookong_ir_uart->misc_dev.fops = &fops;
	kookong_ir_uart->misc_dev.name = OPLUS_CONSUMER_IR_DEVICE_NAME;
	kookong_ir_uart->misc_dev.minor = MISC_DYNAMIC_MINOR;
	misc_register(&kookong_ir_uart->misc_dev);
	parse_hw_config(&pdev->dev);

	return 0;
}

static int kookong_ir_uart_remove(struct platform_device *pdev)
{
	pr_info("kookong_ir: kookong_ir_uart_remove call\n");
	misc_deregister(&kookong_ir_uart->misc_dev);
	kfree(kookong_ir_uart);
	return 0;
}

static struct of_device_id kookong_ir_uart_of_match_table[] = {
	{
		.compatible = "oplus,kookong_ir_uart",/*"kookong,ir-uart",*/
	},
	{},
};
MODULE_DEVICE_TABLE(of, kookong_ir_uart_of_match_table);


static struct platform_driver kookong_ir_uart_driver = {
	.driver = {
		.name = UART_MODULE_NAME,
		.owner = THIS_MODULE,
		.of_match_table = kookong_ir_uart_of_match_table,
	},
	.probe = kookong_ir_uart_probe,
	.remove = kookong_ir_uart_remove,
};

static int __init kookong_ir_uart_init(void)
{
	pr_info("kookong_ir_uart_init call\n");

	platform_driver_register(&kookong_ir_uart_driver);
	return 0;
}


arch_initcall(kookong_ir_uart_init);

MODULE_AUTHOR("oplus, Inc.");
MODULE_DESCRIPTION("oplus kookong UART Bus Module");
MODULE_LICENSE("GPL");
