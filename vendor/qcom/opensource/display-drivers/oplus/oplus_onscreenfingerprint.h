/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_onscreenfingerprint.h
** Description : oplus onscreenfingerprint feature
** Version : 1.0
** Date : 2022/01/14
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/15        1.0           Build this moudle
******************************************************************/
#ifndef _OPLUS_ONSCREENFINGERPRINT_H_
#define _OPLUS_ONSCREENFINGERPRINT_H_

#include <drm/drm_crtc.h>
#include "dsi_panel.h"
#include "dsi_defs.h"
#include "dsi_parser.h"
#include "sde_encoder_phys.h"
#include "sde_crtc.h"
#include "oplus_display_panel_oha.h"

#define FFL_FP_LEVEL 150

enum oplus_ofp_switch {
	OFP_SWITCH_OFF = 0,
	OFP_SWITCH_ON,
};

enum oplus_ofp_notify_delay {
	OFP_POWER_ON_DELAY = 7,
	OFP_POWER_DOZE_DELAY = 20,
};

enum CUST_ALPHA_ENUM {
	CUST_A_NO = 0,
	CUST_A_TRANS,  /* alpha = 0, transparent */
	CUST_A_OPAQUE, /* alpha = 255, opaque */
};

enum ofp_vblank_sync_case {
	OFP_SYNC_DEFAULT = 0,
	OFP_SYNC_BEFORE_HBM_ON,
	OFP_SYNC_AFTER_HBM_ON,
	OFP_SYNC_BEFORE_HBM_OFF,
	OFP_SYNC_AFTER_HBM_OFF,
};

enum ofp_layer_type {
	OFP_FP_LAYER = 1,
	OFP_FPPRESSED_LAYER,
	OFP_AOD_LAYER,
};

enum ofp_request_power_type {
	OFP_REQUEST_NO = 0,
	OFP_REQUEST_POWER_OFF,
	OFP_REQUEST_POWER_ON,
	OFP_REQUEST_POWER_DOZE,
	OFP_REQUEST_POWER_DOZESUSPEND,
};

/**
 * struct hbm on/off time configuration
 */
struct ofp_vblank_sync_time {
	int ofp_on_vblank;
	int ofp_off_vblank;
};

void oplus_ofp_init(struct dsi_panel *panel);

inline bool oplus_ofp_is_support(void);

int oplus_get_ofp_switch_mode(void);

void oplus_set_ofp_switch_mode(int ofp_switch_mode);

int sde_crtc_onscreenfinger_atomic_check(struct sde_crtc_state *cstate,
		struct plane_state *pstates, int cnt);

void oplus_ofp_send_message(struct sde_crtc_state *old_cstate, struct sde_crtc_state *cstate, struct drm_crtc *crtc);

ssize_t oplus_display_notify_fp_press(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count);
ssize_t oplus_display_set_dimlayer_hbm(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count);
ssize_t oplus_display_get_dimlayer_hbm(struct kobject *obj,
		struct kobj_attribute *attr, char *buf);

int dsi_panel_tx_cmd_hbm_pre_check(struct dsi_panel *panel, enum dsi_cmd_set_type type, const char** prop_map);

void dsi_panel_tx_cmd_hbm_post_check(struct dsi_panel *panel, enum dsi_cmd_set_type type);

int oplus_ofp_update_hbm(struct drm_connector *connector);

int dsi_panel_parse_ofp_config(struct dsi_panel *panel);

int dsi_panel_parse_ofp_vblank_config(struct dsi_display_mode *mode,
		struct dsi_parser_utils *utils);

bool sde_crtc_get_dimlayer_mode(struct drm_crtc_state *crtc_state);

bool sde_crtc_get_fingerprint_mode(struct drm_crtc_state *crtc_state);

bool sde_crtc_get_fingerprint_pressed(struct drm_crtc_state *crtc_state);

int sde_crtc_set_onscreenfinger_defer_sync(struct drm_crtc_state *crtc_state,
		bool defer_sync);

int sde_crtc_config_fingerprint_dim_layer(struct drm_crtc_state *crtc_state,
		int stage);

bool _sde_encoder_setup_dither_for_onscreenfingerprint(struct sde_encoder_phys *phys,
		void *dither_cfg, int len, struct sde_hw_pingpong *hw_pp);

int sde_plane_check_fingerprint_layer(const struct drm_plane_state *drm_state);
int oplus_ofp_notify_init(void);
void oplus_ofp_notify_message(int blank, int delay);
void oplus_ofp_notify_fppress_event(int blank);
int oplus_display_panel_set_dimlayer_hbm(void *data);
int oplus_display_panel_get_dimlayer_hbm(void *data);
int oplus_display_panel_notify_fp_press(void *data);
void oplus_set_aod_dim_alpha(int cust);
int oplus_get_panel_power_mode(void);
#endif /*_OPLUS_ONSCREENFINGERPRINT_H_*/
