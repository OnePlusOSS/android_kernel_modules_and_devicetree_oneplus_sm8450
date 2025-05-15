/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_onscreenfingerprint.c
** Description : oplus onscreenfingerprint feature
** Version : 1.0
** Date : 2022/01/14
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   Qianxu         2020/04/15        1.0           Build this moudle
******************************************************************/

#include "sde_crtc.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_display_private_api.h"
#include "oplus_display_panel.h"
#include "oplus_adfr.h"
#include "oplus_aod.h"
#include "oplus_bl.h"
#include "dsi_defs.h"
#include "sde_trace.h"
#include "oplus_dc_diming.h"
#include "oplus_display_panel_seed.h"
#include <linux/ktime.h>
#include <drm/sde_drm.h>
#include <linux/msm_drm_notify.h>
#include <linux/notifier.h>
#include <linux/soc/qcom/panel_event_notifier.h>

#define OPLUS_OFP_CONFIG_GLOBAL (1<<0)
#define OFP_GET_GLOBAL_CONFIG(config) ((config) & OPLUS_OFP_CONFIG_GLOBAL)

/* Add for ofp dim alpha */
extern int oplus_underbrightness_alpha;
extern int msm_drm_notifier_call_chain(unsigned long val, void *v);
extern struct oplus_apollo_backlight_list *p_apollo_backlight;
extern bool apollo_backlight_enable;
extern int oplus_request_power_status;
extern int oplus_panel_alpha;
extern int hbm_mode;
extern bool oplus_ffl_trigger_finish;
extern int oplus_dimlayer_dither_threshold;
extern u32 oplus_last_backlight;
extern int oplus_ofp_on_vblank;
extern int oplus_ofp_off_vblank;
extern int oplus_oha_enable;
/* Add for OnScreenFingerprint fingerpress */
int oplus_dimlayer_hbm = 0;
int oplus_dimlayer_hbm_vblank_count;
int oplus_onscreenfp_status = 0;
atomic_t oplus_dimlayer_hbm_vblank_ref = ATOMIC_INIT(0);
static u32 oplus_ofp_config = 0;
u32 oplus_ofp_switch = 1;
unsigned int oplus_fps_period_us;
int oplus_aod_dim_alpha = CUST_A_NO;
/* Add for ofp fingerpress notify */
static struct task_struct *oplus_ofp_notify_task;
static wait_queue_head_t ofp_notify_task_wq;
static atomic_t ofp_task_task_wakeup = ATOMIC_INIT(0);
static bool ofp_delay_off = false;
static int ofp_delay_time = 0;
int ofp_blank_event = 0;

void oplus_ofp_init(struct dsi_panel *panel)
{
	static bool inited = false;
	u32 config = 0;
	int rc = 0;
	struct dsi_parser_utils *utils = NULL;

	if (!panel)
		return;

	utils = &panel->utils;
	if (!utils)
		return;

	if (inited && oplus_ofp_config) {
		DSI_WARN("kOFP ofp config = %#X already!", oplus_ofp_config);
		return;
	}

	rc = utils->read_u32(utils->data, "oplus,ofp-config", &config);
	if (rc == 0) {
		oplus_ofp_config = config;
	} else {
		oplus_ofp_config = 0;
	}

	inited = true;

	pr_info("kOFP ofp config = %#X\n", oplus_ofp_config);
}

inline bool oplus_ofp_is_support(void)
{
	return (bool) (OFP_GET_GLOBAL_CONFIG(oplus_ofp_config) && oplus_ofp_switch);
}

int oplus_get_ofp_switch_mode(void)
{
	return oplus_ofp_switch;
}

void oplus_set_ofp_switch_mode(int ofp_switch_mode)
{
	oplus_ofp_switch = ofp_switch_mode;
	DSI_INFO("set oplus_ofp_switch = %d\n", oplus_ofp_switch);
}

struct hbm_status
{
	bool aod_hbm_status; /*use for hbm aod cmd*/
	bool hbm_status;     /*use for hbm cmd*/
	bool hbm_pvt_status; /*use for hbm other cmds*/
};

struct hbm_status oplus_hbm_status = {0};
int dsi_panel_tx_cmd_hbm_pre_check(struct dsi_panel *panel, enum dsi_cmd_set_type type, const char** prop_map)
{
	int ret = 0;
	DSI_DEBUG("%s cmd=%s", __func__, prop_map[type]);
	switch(type) {
	case DSI_CMD_AOD_HBM_ON:
		if (oplus_hbm_status.aod_hbm_status == 1) {
			DSI_DEBUG("%s skip cmd=%s", __func__, prop_map[type]);
			ret = 1;
		}
		break;
	case DSI_CMD_AOD_HBM_OFF:
		if (oplus_hbm_status.aod_hbm_status == 0) {
			DSI_DEBUG("%s skip cmd=%s", __func__, prop_map[type]);
			ret = 1;
		}
		break;
	default:
		break;
	}
	return ret;
}

void dsi_panel_tx_cmd_hbm_post_check(struct dsi_panel *panel, enum dsi_cmd_set_type type)
{
	switch(type) {
	case DSI_CMD_AOD_HBM_ON:
		if (oplus_hbm_status.aod_hbm_status == 0) {
			oplus_hbm_status.aod_hbm_status = 1;
		}
		break;
	case DSI_CMD_HBM_ON:
		if (oplus_adfr_is_support()) {
			if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC)
				/* switch to tp vsync because aod off */
				oplus_adfr_aod_fod_vsync_switch(panel, false);
		}
		break;
	case DSI_CMD_AOD_HBM_OFF:
		if (oplus_hbm_status.aod_hbm_status == 1) {
			oplus_hbm_status.aod_hbm_status = 0;
		}
		break;
	case DSI_CMD_HBM_OFF:
		if (oplus_adfr_is_support()) {
			if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC)
				/* switch to tp vsync because unlock successfully or panel disable */
				oplus_adfr_aod_fod_vsync_switch(panel, false);
		}
		break;
	case DSI_CMD_SET_NOLP:
	case DSI_CMD_SET_OFF:
		oplus_hbm_status.aod_hbm_status = 0;
		break;
	default:
		break;
	}
	DSI_DEBUG("%s [hbm_pvt,hbm,hbm_aod] = [%d %d %d]", __func__,
		oplus_hbm_status.hbm_pvt_status, oplus_hbm_status.hbm_status, oplus_hbm_status.aod_hbm_status);

	return;
}

