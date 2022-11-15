/***************************************************************
** Copyright (C),  2022,  OPLUS Mobile Comm Corp.,  Ltd
** File : oplus_onscreenfingerprint.c
** Description : oplus_onscreenfingerprint implement
** Version : 2.0
** Date : 2022/05/16
***************************************************************/

#include "oplus_onscreenfingerprint.h"
#include "oplus_adfr.h"
#include "oplus_display_panel_seed.h"
#include "oplus_bl.h"
#include "sde_encoder_phys.h"
#include "sde_trace.h"
#include <linux/msm_drm_notify.h>
#include "../../../oplus/kernel/touchpanel/oplus_touchscreen_v2/touchpanel_notify/touchpanel_event_notify.h"

/* -------------------- macro -------------------- */
/* fp type bit setting */
#define OPLUS_OFP_FP_TYPE_LCD_CAPACITIVE					(BIT(0))
#define OPLUS_OFP_FP_TYPE_OLED_CAPACITIVE					(BIT(1))
#define OPLUS_OFP_FP_TYPE_OPTICAL_OLD_SOLUTION				(BIT(2))
#define OPLUS_OFP_FP_TYPE_OPTICAL_NEW_SOLUTION				(BIT(3))
#define OPLUS_OFP_FP_TYPE_LOCAL_HBM							(BIT(4))
#define OPLUS_OFP_FP_TYPE_BRIGHTNESS_ADAPTATION				(BIT(5))
#define OPLUS_OFP_FP_TYPE_ULTRASONIC						(BIT(6))
#define OPLUS_OFP_FP_TYPE_ULTRA_LOW_POWER_AOD				(BIT(7))
/* get fp type config */
#define OPLUS_OFP_GET_LCD_CAPACITIVE_CONFIG(fp_type)		((fp_type) & OPLUS_OFP_FP_TYPE_LCD_CAPACITIVE)
#define OPLUS_OFP_GET_OLED_CAPACITIVE_CONFIG(fp_type)		((fp_type) & OPLUS_OFP_FP_TYPE_OLED_CAPACITIVE)
#define OPLUS_OFP_GET_OPTICAL_OLD_SOLUTION_CONFIG(fp_type)	((fp_type) & OPLUS_OFP_FP_TYPE_OPTICAL_OLD_SOLUTION)
#define OPLUS_OFP_GET_OPTICAL_NEW_SOLUTION_CONFIG(fp_type)	((fp_type) & OPLUS_OFP_FP_TYPE_OPTICAL_NEW_SOLUTION)
#define OPLUS_OFP_GET_LOCAL_HBM_CONFIG(fp_type)				((fp_type) & OPLUS_OFP_FP_TYPE_LOCAL_HBM)
#define OPLUS_OFP_GET_BRIGHTNESS_ADAPTATION_CONFIG(fp_type)	((fp_type) & OPLUS_OFP_FP_TYPE_BRIGHTNESS_ADAPTATION)
#define OPLUS_OFP_GET_ULTRASONIC_CONFIG(fp_type)			((fp_type) & OPLUS_OFP_FP_TYPE_ULTRASONIC)
#define OPLUS_OFP_GET_ULTRA_LOW_POWER_AOD_CONFIG(fp_type)	((fp_type) & OPLUS_OFP_FP_TYPE_ULTRA_LOW_POWER_AOD)

/* -------------------- parameters -------------------- */
/* log level config */
unsigned int oplus_ofp_log_level = OPLUS_OFP_LOG_LEVEL_DEBUG;
EXPORT_SYMBOL(oplus_ofp_log_level);
/* dual display id */
unsigned int oplus_ofp_display_id = OPLUS_OFP_PRIMARY_DISPLAY;
EXPORT_SYMBOL(oplus_ofp_display_id);
/* ofp global structure */
static struct oplus_ofp_params g_oplus_ofp_params[2] = {0};

/* -------------------- oplus_ofp_params -------------------- */
static int oplus_ofp_set_display_id(unsigned int display_id)
{
	OFP_DEBUG("start\n");

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_display_id");

	oplus_ofp_display_id = display_id;
	OFP_INFO("oplus_ofp_display_id:%u\n", oplus_ofp_display_id);
	OPLUS_OFP_TRACE_INT("oplus_ofp_display_id", oplus_ofp_display_id);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_display_id");

	OFP_DEBUG("end\n");

	return 0;
}

/* update display id for dual panel */
int oplus_ofp_update_display_id(void)
{
	struct dsi_display *display = oplus_display_get_current_display();

	OFP_DEBUG("start\n");

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_update_display_id");

	if (!display) {
		OFP_ERR("failed to get current display, set default display id to 0\n");
		oplus_ofp_display_id = OPLUS_OFP_PRIMARY_DISPLAY;
	} else {
		if (!strcmp(display->display_type, "primary")) {
			oplus_ofp_display_id = OPLUS_OFP_PRIMARY_DISPLAY;
		} else if (!strcmp(display->display_type, "secondary")) {
			oplus_ofp_display_id = OPLUS_OFP_SECONDARY_DISPLAY;
		}
	}

	OFP_INFO("oplus_ofp_display_id:%u\n", oplus_ofp_display_id);
	OPLUS_OFP_TRACE_INT("oplus_ofp_display_id", oplus_ofp_display_id);

	OPLUS_OFP_TRACE_END("oplus_ofp_update_display_id");

	OFP_DEBUG("end\n");

	return 0;
}

static struct oplus_ofp_params *oplus_ofp_get_params(unsigned int display_id)
{
	return &g_oplus_ofp_params[display_id];
}

/* get fp_type value from panel dtsi */
int oplus_ofp_init(void *dsi_panel)
{
	int rc = 0;
	unsigned int fp_type = 0;
	struct dsi_panel *panel = dsi_panel;
	struct dsi_parser_utils *utils = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!panel || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	utils = &panel->utils;
	if (!utils) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (!strcmp(panel->type, "secondary")) {
		p_oplus_ofp_params = oplus_ofp_get_params(OPLUS_OFP_SECONDARY_DISPLAY);
		if (!p_oplus_ofp_params) {
			OFP_ERR("Invalid params\n");
			return -EINVAL;
		}
		OFP_INFO("init secondary display ofp params\n");
		oplus_ofp_set_display_id(OPLUS_OFP_SECONDARY_DISPLAY);
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_init");

	rc = utils->read_u32(utils->data, "oplus,ofp-fp-type", &fp_type);
	if (rc) {
		OFP_INFO("failed to read oplus,ofp-fp-type, rc=%d\n", rc);
		/* set default value to BIT(0)  */
		p_oplus_ofp_params->fp_type = BIT(0);
	} else {
		p_oplus_ofp_params->fp_type = fp_type;
	}

	OFP_INFO("fp_type:0x%x\n", p_oplus_ofp_params->fp_type);

	if (oplus_ofp_is_supported()) {
		/* indicates whether gamut needs to be bypassed in aod/fod scenarios or not */
		p_oplus_ofp_params->need_to_bypass_gamut = utils->read_bool(utils->data, "oplus,ofp-need-to-bypass-gamut");
		OFP_INFO("need_to_bypass_gamut:%d\n", p_oplus_ofp_params->need_to_bypass_gamut);

		if (!oplus_ofp_oled_capacitive_is_enabled()) {
			/* add for touchpanel event notifier */
			p_oplus_ofp_params->touchpanel_event_notifier.notifier_call = oplus_ofp_touchpanel_event_notifier_call;
			rc = touchpanel_event_register_notifier(&p_oplus_ofp_params->touchpanel_event_notifier);
			if (rc) {
				OFP_ERR("failed to register touchpanel event notifier, rc=%d\n", rc);
			}

			/* add workqueue to send aod off cmd */
			if (!strcmp(panel->type, "primary")) {
				p_oplus_ofp_params->aod_off_set_wq = create_singlethread_workqueue("aod_off_set_0");
			} else {
				p_oplus_ofp_params->aod_off_set_wq = create_singlethread_workqueue("aod_off_set_1");
			}
			INIT_WORK(&p_oplus_ofp_params->aod_off_set_work, oplus_ofp_aod_off_set_work_handler);
		}
	}

	if (!strcmp(panel->type, "secondary")) {
		/* set default display id to primary display */
		oplus_ofp_set_display_id(OPLUS_OFP_PRIMARY_DISPLAY);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_init");

	OFP_DEBUG("end\n");

	return rc;
}

bool oplus_ofp_is_supported(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	/* global config, support oled panel */
	return (bool)(!OPLUS_OFP_GET_LCD_CAPACITIVE_CONFIG(p_oplus_ofp_params->fp_type));
}

bool oplus_ofp_oled_capacitive_is_enabled(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("ofp is not support, oled capacitive is also not supported\n");
		return false;
	}

	return (bool)(OPLUS_OFP_GET_OLED_CAPACITIVE_CONFIG(p_oplus_ofp_params->fp_type));
}

bool oplus_ofp_optical_new_solution_is_enabled(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("ofp is not support, optical new solution is also not supported\n");
		return false;
	}

	return (bool)(OPLUS_OFP_GET_OPTICAL_NEW_SOLUTION_CONFIG(p_oplus_ofp_params->fp_type));
}

bool oplus_ofp_local_hbm_is_enabled(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("ofp is not support, local hbm is also not supported\n");
		return false;
	}

	return (bool)(OPLUS_OFP_GET_LOCAL_HBM_CONFIG(p_oplus_ofp_params->fp_type));
}
/*
static bool oplus_ofp_brightness_adaptation_is_enabled(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("ofp is not support, brightness adaptation is also not supported\n");
		return false;
	}

	return (bool)(OPLUS_OFP_GET_BRIGHTNESS_ADAPTATION_CONFIG(p_oplus_ofp_params->fp_type));
}
*/
bool oplus_ofp_ultrasonic_is_enabled(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("ofp is not support, ultrasonic is also not supported\n");
		return false;
	}

	return (bool)(OPLUS_OFP_GET_ULTRASONIC_CONFIG(p_oplus_ofp_params->fp_type));
}

