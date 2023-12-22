/***************************************************************
** Copyright (C), 2022, OPLUS Mobile Comm Corp., Ltd
**
** File : oplus_display_panel_common.c
** Description : oplus display panel common feature
** Version : 1.0
** Date : 2022/08/01
** Author : Display
******************************************************************/
#ifndef _OPLUS_DISPLAY_PANEL_COMMON_H_
#define _OPLUS_DISPLAY_PANEL_COMMON_H_

#include <linux/err.h>
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"
#include "oplus_display_private_api.h"

#define PANEL_IOCTL_BUF_MAX 41
#define PANEL_REG_MAX_LENS 28
#define PANEL_TX_MAX_BUF 512
#define FFC_MODE_MAX_COUNT 4
#define FFC_DELAY_MAX_FRAMES 10
#define PANEL_NAME_LENS 50
#define RGB_COLOR_WEIGHT 3
#define BPP_SHIFT 12

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

struct panel_id {
	uint32_t DA;
	uint32_t DB;
	uint32_t DC;
};

struct panel_info {
	char version[32];
	char manufacture[32];
};

struct panel_serial_number {
	char serial_number[PANEL_IOCTL_BUF_MAX];
};

struct panel_name {
	char name[PANEL_NAME_LENS];
};

struct display_timing_info {
	uint32_t h_active;
	uint32_t v_active;
	uint32_t refresh_rate;
	uint32_t clk_rate_hz_h32;  /* the high 32bit of clk_rate_hz */
	uint32_t clk_rate_hz_l32;  /* the low 32bit of clk_rate_hz */
};

enum {
	NONE_TYPE = 0,
	LCM_DC_MODE_TYPE,
	LCM_BRIGHTNESS_TYPE,
	MAX_INFO_TYPE,
};

struct panel_reg_get {
	uint32_t reg_rw[PANEL_REG_MAX_LENS];
	uint32_t lens; /*reg_rw lens, lens represent for u32 to user space*/
};

struct panel_reg_rw {
	uint32_t rw_flags; /*1 for read, 0 for write*/
	uint32_t cmd;
	uint32_t lens;     /*lens represent for u8 to kernel space*/
	uint32_t value[PANEL_REG_MAX_LENS]; /*for read, value is empty, just user get function for read the value*/
};

enum PWM_SWITCH_STATE{
	PWM_SWITCH_LOW_STATE = 0,
	PWM_SWITCH_HIGH_STATE,
};

extern bool oplus_temp_compensation_wait_for_vsync_set;

int oplus_display_panel_get_id(void *buf);
int oplus_display_panel_get_max_brightness(void *buf);
int oplus_display_panel_set_max_brightness(void *buf);
int oplus_display_panel_get_lcd_max_brightness(void *buf);
int oplus_display_panel_set_brightness(void *buf);
int oplus_display_panel_get_brightness(void *buf);
int oplus_display_panel_get_vendor(void *buf);
int oplus_display_panel_get_ccd_check(void *buf);
int oplus_display_panel_get_serial_number(void *buf);
int oplus_display_set_qcom_loglevel(void *data);
int oplus_display_panel_set_audio_ready(void *data);
int oplus_display_panel_dump_info(void *data);
int oplus_display_panel_get_dsc(void *data);
int oplus_display_panel_get_closebl_flag(void *data);
int oplus_display_panel_set_closebl_flag(void *data);
int oplus_display_panel_get_reg(void *data);
int oplus_display_panel_set_reg(void *data);
int oplus_display_panel_notify_blank(void *data);
int oplus_display_panel_set_spr(void *data);
int oplus_display_panel_get_spr(void *data);
int oplus_display_panel_get_roundcorner(void *data);
int oplus_display_panel_set_dynamic_osc_clock(void *data);
int oplus_display_panel_get_dynamic_osc_clock(void *data);
int oplus_display_get_softiris_color_status(void *data);
int oplus_display_panel_get_panel_type(void *data);
int oplus_display_panel_hbm_lightspot_check(void);
int oplus_display_set_dither_status(void *buf);
int oplus_display_get_dither_status(void *buf);
int oplus_display_panel_get_oplus_max_brightness(void *buf);
void oplus_display_panel_enable(void);
int oplus_display_get_dp_support(void *buf);
int oplus_display_set_cabc_status(void *buf);
int oplus_display_get_cabc_status(void *buf);
int oplus_display_set_dre_status(void *buf);
int oplus_display_get_dre_status(void *buf);
/* Add for dtsi parse*/
int dsi_panel_parse_oplus_config(struct dsi_panel *panel);
int oplus_panel_set_ffc_mode_unlock(struct dsi_panel *panel);
int oplus_panel_set_ffc_kickoff_lock(struct dsi_panel *panel);
int oplus_panel_check_ffc_config(struct dsi_panel *panel,
		struct oplus_clk_osc *clk_osc_pending);
int oplus_display_update_clk_ffc(struct dsi_display *display,
		struct dsi_display_mode *cur_mode, struct dsi_display_mode *adj_mode);
int oplus_display_update_osc_ffc(struct dsi_display *display,
		u32 osc_rate);
int oplus_display_tx_cmd_set_lock(struct dsi_display *display,
		enum dsi_cmd_set_type type);
int oplus_display_get_iris_loopback_status(void *buf);
int oplus_display_panel_set_dc_real_brightness(void *data);
inline bool oplus_panel_pwm_turbo_is_enabled(struct dsi_panel *panel);
inline bool oplus_panel_pwm_turbo_switch_state(struct dsi_panel *panel);
inline bool oplus_is_support_pwm_switch(struct dsi_panel *panel);
int oplus_panel_send_pwm_turbo_dcs_unlock(struct dsi_panel *panel, bool enabled);
int oplus_panel_update_pwm_turbo_lock(struct dsi_panel *panel, bool enabled);
int oplus_display_panel_set_pwm_turbo(void *data);
int oplus_display_panel_get_pwm_turbo(void *buf);
int oplus_display_pwm_turbo_kickoff(void);
int oplus_wait_for_vsync(struct dsi_panel *panel);
int oplus_sde_early_wakeup(void);
void oplus_save_te_timestamp(struct sde_connector *c_conn, ktime_t timestamp);
int oplus_display_pwm_pulse_switch(void *dsi_panel, unsigned int bl_level);
int oplus_panel_tx_cmd_update(struct dsi_panel *panel, enum dsi_cmd_set_type *type);
int oplus_display_update_dbv(struct dsi_panel *panel);
int oplus_display_panel_set_demua(void);
int oplus_set_pulse_switch(struct dsi_panel *panel, bool enable);
void oplus_apollo_async_bl_delay(struct dsi_panel *panel);
int oplus_display_panel_get_panel_bpp(void *buf);
int oplus_display_panel_get_panel_name(void *buf);

/**
 * oplus_display_send_dcs_lock() - send dcs with lock
 */
int oplus_display_send_dcs_lock(struct dsi_display *display,
		enum dsi_cmd_set_type type);
#endif /*_OPLUS_DISPLAY_PANEL_COMMON_H_*/