int fingerprint_wait_vsync(struct drm_encoder *drm_enc, struct dsi_panel *panel)
{
	SDE_ATRACE_BEGIN("wait_vsync");

	if (!drm_enc || !drm_enc->crtc || !panel) {
		SDE_ERROR("kOFP, %s encoder is disabled", __func__);
		return -ENOLINK;
	}

	if (sde_encoder_is_disabled(drm_enc)) {
		SDE_ERROR("kOFP, %s encoder is disabled", __func__);
		return -EIO;
	}

	mutex_unlock(&panel->panel_lock);
	sde_encoder_wait_for_event(drm_enc,  MSM_ENC_VBLANK);
	mutex_lock(&panel->panel_lock);
	SDE_ATRACE_END("wait_vsync");

	return 0;
}

static int ofp_vblank_sync_delay(struct sde_connector *sde_conn,
		unsigned int fps_period_us,
		enum ofp_vblank_sync_case ofp_sync_case)
{
	int vblank = 0;
	struct dsi_display *dsi_display;
	struct drm_crtc *crtc;
	struct ofp_vblank_sync_time vblank_cfg;
	u32 target_vblank, current_vblank;
	int ret;

	if (!sde_conn || !sde_conn->display) {
		SDE_ERROR("kOFP, Invalid params sde_connector null\n");
		return -EINVAL;
	}

	if (sde_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		return 0;
	}

	dsi_display = sde_conn->display;

	if (!dsi_display || !dsi_display->panel || !dsi_display->panel->cur_mode) {
		SDE_ERROR("kOFP, Invalid params(s) dsi_display %pK, panel %pK\n",
			  dsi_display,
			  ((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	if (!sde_conn->encoder || !sde_conn->encoder->crtc) {
		return 0;
	}

	crtc = sde_conn->encoder->crtc;

	SDE_ATRACE_BEGIN("ofp_vblank_sync_delay");
	if (oplus_ofp_on_vblank >= 0) {
		vblank_cfg.ofp_on_vblank = oplus_ofp_on_vblank;
	}

	if (oplus_ofp_off_vblank >= 0) {
		vblank_cfg.ofp_off_vblank = oplus_ofp_off_vblank;
	}

	switch(ofp_sync_case) {
	case OFP_SYNC_BEFORE_HBM_ON:
			current_vblank = drm_crtc_vblank_count(crtc);
			if (dsi_display->panel->cur_mode->timing.refresh_rate == 60) {
				ret = fingerprint_wait_vsync(sde_conn->encoder, dsi_display->panel);
			} else {
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
					current_vblank != drm_crtc_vblank_count(crtc),
					usecs_to_jiffies(fps_period_us));
			}
			if (!ret) {
				pr_err("kOFP, fp enter:wait sync vblank timeout target_vblank=%d current_vblank=%d\n",
						current_vblank, drm_crtc_vblank_count(crtc));
			}
			break;
	case OFP_SYNC_AFTER_HBM_ON:
			vblank = vblank_cfg.ofp_on_vblank;
			target_vblank = drm_crtc_vblank_count(crtc) + vblank;

			if (vblank) {
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						target_vblank <= drm_crtc_vblank_count(crtc),
						usecs_to_jiffies((vblank + 1) * fps_period_us));
				if (!ret) {
					pr_err("kOFP, OnscreenFingerprint failed to wait vblank timeout target_vblank=%d current_vblank=%d\n",
							target_vblank, drm_crtc_vblank_count(crtc));
				}
			}
			break;
	case OFP_SYNC_BEFORE_HBM_OFF:
			current_vblank = drm_crtc_vblank_count(crtc);

			ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
					current_vblank != drm_crtc_vblank_count(crtc),
					usecs_to_jiffies(fps_period_us));
			break;
	case OFP_SYNC_AFTER_HBM_OFF:
			vblank = vblank_cfg.ofp_off_vblank;
			target_vblank = drm_crtc_vblank_count(crtc) + vblank;

			if (vblank) {
				ret = wait_event_timeout(*drm_crtc_vblank_waitqueue(crtc),
						target_vblank <= drm_crtc_vblank_count(crtc),
						usecs_to_jiffies((vblank + 1) * fps_period_us));
				if (!ret) {
					pr_err("kOFP, OnscreenFingerprint failed to wait vblank timeout target_vblank=%d current_vblank=%d\n",
							target_vblank, drm_crtc_vblank_count(crtc));
				}
			}
			break;
	case OFP_SYNC_DEFAULT:
			ret = fingerprint_wait_vsync(sde_conn->encoder, dsi_display->panel);
			if (ret) {
				pr_err("kOFP, OnscreenFingerprint failed to wait vsync\n");
			}
			break;
	default:
		break;
	}
	SDE_ATRACE_END("ofp_vblank_sync_delay");

	return 0;
}

static int oplus_ofp_update_hbm_enter(struct drm_connector *connector,
		struct dsi_display *dsi_display, unsigned int fps_period_us)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	int rc = 0;

