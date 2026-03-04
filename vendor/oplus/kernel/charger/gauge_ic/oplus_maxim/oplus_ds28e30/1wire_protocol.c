// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include "oplus_charger.h"
#include "deep_cover_coproc.h"

#include "1wire_protocol.h"
#include "ds28e30.h"

#define ONE_WIRE_CONFIG_OUT	writel_relaxed(g_onewire_data->onewire_gpio_cfg_out_val, g_onewire_data->gpio_cfg_out_reg)
#define ONE_WIRE_CONFIG_IN	writel_relaxed(g_onewire_data->onewire_gpio_cfg_in_val, g_onewire_data->gpio_cfg_in_reg)
#define ONE_WIRE_OUT_HIGH	writel_relaxed(g_onewire_data->onewire_gpio_level_high_val, g_onewire_data->gpio_out_high_reg)
#define ONE_WIRE_OUT_LOW	writel_relaxed(g_onewire_data->onewire_gpio_level_low_val, g_onewire_data->gpio_out_low_reg)

#define RESET_LOW_LEVEL_TIME        54
#define RESET_WAIT_IC_REPLY_TIME    9
#define RESET_RELESE_IC_TIME	    50

#define WRITE_ONE_LOW_LEVEL_TIME    10

#define READ_BEGIN_LOW_LEVEL_TIME   500
#define READ_WAIT_LOW_LEVEL_TIME    5
#define READ_RELESE_IC_TIME    	    6

static struct onewire_gpio_data *g_onewire_data;


/* Basic 1-Wire functions */
int ow_reset(void);
void write_byte(unsigned char byte_value);
void write_bit(unsigned char bit_value);
unsigned char read_bit(void);
unsigned char read_byte(void);

/*******************************************************************************
 get 1-wire GPIO input
*******************************************************************************/
void set_data_gpio_in(void)
{
	ONE_WIRE_CONFIG_IN;
}
/*****************************************************************************
delay us subroutine
*****************************************************************************/
static struct timespec get_current_time(void)
{
	struct timespec ts;
	getnstimeofday(&ts);
	return ts;
}

void maxim_delay_us(unsigned int delay_us)    /* 1US */
{
	udelay(delay_us);
}

void maxim_delay_ns(unsigned int delay_ns)
{
	ndelay(delay_ns);
}

/*****************************************************************************
delay ms subroutine
*****************************************************************************/
void maxim_delay_ms(unsigned int delay_ms)    /* 1US */
{
	mdelay(delay_ms);
}

/*****************************************************************************
Basic 1-Wire functions
Reset all of the devices on the 1-Wire Net and return the result.
Returns: TRUE(1):  presense pulse(s) detected, device(s) reset
      FALSE(0): no presense pulses detected
*****************************************************************************/
int ow_reset(void)
{
	unsigned int presence = 0;
	unsigned int value = 0;
	unsigned long flags;
	raw_spin_lock_irqsave(&g_onewire_data->lock, flags);
	ONE_WIRE_CONFIG_OUT;
	ONE_WIRE_OUT_LOW;
	maxim_delay_us(RESET_LOW_LEVEL_TIME);
	ONE_WIRE_CONFIG_IN;
	maxim_delay_us(RESET_WAIT_IC_REPLY_TIME);/* wait for presence */
	value = readl_relaxed(g_onewire_data->gpio_in_reg);
	presence = !((value >> g_onewire_data->gpio_addr_offset) & 0x1);
	maxim_delay_us(RESET_RELESE_IC_TIME);
	ONE_WIRE_OUT_HIGH;
	ONE_WIRE_CONFIG_IN;
	raw_spin_unlock_irqrestore(&g_onewire_data->lock, flags);
	chg_info(" %s ++++value 0x%x presence %d", __func__, value, presence);
	return (presence); /* presence signal returned */
}

/* Send 1 bit of communication to the 1-Wire Net.
 The parameter 'sendbit' least significant bit is used.
 'bitvalue' - 1 bit to send (least significant byte)
 */
void write_bit(unsigned char bitval)
{
	ONE_WIRE_OUT_LOW;
	maxim_delay_ns(g_onewire_data->write_begin_low_level_time);/* keeping logic low for 1 us */
	if(bitval != 0)
		ONE_WIRE_OUT_HIGH;                     /* ONE_WIRE_OUT_HIGH; set 1-wire to logic high if bitval='1' */
	maxim_delay_us(WRITE_ONE_LOW_LEVEL_TIME);  /*  waiting for 10us */
	ONE_WIRE_OUT_HIGH;
	maxim_delay_us(g_onewire_data->write_relese_ic_time);     /*  waiting for 5us to recover to logic high */
}

/* Send 1 bit of read communication to the 1-Wire Net and and return the
 result 1 bit read from the 1-Wire Net.
 Returns:  1 bits read from 1-Wire Net
 */
