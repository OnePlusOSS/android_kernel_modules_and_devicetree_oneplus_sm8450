// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef _1WIRE_PROTOCOL_H
#define _1WIRE_PROTOCOL_H

#include <linux/ctype.h>
#include <linux/types.h>

struct onewire_gpio_data {
	int ow_gpio;
	void *gpio_out_high_reg;
	void *gpio_out_low_reg;
	void *gpio_cfg_out_reg;
	void *gpio_cfg_in_reg;
	void *gpio_in_reg;
	raw_spinlock_t lock;
	struct pinctrl *ow_gpio_pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_sleep;
	int version;
	int gpio_num;
	unsigned int onewire_gpio_cfg_addr_out;
	unsigned int onewire_gpio_cfg_addr_in;
	unsigned int onewire_gpio_level_addr_high;
	unsigned int onewire_gpio_level_addr_low;
	unsigned int onewire_gpio_in_addr;
	unsigned int onewire_gpio_cfg_out_val;
	unsigned int onewire_gpio_cfg_in_val;
	unsigned int onewire_gpio_level_high_val;
	unsigned int onewire_gpio_level_low_val;
	unsigned int gpio_addr_offset;
	unsigned int write_begin_low_level_time;
	unsigned int write_relese_ic_time;
	bool maxim_romid_crc_support;
	unsigned int gpio_reg[2];
};

void delay_us(unsigned int delay_us);
void delay_ms(unsigned int delay_ms);
void delay_ns(unsigned int delay_ns);
/* Basic 1-Wire functions */
int write_get_pin(void);
int ow_reset(void);
void write_byte(unsigned char byte_value);
void write_bit(unsigned char bit_value);
unsigned char read_bit(void);
unsigned char read_byte(void);
int onewire_init(struct onewire_gpio_data *onewire_data);
void onewire_set_gpio_config_in(void);
void onewire_set_gpio_config_out(void);
bool get_maxim_romid_crc_support(void);

#endif /* _1WIRE_PROTOCOL_H */