	if (!c_conn) {
		SDE_ERROR("kOFP, Invalid params sde_connector null\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		return 0;
	}

	if (!dsi_display || !dsi_display->panel) {
		SDE_ERROR("kOFP, Invalid params(s) dsi_display %pK, panel %pK\n",
			  dsi_display,
			  ((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	mutex_lock(&dsi_display->panel->panel_lock);

	if (!dsi_display->panel->panel_initialized) {
		dsi_display->panel->is_hbm_enabled = false;
		pr_err("kOFP, panel not initialized, failed to Enter OnscreenFingerprint\n");
		mutex_unlock(&dsi_display->panel->panel_lock);
		return 0;
	}

	dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);

	oplus_ofp_handle_display_effect(dsi_display, OFP_SYNC_BEFORE_HBM_ON);

	if (OPLUS_DISPLAY_AOD_SCENE != get_oplus_display_scene() &&
			dsi_display->panel->bl_config.bl_level) {
		if (dsi_display->config.panel_mode != DSI_OP_VIDEO_MODE) {
			if (OPLUS_DISPLAY_AOD_HBM_SCENE == get_oplus_display_scene()) {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
			} else {
				ofp_vblank_sync_delay(c_conn, fps_period_us, OFP_SYNC_DEFAULT);
				SDE_ATRACE_BEGIN("DSI_CMD_HBM_ON");
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_ON);
				SDE_ATRACE_END("DSI_CMD_HBM_ON");
			}
		}
	} else {
		rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
	}

	dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);

	mutex_unlock(&dsi_display->panel->panel_lock);

	if (rc) {
		pr_err("kOFP, failed to send DSI_CMD_HBM_ON cmds, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int oplus_ofp_update_hbm_exit(struct drm_connector *connector,
		struct dsi_display *dsi_display, unsigned int fps_period_us)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct dsi_panel *panel;
	int rc = 0;

	if (!c_conn) {
		SDE_ERROR("kOFP, Invalid params sde_connector null\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		return 0;
	}

	if (!dsi_display || !dsi_display->panel) {
		SDE_ERROR("kOFP, Invalid params(s) dsi_display %pK, panel %pK\n",
			  dsi_display,
			  ((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	panel = dsi_display->panel;

	mutex_lock(&dsi_display->panel->panel_lock);

	if (!dsi_display->panel->panel_initialized) {
		dsi_display->panel->is_hbm_enabled = true;
		pr_err("kOFP, panel not initialized, failed to Exit OnscreenFingerprint\n");
		mutex_unlock(&dsi_display->panel->panel_lock);
		return 0;
	}

	ofp_vblank_sync_delay(c_conn, fps_period_us, OFP_SYNC_DEFAULT);

	dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);

	if (dsi_display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		panel->oplus_priv.skip_mipi_last_cmd = true;
	}

	if (dsi_display->config.panel_mode == DSI_OP_VIDEO_MODE) {
		panel->oplus_priv.skip_mipi_last_cmd = false;
	}

	if (OPLUS_DISPLAY_AOD_HBM_SCENE == get_oplus_display_scene()) {
		if (OPLUS_DISPLAY_POWER_DOZE_SUSPEND == get_oplus_display_power_status() ||
				OPLUS_DISPLAY_POWER_DOZE == get_oplus_display_power_status()) {
			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF);
			mutex_unlock(&dsi_display->panel->panel_lock);
			/* Update aod light mode and fix 3658965*/
			mutex_lock(&dsi_display->panel->panel_lock);
			oplus_update_aod_light_mode_unlock(panel);
			set_oplus_display_scene(OPLUS_DISPLAY_AOD_SCENE);
		} else {
			if (!sde_crtc_get_fingerprint_mode(connector->state->crtc->state)) {
				/* Handle display effect update */
				oplus_ofp_handle_display_effect(dsi_display, OFP_SYNC_BEFORE_HBM_OFF);
				dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_AOR_RESTORE);
			}

			rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_OFF);

			if (oplus_display_panel_get_global_hbm_status())
				oplus_display_panel_set_global_hbm_status(GLOBAL_HBM_DISABLE);
			/* Handle display backlight refresh */
			oplus_ofp_hande_display_backlight(dsi_display, 0);
			set_oplus_display_scene(OPLUS_DISPLAY_NORMAL_SCENE);
			/* set nolp would exit hbm, restore when panel status on hbm */
			if (panel->bl_config.bl_level > panel->bl_config.bl_normal_max_level) {
				/* Handle display backlight update */
				oplus_ofp_hande_display_backlight(dsi_display, 0);
			}

			if (oplus_display_get_hbm_mode()) {
				rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_ON);
			}
		}
	} else if (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {
		rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_AOD_HBM_OFF);
		mutex_unlock(&dsi_display->panel->panel_lock);
		/* Update aod light mode and fix 3658965*/
		mutex_lock(&dsi_display->panel->panel_lock);
		oplus_update_aod_light_mode_unlock(panel);
	} else {
		/* Handle display effect update */
		oplus_ofp_handle_display_effect(dsi_display, OFP_SYNC_BEFORE_HBM_OFF);
		rc = dsi_panel_tx_cmd_set(dsi_display->panel, DSI_CMD_HBM_OFF);
		if (oplus_display_panel_get_global_hbm_status())
			oplus_display_panel_set_global_hbm_status(GLOBAL_HBM_DISABLE);
		/* Handle display backlight update */
		oplus_ofp_hande_display_backlight(dsi_display, 0);
	}
	dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
	mutex_unlock(&dsi_display->panel->panel_lock);

	return rc;
}
int oplus_ofp_update_hbm(struct drm_connector *connector)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct dsi_display *dsi_display;
	int rc = 0;
	int fingerprint_mode;

	if (!c_conn) {
		SDE_ERROR("kOFP, Invalid params sde_connector null\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		return 0;
	}

	dsi_display = c_conn->display;

	if (!dsi_display || !dsi_display->panel) {
		SDE_ERROR("kOFP, Invalid params(s) dsi_display %pK, panel %pK\n",
			  dsi_display,
			  ((dsi_display) ? dsi_display->panel : NULL));
		return -EINVAL;
	}

	if (!c_conn->encoder || !c_conn->encoder->crtc ||
			!c_conn->encoder->crtc->state) {
		return 0;
	}

	fingerprint_mode = sde_crtc_get_fingerprint_mode(c_conn->encoder->crtc->state);

	if (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {
		if (sde_crtc_get_fingerprint_pressed(c_conn->encoder->crtc->state)) {
			sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, true);

		} else {
			sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, false);
			fingerprint_mode = false;
		}

	} else {
		sde_crtc_set_onscreenfinger_defer_sync(c_conn->encoder->crtc->state, true);
	}

	if (fingerprint_mode != dsi_display->panel->is_hbm_enabled) {
		unsigned int fps_period_us =
			1000000/dsi_display->modes->timing.refresh_rate + 1;
		pr_err("kOFP, OnscreenFingerprint mode: %s",
				fingerprint_mode ? "Enter" : "Exit");

		dsi_display->panel->is_hbm_enabled = fingerprint_mode;

		if (fingerprint_mode) {
			rc = oplus_ofp_update_hbm_enter(connector, dsi_display, fps_period_us);
			if (rc) {
				pr_err("kOFP, failed to oplus_ofp_update_hbm_enter, rc=%d\n", rc);
			}
		} else {
			rc = oplus_ofp_update_hbm_exit(connector, dsi_display, fps_period_us);
			if (rc) {
				pr_err("kOFP, failed to oplus_ofp_update_hbm_exit, rc=%d\n", rc);
			}
		}
	}

	if (oplus_dimlayer_hbm_vblank_count > 0) {
		oplus_dimlayer_hbm_vblank_count--;

	} else {
		while (atomic_read(&oplus_dimlayer_hbm_vblank_ref) > 0) {
			drm_crtc_vblank_put(connector->state->crtc);
			atomic_dec(&oplus_dimlayer_hbm_vblank_ref);
		}
	}

	return 0;
}
EXPORT_SYMBOL(oplus_ofp_update_hbm);

int sde_crtc_onscreenfinger_atomic_check(struct sde_crtc_state *cstate,
		struct plane_state *pstates, int cnt)
{
	int fp_index = -1;
	int fppressed_index = -1;
	int aod_index = -1;
	int zpos = INT_MAX;
	int mode;
	int panel_power_mode;
	int dimlayer_bl = 0;
	int i;
	struct dsi_display *display = get_main_display();

	if (!display) {
		pr_err("kOFP, failed to find display\n");
		return -EINVAL;
	}

	for (i = 0; i < cnt; i++) {
		mode = sde_plane_check_fingerprint_layer(pstates[i].drm_pstate);
		if (mode == OFP_FP_LAYER)
			fp_index = i;
		if (mode == OFP_FPPRESSED_LAYER)
			fppressed_index = i;
		if (mode == OFP_AOD_LAYER)
			aod_index = i;
		if (pstates[i].sde_pstate)
			pstates[i].sde_pstate->is_skip = false;
	}

	if (!is_dsi_panel(cstate->base.crtc))
		return 0;

	dimlayer_bl = oplus_ofp_hande_display_softiris();

	if (fppressed_index >= 0) {
		if (oplus_onscreenfp_status == 0) {
			pstates[fppressed_index].sde_pstate->is_skip = true;
			fppressed_index = -1;
		}
	}

	if (aod_index >= 0)
		cstate->aod_skip_pcc = true;
	else
		cstate->aod_skip_pcc = false;

	if (fp_index >= 0)
		display->panel->is_ofp_enabled = true;
	else
		display->panel->is_ofp_enabled = false;

	if (oplus_oha_is_support()) {
		if ((aod_index >= 0) && (fp_index < 0) && (OHA_SWITCH_ON == oplus_oha_enable))
			cstate->oha_mode = true;
		else
			cstate->oha_mode = false;
	}

	SDE_EVT32(cstate->fingerprint_dim_layer);
	cstate->fingerprint_dim_layer = NULL;
	cstate->fingerprint_mode = false;
	cstate->fingerprint_pressed = false;

	if (oplus_dimlayer_hbm || dimlayer_bl) {
		if (fp_index >= 0 && fppressed_index >= 0) {
			if (pstates[fp_index].stage >= pstates[fppressed_index].stage) {
				SDE_ERROR("kOFP, Bug!!: fp layer top of fppressed layer\n");
				return -EINVAL;
			}
		}
		/* Close ofp when SUA feature request */
		if (OFP_SWITCH_OFF == oplus_get_ofp_switch_mode()) {
			oplus_underbrightness_alpha = 0;
			cstate->fingerprint_dim_layer = NULL;
			cstate->fingerprint_mode = false;
			return 0;
		}

		if (display->panel->bl_config.bl_level != 0)
			cstate->fingerprint_mode = true;
		else
			cstate->fingerprint_mode = false;

		SDE_DEBUG("kOFP, debug for get cstate->fingerprint_mode = %d\n", cstate->fingerprint_mode);

		panel_power_mode = oplus_get_panel_power_mode();

		/* when aod layer is present */
		if (aod_index >= 0) {
			/* set dimlayer alpha transparent, appear AOD layer by force */
			if (((fp_index >= 0) || (fppressed_index < 0)) &&
				((panel_power_mode == SDE_MODE_DPMS_LP1) || (panel_power_mode == SDE_MODE_DPMS_LP2))) {
				oplus_set_aod_dim_alpha(CUST_A_TRANS);
			}
			/*
			 * set dimlayer alpha opaque, disappear AOD layer by force when pressed down
			 * and SDE_MODE_DPMS_LP1/SDE_MODE_DPMS_LP2
			 */
			if (((oplus_onscreenfp_status == 1) && (panel_power_mode != SDE_MODE_DPMS_ON))
				|| (oplus_request_power_status == OFP_REQUEST_POWER_ON))
				oplus_set_aod_dim_alpha(CUST_A_OPAQUE);
		} else { /* when screen on, restore dimlayer alpha */
			if (oplus_get_panel_brightness() != 0)
				oplus_set_aod_dim_alpha(CUST_A_NO);
		}

		SDE_DEBUG("kOFP, aod_index=%d, fp_index=%d, fppressed_index=%d, fp_mode=%d, panel_power_mode=%d,bl=%d\n",
			aod_index, fp_index, fppressed_index, oplus_onscreenfp_status, panel_power_mode, oplus_get_panel_brightness());

		/* find the min zpos in fp_index/fppressed_index stage to dim layer, then fp_index/fppressed_index stage increase one */
		if (fp_index >= 0) {
			if (zpos > pstates[fp_index].stage)
				zpos = pstates[fp_index].stage;
		}
		if (fppressed_index >= 0) {
			if (zpos > pstates[fppressed_index].stage)
				zpos = pstates[fppressed_index].stage;
		}

		/* increase zpos(sde stage) which is on the dim layer, stage which is under dim layer zpos preserve */
		for (i = 0; i < cnt; i++) {
			if (pstates[i].stage >= zpos) {
				pstates[i].stage++;
			}
		}

		/* when no aod_index/fppressed_index/fp_index layer, dim layer's zpos is the most stage */
		if (zpos == INT_MAX) {
			zpos = 0;
			for (i = 0; i < cnt; i++) {
				if (pstates[i].stage > zpos)
					zpos = pstates[i].stage;
			}
			zpos++;
		}

		SDE_EVT32(zpos, fp_index, aod_index, fppressed_index, cstate->num_dim_layers);
		if (sde_crtc_config_fingerprint_dim_layer(&cstate->base, zpos)) {
			/* Failed to config dim layer */
			SDE_EVT32(zpos, fp_index, aod_index, fppressed_index, cstate->num_dim_layers);
			return -EINVAL;
		}
		if (fppressed_index >= 0)
			cstate->fingerprint_pressed = true;
		else
			cstate->fingerprint_pressed = false;

		SDE_DEBUG("kOFP, debug for get cstate->fingerprint_pressed = %d\n", cstate->fingerprint_pressed);
	} else {
		oplus_underbrightness_alpha = 0;
		oplus_set_aod_dim_alpha(CUST_A_NO);
		cstate->fingerprint_dim_layer = NULL;
		cstate->fingerprint_mode = false;
		cstate->fingerprint_pressed = false;
	}
	SDE_EVT32(cstate->fingerprint_dim_layer);

	return 0;
}

/* Add for ofp fingerpress notify */
void oplus_ofp_delay_notify(int blank, int delay)
{
	if (!ofp_delay_off)
		return;

	usleep_range(delay*1000, delay*1000 + 100);
	oplus_ofp_notify_fppress_event(blank);
	ofp_delay_off = false;
	return;
}

static int oplus_ofp_worker_kthread(void *data)
{
	int ret = 0;

	while (1) {
		ret = wait_event_interruptible(ofp_notify_task_wq, atomic_read(&ofp_task_task_wakeup));
		atomic_set(&ofp_task_task_wakeup, 0);
		oplus_ofp_delay_notify(ofp_blank_event, ofp_delay_time);
		if (kthread_should_stop())
			break;
	}
	return 0;
}

int oplus_ofp_notify_init(void)
{
	int ret = 0;

	if (!oplus_ofp_notify_task) {
		oplus_ofp_notify_task = kthread_create(oplus_ofp_worker_kthread, NULL, "ofp_notify");
		if (IS_ERR(oplus_ofp_notify_task)) {
			pr_err("kOFP, [%s]: fail to init create\n", __func__);
			ret = PTR_ERR(oplus_ofp_notify_task);
			oplus_ofp_notify_task = NULL;
			return ret;
		}
		init_waitqueue_head(&ofp_notify_task_wq);
		wake_up_process(oplus_ofp_notify_task);
		pr_info("kOFP, [%s]: init create\n", __func__);
	}
	return ret;
}

void oplus_ofp_notify_message(int blank, int delay)
{
	if (oplus_ofp_notify_task != NULL) {
		ofp_delay_off = true;
		ofp_delay_time = delay;
		ofp_blank_event = blank;
		atomic_set(&ofp_task_task_wakeup, 1);
		wake_up_interruptible(&ofp_notify_task_wq);
	} else {
		pr_info("[oplus_ofp_notify_message] notify is NULL\n");
	}
}

void oplus_ofp_notify_fppress_event(int blank)
{
	struct msm_drm_notifier notifier_data;
	enum panel_event_notification_type notify_type;
	struct dsi_display *display = get_main_display();
	notifier_data.data = &blank;

	if (!display) {
		SDE_ERROR("kOFP, failed to find display\n");
		return;
	}

	pr_err("kOFP, fingerprint status: %s",
		   blank ? "pressed" : "up");
	msm_drm_notifier_call_chain(MSM_DRM_ONSCREENFINGERPRINT_EVENT,
			&notifier_data);
	if (blank)
		notify_type = DRM_PANEL_EVENT_ONSCREENFINGERPRINT_UI_READY;
	else
		notify_type = DRM_PANEL_EVENT_ONSCREENFINGERPRINT_UI_DISAPPEAR;

	SDE_ATRACE_BEGIN("oplus_notify_fingerprint_press_event");
	oplus_panel_event_notification_trigger(display, notify_type);
	SDE_ATRACE_END("oplus_notify_fingerprint_press_event");
}

void oplus_ofp_send_message(struct sde_crtc_state *old_cstate,
		struct sde_crtc_state *cstate, struct drm_crtc *crtc)
{
	if (old_cstate->fingerprint_pressed != cstate->fingerprint_pressed) {
		int blank = cstate->fingerprint_pressed;

		oplus_ofp_notify_init();
		if (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {
			oplus_ofp_notify_message(blank, OFP_POWER_DOZE_DELAY);
		} else {
			oplus_ofp_notify_message(blank, OFP_POWER_ON_DELAY);
		}
	}
}


ssize_t oplus_display_notify_fp_press(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	struct drm_device *drm_dev = display->drm_dev;
	struct drm_connector *dsi_connector = display->drm_conn;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct msm_drm_private *priv = drm_dev->dev_private;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int onscreenfp_status = 0;
	int vblank_get = -EINVAL;
	int err = 0;
	int i;

	if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
		pr_err("kOFP, [%s]: display not ready\n", __func__);
		return count;
	}

	sscanf(buf, "%du", &onscreenfp_status);
	onscreenfp_status = !!onscreenfp_status;

	if (onscreenfp_status == oplus_onscreenfp_status) {
		return count;
	}

	pr_err("kOFP, notify fingerpress %s\n", onscreenfp_status ? "on" : "off");

	vblank_get = drm_crtc_vblank_get(dsi_connector->state->crtc);

	if (vblank_get) {
		pr_err("kOFP, failed to get crtc vblank\n", vblank_get);
	}

	oplus_onscreenfp_status = onscreenfp_status;

	if (onscreenfp_status &&
			OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene()) {
		/* enable the clk vote for CMD mode panels */
		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					DSI_ALL_CLKS, DSI_CLK_ON);
		}

		mutex_lock(&display->panel->panel_lock);

		if (display->panel->panel_initialized) {
			err = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_ON);
		}