static bool oplus_ofp_ultra_low_power_aod_is_enabled(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("ofp is not support, ultra low power aod is also not supported\n");
		return false;
	}

	return (bool)(OPLUS_OFP_GET_ULTRA_LOW_POWER_AOD_CONFIG(p_oplus_ofp_params->fp_type));
}

bool oplus_ofp_get_hbm_state(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_hbm_state");
	OFP_DEBUG("oplus_ofp_get_hbm_state:%d\n", p_oplus_ofp_params->hbm_state);
	OPLUS_OFP_TRACE_END("oplus_ofp_get_hbm_state");

	OFP_DEBUG("end\n");

	return p_oplus_ofp_params->hbm_state;
}

static int oplus_ofp_set_hbm_state(bool hbm_state)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_hbm_state");

	p_oplus_ofp_params->hbm_state = hbm_state;
	OFP_INFO("oplus_ofp_hbm_state:%d\n", hbm_state);
	OPLUS_OFP_TRACE_INT("oplus_ofp_hbm_state", p_oplus_ofp_params->hbm_state);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_hbm_state");

	OFP_DEBUG("end\n");

	return 0;
}

static bool oplus_ofp_get_aod_state(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_aod_state");
	OFP_DEBUG("oplus_ofp_aod_state:%d\n", p_oplus_ofp_params->aod_state);
	OPLUS_OFP_TRACE_END("oplus_ofp_get_aod_state");

	OFP_DEBUG("end\n");

	return p_oplus_ofp_params->aod_state;
}

static int oplus_ofp_set_aod_state(bool aod_state)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_aod_state");

	p_oplus_ofp_params->aod_state = aod_state;
	OFP_INFO("oplus_ofp_aod_state:%d\n", p_oplus_ofp_params->aod_state);
	OPLUS_OFP_TRACE_INT("oplus_ofp_aod_state", p_oplus_ofp_params->aod_state);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_aod_state");

	OFP_DEBUG("end\n");

	return 0;
}

/* aod unlocking value update */
static int oplus_ofp_aod_unlocking_update(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_aod_unlocking_update");

	if (p_oplus_ofp_params->fp_press || p_oplus_ofp_params->doze_active) {
		/* press icon layer is ready in aod mode */
		p_oplus_ofp_params->aod_unlocking = true;
		OFP_INFO("oplus_ofp_aod_unlocking:%d\n", p_oplus_ofp_params->aod_unlocking);
		OPLUS_OFP_TRACE_INT("oplus_ofp_aod_unlocking", p_oplus_ofp_params->aod_unlocking);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_aod_unlocking_update");

	OFP_DEBUG("end\n");

	return 0;
}

/* update hbm_enable property value */
int oplus_ofp_property_update(void *sde_connector, void *sde_connector_state, int prop_id, uint64_t prop_val)
{
	struct sde_connector *c_conn = sde_connector;
	struct sde_connector_state *c_state = sde_connector_state;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!c_conn || !c_state || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		OFP_DEBUG("not in dsi mode, should not update ofp properties\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_property_update");

	switch (prop_id) {
	case CONNECTOR_PROP_HBM_ENABLE:
		if (prop_val != p_oplus_ofp_params->hbm_enable) {
			OFP_INFO("HBM_ENABLE:%lu,dim:%lu,fingerpress:%lu,icon:%lu,aod:%lu\n", prop_val, (prop_val & OPLUS_OFP_PROPERTY_DIM_LAYER),
				(prop_val & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER), (prop_val & OPLUS_OFP_PROPERTY_ICON_LAYER),
					(prop_val & OPLUS_OFP_PROPERTY_AOD_LAYER));
		}
		p_oplus_ofp_params->hbm_enable = prop_val;
		OPLUS_OFP_TRACE_INT("oplus_ofp_hbm_enable", p_oplus_ofp_params->hbm_enable);

		msm_property_set_dirty(&c_conn->property_info, &c_state->property_state, CONNECTOR_PROP_HBM_ENABLE);
		break;

	default:
		break;
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_property_update");

	OFP_DEBUG("end\n");

	return 0;
}

/* -------------------- fod -------------------- */
int oplus_ofp_parse_dtsi_config(void *dsi_display_mode, void *dsi_parser_utils)
{
	unsigned int data = 0;
	int rc = 0;
	struct dsi_display_mode *mode = dsi_display_mode;
	struct dsi_parser_utils *utils = dsi_parser_utils;
	struct dsi_display_mode_priv_info *priv_info;

	OFP_DEBUG("start\n");

	if (oplus_ofp_oled_capacitive_is_enabled() || oplus_ofp_ultrasonic_is_enabled()) {
		OFP_DEBUG("no need to parse ofp dtsi config\n");
		return 0;
	}

	if (!mode || !mode->priv_info)
		return -EINVAL;

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_parse_dtsi_config");

	priv_info = mode->priv_info;

	/* indicates whether need to separate backlight before hbm on in power on mode or not */
	priv_info->oplus_ofp_need_to_separate_backlight = utils->read_bool(utils->data, "oplus,ofp-need-to-separate-backlight");
	OFP_DEBUG("oplus_ofp_need_to_separate_backlight:%d\n", priv_info->oplus_ofp_need_to_separate_backlight);

	/* indicates how many frames does hbm on cmds take effect */
	rc = utils->read_u32(utils->data, "oplus,ofp-hbm-on-period", &data);
	if (rc) {
		OFP_DEBUG("failed to parse oplus,ofp-hbm-on-period\n");
		priv_info->oplus_ofp_hbm_on_period = 1;
	} else {
		priv_info->oplus_ofp_hbm_on_period = data;
	}
	OFP_DEBUG("oplus_ofp_hbm_on_period:%u\n", priv_info->oplus_ofp_hbm_on_period);

	OPLUS_OFP_TRACE_END("oplus_ofp_parse_dtsi_config");

	OFP_DEBUG("end\n");

	return 0;
}

/* cmd set */
static int oplus_ofp_panel_cmd_set_nolock(void *dsi_panel, enum dsi_cmd_set_type type)
{
	int rc = 0;
	int seed_mode = 0;
	struct dsi_panel *panel = dsi_panel;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!panel || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_panel_cmd_set_nolock");

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		OFP_DEBUG("should not send cmd sets if panel is not initialized\n");
		goto error;
	}

	OPLUS_OFP_TRACE_BEGIN("dsi_panel_tx_cmd_set");
	rc = dsi_panel_tx_cmd_set(panel, type);
	OPLUS_OFP_TRACE_END("dsi_panel_tx_cmd_set");
	if (rc) {
		OFP_ERR("[%s] failed to send %s, rc=%d\n",
			panel->name, cmd_set_prop_map[type], rc);
		goto error;
	}

	/* after tx cmd set */
	switch (type) {
	case DSI_CMD_HBM_ON:
		oplus_ofp_set_hbm_state(true);

		/* do not use loading effect compensation mode to FOD sensing,
			it causes the luminance drop in FOD pattern */
		OPLUS_OFP_TRACE_BEGIN("dsi_panel_seed_mode");
		rc = dsi_panel_seed_mode(panel, PANEL_LOADING_EFFECT_OFF);
		if (rc) {
			OFP_ERR("failed to set seed mode:PANEL_LOADING_EFFECT_OFF\n");
		}
		OPLUS_OFP_TRACE_END("dsi_panel_seed_mode");
		break;

	case DSI_CMD_HBM_OFF:
		oplus_ofp_set_hbm_state(false);

		/* if backlight level is in global hbm range before hbm on, reset the oplus_global_hbm_flags,
			so that it can reenter global hbm level after hbm off */
		if (oplus_display_panel_get_global_hbm_status()) {
			oplus_display_panel_set_global_hbm_status(GLOBAL_HBM_DISABLE);
		}

		/* recovery backlight level */
		OPLUS_OFP_TRACE_BEGIN("dsi_panel_set_backlight");
		rc = dsi_panel_set_backlight(panel, panel->bl_config.bl_level);
		OPLUS_OFP_TRACE_END("dsi_panel_set_backlight");
		if (rc) {
			OFP_ERR("unable to set backlight\n");
			goto error;
		}

		/* recovery loading effect mode */
		seed_mode = oplus_display_get_seed_mode();
		OPLUS_OFP_TRACE_BEGIN("dsi_panel_seed_mode");
		rc = dsi_panel_seed_mode(panel, seed_mode);
		if (rc) {
			OFP_ERR("failed to set seed mode:%d\n", seed_mode);
		}
		OPLUS_OFP_TRACE_END("dsi_panel_seed_mode");
		break;

	case DSI_CMD_ULTRA_LOW_POWER_AOD_ON:
		p_oplus_ofp_params->ultra_low_power_aod_state = true;
		OFP_INFO("ultra_low_power_aod_state:%d\n", p_oplus_ofp_params->ultra_low_power_aod_state);
		OPLUS_OFP_TRACE_INT("oplus_ofp_ultra_low_power_aod_state", p_oplus_ofp_params->ultra_low_power_aod_state);
		break;

	case DSI_CMD_ULTRA_LOW_POWER_AOD_OFF:
		p_oplus_ofp_params->ultra_low_power_aod_state = false;
		OFP_INFO("ultra_low_power_aod_state:%d\n", p_oplus_ofp_params->ultra_low_power_aod_state);
		OPLUS_OFP_TRACE_INT("oplus_ofp_ultra_low_power_aod_state", p_oplus_ofp_params->ultra_low_power_aod_state);
		break;

	default:
		break;
	}

error:
	OPLUS_OFP_TRACE_END("oplus_ofp_panel_cmd_set_nolock");
	OFP_DEBUG("end\n");

	return rc;
}

static int oplus_ofp_panel_cmd_set(void *dsi_panel, enum dsi_cmd_set_type type)
{
	int rc = 0;
	struct dsi_panel *panel = dsi_panel;

	OFP_DEBUG("start\n");

	if (!panel) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_panel_cmd_set");

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		rc = -EINVAL;
		OFP_DEBUG("should not send cmd sets if panel is not initialized\n");
		goto error;
	}

	rc = oplus_ofp_panel_cmd_set_nolock(panel, type);
	if (rc) {
		OFP_ERR("[%s] failed to send %s, rc=%d\n",
			panel->name, cmd_set_prop_map[type], rc);
	}

error:
	mutex_unlock(&panel->panel_lock);

	OPLUS_OFP_TRACE_END("oplus_ofp_panel_cmd_set");
	OFP_DEBUG("end\n");

	return rc;
}

