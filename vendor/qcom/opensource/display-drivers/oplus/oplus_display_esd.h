/***************************************************************
** Copyright (C),  2021,  oplus Mobile Comm Corp.,  Ltd
**
** File : oplus_display_esd.h
** Description : oplus esd feature
** Version : 1.0
** Date : 2021/11/26
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Six.Xu         2021/11/26        1.0           Build this moudle
******************************************************************/
#ifndef _OPLUS_ESD_H_
#define _OPLUS_ESD_H_

#include "dsi_panel.h"
#include "dsi_defs.h"
#include "oplus_display_private_api.h"

int oplus_panel_parse_esd_reg_read_configs(struct dsi_panel *panel);
bool oplus_display_validate_reg_read(struct dsi_panel *panel);

#endif /* _OPLUS_ESD_H_ */