		mutex_unlock(&display->panel->panel_lock);

		if (err) {
			pr_err("failed to setting aod hbm on mode %d\n", err);
		}

		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					DSI_ALL_CLKS, DSI_CLK_OFF);
		}
	}

	drm_modeset_lock_all(drm_dev);

	state = drm_atomic_state_alloc(drm_dev);

	if (!state) {
		goto error;
	}

	state->acquire_ctx = mode_config->acquire_ctx;
	crtc = dsi_connector->state->crtc;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);

	for (i = 0; i < priv->num_crtcs; i++) {
		if (priv->disp_thread[i].crtc_id == crtc->base.id) {
			if (priv->disp_thread[i].thread) {
				kthread_flush_worker(&priv->disp_thread[i].worker);
			}
		}
	}

	err = drm_atomic_commit(state);
	drm_atomic_state_put(state);

error:
	drm_modeset_unlock_all(drm_dev);

	if (!vblank_get) {
		drm_crtc_vblank_put(dsi_connector->state->crtc);
	}

	return count;
}


ssize_t oplus_display_set_dimlayer_hbm(struct kobject *obj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = display->drm_conn;
	int err = 0;
	int value = 0;

	sscanf(buf, "%d", &value);
	value = !!value;

	if (oplus_dimlayer_hbm == value) {
		return count;
	}

	if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
		pr_err("kOFP, [%s]: display not ready\n", __func__);

	} else {
		err = drm_crtc_vblank_get(dsi_connector->state->crtc);

		if (err) {
			pr_err("kOFP, failed to get crtc vblank, error=%d\n", err);

		} else {
			/* do vblank put after 5 frames */
			oplus_dimlayer_hbm_vblank_count = 5;
			atomic_inc(&oplus_dimlayer_hbm_vblank_ref);
		}
	}

	oplus_dimlayer_hbm = value;

	pr_err("kOFP, debug for oplus_display_set_dimlayer_hbm set oplus_dimlayer_hbm = %d\n",
			oplus_dimlayer_hbm);

	return count;
}