unsigned char read_bit(void)
{
	unsigned int vamm = 0;
	unsigned int value;
	unsigned char i;
	struct timespec r_start_ns;
	struct timespec r_end_ns;
	long r_diff_ns;

	ONE_WIRE_CONFIG_OUT;
	if (g_onewire_data->maxim_romid_crc_support) {
		r_start_ns = get_current_time();
		/* Execute output '0' 5 times*/
		ONE_WIRE_OUT_LOW;
		ONE_WIRE_OUT_LOW;
		ONE_WIRE_OUT_LOW;
		ONE_WIRE_OUT_LOW;
		ONE_WIRE_OUT_LOW;
		/* set 1-wire as input */
		ONE_WIRE_CONFIG_IN;
		for (i = 0; i < 7; i++) {
			value = readl_relaxed(g_onewire_data->gpio_in_reg);
			value = (value >> g_onewire_data->gpio_addr_offset) & 0x1;
			vamm += value;
		}
		r_end_ns = get_current_time();
		r_diff_ns = r_end_ns.tv_nsec - r_start_ns.tv_nsec;

		/* set threshold to justify logic '1' or '0' */
		if (vamm > 5)
			vamm = 1;
		else
			vamm = 0;
	} else {
		ONE_WIRE_OUT_LOW;
		ONE_WIRE_OUT_LOW;
		ONE_WIRE_CONFIG_IN;
		maxim_delay_ns(READ_BEGIN_LOW_LEVEL_TIME);/*  TODO wsx */
		value = readl_relaxed(g_onewire_data->gpio_in_reg);
		vamm = (value >> g_onewire_data->gpio_addr_offset) & 0x1;
	}
	maxim_delay_us(READ_WAIT_LOW_LEVEL_TIME);/* Keep GPIO at the input state */
	ONE_WIRE_OUT_HIGH;
	ONE_WIRE_CONFIG_OUT;
	if (g_onewire_data->maxim_romid_crc_support)
		check_romid_bit(r_diff_ns, vamm, i);
	maxim_delay_us(READ_RELESE_IC_TIME);     /* Keep GPIO at the output state */
	return (vamm);                           /*  return value of 1-wire dat pin */
}


/* Send 8 bits of communication to the 1-Wire Net and verify that the
 8 bits read from the 1-Wire Net is the same (write operation).
 The parameter 'sendbyte' least significant 8 bits are used.
 'val' - 8 bits to send (least significant byte)
 Returns:  TRUE: bytes written and echo was the same
           FALSE: echo was not the same
 */
void write_byte(unsigned char val)
{
	unsigned char i;
	unsigned char temp;
	unsigned long flags;
	raw_spin_lock_irqsave(&g_onewire_data->lock, flags);
	ONE_WIRE_CONFIG_OUT;
	for (i = 0; i < BYTE_LENGTH_8; i++) { /*  writes byte, one bit at a time */
		temp = val >> i;                  /*  shifts val right */
		temp &= 0x01;                     /*  copy that bit to temp */
		write_bit(temp);                  /*  write bit in temp into */
	}
	raw_spin_unlock_irqrestore(&g_onewire_data->lock, flags);
}

/* Send 8 bits of read communication to the 1-Wire Net and and return the
 result 8 bits read from the 1-Wire Net.
 Returns:  8 bits read from 1-Wire Net
 */
unsigned char read_byte(void)
{
	unsigned char i;
	unsigned char value = 0;
	unsigned long flags;
	raw_spin_lock_irqsave(&g_onewire_data->lock, flags);
	for (i = 0; i < BYTE_LENGTH_8; i++) {
		/*reads byte in, one byte at a time and then shifts it left */
		if(read_bit())
			value |= 0x01 << i;
	}

	raw_spin_unlock_irqrestore(&g_onewire_data->lock, flags);
	return (value);
}

int onewire_init(struct onewire_gpio_data *onewire_data)
{
	chg_info("%s entry", __func__);
	g_onewire_data = (struct onewire_gpio_data *)kmalloc(sizeof(struct onewire_gpio_data),
			GFP_KERNEL);
	if (!g_onewire_data) {
		chg_info("Failed to allocate memory\n");
		return -ENOMEM;
	}

	if (onewire_data == NULL) {
		kfree(g_onewire_data);
		chg_info("%s onewire_data is null return", __func__);
		return -1;
	}
	raw_spin_lock_init(&g_onewire_data->lock);

	g_onewire_data->gpio_cfg_out_reg = onewire_data->gpio_cfg_out_reg;
	g_onewire_data->gpio_cfg_in_reg = onewire_data->gpio_cfg_in_reg;
	g_onewire_data->gpio_out_high_reg = onewire_data->gpio_out_high_reg;
	g_onewire_data->gpio_out_low_reg = onewire_data->gpio_out_low_reg;
	g_onewire_data->gpio_in_reg = onewire_data->gpio_in_reg;
	g_onewire_data->gpio_addr_offset = onewire_data->gpio_addr_offset;
	g_onewire_data->onewire_gpio_cfg_out_val = onewire_data->onewire_gpio_cfg_out_val;
	g_onewire_data->onewire_gpio_cfg_in_val = onewire_data->onewire_gpio_cfg_in_val;
	g_onewire_data->onewire_gpio_level_high_val = onewire_data->onewire_gpio_level_high_val;
	g_onewire_data->onewire_gpio_level_low_val = onewire_data->onewire_gpio_level_low_val;
	g_onewire_data->write_begin_low_level_time = onewire_data->write_begin_low_level_time;
	g_onewire_data->write_relese_ic_time = onewire_data->write_relese_ic_time;
	g_onewire_data->maxim_romid_crc_support = onewire_data->maxim_romid_crc_support;
	chg_info("cfg_out_reg is 0x%p, cfg_in_reg is 0x%p, out_high_reg 0x%p, \
		out_low_reg 0x%p, in_reg 0x%p, offset 0x%x",
	g_onewire_data->gpio_cfg_out_reg, g_onewire_data->gpio_cfg_in_reg,
	g_onewire_data->gpio_out_high_reg, g_onewire_data->gpio_out_low_reg,
	g_onewire_data->gpio_in_reg, g_onewire_data->gpio_addr_offset);

	ONE_WIRE_CONFIG_OUT;
	ONE_WIRE_OUT_HIGH;
	return 0;
}

void onewire_set_gpio_config_out(void)
{
	ONE_WIRE_CONFIG_OUT;
	chg_info("%s set gpio out", __func__);
}

bool get_maxim_romid_crc_support(void)
{
	return g_onewire_data->maxim_romid_crc_support;
}