/* uniform interface for cmd set */
static int oplus_ofp_display_cmd_set(void *dsi_display, enum dsi_cmd_set_type type)
{
	int rc = 0;
	struct dsi_display *display = dsi_display;

	OFP_DEBUG("start\n");

	if (!display || !display->panel) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_display_cmd_set");

	mutex_lock(&display->display_lock);

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
		if (rc) {
			OFP_ERR("[%s] failed to enable DSI clocks, rc=%d\n", display->name, rc);
			goto error;
		}
	}

	rc = oplus_ofp_panel_cmd_set(display->panel, type);
	if (rc) {
		OFP_ERR("[%s] failed to send %s, rc=%d\n",
			display->name, cmd_set_prop_map[type], rc);
	}

	/* disable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);
		if (rc) {
			OFP_ERR("[%s] failed to disable DSI clocks, rc=%d\n", display->name, rc);
		}
	}

error:
	mutex_unlock(&display->display_lock);

	OPLUS_OFP_TRACE_END("oplus_ofp_display_cmd_set");
	OFP_DEBUG("end\n");

	return rc;
}

/* wait te and delay some us */
static int oplus_ofp_vblank_wait(void *sde_connector, bool need_wait_te, unsigned int delay_us)
{
	struct sde_connector *c_conn = sde_connector;
	struct drm_encoder *drm_enc = NULL;

	OFP_DEBUG("start\n");

	if (!need_wait_te && !delay_us) {
		return 0;
	}

	if (!c_conn) {
		OFP_ERR("Invalid c_conn params\n");
		return -EINVAL;
	}

	drm_enc = c_conn->encoder;
	if (!drm_enc) {
		OFP_ERR("Invalid drm_enc params\n");
		return -EINVAL;
	}

	if (sde_encoder_is_disabled(drm_enc)) {
		OFP_ERR("sde encoder is disabled\n");
		return -EFAULT;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_vblank_wait");

	if (need_wait_te) {
		OPLUS_OFP_TRACE_BEGIN("sde_encoder_wait_for_event");
		sde_encoder_wait_for_event(drm_enc, MSM_ENC_VBLANK);
		OFP_INFO("wait for vblank event done\n");
		OPLUS_OFP_TRACE_END("sde_encoder_wait_for_event");
	}

	if (delay_us) {
		OPLUS_OFP_TRACE_BEGIN("usleep_range");
		usleep_range(delay_us, (delay_us + 10));
		OFP_INFO("usleep_range %u done\n", delay_us);
		OPLUS_OFP_TRACE_END("usleep_range");
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_vblank_wait");

	OFP_DEBUG("end\n");

	return 0;
}

static int oplus_ofp_hbm_wait_handle(void *sde_connector, bool hbm_en)
{
	bool need_wait_te = false;
	int rc = 0;
	unsigned int refresh_rate = 0;
	unsigned int us_per_frame = 0;
	unsigned int delay_us = 0;
	struct sde_connector *c_conn = sde_connector;
	struct dsi_display *display = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!c_conn || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (p_oplus_ofp_params->aod_unlocking && hbm_en) {
		OFP_DEBUG("no need to delay in aod unlocking\n");
		return 0;
	}

	display = c_conn->display;

	if (!display || !display->panel) {
		OFP_ERR("Invalid display params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_hbm_wait_handle");

	refresh_rate = display->panel->cur_mode->timing.refresh_rate;
	us_per_frame = 1000000/refresh_rate;

	if (hbm_en) {
		/* backlight will affect hbm on time in some panel, need to separate the 51 cmd for stable hbm on time */
		if (display->panel->cur_mode->priv_info->oplus_ofp_need_to_separate_backlight) {
			/* wait 1 te and 1 frame, then send hbm on cmd in the second half of the frame */
			need_wait_te = true;
			delay_us = us_per_frame + (us_per_frame >> 1) + 700;
		} else {
			/* wait 1 te ,then send hbm on cmd in the second half of the frame */
			need_wait_te = true;
			delay_us = (us_per_frame >> 1) + 700;
		}
	} else {
		/* wait 1 te ,then send hbm off cmd in the second half of the frame */
		need_wait_te = true;
		delay_us = (us_per_frame >> 1) + 700;
	}

	OFP_INFO("need_wait_te=%d, delay_us=%u\n", need_wait_te, delay_us);
	rc = oplus_ofp_vblank_wait(c_conn, need_wait_te, delay_us);
	if (rc) {
		OFP_ERR("oplus_ofp_vblank_wait failed\n");
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_hbm_wait_handle");

	OFP_DEBUG("end\n");

	return rc;
}

static int oplus_ofp_set_panel_hbm(void *sde_connector, bool hbm_en)
{
	int rc = 0;
	struct sde_connector *c_conn = sde_connector;
	struct dsi_display *display = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (oplus_ofp_get_hbm_state() == hbm_en) {
		OFP_DEBUG("already in hbm state %d\n", hbm_en);
		return 0;
	}

	if (!c_conn || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	display = c_conn->display;

	if (!display || !display->panel) {
		OFP_ERR("Invalid display params\n");
		return -EINVAL;
	}

	if (!dsi_panel_initialized(display->panel)) {
		OFP_ERR("should not set panel hbm if panel is not initialized\n");
		return -EFAULT;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_panel_hbm");

	if (oplus_ofp_get_aod_state()) {
		OFP_INFO("send aod off cmd before hbm on because panel is still in aod mode\n");
		rc = oplus_ofp_aod_off_handle(display);
		if (rc) {
			OFP_ERR("failed to send aod off cmd\n");
		}
	}

	/* delay before hbm cmd */
	oplus_ofp_hbm_wait_handle(c_conn, hbm_en);

	/* send hbm cmd */
	if (hbm_en) {
		rc = oplus_ofp_display_cmd_set(display, DSI_CMD_HBM_ON);
		if (rc) {
			OFP_ERR("[%s] failed to send DSI_CMD_HBM_ON cmds, rc=%d\n", display->name, rc);
		}
	} else {
		rc = oplus_ofp_display_cmd_set(display, DSI_CMD_HBM_OFF);
		if (rc) {
			OFP_ERR("[%s] failed to send DSI_CMD_HBM_OFF cmds, rc=%d\n", display->name, rc);
		}
	}
	OFP_INFO("hbm cmd is flushed\n");

	if (!hbm_en && p_oplus_ofp_params->aod_unlocking) {
		p_oplus_ofp_params->aod_unlocking = false;
		OFP_INFO("oplus_ofp_aod_unlocking:%d\n", p_oplus_ofp_params->aod_unlocking);
		OPLUS_OFP_TRACE_INT("oplus_ofp_aod_unlocking", p_oplus_ofp_params->aod_unlocking);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_set_panel_hbm");

	OFP_DEBUG("end\n");

	return rc;
}

int oplus_ofp_hbm_handle(void *sde_encoder_virt)
{
	int rc = 0;
	unsigned int bl_level = 0;
	struct sde_encoder_virt *sde_enc = sde_encoder_virt;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (oplus_ofp_oled_capacitive_is_enabled() || oplus_ofp_ultrasonic_is_enabled() || oplus_ofp_local_hbm_is_enabled()) {
		OFP_DEBUG("no need to handle hbm\n");
		return 0;
	}

	if (!sde_enc || !sde_enc->cur_master || !sde_enc->cur_master->connector || !p_oplus_ofp_params) {
		OFP_ERR("Invalid sde_enc params\n");
		return -EINVAL;
	}

	if (p_oplus_ofp_params->hbm_mode) {
		OFP_DEBUG("already in hbm mode %u\n", p_oplus_ofp_params->hbm_mode);
		return 0;
	}

	c_conn = to_sde_connector(sde_enc->cur_master->connector);
	if (!c_conn) {
		OFP_ERR("Invalid c_conn params\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		OFP_DEBUG("not in dsi mode, should not handle hbm\n");
		return 0;
	}

	display = c_conn->display;

	if (!display || !display->panel) {
		OFP_ERR("Invalid display params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_hbm_handle");

	bl_level = display->panel->bl_config.bl_level;
	OFP_DEBUG("hbm_enable:%lu, bl_level=%u\n", p_oplus_ofp_params->hbm_enable, bl_level);

	if ((!p_oplus_ofp_params->doze_active && (p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_DIM_LAYER
			|| p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER) && bl_level)
				|| (p_oplus_ofp_params->doze_active && (p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER)
					&& bl_level)) {
		rc = oplus_ofp_set_panel_hbm(c_conn, true);
		if (rc) {
			OFP_ERR("failed to set panel hbm on\n");
		}
	} else if ((!(p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_DIM_LAYER)
					&& !(p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER))
						|| !p_oplus_ofp_params->fp_press || !bl_level) {
		rc = oplus_ofp_set_panel_hbm(c_conn, false);
		if (rc) {
			OFP_ERR("failed to set panel hbm off\n");
		}
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_hbm_handle");

	OFP_DEBUG("end\n");

	return rc;
}

int oplus_ofp_cmd_post_wait(void *dsi_cmd_desc, enum dsi_cmd_set_type type)
{
	struct dsi_cmd_desc *cmds = dsi_cmd_desc;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_cmd_post_wait");

	if (cmds->post_wait_ms) {
		/* hbm on cmds can be unstable and cause flicker, and wait 1 te solution will increase 1 frame delay.
			therefore, delete the delay of hbm on cmds, then using te counting to check whether hbm on is taking
				effect or not to shorten the ui ready time */
		if (p_oplus_ofp_params->aod_unlocking && (type == DSI_CMD_HBM_ON)) {
			OFP_DEBUG("no need to wait when cmd is DSI_CMD_HBM_ON in aod unlocking\n");
		} else {
				usleep_range(cmds->post_wait_ms*1000,
						((cmds->post_wait_ms*1000)+10));
		}
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_cmd_post_wait");

	OFP_DEBUG("end\n");

	return 0;
}

/* update panel hbm status */
int oplus_ofp_panel_hbm_status_update(void *sde_encoder_phys)
{
	static bool last_hbm_state = false;
	static unsigned int rd_ptr_count = 0;
	struct sde_encoder_phys *phys_enc = sde_encoder_phys;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!(oplus_ofp_optical_new_solution_is_enabled() || oplus_ofp_local_hbm_is_enabled())) {
		OFP_DEBUG("no need to update panel hbm status\n");
		return 0;
	}

	if (!phys_enc || !phys_enc->connector || !p_oplus_ofp_params) {
		OFP_ERR("Invalid phys_enc params\n");
		return -EINVAL;
	}

	c_conn = to_sde_connector(phys_enc->connector);
	if (!c_conn) {
		OFP_ERR("Invalid c_conn params\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		OFP_DEBUG("not in dsi mode, should not update panel hbm status\n");
		return 0;
	}

	display = c_conn->display;
	if (!display || !display->panel || !display->panel->cur_mode) {
		OFP_ERR("Invalid display params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_panel_hbm_status_update");

	if (p_oplus_ofp_params->aod_unlocking && oplus_ofp_get_hbm_state()) {
		/* since the delay of hbm on cmds is removed in aod unlocking, it is necessary to count te irq
			to confirm whether the hbm cmds are taking effect or not */
		if (!last_hbm_state && oplus_ofp_get_hbm_state()) {
			rd_ptr_count = 1;
		} else {
			rd_ptr_count++;
		}

		if (rd_ptr_count == display->panel->cur_mode->priv_info->oplus_ofp_hbm_on_period) {
			/*  hbm cmds are taking effect in panel module */
			p_oplus_ofp_params->panel_hbm_status = true;
			OFP_INFO("oplus_ofp_panel_hbm_status:%d\n", p_oplus_ofp_params->panel_hbm_status);
		}
	} else {
		if (!last_hbm_state && oplus_ofp_get_hbm_state()) {
			/*  hbm cmds are taking effect in panel module */
			p_oplus_ofp_params->panel_hbm_status = true;
			OFP_INFO("oplus_ofp_panel_hbm_status:%d\n", p_oplus_ofp_params->panel_hbm_status);
		} else if (last_hbm_state && !oplus_ofp_get_hbm_state()) {
			/*  hbm cmds are not taking effect in panel module */
			p_oplus_ofp_params->panel_hbm_status = false;
			OFP_INFO("oplus_ofp_panel_hbm_status:%d\n", p_oplus_ofp_params->panel_hbm_status);
			rd_ptr_count = 0;
		}
	}

	last_hbm_state = oplus_ofp_get_hbm_state();
	OPLUS_OFP_TRACE_INT("oplus_ofp_panel_hbm_status", p_oplus_ofp_params->panel_hbm_status);

	OPLUS_OFP_TRACE_END("oplus_ofp_panel_hbm_status_update");

	OFP_DEBUG("end\n");

	return 0;
}

/* update pressed icon status */
int oplus_ofp_pressed_icon_status_update(void *sde_encoder_phys, unsigned int irq_type)
{
	static uint64_t last_hbm_enable = 0;
	struct sde_encoder_phys *phys_enc = sde_encoder_phys;
	struct sde_connector *c_conn = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if ((!oplus_ofp_optical_new_solution_is_enabled() || oplus_ofp_ultrasonic_is_enabled())) {
		OFP_DEBUG("no need to update pressed icon status\n");
		return 0;
	}

	if (!phys_enc || !phys_enc->connector || !p_oplus_ofp_params) {
		OFP_ERR("Invalid phys_enc params\n");
		return -EINVAL;
	}

	c_conn = to_sde_connector(phys_enc->connector);
	if (!c_conn) {
		OFP_ERR("Invalid c_conn params\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		OFP_DEBUG("not in dsi mode, should not update pressed icon status\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_pressed_icon_status_update");

	if (irq_type == OPLUS_OFP_PP_DONE) {
		if ((!(last_hbm_enable & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER))
				&& (p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER)) {
			/* pressed icon has been flush to DDIC ram */
			p_oplus_ofp_params->pressed_icon_status = OPLUS_OFP_PRESSED_ICON_ON_PP_DONE;
			OFP_INFO("oplus_ofp_pressed_icon_status:OPLUS_OFP_PRESSED_ICON_ON_PP_DONE\n");
		} else if ((last_hbm_enable & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER)
						&& (!(p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_FINGERPRESS_LAYER))) {
			/* pressed icon has not been flush to DDIC ram */
			p_oplus_ofp_params->pressed_icon_status = OPLUS_OFP_PRESSED_ICON_OFF_PP_DONE;
			OFP_INFO("oplus_ofp_pressed_icon_status:OPLUS_OFP_PRESSED_ICON_OFF_PP_DONE\n");
		}
		last_hbm_enable = p_oplus_ofp_params->hbm_enable;
	} else if (irq_type == OPLUS_OFP_RD_PTR) {
		if (p_oplus_ofp_params->pressed_icon_status == OPLUS_OFP_PRESSED_ICON_ON_PP_DONE) {
			/* pressed icon has been displayed in panel */
			p_oplus_ofp_params->pressed_icon_status = OPLUS_OFP_PRESSED_ICON_ON;
			OFP_INFO("oplus_ofp_pressed_icon_status:OPLUS_OFP_PRESSED_ICON_ON\n");
		} else if (p_oplus_ofp_params->pressed_icon_status == OPLUS_OFP_PRESSED_ICON_OFF_PP_DONE) {
			/* pressed icon has not been displayed in panel */
			p_oplus_ofp_params->pressed_icon_status = OPLUS_OFP_PRESSED_ICON_OFF;
			OFP_INFO("oplus_ofp_pressed_icon_status:OPLUS_OFP_PRESSED_ICON_OFF\n");
		}
	}
	OPLUS_OFP_TRACE_INT("oplus_ofp_pressed_icon_status", p_oplus_ofp_params->pressed_icon_status);

	OPLUS_OFP_TRACE_END("oplus_ofp_pressed_icon_status_update");

	OFP_DEBUG("end\n");

	return 0;
}

static int oplus_ofp_send_uiready_event(unsigned int ui_status)
{
	struct msm_drm_notifier notifier_data;
	enum panel_event_notification_type notify_type = DRM_PANEL_EVENT_ONSCREENFINGERPRINT_UI_DISAPPEAR;
	/* warning: only support primary display currently, improve it if need */
	struct dsi_display *display = get_main_display();

	OFP_DEBUG("start\n");

	if (!display) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_send_uiready_event");

	/* msm_drm_notifier_call_chain */
	notifier_data.id = 0;
	notifier_data.data = &ui_status;
	msm_drm_notifier_call_chain(MSM_DRM_ONSCREENFINGERPRINT_EVENT, &notifier_data);
	OFP_DEBUG("msm_drm_notifier_call_chain:%u\n", ui_status);

	/* oplus_panel_event_notification_trigger */
	if (ui_status == OPLUS_OFP_UI_READY) {
		notify_type = DRM_PANEL_EVENT_ONSCREENFINGERPRINT_UI_READY;
	} else {
		notify_type = DRM_PANEL_EVENT_ONSCREENFINGERPRINT_UI_DISAPPEAR;
	}
	oplus_panel_event_notification_trigger(display, notify_type);
	OFP_DEBUG("oplus_panel_event_notification_trigger:%u\n", notify_type);

	OPLUS_OFP_TRACE_END("oplus_ofp_send_uiready_event");

	OFP_DEBUG("end\n");

	return 0;
}

/* notify uiready */
int oplus_ofp_notify_uiready(void *sde_encoder_phys)
{
	static unsigned int last_notifier_chain_value = OPLUS_OFP_UI_DISAPPEAR;
	struct sde_encoder_phys *phys_enc = sde_encoder_phys;
	struct sde_connector *c_conn = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (oplus_ofp_oled_capacitive_is_enabled()) {
		OFP_DEBUG("no need to notify uiready\n");
		return 0;
	}

	if (!phys_enc || !phys_enc->connector || !p_oplus_ofp_params) {
		OFP_ERR("Invalid phys_enc params\n");
		return -EINVAL;
	}

	c_conn = to_sde_connector(phys_enc->connector);
	if (!c_conn) {
		OFP_ERR("Invalid c_conn params\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		OFP_DEBUG("not in dsi mode, should not notify uiready\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_notify_uiready");

	if (p_oplus_ofp_params->aod_unlocking) {
		if ((p_oplus_ofp_params->pressed_icon_status == OPLUS_OFP_PRESSED_ICON_ON) && p_oplus_ofp_params->panel_hbm_status) {
			/* pressed icon has been displayed in panel and hbm cmds is also taking effect */
			p_oplus_ofp_params->notifier_chain_value = OPLUS_OFP_UI_READY;
		} else if ((p_oplus_ofp_params->pressed_icon_status == OPLUS_OFP_PRESSED_ICON_OFF) || (!(p_oplus_ofp_params->panel_hbm_status))) {
			/* finger is not pressed down */
			p_oplus_ofp_params->notifier_chain_value = OPLUS_OFP_UI_DISAPPEAR;
		}
	} else {
		if ((p_oplus_ofp_params->pressed_icon_status == OPLUS_OFP_PRESSED_ICON_ON) && p_oplus_ofp_params->panel_hbm_status) {
			/* pressed icon has been displayed in panel and hbm cmds is also taking effect */
			p_oplus_ofp_params->notifier_chain_value = OPLUS_OFP_UI_READY;
		} else if (p_oplus_ofp_params->pressed_icon_status == OPLUS_OFP_PRESSED_ICON_OFF) {
			/* hbm is on but pressed icon has not been displayed */
			p_oplus_ofp_params->notifier_chain_value = OPLUS_OFP_UI_DISAPPEAR;
		}
	}

	if (last_notifier_chain_value != p_oplus_ofp_params->notifier_chain_value) {
		/* send uiready immediately */
		OFP_INFO("send uiready:%u\n", p_oplus_ofp_params->notifier_chain_value);
		oplus_ofp_send_uiready_event(p_oplus_ofp_params->notifier_chain_value);
		OPLUS_OFP_TRACE_INT("oplus_ofp_notifier_chain_value", p_oplus_ofp_params->notifier_chain_value);
	}

	last_notifier_chain_value = p_oplus_ofp_params->notifier_chain_value;

	OPLUS_OFP_TRACE_END("oplus_ofp_notify_uiready");

	OFP_DEBUG("end\n");

	return 0;
}

/* need filter backlight in hbm mode, hbm state and aod unlocking process */
bool oplus_ofp_backlight_filter(void *dsi_panel, unsigned int bl_level)
{
	bool need_filter_backlight = false;
	struct dsi_panel *panel = dsi_panel;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!panel || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_backlight_filter");

	if (oplus_ofp_get_hbm_state()) {
		if (!bl_level) {
			p_oplus_ofp_params->hbm_mode = 0;
			OFP_INFO("oplus_ofp_hbm_mode:%u\n", p_oplus_ofp_params->hbm_mode);
			OPLUS_OFP_TRACE_INT("oplus_ofp_hbm_mode", p_oplus_ofp_params->hbm_mode);
			oplus_ofp_set_hbm_state(false);
			OFP_DEBUG("backlight is 0, set hbm mode and hbm state to false\n");

			if (p_oplus_ofp_params->aod_unlocking) {
				p_oplus_ofp_params->aod_unlocking = false;
				OFP_INFO("oplus_ofp_aod_unlocking:%d\n", p_oplus_ofp_params->aod_unlocking);
				OPLUS_OFP_TRACE_INT("oplus_ofp_aod_unlocking", p_oplus_ofp_params->aod_unlocking);
			}

			need_filter_backlight = false;
		} else {
			OFP_INFO("hbm state is true, filter backlight %u setting\n", bl_level);
			need_filter_backlight = true;
		}
	} else if (p_oplus_ofp_params->aod_unlocking && p_oplus_ofp_params->fp_press && bl_level) {
		OFP_INFO("aod unlocking is true, filter backlight %u setting\n", bl_level);
		need_filter_backlight = true;
	} else if (!p_oplus_ofp_params->aod_unlocking && !p_oplus_ofp_params->doze_active
				&& (p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_DIM_LAYER) && bl_level
					&& panel->cur_mode->priv_info->oplus_ofp_need_to_separate_backlight) {
		/* backlight will affect hbm on time in some panel, need to separate the 51 cmd for stable hbm on time */
		OFP_INFO("dim layer exist, filter backlight %u setting in advance\n", bl_level);
		need_filter_backlight = true;
	} else if (oplus_ofp_get_aod_state()) {
		OFP_INFO("aod state is true, filter backlight %u setting\n", bl_level);
		need_filter_backlight = true;
	} else if (!oplus_ofp_get_aod_state() && (p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_AOD_LAYER) && bl_level) {
		OFP_INFO("aod layer exist, filter backlight %u setting\n", bl_level);
		need_filter_backlight = true;
	} else if (p_oplus_ofp_params->dimlayer_hbm || p_oplus_ofp_params->hbm_enable) {
		OFP_INFO("backlight lvl:%u\n", bl_level);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_backlight_filter");

	OFP_DEBUG("end\n");

	return need_filter_backlight;
}

static bool oplus_ofp_need_to_bypass_pq(void)
{
	bool need_to_bypass_pq = false;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_need_to_bypass_pq");

	/* need to bypass pq when aod/dim/fingerpress/icon layer exit */
	if (p_oplus_ofp_params->hbm_enable) {
		need_to_bypass_pq = true;
	}

	OFP_DEBUG("need_to_bypass_pq:%d\n", need_to_bypass_pq);

	OPLUS_OFP_TRACE_END("oplus_ofp_need_to_bypass_pq");

	OFP_DEBUG("end\n");

	return need_to_bypass_pq;
}

bool oplus_ofp_need_pcc_change(void)
{
	bool need_pcc_change = false;
	bool new_bypass_pq_status = false;
	static bool last_bypass_pq_status = false;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return false;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_need_pcc_change");

	new_bypass_pq_status = oplus_ofp_need_to_bypass_pq();

	/* if aod/dim/fingerpress/icon layer exist, pcc config will be bypassed, and if aod/dim/fingerpress/icon layer all disappear,
		pcc config will be restored, so need to update pcc config */
	if (new_bypass_pq_status != last_bypass_pq_status) {
		need_pcc_change = true;
		OFP_INFO("need pcc change\n");
	}

	last_bypass_pq_status = new_bypass_pq_status;

	OPLUS_OFP_TRACE_END("oplus_ofp_need_pcc_change");

	OFP_DEBUG("end\n");

	return need_pcc_change;
}

int oplus_ofp_set_dspp_pcc_feature(void *sde_hw_cp_cfg, bool before_setup_pcc)
{
	struct sde_hw_cp_cfg *hw_cfg = sde_hw_cp_cfg;
	static struct drm_msm_pcc *saved_pcc = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!hw_cfg || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_dspp_pcc_feature");

	if (before_setup_pcc) {
		saved_pcc = hw_cfg->payload;

		if (oplus_ofp_need_to_bypass_pq()) {
			hw_cfg->payload = NULL;
			OFP_DEBUG("bypass dspp pcc feature\n");
		}
	} else {
		hw_cfg->payload = saved_pcc;
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_set_dspp_pcc_feature");

	OFP_DEBUG("end\n");

	return 0;
}

int oplus_ofp_bypass_dspp_gamut(void *sde_hw_cp_cfg)
{
	struct sde_hw_cp_cfg *hw_cfg = sde_hw_cp_cfg;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!hw_cfg || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_bypass_dspp_gamut");


	if (oplus_ofp_need_to_bypass_pq() && p_oplus_ofp_params->need_to_bypass_gamut) {
		hw_cfg->payload = NULL;
		OFP_DEBUG("bypass dspp gamut\n");
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_bypass_dspp_gamut");

	OFP_DEBUG("end\n");

	return 0;
}

/* -------------------- aod -------------------- */
/* aod off handle */
int oplus_ofp_aod_off_handle(void *dsi_display)
{
	int rc = 0;
	struct dsi_display *display = dsi_display;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_aod_off_handle");

	/* doze disable handle */
	OFP_INFO("aod off handle\n");

	/* make sure that ultra low power aod mode exit firstly */
	if (oplus_ofp_ultra_low_power_aod_is_enabled() && p_oplus_ofp_params->ultra_low_power_aod_state) {
		rc = oplus_ofp_display_cmd_set(display, DSI_CMD_ULTRA_LOW_POWER_AOD_OFF);
		if (rc) {
			OFP_ERR("[%s] failed to send DSI_CMD_ULTRA_LOW_POWER_AOD_OFF cmds, rc=%d\n", display->name, rc);
		}
	}

	rc = dsi_panel_set_nolp(display->panel);
	if (rc) {
		OFP_ERR("[%s] failed to send DSI_CMD_SET_NOLP cmds, rc=%d\n", display->name, rc);
	}
	oplus_ofp_set_aod_state(false);

	if (!oplus_ofp_oled_capacitive_is_enabled()) {
		/* update aod unlocking value */
		oplus_ofp_aod_unlocking_update();
	}

#ifdef OPLUS_BUG_STABILITY
	/* switch to tp vsync when exit aod */
	if (oplus_adfr_is_support()) {
		if (oplus_adfr_get_vsync_mode() == OPLUS_DOUBLE_TE_VSYNC) {
			sde_encoder_adfr_aod_fod_source_switch(display, OPLUS_TE_SOURCE_TP);
		} else if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
			oplus_adfr_aod_fod_vsync_switch(display->panel, false);
		}
	}
#endif /* OPLUS_BUG_STABILITY */

	OPLUS_OFP_TRACE_END("oplus_ofp_aod_off_handle");

	OFP_DEBUG("end\n");

	return rc;
}

int oplus_ofp_power_mode_handle(void *dsi_display, int power_mode)
{
	int rc = 0;
	struct dsi_display *display = dsi_display;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_power_mode_handle");

	switch (power_mode) {
	case SDE_MODE_DPMS_LP1:
	case SDE_MODE_DPMS_LP2:
		if (!p_oplus_ofp_params->doze_active) {
			p_oplus_ofp_params->doze_active = true;
			OFP_INFO("oplus_ofp_doze_active:%d\n", p_oplus_ofp_params->doze_active);
			OPLUS_OFP_TRACE_INT("oplus_ofp_doze_active", p_oplus_ofp_params->doze_active);
		}

		if (!oplus_ofp_get_aod_state()) {
			if (!oplus_ofp_oled_capacitive_is_enabled() && !oplus_ofp_ultrasonic_is_enabled()) {
				/* hbm mode -> normal mode -> aod mode */
				if (oplus_ofp_get_hbm_state()) {
					rc = oplus_ofp_display_cmd_set(display, DSI_CMD_HBM_OFF);
					if (rc) {
						OFP_ERR("[%s] failed to send DSI_CMD_HBM_OFF cmds, rc=%d\n", display->name, rc);
					}
				}
			}

			/* reset aod unlocking flag when fingerprint unlocking failed */
			if (p_oplus_ofp_params->aod_unlocking) {
				p_oplus_ofp_params->aod_unlocking = false;
				OFP_INFO("oplus_ofp_aod_unlocking:%d\n", p_oplus_ofp_params->aod_unlocking);
				OPLUS_OFP_TRACE_INT("oplus_ofp_aod_unlocking", p_oplus_ofp_params->aod_unlocking);
			}

#ifdef OPLUS_BUG_STABILITY
			/* switch to te vsync because tp vsync is 15hz on AOD mode */
			if (oplus_adfr_is_support()) {
				if (oplus_adfr_get_vsync_mode() == OPLUS_DOUBLE_TE_VSYNC) {
					sde_encoder_adfr_aod_fod_source_switch(display, OPLUS_TE_SOURCE_TE);
				} else if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
					oplus_adfr_aod_fod_vsync_switch(display->panel, true);
				}
			}
#endif /* OPLUS_BUG_STABILITY */

			oplus_ofp_set_aod_state(true);

			/* aod on */
			rc = dsi_panel_set_lp1(display->panel);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_SET_LP1 cmds, rc=%d\n", display->name, rc);
			}
			rc = dsi_panel_set_lp2(display->panel);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_SET_LP2 cmds, rc=%d\n", display->name, rc);
			}

			if (p_oplus_ofp_params->aod_light_mode) {
				rc = oplus_ofp_display_cmd_set(display, DSI_CMD_AOD_LOW_LIGHT_MODE);
				if (rc) {
					OFP_ERR("[%s] failed to send DSI_CMD_AOD_LOW_LIGHT_MODE cmds, rc=%d\n", display->name, rc);
				}
			}
		}
		break;

	case SDE_MODE_DPMS_ON:
		if (p_oplus_ofp_params->doze_active) {
			p_oplus_ofp_params->doze_active = false;
			OFP_INFO("oplus_ofp_doze_active:%d\n", p_oplus_ofp_params->doze_active);
			OPLUS_OFP_TRACE_INT("oplus_ofp_doze_active", p_oplus_ofp_params->doze_active);
		}

		if (oplus_ofp_get_aod_state()) {
			rc = oplus_ofp_aod_off_handle(display);
			if (rc) {
				OFP_ERR("[%s] failed to handle aod off, rc=%d\n", display->name, rc);
			}
		}
		break;

	case SDE_MODE_DPMS_OFF:
		if (p_oplus_ofp_params->doze_active) {
			p_oplus_ofp_params->doze_active = false;
			OFP_INFO("oplus_ofp_doze_active:%d\n", p_oplus_ofp_params->doze_active);
			OPLUS_OFP_TRACE_INT("oplus_ofp_doze_active", p_oplus_ofp_params->doze_active);
		}

		if (oplus_ofp_get_aod_state()) {
			rc = oplus_ofp_aod_off_handle(display);
			if (rc) {
				OFP_ERR("[%s] failed to handle aod off, rc=%d\n", display->name, rc);
			}
		}
		break;

	default:
		OFP_DEBUG("power_mode:%d\n", power_mode);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_power_mode_handle");

	OFP_DEBUG("end\n");

	return rc;
}

void oplus_ofp_aod_off_set_work_handler(struct work_struct *work_item)
{
	int rc = 0;
	struct dsi_display *display = oplus_display_get_current_display();

	OFP_DEBUG("start\n");

	if (!display) {
		OFP_ERR("Invalid params\n");
		return;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_aod_off_set_work_handler");

	OFP_INFO("send aod off cmd to speed up aod unlocking\n");
	rc = oplus_ofp_aod_off_handle(display);
	if (rc) {
		OFP_ERR("failed to send aod off cmd\n");
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_aod_off_set_work_handler");

	OFP_DEBUG("end\n");

	return;
}

static int oplus_ofp_aod_off_set(void)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	return 0;

	if (oplus_ofp_oled_capacitive_is_enabled()) {
		OFP_DEBUG("no need to send aod off cmd in doze mode to speed up fingerprint unlocking\n");
		return 0;
	}

	if (!p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	if (oplus_ofp_get_hbm_state()) {
		OFP_DEBUG("ignore aod off setting in hbm state\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_aod_off_set");

	if (oplus_ofp_get_aod_state() && p_oplus_ofp_params->doze_active) {
		OFP_INFO("queue aod off set work\n");
		queue_work(p_oplus_ofp_params->aod_off_set_wq, &p_oplus_ofp_params->aod_off_set_work);
		oplus_ofp_set_aod_state(false);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_aod_off_set");

	OFP_DEBUG("end\n");

	return 0;
}

/*
 touchpanel notify touchdown event when fingerprint is pressed,
 then display driver send aod off cmd immediately and vsync change to 120hz/90hz,
 so that press icon layer can sent down faster
*/
int oplus_ofp_touchpanel_event_notifier_call(struct notifier_block *nb, unsigned long action, void *data)
{
	struct touchpanel_event *tp_event = (struct touchpanel_event *)data;

	OFP_DEBUG("start\n");

	if (!oplus_ofp_is_supported() || oplus_ofp_oled_capacitive_is_enabled()) {
		OFP_DEBUG("no need to send aod off cmd in doze mode to speed up fingerprint unlocking\n");
		return NOTIFY_OK;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_touchpanel_event_notifier_call");

	if (tp_event) {
		if (action == EVENT_ACTION_FOR_FINGPRINT) {
			OFP_DEBUG("EVENT_ACTION_FOR_FINGPRINT\n");

			if (tp_event->touch_state == 1) {
				OFP_INFO("tp touchdown\n");
				/* send aod off cmd in doze mode to speed up fingerprint unlocking */
				oplus_ofp_aod_off_set();
			}
		}
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_touchpanel_event_notifier_call");

	OFP_DEBUG("end\n");

	return NOTIFY_OK;
}

/*
 since setting the backlight while the aod layer is exist will cause splash issue,
 the backlight will be filtered at this time and restored after the aod layer disappears
 */
int oplus_ofp_aod_off_backlight_recovery(void *sde_encoder_virt)
{
	int rc = 0;
	static bool last_aod_layer_status = false;
	bool new_aod_layer_status = false;
	struct sde_encoder_virt *sde_enc = sde_encoder_virt;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!sde_enc || !sde_enc->cur_master || !sde_enc->cur_master->connector || !p_oplus_ofp_params) {
		OFP_ERR("Invalid sde_enc params\n");
		return -EINVAL;
	}

	c_conn = to_sde_connector(sde_enc->cur_master->connector);
	if (!c_conn) {
		OFP_ERR("Invalid c_conn params\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		OFP_DEBUG("not in dsi mode, should not recovery backlight after aod off\n");
		return 0;
	}

	display = c_conn->display;

	if (!display || !display->panel) {
		OFP_ERR("Invalid display params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_aod_off_backlight_recovery");

	new_aod_layer_status = p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_AOD_LAYER;

	if ((last_aod_layer_status != new_aod_layer_status) && !new_aod_layer_status) {
		OFP_DEBUG("recovery backlight level after aod off\n");
		rc = dsi_panel_set_backlight(display->panel, display->panel->bl_config.bl_level);
		if (rc) {
			OFP_ERR("unable to set backlight\n");
		}
	}

	last_aod_layer_status = new_aod_layer_status;

	OPLUS_OFP_TRACE_END("oplus_ofp_aod_off_backlight_recovery");

	OFP_DEBUG("end\n");

	return rc;
}

int oplus_ofp_ultra_low_power_aod_update(void *sde_encoder_virt)
{
	int rc = 0;
	struct sde_encoder_virt *sde_enc = sde_encoder_virt;
	struct sde_connector *c_conn = NULL;
	struct dsi_display *display = NULL;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!(oplus_ofp_is_supported() && oplus_ofp_ultra_low_power_aod_is_enabled())) {
		OFP_DEBUG("ultra low power aod is not supported\n");
		return 0;
	} else if (!oplus_ofp_get_aod_state()) {
		OFP_DEBUG("not in aod mode, should not updata ultra low power aod\n");
		return 0;
	} else if (oplus_ofp_get_hbm_state()) {
		OFP_DEBUG("ignore ultra low power aod update in hbm state\n");
		return 0;
	}

	if (!sde_enc || !sde_enc->cur_master || !sde_enc->cur_master->connector || !p_oplus_ofp_params) {
		OFP_ERR("Invalid sde_enc params\n");
		return -EINVAL;
	}

	c_conn = to_sde_connector(sde_enc->cur_master->connector);
	if (!c_conn) {
		OFP_ERR("Invalid c_conn params\n");
		return -EINVAL;
	}

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
		OFP_DEBUG("not in dsi mode, should not updata ultra low power aod\n");
		return 0;
	}

	display = c_conn->display;

	if (!display || !display->panel) {
		OFP_ERR("Invalid display params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_ultra_low_power_aod_update");

	if (!(p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_ICON_LAYER)
		&& (p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_AOD_LAYER)
			&& p_oplus_ofp_params->ultra_low_power_aod_mode) {
		/* when icon layer disappear, enable ultra low power aod immediately */
		if (!p_oplus_ofp_params->ultra_low_power_aod_state) {
			OFP_INFO("update ultra low power aod on\n");
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_ULTRA_LOW_POWER_AOD_ON);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_ULTRA_LOW_POWER_AOD_ON cmds, rc=%d\n", display->name, rc);
			}
		}
	} else {
		if (p_oplus_ofp_params->ultra_low_power_aod_state) {
			OFP_INFO("update ultra low power aod off\n");
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_ULTRA_LOW_POWER_AOD_OFF);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_ULTRA_LOW_POWER_AOD_OFF cmds, rc=%d\n", display->name, rc);
			}
		}
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_ultra_low_power_aod_update");

	OFP_DEBUG("end\n");

	return rc;
}

/* -------------------- node -------------------- */
/* fp_type */
int oplus_ofp_set_fp_type(void *buf)
{
	unsigned int *fp_type = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_fp_type");

	p_oplus_ofp_params->fp_type = *fp_type;
	OFP_INFO("fp_type:0x%x\n", p_oplus_ofp_params->fp_type);
	OPLUS_OFP_TRACE_INT("oplus_ofp_fp_type", p_oplus_ofp_params->fp_type);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_fp_type");

	OFP_DEBUG("end\n");

	return 0;
}

int oplus_ofp_get_fp_type(void *buf)
{
	unsigned int *fp_type = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_fp_type");

	*fp_type = p_oplus_ofp_params->fp_type;
	OFP_INFO("fp_type:0x%x\n", *fp_type);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_fp_type");

	OFP_DEBUG("end\n");

	return 0;
}

ssize_t oplus_ofp_set_fp_type_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int fp_type = 0;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return count;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_fp_type_attr");

	sscanf(buf, "%u", &fp_type);

	p_oplus_ofp_params->fp_type = fp_type;
	OFP_INFO("fp_type:0x%x\n", p_oplus_ofp_params->fp_type);
	OPLUS_OFP_TRACE_INT("oplus_ofp_fp_type", p_oplus_ofp_params->fp_type);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_fp_type_attr");

	OFP_DEBUG("end\n");

	return count;
}

ssize_t oplus_ofp_get_fp_type_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_fp_type_attr");

	OFP_INFO("fp_type:0x%x\n", p_oplus_ofp_params->fp_type);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_fp_type_attr");

	OFP_DEBUG("end\n");

	return sprintf(buf, "%u\n", p_oplus_ofp_params->fp_type);
}

/* ----- fod part ----- */
/* hbm */
int oplus_ofp_set_hbm(void *buf)
{
	int rc = 0;
	unsigned int *hbm_mode = buf;
	struct dsi_display *display = oplus_display_get_current_display();
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OFP_INFO("hbm mode:%u to %u\n", p_oplus_ofp_params->hbm_mode, *hbm_mode);

	if (!dsi_panel_initialized(display->panel)) {
		OFP_ERR("should not set hbm if panel is not initialized\n");
		return -EFAULT;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_hbm");

	p_oplus_ofp_params->hbm_mode = (*hbm_mode);
	OFP_INFO("oplus_ofp_hbm_mode:%u\n", p_oplus_ofp_params->hbm_mode);
	OPLUS_OFP_TRACE_INT("oplus_ofp_hbm_mode", p_oplus_ofp_params->hbm_mode);

	if (p_oplus_ofp_params->hbm_mode) {
		rc = oplus_ofp_display_cmd_set(display, DSI_CMD_HBM_ON);
		if (rc) {
			OFP_ERR("[%s] failed to send DSI_CMD_HBM_ON cmds, rc=%d\n", display->name, rc);
		}
	} else {
		rc = oplus_ofp_display_cmd_set(display, DSI_CMD_HBM_OFF);
		if (rc) {
			OFP_ERR("[%s] failed to send DSI_CMD_HBM_OFF cmds, rc=%d\n", display->name, rc);
		}
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_set_hbm");

	OFP_DEBUG("end\n");

	return rc;
}

int oplus_ofp_get_hbm(void *buf)
{
	unsigned int *hbm_mode = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_hbm");

	*hbm_mode = p_oplus_ofp_params->hbm_mode;
	OFP_INFO("hbm_mode:%u\n", *hbm_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_hbm");

	OFP_DEBUG("end\n");

	return 0;
}

ssize_t oplus_ofp_set_hbm_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	unsigned int hbm_mode = 0;
	struct dsi_display *display = oplus_display_get_current_display();
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return count;
	}

	sscanf(buf, "%u", &hbm_mode);
	OFP_INFO("hbm mode:%u to %u\n", p_oplus_ofp_params->hbm_mode, hbm_mode);

	if (!dsi_panel_initialized(display->panel)) {
		OFP_ERR("should not set hbm if panel is not initialized\n");
		return count;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_hbm_attr");

	p_oplus_ofp_params->hbm_mode = hbm_mode;
	OFP_INFO("oplus_ofp_hbm_mode:%u\n", p_oplus_ofp_params->hbm_mode);
	OPLUS_OFP_TRACE_INT("oplus_ofp_hbm_mode", p_oplus_ofp_params->hbm_mode);

	if (p_oplus_ofp_params->hbm_mode) {
		rc = oplus_ofp_display_cmd_set(display, DSI_CMD_HBM_ON);
		if (rc) {
			OFP_ERR("[%s] failed to send DSI_CMD_HBM_ON cmds, rc=%d\n", display->name, rc);
		}
	} else {
		rc = oplus_ofp_display_cmd_set(display, DSI_CMD_HBM_OFF);
		if (rc) {
			OFP_ERR("[%s] failed to send DSI_CMD_HBM_OFF cmds, rc=%d\n", display->name, rc);
		}
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_set_hbm_attr");

	OFP_DEBUG("end\n");

	return count;
}

ssize_t oplus_ofp_get_hbm_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_hbm_attr");

	OFP_INFO("hbm_mode:%u\n", p_oplus_ofp_params->hbm_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_hbm_attr");

	OFP_DEBUG("end\n");

	return sprintf(buf, "%u\n", p_oplus_ofp_params->hbm_mode);
}

/* dimlayer_hbm */
int oplus_ofp_set_dimlayer_hbm(void *buf)
{
	unsigned int *dimlayer_hbm = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!oplus_ofp_is_supported() || oplus_ofp_oled_capacitive_is_enabled() || oplus_ofp_ultrasonic_is_enabled()) {
		OFP_DEBUG("no need to set dimlayer hbm\n");
		return 0;
	}

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_dimlayer_hbm");

	p_oplus_ofp_params->dimlayer_hbm = *dimlayer_hbm;
	OFP_INFO("dimlayer_hbm:%u\n", p_oplus_ofp_params->dimlayer_hbm);
	OPLUS_OFP_TRACE_INT("oplus_ofp_dimlayer_hbm", p_oplus_ofp_params->dimlayer_hbm);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_dimlayer_hbm");

	OFP_DEBUG("end\n");

	return 0;
}

int oplus_ofp_get_dimlayer_hbm(void *buf)
{
	unsigned int *dimlayer_hbm = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_dimlayer_hbm");

	*dimlayer_hbm = p_oplus_ofp_params->dimlayer_hbm;
	OFP_INFO("dimlayer_hbm:%u\n", *dimlayer_hbm);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_dimlayer_hbm");

	OFP_DEBUG("end\n");

	return 0;
}

ssize_t oplus_ofp_set_dimlayer_hbm_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int dimlayer_hbm = 0;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!oplus_ofp_is_supported() || oplus_ofp_oled_capacitive_is_enabled() || oplus_ofp_ultrasonic_is_enabled()) {
		OFP_DEBUG("no need to set dimlayer hbm\n");
		return count;
	}

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return count;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_dimlayer_hbm_attr");

	sscanf(buf, "%u", &dimlayer_hbm);

	p_oplus_ofp_params->dimlayer_hbm = dimlayer_hbm;
	OFP_INFO("dimlayer_hbm:%u\n", p_oplus_ofp_params->dimlayer_hbm);
	OPLUS_OFP_TRACE_INT("oplus_ofp_dimlayer_hbm", p_oplus_ofp_params->dimlayer_hbm);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_dimlayer_hbm_attr");

	OFP_DEBUG("end\n");

	return count;
}

ssize_t oplus_ofp_get_dimlayer_hbm_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_dimlayer_hbm_attr");

	OFP_INFO("dimlayer_hbm:%u\n", p_oplus_ofp_params->dimlayer_hbm);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_dimlayer_hbm_attr");

	OFP_DEBUG("end\n");

	return sprintf(buf, "%u\n", p_oplus_ofp_params->dimlayer_hbm);
}

/* notify_fppress */
int oplus_ofp_notify_fp_press(void *buf)
{
	unsigned int *fp_press = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!oplus_ofp_is_supported() || oplus_ofp_oled_capacitive_is_enabled() || oplus_ofp_local_hbm_is_enabled()) {
		OFP_DEBUG("no need to set fp press\n");
		return 0;
	}

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_notify_fp_press");

	if (*fp_press) {
		/* finger is pressed down and pressed icon layer is ready */
		p_oplus_ofp_params->fp_press = true;
	} else {
		p_oplus_ofp_params->fp_press = false;
	}
	OFP_INFO("oplus_ofp_fp_press:%d\n", p_oplus_ofp_params->fp_press);
	OPLUS_OFP_TRACE_INT("oplus_ofp_fp_press", p_oplus_ofp_params->fp_press);

	if (p_oplus_ofp_params->fp_press) {
		/* send aod off cmd in doze mode to speed up fingerprint unlocking */
		OFP_DEBUG("fp press is true\n");
		oplus_ofp_aod_off_set();
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_notify_fp_press");

	OFP_DEBUG("end\n");

	return 0;
}

/* notify fp press for sysfs */
ssize_t oplus_ofp_notify_fp_press_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int fp_press = 0;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!oplus_ofp_is_supported() || oplus_ofp_oled_capacitive_is_enabled() || oplus_ofp_local_hbm_is_enabled()) {
		OFP_DEBUG("no need to set fp press\n");
		return count;
	}

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return count;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_notify_fp_press_attr");

	sscanf(buf, "%d", &fp_press);

	if (fp_press) {
		/* finger is pressed down and pressed icon layer is ready */
		p_oplus_ofp_params->fp_press = true;
	} else {
		p_oplus_ofp_params->fp_press = false;
	}
	OFP_INFO("oplus_ofp_fp_press:%d\n", p_oplus_ofp_params->fp_press);
	OPLUS_OFP_TRACE_INT("oplus_ofp_fp_press", p_oplus_ofp_params->fp_press);

	if (p_oplus_ofp_params->fp_press) {
		/* send aod off cmd in doze mode to speed up fingerprint unlocking */
		OFP_DEBUG("fp press is true\n");
		oplus_ofp_aod_off_set();
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_notify_fp_press_attr");

	OFP_DEBUG("end\n");

	return count;
}

/* ----- aod part ----- */
/* aod_light_mode_set */
int oplus_ofp_set_aod_light_mode(void *buf)
{
	int rc = 0;
	unsigned int *aod_light_mode = buf;
	struct dsi_display *display = oplus_display_get_current_display();
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OFP_INFO("set aod brightness to %s nit\n", (*aod_light_mode == 0)? "50" : "10");

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("aod is not supported\n");
		return 0;
	} else if (!oplus_ofp_get_aod_state()) {
		OFP_ERR("not in aod mode, should not set aod_light_mode\n");
		return 0;
	} else if (oplus_ofp_get_hbm_state()) {
		OFP_INFO("ignore aod light mode setting in hbm state\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_aod_light_mode");

	if (*aod_light_mode != p_oplus_ofp_params->aod_light_mode) {
		if (*aod_light_mode) {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_AOD_LOW_LIGHT_MODE);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_AOD_LOW_LIGHT_MODE cmds, rc=%d\n", display->name, rc);
			}
		} else {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_AOD_HIGH_LIGHT_MODE);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_AOD_HIGH_LIGHT_MODE cmds, rc=%d\n", display->name, rc);
			}
		}

		p_oplus_ofp_params->aod_light_mode = (*aod_light_mode);
		OFP_INFO("aod_light_mode:%u\n", p_oplus_ofp_params->aod_light_mode);
		OPLUS_OFP_TRACE_INT("oplus_ofp_aod_light_mode", p_oplus_ofp_params->aod_light_mode);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_set_aod_light_mode");

	OFP_DEBUG("end\n");

	return rc;
}

int oplus_ofp_get_aod_light_mode(void *buf)
{
	unsigned int *aod_light_mode = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_aod_light_mode");

	*aod_light_mode = p_oplus_ofp_params->aod_light_mode;
	OFP_INFO("aod_light_mode:%u\n", *aod_light_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_aod_light_mode");

	OFP_DEBUG("end\n");

	return 0;
}

ssize_t oplus_ofp_set_aod_light_mode_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	unsigned int aod_light_mode = 0;
	struct dsi_display *display = oplus_display_get_current_display();
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return count;
	}

	sscanf(buf, "%u", &aod_light_mode);
	OFP_INFO("set aod brightness to %s nit\n", (aod_light_mode == 0)? "50" : "10");

	if (!oplus_ofp_is_supported()) {
		OFP_DEBUG("aod is not supported\n");
		return count;
	} else if (!oplus_ofp_get_aod_state()) {
		OFP_ERR("not in aod mode, should not set aod_light_mode\n");
		return count;
	} else if (oplus_ofp_get_hbm_state()) {
		OFP_INFO("ignore aod light mode setting in hbm state\n");
		return count;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_aod_light_mode_attr");

	if (aod_light_mode != p_oplus_ofp_params->aod_light_mode) {
		if (aod_light_mode) {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_AOD_LOW_LIGHT_MODE);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_AOD_LOW_LIGHT_MODE cmds, rc=%d\n", display->name, rc);
			}
		} else {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_AOD_HIGH_LIGHT_MODE);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_AOD_HIGH_LIGHT_MODE cmds, rc=%d\n", display->name, rc);
			}
		}

		p_oplus_ofp_params->aod_light_mode = aod_light_mode;
		OFP_INFO("aod_light_mode:%u\n", p_oplus_ofp_params->aod_light_mode);
		OPLUS_OFP_TRACE_INT("oplus_ofp_aod_light_mode", p_oplus_ofp_params->aod_light_mode);
	}

	OPLUS_OFP_TRACE_END("oplus_ofp_set_aod_light_mode_attr");

	OFP_DEBUG("end\n");

	return count;
}

ssize_t oplus_ofp_get_aod_light_mode_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_aod_light_mode_attr");

	OFP_INFO("aod_light_mode:%u\n", p_oplus_ofp_params->aod_light_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_aod_light_mode_attr");

	OFP_DEBUG("end\n");

	return sprintf(buf, "%u\n", p_oplus_ofp_params->aod_light_mode);
}

/* ultra_low_power_aod_mode */
int oplus_ofp_set_ultra_low_power_aod_mode(void *buf)
{
	int rc = 0;
	unsigned int *ultra_low_power_aod_mode = buf;
	struct dsi_display *display = oplus_display_get_current_display();
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OFP_INFO("ultra_low_power_aod_mode %s\n", (*ultra_low_power_aod_mode)? "enable" : "disable");

	if (!(oplus_ofp_is_supported() && oplus_ofp_ultra_low_power_aod_is_enabled())) {
		OFP_DEBUG("ultra low power aod is not supported\n");
		return 0;
	} else if (!oplus_ofp_get_aod_state()) {
		OFP_ERR("not in aod mode, should not set ultra_low_power_aod_mode\n");
		return 0;
	} else if (oplus_ofp_get_hbm_state()) {
		OFP_INFO("ignore ultra low power aod mode setting in hbm state\n");
		return 0;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_ultra_low_power_aod_mode");

	/* this could lead to discontinuous animation when enter or exit ultra low power aod mode frequently,
		so ignore this cmd until icon layer disappear */
	if (!(p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_ICON_LAYER)) {
		if (*ultra_low_power_aod_mode && !p_oplus_ofp_params->ultra_low_power_aod_state) {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_ULTRA_LOW_POWER_AOD_ON);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_ULTRA_LOW_POWER_AOD_ON cmds, rc=%d\n", display->name, rc);
			}
		} else if (!(*ultra_low_power_aod_mode) && p_oplus_ofp_params->ultra_low_power_aod_state) {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_ULTRA_LOW_POWER_AOD_OFF);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_ULTRA_LOW_POWER_AOD_OFF cmds, rc=%d\n", display->name, rc);
			}
		}
	}
	p_oplus_ofp_params->ultra_low_power_aod_mode = (*ultra_low_power_aod_mode);
	OFP_INFO("ultra_low_power_aod_mode:%u\n", p_oplus_ofp_params->ultra_low_power_aod_mode);
	OPLUS_OFP_TRACE_INT("oplus_ofp_ultra_low_power_aod_mode", p_oplus_ofp_params->ultra_low_power_aod_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_ultra_low_power_aod_mode");

	OFP_DEBUG("end\n");

	return rc;
}

int oplus_ofp_get_ultra_low_power_aod_mode(void *buf)
{
	unsigned int *ultra_low_power_aod_mode = buf;
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_ultra_low_power_aod_mode");

	*ultra_low_power_aod_mode = p_oplus_ofp_params->ultra_low_power_aod_mode;
	OFP_INFO("ultra_low_power_aod_mode:%u\n", *ultra_low_power_aod_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_ultra_low_power_aod_mode");

	OFP_DEBUG("end\n");

	return 0;
}

ssize_t oplus_ofp_set_ultra_low_power_aod_mode_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	unsigned int ultra_low_power_aod_mode = 0;
	struct dsi_display *display = oplus_display_get_current_display();
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !display || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return count;
	}

	sscanf(buf, "%u", &ultra_low_power_aod_mode);
	OFP_INFO("ultra_low_power_aod_mode %s\n", ultra_low_power_aod_mode? "enable" : "disable");

	if (!(oplus_ofp_is_supported() && oplus_ofp_ultra_low_power_aod_is_enabled())) {
		OFP_DEBUG("ultra low power aod is not supported\n");
		return count;
	} else if (!oplus_ofp_get_aod_state()) {
		OFP_ERR("not in aod mode, should not set ultra_low_power_aod_mode\n");
		return count;
	} else if (oplus_ofp_get_hbm_state()) {
		OFP_INFO("ignore ultra low power aod mode setting in hbm state\n");
		return count;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_set_ultra_low_power_aod_mode_attr");

	/* this could lead to discontinuous animation when enter or exit ultra low power aod mode frequently,
		so ignore this cmd until icon layer disappear */
	if (!(p_oplus_ofp_params->hbm_enable & OPLUS_OFP_PROPERTY_ICON_LAYER)) {
		if (ultra_low_power_aod_mode && !p_oplus_ofp_params->ultra_low_power_aod_state) {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_ULTRA_LOW_POWER_AOD_ON);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_ULTRA_LOW_POWER_AOD_ON cmds, rc=%d\n", display->name, rc);
			}
		} else if (!ultra_low_power_aod_mode && p_oplus_ofp_params->ultra_low_power_aod_state) {
			rc = oplus_ofp_display_cmd_set(display, DSI_CMD_ULTRA_LOW_POWER_AOD_OFF);
			if (rc) {
				OFP_ERR("[%s] failed to send DSI_CMD_ULTRA_LOW_POWER_AOD_OFF cmds, rc=%d\n", display->name, rc);
			}
		}
	}
	p_oplus_ofp_params->ultra_low_power_aod_mode = ultra_low_power_aod_mode;
	OFP_INFO("ultra_low_power_aod_mode:%u\n", p_oplus_ofp_params->ultra_low_power_aod_mode);
	OPLUS_OFP_TRACE_INT("oplus_ofp_ultra_low_power_aod_mode", p_oplus_ofp_params->ultra_low_power_aod_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_set_ultra_low_power_aod_mode_attr");

	OFP_DEBUG("end\n");

	return count;
}

ssize_t oplus_ofp_get_ultra_low_power_aod_mode_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	struct oplus_ofp_params *p_oplus_ofp_params = oplus_ofp_get_params(oplus_ofp_display_id);

	OFP_DEBUG("start\n");

	if (!buf || !p_oplus_ofp_params) {
		OFP_ERR("Invalid params\n");
		return -EINVAL;
	}

	OPLUS_OFP_TRACE_BEGIN("oplus_ofp_get_ultra_low_power_aod_mode_attr");

	OFP_INFO("ultra_low_power_aod_mode:%u\n", p_oplus_ofp_params->ultra_low_power_aod_mode);

	OPLUS_OFP_TRACE_END("oplus_ofp_get_ultra_low_power_aod_mode_attr");

	OFP_DEBUG("end\n");

	return sprintf(buf, "%u\n", p_oplus_ofp_params->ultra_low_power_aod_mode);
}