ssize_t oplus_display_get_dimlayer_hbm(struct kobject *obj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_dimlayer_hbm);
}

static struct oplus_brightness_alpha brightness_alpha_lut[] = {
	{0, 0xff},
	{1, 0xee},
	{2, 0xe8},
	{3, 0xe6},
	{4, 0xe5},
	{6, 0xe4},
	{10, 0xe0},
	{20, 0xd5},
	{30, 0xce},
	{45, 0xc6},
	{70, 0xb7},
	{100, 0xad},
	{150, 0xa0},
	{227, 0x8a},
	{300, 0x80},
	{400, 0x6e},
	{500, 0x5b},
	{600, 0x50},
	{800, 0x38},
	{1023, 0x18},
};

void oplus_set_aod_dim_alpha(int cust)
{
	oplus_aod_dim_alpha = cust;
	DSI_DEBUG("kOFP, set oplus_aod_dim_alpha = %d\n", oplus_aod_dim_alpha);
}

int oplus_get_panel_power_mode(void)
{
	struct dsi_display *display = get_main_display();

	if (!display)
		return 0;

	return display->panel->power_mode;
}

static int bl_to_alpha(int brightness)
{
	struct dsi_display *display = get_main_display();
	struct oplus_brightness_alpha *lut = NULL;
	int count = 0;
	int i = 0;
	int alpha;

	if (!display) {
		return 0;
	}

	if (display->panel->ba_seq && display->panel->ba_count) {
		count = display->panel->ba_count;
		lut = display->panel->ba_seq;

	} else {
		count = ARRAY_SIZE(brightness_alpha_lut);
		lut = brightness_alpha_lut;
	}

	for (i = 0; i < count; i++) {
		if (lut[i].brightness >= brightness) {
			break;
		}
	}

	if (i == 0) {
		alpha = lut[0].alpha;
	} else if (i == count) {
		alpha = lut[count - 1].alpha;
	} else
		alpha = interpolate(brightness, lut[i - 1].brightness,
				lut[i].brightness, lut[i - 1].alpha,
				lut[i].alpha);

	return alpha;
}

static int brightness_to_alpha(int brightness)
{
	int alpha;
	struct dsi_display *display = get_main_display();

	if ((strcmp(display->panel->oplus_priv.vendor_name, "AMB655X") != 0)
		&& (strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01") != 0)) {
		if (brightness == 0 || brightness == 1) {
			brightness = oplus_last_backlight;
		}
	}

	if (oplus_dimlayer_hbm) {
		alpha = bl_to_alpha(brightness);

	} else {
		alpha = bl_to_alpha_dc(brightness);
	}

	return alpha;
}

static int oplus_get_panel_brightness_to_alpha(void)
{
	struct dsi_display *display = get_main_display();
	int index = 0;
	uint32_t brightness_panel = 0;

	if (!display || !display->panel) {
		return 0;
	}

	if (oplus_panel_alpha) {
		return oplus_panel_alpha;
	}

	/* force dim layer alpha in AOD scene */
	if (oplus_aod_dim_alpha != CUST_A_NO) {
		if (oplus_aod_dim_alpha == CUST_A_TRANS)
			return 0;
		else if (oplus_aod_dim_alpha == CUST_A_OPAQUE)
			return 255;
	}

	if (hbm_mode) {
		return 0;
	}

	if (!oplus_ffl_trigger_finish) {
		return brightness_to_alpha(FFL_FP_LEVEL);
	}

	if (apollo_backlight_enable) {
		if (p_apollo_backlight) {
			index = oplus_find_index_invmaplist(display->panel->bl_config.bl_level);
			if (index >= 0) {
				DSI_DEBUG("[%s] index = %d, panel_level = %d, apollo_level = %d",
						__func__,
						index,
						p_apollo_backlight->panel_bl_list[index],
						p_apollo_backlight->apollo_bl_list[index]);
				brightness_panel = p_apollo_backlight->panel_bl_list[index];
				return brightness_to_alpha(brightness_panel);
			}
		} else {
			DSI_ERR("invalid p_apollo_backlight\n");
		}
	}

	return brightness_to_alpha(display->panel->bl_config.bl_level);
}

/* Add for ofp parse config */
int dsi_panel_parse_ofp_config(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct oplus_brightness_alpha *seq;

	if (panel->host_config.ext_bridge_mode) {
		return 0;
	}

	arr = utils->get_property(utils->data, "oplus,dsi-ofp-brightness", &length);

	if (!arr) {
		DSI_ERR("[%s] oplus,dsi-ofp-brightness  not found\n", panel->name);
		return -EINVAL;
	}

	if (length & 0x1) {
		DSI_ERR("[%s] oplus,dsi-ofp-brightness length error\n", panel->name);
		return -EINVAL;
	}

	DSI_DEBUG("RESET SEQ LENGTH = %d\n", length);
	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);

	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "oplus,dsi-ofp-brightness",
				   arr_32, length);

	if (rc) {
		DSI_ERR("[%s] cannot read dsi-ofp-brightness\n", panel->name);
		goto error_free_arr_32;
	}

	count = length / 2;
	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);

	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->ba_seq = seq;
	panel->ba_count = count;

	for (i = 0; i < length; i += 2) {
		seq->brightness = arr_32[i];
		seq->alpha = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}

int dsi_panel_parse_ofp_vblank_config(struct dsi_display_mode *mode,
		struct dsi_parser_utils *utils)
{
	int rc;
	struct ofp_vblank_sync_time vblank_cfg;
	int val = 0;

	rc = utils->read_u32(utils->data, "oplus,ofp-on-vblank", &val);

	if (rc) {
		DSI_DEBUG("oplus,ofp-on-vblank is not defined, rc=%d\n", rc);
		vblank_cfg.ofp_on_vblank = 0;

	} else {
		vblank_cfg.ofp_on_vblank = val;
		DSI_INFO("oplus,ofp-on-vblank is %d", val);
	}

	rc = utils->read_u32(utils->data, "oplus,ofp-off-vblank", &val);

	if (rc) {
		DSI_DEBUG("oplus,ofp-on-vblank is not defined, rc=%d\n", rc);
		vblank_cfg.ofp_off_vblank = 0;

	} else {
		vblank_cfg.ofp_off_vblank = val;
		DSI_INFO("oplus,ofp-off-vblank is %d", val);
	}

	return 0;
}
/* End of add for ofp parse config */
EXPORT_SYMBOL(dsi_panel_parse_ofp_vblank_config);

bool sde_crtc_get_dimlayer_mode(struct drm_crtc_state *crtc_state)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return false;
	}

	cstate = to_sde_crtc_state(crtc_state);
	return !!cstate->fingerprint_dim_layer;
}

bool sde_crtc_get_fingerprint_mode(struct drm_crtc_state *crtc_state)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return false;
	}

	cstate = to_sde_crtc_state(crtc_state);
	return !!cstate->fingerprint_mode;
}

bool sde_crtc_get_fingerprint_pressed(struct drm_crtc_state *crtc_state)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return false;
	}

	cstate = to_sde_crtc_state(crtc_state);
	return cstate->fingerprint_pressed;
}

int sde_crtc_set_onscreenfinger_defer_sync(struct drm_crtc_state *crtc_state,
		bool defer_sync)
{
	struct sde_crtc_state *cstate;

	if (!crtc_state) {
		return -EINVAL;
	}

	cstate = to_sde_crtc_state(crtc_state);
	cstate->fingerprint_defer_sync = defer_sync;
	return 0;
}

int sde_crtc_config_fingerprint_dim_layer(struct drm_crtc_state *crtc_state,
		int stage)
{
	struct sde_crtc_state *cstate;
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	struct sde_hw_dim_layer *fingerprint_dim_layer;
	int alpha = oplus_get_panel_brightness_to_alpha();
	struct sde_kms *kms;

	kms = _sde_crtc_get_kms_(crtc_state->crtc);

	if (!kms || !kms->catalog) {
		SDE_ERROR("kOFP, invalid kms\n");
		return -EINVAL;
	}

	cstate = to_sde_crtc_state(crtc_state);

	if (cstate->num_dim_layers == SDE_MAX_DIM_LAYERS - 1) {
		pr_err("kOFP, failed to get available dim layer for custom\n");
		return -EINVAL;
	}

	if ((stage + SDE_STAGE_0) >= kms->catalog->mixer[0].sblk->maxblendstages) {
		return -EINVAL;
	}

	fingerprint_dim_layer = &cstate->dim_layer[cstate->num_dim_layers];
	fingerprint_dim_layer->flags = SDE_DRM_DIM_LAYER_INCLUSIVE;
	fingerprint_dim_layer->stage = stage + SDE_STAGE_0;

	DSI_DEBUG("kOFP, fingerprint_dim_layer: stage = %d, alpha = %d\n", stage, alpha);
	fingerprint_dim_layer->rect.x = 0;
	fingerprint_dim_layer->rect.y = 0;
	fingerprint_dim_layer->rect.w = mode->hdisplay;
	fingerprint_dim_layer->rect.h = mode->vdisplay;
	fingerprint_dim_layer->color_fill = (struct sde_mdss_color) {0, 0, 0, alpha};

	cstate->fingerprint_dim_layer = fingerprint_dim_layer;
	oplus_underbrightness_alpha = alpha;

	return 0;
}
EXPORT_SYMBOL(sde_crtc_config_fingerprint_dim_layer);

bool _sde_encoder_setup_dither_for_onscreenfingerprint(struct sde_encoder_phys *phys,
		void *dither_cfg, int len, struct sde_hw_pingpong *hw_pp)
{
	struct drm_encoder *drm_enc = phys->parent;
	struct drm_msm_dither dither;

	if (!drm_enc || !drm_enc->crtc) {
		return -EFAULT;
	}
	if (!sde_crtc_get_dimlayer_mode(drm_enc->crtc->state)) {
		return -EINVAL;
	}
	if (len != sizeof(dither)) {
		return -EINVAL;
	}
	if (oplus_get_panel_brightness_to_alpha() < oplus_dimlayer_dither_threshold) {
		return -EINVAL;
	}
	if (hw_pp == 0) {
		return 0;
	}
	memcpy(&dither, dither_cfg, len);
	dither.c0_bitdepth = 6;
	dither.c1_bitdepth = 6;
	dither.c2_bitdepth = 6;
	dither.c3_bitdepth = 6;
	dither.temporal_en = 1;
	phys->hw_pp->ops.setup_dither(hw_pp, &dither, len);
	return 0;
}

int sde_plane_check_fingerprint_layer(const struct drm_plane_state *drm_state)
{
	struct sde_plane_state *pstate;

	if (!drm_state) {
		return 0;
	}

	pstate = to_sde_plane_state(drm_state);

	return sde_plane_get_property(pstate, PLANE_PROP_CUSTOM);
}
EXPORT_SYMBOL(sde_plane_check_fingerprint_layer);

int oplus_display_panel_get_dimlayer_hbm(void *data)
{
	uint32_t *dimlayer_hbm = data;

	(*dimlayer_hbm) = oplus_dimlayer_hbm;

	return 0;
}

int oplus_display_panel_set_dimlayer_hbm(void *data)
{
	struct dsi_display *display = get_main_display();
	struct drm_connector *dsi_connector = NULL;
	uint32_t *dimlayer_hbm = data;
	int err = 0;
	int value = (*dimlayer_hbm);

	if (!display) {
		pr_err("kOFP, failed to find display\n");
		return -EINVAL;
	}

	dsi_connector = display->drm_conn;

	value = !!value;
	if (oplus_dimlayer_hbm == value)
		return 0;
	if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
		pr_err("kOFP, [%s]: dsi_connector not ready\n", __func__);
		return -EINVAL;
	} else {
		err = drm_crtc_vblank_get(dsi_connector->state->crtc);
		if (err) {
			pr_err("kOFP, failed to get crtc vblank, error=%d\n", err);
		} else {
			/* do vblank put after 5 frames */
			oplus_dimlayer_hbm_vblank_count = 5;
			atomic_inc(&oplus_dimlayer_hbm_vblank_ref);
		}
	}
	oplus_dimlayer_hbm = value;

	pr_err("kOFP, debug for oplus_display_set_dimlayer_hbm set oplus_dimlayer_hbm = %d\n", oplus_dimlayer_hbm);

	return 0;
}

int oplus_display_panel_notify_fp_press(void *data)
{
	struct dsi_display *display = get_main_display();
	struct drm_device *drm_dev = NULL;
	struct drm_connector *dsi_connector = NULL;
	struct drm_mode_config *mode_config = NULL;
	struct msm_drm_private *priv = NULL;
	struct drm_atomic_state *state;
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int onscreenfp_status = 0;
	int vblank_get = -EINVAL;
	int err = 0;
	int i;
	bool if_con = false;
	uint32_t *p_onscreenfp_status = data;

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	struct drm_display_mode *cmd_mode = NULL;
	struct drm_display_mode *vid_mode = NULL;
	struct drm_display_mode *mode = NULL;
	bool mode_changed = false;
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

	if (!display) {
		pr_err("kOFP, failed to find display\n");
		return -EINVAL;
	}

	drm_dev = display->drm_dev;

	if (!drm_dev) {
		pr_err("kOFP, failed to find drm_dev\n");
		return -EINVAL;
	}
	mode_config = &drm_dev->mode_config;
	priv = drm_dev->dev_private;

	if (!mode_config || !priv) {
		pr_err("kOFP, failed to find mode_config and priv\n");
		return -EINVAL;
	}

	dsi_connector = display->drm_conn;

	if (!dsi_connector || !dsi_connector->state || !dsi_connector->state->crtc) {
		pr_err("kOFP, [%s]: dsi_connector not ready\n", __func__);
		return -EINVAL;
	}

	onscreenfp_status = (*p_onscreenfp_status);

	onscreenfp_status = !!onscreenfp_status;
	if (onscreenfp_status == oplus_onscreenfp_status)
		return 0;

	pr_err("kOFP, notify fingerpress %s\n", onscreenfp_status ? "on" : "off");

	vblank_get = drm_crtc_vblank_get(dsi_connector->state->crtc);
	if (vblank_get) {
		pr_err("kOFP, failed to get crtc vblank\n", vblank_get);
	}
	oplus_onscreenfp_status = onscreenfp_status;

	if_con = false;/*if_con = onscreenfp_status && (OPLUS_DISPLAY_AOD_SCENE == get_oplus_display_scene());*/
#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if_con = if_con && !display->panel->oplus_priv.is_aod_ramless;
#endif /* OPLUS_FEATURE_AOD_RAMLESS */
	if (if_con) {
		/* enable the clk vote for CMD mode panels */
		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					DSI_ALL_CLKS, DSI_CLK_ON);
		}

		mutex_lock(&display->panel->panel_lock);

		if (display->panel->panel_initialized)
			err = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_AOD_HBM_ON);

		mutex_unlock(&display->panel->panel_lock);
		if (err)
			pr_err("failed to setting aod hbm on mode %d\n", err);

		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			dsi_display_clk_ctrl(display->dsi_clk_handle,
					DSI_ALL_CLKS, DSI_CLK_OFF);
		}
	}

	drm_modeset_lock_all(drm_dev);

	state = drm_atomic_state_alloc(drm_dev);
	if (!state)
		goto error;

	state->acquire_ctx = mode_config->acquire_ctx;
	crtc = dsi_connector->state->crtc;
	crtc_state = drm_atomic_get_crtc_state(state, crtc);
	for (i = 0; i < priv->num_crtcs; i++) {
		if (priv->disp_thread[i].crtc_id == crtc->base.id) {
			if (priv->disp_thread[i].thread)
				kthread_flush_worker(&priv->disp_thread[i].worker);
		}
	}

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (display->panel->oplus_priv.is_aod_ramless) {
		struct drm_display_mode *set_mode = NULL;

		if (oplus_display_mode == 2)
			goto error;

		list_for_each_entry(mode, &dsi_connector->modes, head) {
			if (drm_mode_vrefresh(mode) == 0)
				continue;
			if (mode->flags & DRM_MODE_FLAG_VID_MODE_PANEL)
				vid_mode = mode;
			if (mode->flags & DRM_MODE_FLAG_CMD_MODE_PANEL)
				cmd_mode = mode;
		}

		set_mode = oplus_display_mode ? vid_mode : cmd_mode;
		set_mode = onscreenfp_status ? vid_mode : set_mode;
		if (!crtc_state->active || !crtc_state->enable)
			goto error;

		if (set_mode && drm_mode_vrefresh(set_mode) != drm_mode_vrefresh(&crtc_state->mode)) {
			mode_changed = true;
		} else {
			mode_changed = false;
		}

		if (mode_changed) {
			display->panel->dyn_clk_caps.dyn_clk_support = false;
			drm_atomic_set_mode_for_crtc(crtc_state, set_mode);
		}

		wake_up(&oplus_aod_wait);
	}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

	err = drm_atomic_commit(state);
	drm_atomic_state_put(state);

#ifdef OPLUS_FEATURE_AOD_RAMLESS
	if (display->panel->oplus_priv.is_aod_ramless && mode_changed) {
		for (i = 0; i < priv->num_crtcs; i++) {
			if (priv->disp_thread[i].crtc_id == crtc->base.id) {
				if (priv->disp_thread[i].thread) {
					kthread_flush_worker(&priv->disp_thread[i].worker);
				}
			}
		}
		if (oplus_display_mode == 1)
			display->panel->dyn_clk_caps.dyn_clk_support = true;
	}
#endif /* OPLUS_FEATURE_AOD_RAMLESS */

error:
	drm_modeset_unlock_all(drm_dev);
	if (!vblank_get)
		drm_crtc_vblank_put(dsi_connector->state->crtc);

	return 0;
}

