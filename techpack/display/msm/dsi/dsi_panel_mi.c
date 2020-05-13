/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (C) 2020 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"mi-dsi-panel:[%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include <video/mipi_display.h>

#include "dsi_panel.h"
#include "dsi_display.h"
#include "dsi_ctrl_hw.h"
#include "dsi_parser.h"
#include "dsi_mi_feature.h"
#include "../../../../kernel/irq/internals.h"

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

static struct dsi_read_config g_dsi_read_cfg;
static struct dsi_panel_cmd_set gamma_cmd_set[DSI_CMD_SET_MI_GAMMA_SWITCH_MAX];

static void panelon_dimming_enable_delayed_work(struct work_struct *work)
{
	struct dsi_panel_mi_cfg *mi_cfg = container_of(work,
				struct dsi_panel_mi_cfg, dimming_enable_delayed_work.work);
	struct dsi_panel *dsi_panel = mi_cfg->dsi_panel;

	if (dsi_panel)
		dsi_panel_set_disp_param(dsi_panel, DISPPARAM_DIMMING);
}

static void enter_aod_delayed_work(struct work_struct *work)
{
	struct dsi_panel_mi_cfg *mi_cfg = container_of(work,
				struct dsi_panel_mi_cfg, enter_aod_delayed_work.work);
	struct dsi_panel *panel = mi_cfg->dsi_panel;

	if (!panel)
		return;

	mutex_lock(&panel->panel_lock);

	if (!panel->panel_initialized)
		goto exit;

	if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
			panel->power_mode == SDE_MODE_DPMS_LP2) {
		if (mi_cfg->layer_fod_unlock_success || mi_cfg->sysfs_fod_unlock_success) {
			pr_info("[%d,%d]Fod fingerprint unlocked successfully, skip to enter aod mode\n",
				mi_cfg->layer_fod_unlock_success, mi_cfg->sysfs_fod_unlock_success);
			goto exit;
		} else {
			if (!mi_cfg->unset_doze_brightness) {
				mi_cfg->unset_doze_brightness = mi_cfg->doze_brightness_state;
			}
			pr_info("delayed_work runing --- set doze brightness\n");
			dsi_panel_set_doze_brightness(panel, mi_cfg->unset_doze_brightness, false);
		}
	}

exit:
	mutex_unlock(&panel->panel_lock);
}


static int dsi_panel_parse_gamma_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	if (mi_cfg->gamma_update_flag) {
		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-flash-read-total-param",
				&mi_cfg->gamma_cfg.flash_read_total_param);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-flash-read-total-param\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-flash-read-c1-index",
				&mi_cfg->gamma_cfg.flash_read_c1_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-flash-read-c1-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-update-c8-index",
				&mi_cfg->gamma_cfg.update_c8_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-update-c8-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-update-c9-index",
				&mi_cfg->gamma_cfg.update_c9_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-update-c9-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-gamma-update-b3-index",
				&mi_cfg->gamma_cfg.update_b3_index);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-panel-gamma-update-b3-index\n");

		mi_cfg->gamma_cfg.black_setting_flag = utils->read_bool(of_node,
				"mi,mdss-dsi-panel-gamma-black-setting-flag");
		if (!mi_cfg->gamma_cfg.black_setting_flag)
			pr_info("can't get mi,mdss-dsi-panel-gamma-black-setting-flag\n");
	}

	return rc;
}

static int dsi_panel_parse_greenish_gamma_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	if (mi_cfg->greenish_gamma_update_flag) {
		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-greenish-update-gamma-length",
				&mi_cfg->greenish_gamma_read_len);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-greenish-update-gamma-length\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-greenish-update-gamma-offset",
				&mi_cfg->greenish_gamma_cfg.greenish_gamma_update_offset);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-greenish-update-gamma-offset\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-param-count",
				&mi_cfg->greenish_gamma_cfg.greenish_gamma_update_param_count);
		if (rc)
			pr_info("failed to get mi,mdss-dsi-update-gamma-param-count\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-1st-index",
				&mi_cfg->greenish_gamma_cfg.index_1st_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-1st-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-2nd-index",
				&mi_cfg->greenish_gamma_cfg.index_2nd_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-2nd-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-3rd-index",
				&mi_cfg->greenish_gamma_cfg.index_3rd_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-3rd-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-4th-index",
				&mi_cfg->greenish_gamma_cfg.index_4th_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-4th-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-5th-index",
				&mi_cfg->greenish_gamma_cfg.index_5th_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-5th-index\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-update-gamma-6th-index",
				&mi_cfg->greenish_gamma_cfg.index_6th_param);
		if (rc)
			pr_info("failed to get mdss-dsi-update-gamma-6th-index\n");
	}

	return rc;
}

static int dsi_panel_parse_white_point_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	rc = utils->read_u32(of_node,
			"mi,mdss-dsi-white-point-register-read-length",
			&mi_cfg->wp_reg_read_len);
	if (rc)
		pr_info("failed to get mi,mdss-dsi-white-point-register-read-length\n");

	rc = utils->read_u32(of_node,
			"mi,mdss-dsi-white-point-info-index",
			&mi_cfg->wp_info_index);
	if (rc)
		pr_info("failed to get mi,mdss-dsi-white-point-info-index\n");

	rc = utils->read_u32(of_node,
			"mi,mdss-dsi-white-point-info-length",
			&mi_cfg->wp_info_len);
	if (rc)
		pr_info("failed to get mi,mdss-dsi-white-point-info-length\n");

	return rc;
}

static int dsi_panel_parse_elvss_dimming_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	rc = utils->read_u32(of_node, "mi,mdss-dsi-elvss-dimming-register-read-length",
					&mi_cfg->elvss_dimming_read_len);
	if (rc)
		pr_err("failed to get mi,mdss-dsi-elvss-dimming-register-read-length\n");

	rc = utils->read_u32(of_node, "mi,mdss-dsi-elvss-dimming-update-hbm-fod-on-index",
					&mi_cfg->update_hbm_fod_on_index);
	if (rc)
		pr_err("failed to get mi,mdss-dsi-elvss-dimming-update-hbm-fod-on-index\n");

	rc = utils->read_u32(of_node, "mi,mdss-dsi-elvss-dimming-update-hbm-fod-off-index",
					&mi_cfg->update_hbm_fod_off_index);
	if (rc)
		pr_err("failed to get mi,mdss-dsi-elvss-dimming-update-hbm-fod-off-index\n");

	return rc;
}

int dsi_panel_parse_esd_gpio_config(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;

	mi_cfg->esd_err_irq_gpio = of_get_named_gpio_flags(
			utils->data, "mi,esd-err-irq-gpio",
			0, (enum of_gpio_flags *)&(mi_cfg->esd_err_irq_flags));
	if (gpio_is_valid(mi_cfg->esd_err_irq_gpio)) {
		mi_cfg->esd_err_irq = gpio_to_irq(mi_cfg->esd_err_irq_gpio);
		rc = gpio_request(mi_cfg->esd_err_irq_gpio, "esd_err_irq_gpio");
		if (rc)
			pr_err("Failed to request esd irq gpio %d, rc=%d\n",
				mi_cfg->esd_err_irq_gpio, rc);
		else
			gpio_direction_input(mi_cfg->esd_err_irq_gpio);
	} else {
		rc = -EINVAL;
	}

	return rc;
}

int dsi_panel_parse_mi_config(struct dsi_panel *panel,
				struct device_node *of_node)
{
	int rc = 0;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_panel_mi_cfg *mi_cfg = &panel->mi_cfg;
	u32 length = 0;
	const u32 *arr;

	mi_cfg->dsi_panel = panel;

	INIT_DELAYED_WORK(&mi_cfg->enter_aod_delayed_work, enter_aod_delayed_work);

	mi_cfg->bl_is_big_endian= utils->read_bool(utils->data,
			"mi,mdss-dsi-bl-dcs-big-endian-type");

	mi_cfg->mi_feature_enabled = utils->read_bool(of_node,
			"mi,feature-enabled");
	if (mi_cfg->mi_feature_enabled) {
		pr_info("mi feature enabled\n");
	} else {
		pr_info("mi feature disabled\n");
		return 0;
	}

	mi_cfg->hbm_51_ctrl_flag = utils->read_bool(utils->data,
		"mi,mdss-dsi-panel-hbm-51-ctrl-flag");
	if (mi_cfg->hbm_51_ctrl_flag) {
		rc = utils->read_u32(of_node,
			"mi,mdss-dsi-panel-hbm-off-51-index", &mi_cfg->hbm_off_51_index);
		if (rc) {
			pr_err("mi,mdss-dsi-panel-hbm-off-51-index not defined,but need\n");
		}
		rc = utils->read_u32(of_node,
			"mi,mdss-dsi-panel-fod-off-51-index", &mi_cfg->fod_off_51_index);
		if (rc) {
			pr_err("mi,mdss-dsi-panel-fod-off-51-index not defined,but need\n");
		}
		mi_cfg->vi_setting_enabled = utils->read_bool(of_node,
			"mi,mdss-dsi-panel-vi-setting-enabled");
		if (mi_cfg->vi_setting_enabled) {
			pr_info("mi vi_setting_enabled = %d\n", mi_cfg->vi_setting_enabled);
			rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-vi-switch-threshold", &mi_cfg->vi_switch_threshold);
			if (rc)
				pr_err("mi,mdss-dsi-panel-vi-switch-threshold not defined,but need\n");
		}
		mi_cfg->dynamic_elvss_enabled = utils->read_bool(of_node,
			"mi,mdss-dsi-panel-dynamic-elvss-enabled");
		if (mi_cfg->dynamic_elvss_enabled) {
			pr_info("mi dynamic_elvss_enabled = %d\n", mi_cfg->dynamic_elvss_enabled);
		}
	}

	mi_cfg->fod_dimlayer_enabled = utils->read_bool(of_node,
		"mi,mdss-dsi-panel-fod-dimlayer-enabled");
	if (mi_cfg->fod_dimlayer_enabled) {
		pr_info("fod dimlayer enabled.\n");
	} else {
		pr_info("fod dimlayer disabled.\n");
	}

	if (mi_cfg->fod_dimlayer_enabled) {
		mi_cfg->prepare_before_fod_hbm_on = utils->read_bool(of_node,
			"mi,mdss-panel-prepare-before-fod-hbm-on");
		if (mi_cfg->prepare_before_fod_hbm_on) {
			pr_info("fod hbm on need prepare.\n");
		} else {
			pr_info("fod hbm on doesn't need prepare.\n");
		}

		mi_cfg->delay_before_fod_hbm_on = utils->read_bool(of_node,
			"mi,mdss-panel-delay-before-fod-hbm-on");
		if (mi_cfg->delay_before_fod_hbm_on) {
			pr_info("delay before fod hbm on.\n");
		}

		mi_cfg->delay_after_fod_hbm_on = utils->read_bool(of_node,
			"mi,mdss-panel-delay-after-fod-hbm-on");
		if (mi_cfg->delay_after_fod_hbm_on) {
			pr_info("delay after fod hbm on.\n");
		}

		mi_cfg->delay_before_fod_hbm_off = utils->read_bool(of_node,
			"mi,mdss-panel-delay-before-fod-hbm-off");
		if (mi_cfg->delay_before_fod_hbm_off) {
			pr_info("delay before fod hbm off.\n");
		}

		mi_cfg->delay_after_fod_hbm_off = utils->read_bool(of_node,
			"mi,mdss-panel-delay-after-fod-hbm-off");
		if (mi_cfg->delay_after_fod_hbm_off) {
			pr_info("delay after fod hbm off.\n");
		}

		rc = utils->read_u32(of_node,
			"mi,mdss-dsi-dimlayer-brightness-alpha-lut-item-count",
			&mi_cfg->brightnes_alpha_lut_item_count);
		if (rc || mi_cfg->brightnes_alpha_lut_item_count <= 0) {
			pr_err("can't get brightnes_alpha_lut_item_count\n");
			mi_cfg->fod_dimlayer_enabled = false;
			goto skip_dimlayer_parse;
		}

		arr = utils->get_property(utils->data,
				"mi,mdss-dsi-dimlayer-brightness-alpha-lut", &length);

		length = length / sizeof(u32);

		pr_info("length: %d\n", length);
		if (!arr || length & 0x1 || length != mi_cfg->brightnes_alpha_lut_item_count * 2) {
			pr_err("read mi,mdss-dsi-dimlayer-brightness-alpha-lut failed\n");
			mi_cfg->fod_dimlayer_enabled = false;
			goto skip_dimlayer_parse;
		}

		mi_cfg->brightness_alpha_lut = (struct brightness_alpha *)kzalloc(length * sizeof(u32), GFP_KERNEL);
		if (!mi_cfg->brightness_alpha_lut) {
			pr_err("no memory for brightnes alpha lut\n");
			mi_cfg->fod_dimlayer_enabled = false;
			goto skip_dimlayer_parse;
		}

		rc = utils->read_u32_array(utils->data, "mi,mdss-dsi-dimlayer-brightness-alpha-lut",
			(u32 *)mi_cfg->brightness_alpha_lut, length);
		if (rc) {
			pr_err("cannot read mi,mdss-dsi-dimlayer-brightness-alpha-lut\n");
			mi_cfg->fod_dimlayer_enabled = false;
			kfree(mi_cfg->brightness_alpha_lut);
			goto skip_dimlayer_parse;
		}
	}

skip_dimlayer_parse:
	mi_cfg->disp_rate_gpio = utils->get_named_gpio(utils->data,
		"mi,mdss-dsi-panel-disp-rate-gpio",0);
	if (gpio_is_valid(mi_cfg->disp_rate_gpio)) {
		rc = gpio_request(mi_cfg->disp_rate_gpio, "disp_rate");
		if (rc) {
			pr_err("request for disp_rate gpio failed, rc=%d\n", rc);
		}
		rc = gpio_direction_output(mi_cfg->disp_rate_gpio, 0);
		if (rc) {
			pr_err("unable to set dir for disp_rate gpio rc=%d\n", rc);
		}
	} else {
		pr_info("panel disp_rate gpio not specified\n");
	}

	rc = utils->read_u32(of_node,
		"mi,mdss-panel-on-dimming-delay", &mi_cfg->panel_on_dimming_delay);
	if (rc) {
		mi_cfg->panel_on_dimming_delay = 0;
		pr_info("panel on dimming delay disabled\n");
	} else {
		pr_info("panel on dimming delay %d ms\n", mi_cfg->panel_on_dimming_delay);
	}

	if (mi_cfg->panel_on_dimming_delay)
		INIT_DELAYED_WORK(&mi_cfg->dimming_enable_delayed_work, panelon_dimming_enable_delayed_work);

	rc = utils->read_u32(of_node,
			"mi,disp-fod-off-dimming-delay", &mi_cfg->fod_off_dimming_delay);
	if (rc) {
		mi_cfg->fod_off_dimming_delay = DEFAULT_FOD_OFF_DIMMING_DELAY;
		pr_info("default fod_off_dimming_delay %d\n", DEFAULT_FOD_OFF_DIMMING_DELAY);
	} else {
		pr_info("fod_off_dimming_delay %d\n", mi_cfg->fod_off_dimming_delay);
	}

	mi_cfg->gamma_update_flag = utils->read_bool(utils->data,
			"mi,mdss-dsi-panel-gamma-update-flag");
	if (mi_cfg->gamma_update_flag) {
		pr_info("mi,mdss-dsi-panel-gamma-update-flag feature is defined\n");

		rc = dsi_panel_parse_gamma_config(panel, of_node);
		if (rc)
			pr_info("failed to parse gamma config\n");
	} else {
		pr_info("mi,mdss-dsi-panel-gamma-update-flag feature not defined\n");
	}

	mi_cfg->greenish_gamma_update_flag = utils->read_bool(utils->data,
			"mi,mdss-dsi-greenish-update-gamma-flag");
	if (mi_cfg->greenish_gamma_update_flag) {
		pr_info("mi,mdss-dsi-greenish-update-gamma-flag feature is defined\n");

		rc = dsi_panel_parse_greenish_gamma_config(panel, of_node);
		if (rc)
			pr_info("failed to parse greenish gamma config\n");
	} else {
		pr_info("mi,mdss-dsi-greenish-update-gamma-flag feature not defined\n");
	}

	mi_cfg->dc_update_flag = utils->read_bool(utils->data,
			"mi,mdss-dsi-panel-dc-update-flag");
	if (mi_cfg->dc_update_flag) {
		pr_info("mi,mdss-dsi-panel-dc-update-flag feature is defined\n");

		rc = utils->read_u32(of_node,
				"mi,mdss-dsi-panel-dc-update-d2-index",
				&mi_cfg->dc_cfg.update_d2_index);
		if (rc)
			pr_info("failed to parse dc config\n");
	} else {
		pr_info("mi,mdss-dsi-panel-dc-update-flag feature not defined\n");
	}

	mi_cfg->wp_read_enabled= utils->read_bool(utils->data,
				"mi,mdss-dsi-white-point-read-enabled");
	if (mi_cfg->wp_read_enabled) {
		rc = dsi_panel_parse_white_point_config(panel, of_node);
		if (rc)
			pr_info("failed to parse white point config\n");
	} else {
		pr_info("mi white point read not defined\n");
	}

	mi_cfg->elvss_dimming_check_enable = utils->read_bool(of_node,
			"mi,elvss_dimming_check_enable");
	if (mi_cfg->elvss_dimming_check_enable) {
		pr_info("mi,elvss_dimming_check_enable is defined\n");

		rc = dsi_panel_parse_elvss_dimming_config(panel, of_node);
		if (rc)
			pr_info("failed to parse elvss dimming config\n");
	} else {
		pr_info("mi,elvss_dimming_check_enable not defined\n");
	}

	rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-dc-threshold", &mi_cfg->dc_threshold);
	if (rc) {
		mi_cfg->dc_threshold = 440;
		pr_info("default dc backlight threshold is %d\n", mi_cfg->dc_threshold);
	} else {
		pr_info("dc backlight threshold %d \n", mi_cfg->dc_threshold);
	}

	rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-dc-type", &mi_cfg->dc_type);
	if (rc) {
		mi_cfg->dc_type = 1;
		pr_info("default dc backlight type is %d\n", mi_cfg->dc_type);
	} else {
		pr_info("dc backlight type %d \n", mi_cfg->dc_type);
	}
	if (mi_cfg->dc_type == 0 && mi_cfg->hbm_51_ctrl_flag) {
		rc = of_property_read_u32(of_node,
			"mi,mdss-dsi-panel-fod-on-b2-index", &mi_cfg->fod_on_b2_index);
		if (rc) {
			mi_cfg->fod_on_b2_index = 0;
			pr_info("mi,mdss-dsi-panel-fod-on-b2-index not defined\n");
		}
	}

	mi_cfg->hbm_enabled = false;
	mi_cfg->fod_hbm_enabled = false;
	mi_cfg->fod_hbm_layer_enabled = false;
	mi_cfg->doze_brightness_state = DOZE_TO_NORMAL;
	mi_cfg->unset_doze_brightness = DOZE_TO_NORMAL;
	mi_cfg->skip_dimmingon = STATE_NONE;
	mi_cfg->fod_backlight_flag = false;
	mi_cfg->fod_flag = false;
	mi_cfg->in_aod = false;
	mi_cfg->fod_hbm_off_time = ktime_get();
	mi_cfg->fod_backlight_off_time = ktime_get();
	mi_cfg->dc_enable = false;

	return rc;
}

void display_utc_time_marker(char *annotation)
{
	struct timespec ts;
	struct rtc_time tm;

	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
	pr_info("%s: %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		annotation, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
}

int dsi_panel_esd_irq_ctrl(struct dsi_panel *panel,
				bool enable)
{
	struct dsi_panel_mi_cfg *mi_cfg;
	struct irq_desc *desc;

	if (!panel || !panel->panel_initialized) {
		pr_err("Panel not ready!\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	if (gpio_is_valid(mi_cfg->esd_err_irq_gpio)) {
		if (mi_cfg->esd_err_irq) {
			if (enable) {
				if (!mi_cfg->esd_err_enabled) {
					desc = irq_to_desc(mi_cfg->esd_err_irq);
					if (!irq_settings_is_level(desc))
						desc->istate &= ~IRQS_PENDING;
					enable_irq_wake(mi_cfg->esd_err_irq);
					enable_irq(mi_cfg->esd_err_irq);
					mi_cfg->esd_err_enabled = true;
					pr_info("panel esd irq is enable\n");
				}
			} else {
				if (mi_cfg->esd_err_enabled) {
					disable_irq_wake(mi_cfg->esd_err_irq);
					disable_irq_nosync(mi_cfg->esd_err_irq);
					mi_cfg->esd_err_enabled = false;
					pr_info("panel esd irq is disable\n");
				}
			}
		}
	} else {
		pr_info("panel esd irq gpio invalid\n");
	}

	mutex_unlock(&panel->panel_lock);
	return 0;
}


int dsi_panel_update_elvss_dimming(struct dsi_panel *panel)
{
	int rc = 0;
	int retval = 0;
	struct dsi_panel_mi_cfg *mi_cfg;
	struct dsi_read_config elvss_dimming_read;
	struct dsi_panel_cmd_set *cmd_set;
	struct dsi_cmd_desc *cmds;
	struct dsi_display_mode_priv_info *priv_info;
	u32 count;
	u8 *tx_buf;

	if (!panel || !panel->cur_mode) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	mi_cfg = &panel->mi_cfg;
	if (!mi_cfg->elvss_dimming_check_enable) {
		pr_debug("elvss_dimming_check_enable not defined, return\n");
		return 0;
	}

	mutex_lock(&panel->panel_lock);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ELVSS_DIMMING_OFFSET);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_ELVSS_DIMMING_OFFSET command\n");
		retval = -EAGAIN;
		goto error;
	}

	priv_info = panel->cur_mode->priv_info;
	cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_ELVSS_DIMMING_READ];
	elvss_dimming_read.read_cmd = *cmd_set;
	elvss_dimming_read.cmds_rlen = mi_cfg->elvss_dimming_read_len;
	elvss_dimming_read.is_read = 1;

	rc = dsi_panel_read_cmd_set(panel, &elvss_dimming_read);
	if (rc <= 0) {
		pr_err("[%s]failed to read elvss_dimming, rc=%d\n", panel->name, rc);
		retval = -EAGAIN;
	} else {
		pr_info("elvss dimming read result %x\n", elvss_dimming_read.rbuf[0]);
		cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_ON].cmds;
		count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_ON].count;
		if (cmds && count >= mi_cfg->update_hbm_fod_on_index) {
			tx_buf = (u8 *)cmds[mi_cfg->update_hbm_fod_on_index].msg.tx_buf;
			tx_buf[1] = elvss_dimming_read.rbuf[0] & 0x7F;
		}
		cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].cmds;
		count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].count;
		if (cmds && count >= mi_cfg->update_hbm_fod_off_index) {
			tx_buf = (u8 *)cmds[mi_cfg->update_hbm_fod_off_index].msg.tx_buf;
			tx_buf[1] = elvss_dimming_read.rbuf[0] & 0x7F;
		}
		retval = 0;
	}
error:
	mutex_unlock(&panel->panel_lock);
	return retval;
}

int dsi_panel_read_greenish_gamma_setting(struct dsi_panel *panel)
{
	int rc =0;
	int retval = 0;
	int i = 0;
	int param_count = 0;
	u32 count = 0;
	u32 offset = 0;
	struct dsi_panel_mi_cfg *mi_cfg;
	struct greenish_gamma_cfg *greenish_gamma_cfg;
	struct dsi_panel_cmd_set *cmd_set;
	struct dsi_cmd_desc *cmds;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_read_config greenish_gamma_read;
	u8 *tx_buf;

	if (!panel || !panel->cur_mode) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	mi_cfg = &panel->mi_cfg;
	greenish_gamma_cfg = &mi_cfg->greenish_gamma_cfg;

	if (!mi_cfg->greenish_gamma_update_flag) {
		pr_debug("greenish_gamma_update_flag not defined, return\n");
		return 0;
	}


	mutex_lock(&panel->panel_lock);

	priv_info = panel->cur_mode->priv_info;

	/* level2-key-enable */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LEVEL2_KEY_ENABLE);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_LEVEL2_KEY_ENABLE command\n");
		retval = -EAGAIN;
		goto error;
	}

	/* pre read command */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_PRE_READ);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_PRE_READ command\n");
		retval = -EAGAIN;
		goto error;
	}

	/* read 1st~6th param */
	offset = greenish_gamma_cfg->greenish_gamma_update_offset;
	param_count = greenish_gamma_cfg->greenish_gamma_update_param_count;
	for(i = 1; i <= param_count; i++) {
		switch (i) {
		case 1:
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_READ_1ST_PRE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_READ_1ST_PRE command\n");
				retval = -EAGAIN;
				goto error;
			}

			cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_READ_B7];
			greenish_gamma_read.read_cmd = *cmd_set;
			greenish_gamma_read.cmds_rlen = mi_cfg->greenish_gamma_read_len;
			greenish_gamma_read.is_read = 1;

			rc = dsi_panel_read_cmd_set(panel, &greenish_gamma_read);
			if (rc <= 0) {
				pr_err("[%s]failed to read 1st greenish_gamma, rc=%d\n", panel->name, rc);
				retval = -EAGAIN;
			} else {
				pr_info("greenish gamma read result 1st para %x, %x\n",
					greenish_gamma_read.rbuf[0], greenish_gamma_read.rbuf[1]);
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].count;
				if (cmds && count >= greenish_gamma_cfg->index_1st_param) {
					tx_buf = (u8 *)cmds[greenish_gamma_cfg->index_1st_param].msg.tx_buf;
					if (greenish_gamma_read.rbuf[1] >= (u8)offset) {
						tx_buf[1] = greenish_gamma_read.rbuf[0];
						tx_buf[2] = greenish_gamma_read.rbuf[1] - (u8)offset;
					} else {
						tx_buf[1] = greenish_gamma_read.rbuf[0] - (u8)0x1;
						tx_buf[2] = greenish_gamma_read.rbuf[1] + (u8)0x100 - (u8)offset;
					}
					pr_info("greenish gamma set result 1st para %x, %x\n", tx_buf[1], tx_buf[2]);
				}
				retval = 0;
			}
			break;
		case 2:
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_READ_2ND_PRE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_READ_2ND_PRE command\n");
				retval = -EAGAIN;
				goto error;
			}

			cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_READ_B7];
			greenish_gamma_read.read_cmd = *cmd_set;
			greenish_gamma_read.cmds_rlen = mi_cfg->greenish_gamma_read_len;
			greenish_gamma_read.is_read = 1;

			rc = dsi_panel_read_cmd_set(panel, &greenish_gamma_read);
			if (rc <= 0) {
				pr_err("[%s]failed to read 2nd greenish_gamma, rc=%d\n", panel->name, rc);
				retval = -EAGAIN;
			} else {
				pr_info("greenish gamma read result 2nd para %x, %x\n",
					greenish_gamma_read.rbuf[0], greenish_gamma_read.rbuf[1]);
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].count;
				if (cmds && count >= greenish_gamma_cfg->index_2nd_param) {
					tx_buf = (u8 *)cmds[greenish_gamma_cfg->index_2nd_param].msg.tx_buf;
					if (greenish_gamma_read.rbuf[1] >= (u8)offset) {
						tx_buf[1] = greenish_gamma_read.rbuf[0];
						tx_buf[2] = greenish_gamma_read.rbuf[1] - (u8)offset;
					} else {
						tx_buf[1] = greenish_gamma_read.rbuf[0] - (u8)0x1;
						tx_buf[2] = greenish_gamma_read.rbuf[1] + (u8)0x100 - (u8)offset;
					}
					pr_info("greenish gamma set result 2nd para %x, %x\n", tx_buf[1], tx_buf[2]);
				}
				retval = 0;
			}
			break;
		case 3:
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_READ_3RD_PRE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_READ_3RD_PRE command\n");
				retval = -EAGAIN;
				goto error;
			}

			cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_READ_B7];
			greenish_gamma_read.read_cmd = *cmd_set;
			greenish_gamma_read.cmds_rlen = mi_cfg->greenish_gamma_read_len;
			greenish_gamma_read.is_read = 1;

			rc = dsi_panel_read_cmd_set(panel, &greenish_gamma_read);
			if (rc <= 0) {
				pr_err("[%s]failed to read 3rd greenish_gamma, rc=%d\n", panel->name, rc);
				retval = -EAGAIN;
			} else {
				pr_info("greenish gamma read result 3rd para %x, %x\n",
					greenish_gamma_read.rbuf[0], greenish_gamma_read.rbuf[1]);
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].count;
				if (cmds && count >= greenish_gamma_cfg->index_3rd_param) {
					tx_buf = (u8 *)cmds[greenish_gamma_cfg->index_3rd_param].msg.tx_buf;
					if (greenish_gamma_read.rbuf[1] >= (u8)offset) {
						tx_buf[1] = greenish_gamma_read.rbuf[0];
						tx_buf[2] = greenish_gamma_read.rbuf[1] - (u8)offset;
					} else {
						tx_buf[1] = greenish_gamma_read.rbuf[0] - (u8)0x1;
						tx_buf[2] = greenish_gamma_read.rbuf[1] + (u8)0x100 - (u8)offset;
					}
					pr_info("greenish gamma set result 3rd para %x, %x\n", tx_buf[1], tx_buf[2]);
				}
				retval = 0;
			}
			break;
		case 4:
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_READ_4TH_PRE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_READ_4TH_PRE command\n");
				retval = -EAGAIN;
				goto error;
			}

			cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_READ_B7];
			greenish_gamma_read.read_cmd = *cmd_set;
			greenish_gamma_read.cmds_rlen = mi_cfg->greenish_gamma_read_len;
			greenish_gamma_read.is_read = 1;

			rc = dsi_panel_read_cmd_set(panel, &greenish_gamma_read);
			if (rc <= 0) {
				pr_err("[%s]failed to read 4th greenish_gamma, rc=%d\n", panel->name, rc);
				retval = -EAGAIN;
			} else {
				pr_info("greenish gamma read result 4th para %x, %x\n",
					greenish_gamma_read.rbuf[0], greenish_gamma_read.rbuf[1]);
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].count;
				if (cmds && count >= greenish_gamma_cfg->index_4th_param) {
					tx_buf = (u8 *)cmds[greenish_gamma_cfg->index_4th_param].msg.tx_buf;
					if (greenish_gamma_read.rbuf[1] >= (u8)offset) {
						tx_buf[1] = greenish_gamma_read.rbuf[0];
						tx_buf[2] = greenish_gamma_read.rbuf[1] - (u8)offset;
					} else {
						tx_buf[1] = greenish_gamma_read.rbuf[0] - (u8)0x1;
						tx_buf[2] = greenish_gamma_read.rbuf[1] + (u8)0x100 - (u8)offset;
					}
					pr_info("greenish gamma set result 4th para %x, %x\n", tx_buf[1], tx_buf[2]);
				}
				retval = 0;
			}
			break;
		case 5:
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_READ_5TH_PRE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_READ_5TH_PRE command\n");
				retval = -EAGAIN;
				goto error;
			}

			cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_READ_B7];
			greenish_gamma_read.read_cmd = *cmd_set;
			greenish_gamma_read.cmds_rlen = mi_cfg->greenish_gamma_read_len;
			greenish_gamma_read.is_read = 1;

			rc = dsi_panel_read_cmd_set(panel, &greenish_gamma_read);
			if (rc <= 0) {
				pr_err("[%s]failed to read 5th greenish_gamma, rc=%d\n", panel->name, rc);
				retval = -EAGAIN;
			} else {
				pr_info("greenish gamma read result 5th para %x, %x\n",
					greenish_gamma_read.rbuf[0], greenish_gamma_read.rbuf[1]);
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].count;
				if (cmds && count >= greenish_gamma_cfg->index_5th_param) {
					tx_buf = (u8 *)cmds[greenish_gamma_cfg->index_5th_param].msg.tx_buf;
					if (greenish_gamma_read.rbuf[1] >= (u8)offset) {
						tx_buf[1] = greenish_gamma_read.rbuf[0];
						tx_buf[2] = greenish_gamma_read.rbuf[1] - (u8)offset;
					} else {
						tx_buf[1] = greenish_gamma_read.rbuf[0] - (u8)0x1;
						tx_buf[2] = greenish_gamma_read.rbuf[1] + (u8)0x100 - (u8)offset;
					}
					pr_info("greenish gamma set result 5th para %x, %x\n", tx_buf[1], tx_buf[2]);
				}
				retval = 0;
			}
			break;
		case 6:
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_READ_6TH_PRE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_READ_6TH_PRE command\n");
				retval = -EAGAIN;
				goto error;
			}

			cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_READ_B7];
			greenish_gamma_read.read_cmd = *cmd_set;
			greenish_gamma_read.cmds_rlen = mi_cfg->greenish_gamma_read_len;
			greenish_gamma_read.is_read = 1;

			rc = dsi_panel_read_cmd_set(panel, &greenish_gamma_read);
			if (rc <= 0) {
				pr_err("[%s]failed to read 6th greenish_gamma, rc=%d\n", panel->name, rc);
				retval = -EAGAIN;
			} else {
				pr_info("greenish gamma read result 6th para %x, %x\n",
					greenish_gamma_read.rbuf[0], greenish_gamma_read.rbuf[1]);
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].count;
				if (cmds && count >= greenish_gamma_cfg->index_6th_param) {
					tx_buf = (u8 *)cmds[greenish_gamma_cfg->index_6th_param].msg.tx_buf;
					if (greenish_gamma_read.rbuf[1] >= (u8)offset) {
						tx_buf[1] = greenish_gamma_read.rbuf[0];
						tx_buf[2] = greenish_gamma_read.rbuf[1] - (u8)offset;
					} else {
						tx_buf[1] = greenish_gamma_read.rbuf[0] - (u8)0x1;
						tx_buf[2] = greenish_gamma_read.rbuf[1] + (u8)0x100 - (u8)offset;
					}
					pr_info("greenish gamma set result 6th para %x, %x\n", tx_buf[1], tx_buf[2]);
				}
				retval = 0;
			}
			break;
		default:
			break;
		}
	}

	/* level2-key-disable */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LEVEL2_KEY_DISABLE);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_LEVEL2_KEY_DISABLE command\n");
		retval = -EAGAIN;
		goto error;
	} else {
		greenish_gamma_cfg->gamma_update_done = true;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return retval;
}

int dsi_panel_update_greenish_gamma_setting(struct dsi_panel *panel)
{
	int rc =0;
	int retval = 0;
	int i = 0;
	struct dsi_panel_mi_cfg *mi_cfg;
	struct dsi_cmd_desc *cmds;
	struct dsi_display_mode_priv_info *priv_info;
	u8 *tx_buf;

	if (!panel || !panel->cur_mode) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	mi_cfg = &panel->mi_cfg;

	if (!mi_cfg->greenish_gamma_update_flag || !mi_cfg->greenish_gamma_cfg.gamma_update_done) {
		pr_debug("greenish_gamma_update_flag not defined or gamma update has not completed, return\n");
		return 0;
	}

	mutex_lock(&panel->panel_lock);

	priv_info = panel->cur_mode->priv_info;
	cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_B7].cmds;

	/* greenish gamma seeting cmd */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_B7);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_B7 command\n");
		retval = -EAGAIN;
		goto error;
	} else {
		pr_info("greenish gamma DSI_CMD_SET_MI_GAMMA_B7 set is\n");
		for (i = 0; i <= 15; i++) {
			tx_buf = (u8 *)cmds[i].msg.tx_buf;
			pr_info("DSI_CMD_SET_MI_GAMMA_B7 %d line tx_buf %x %x\n", i,
				tx_buf[0], tx_buf[1]);
		}
	}

error:
	mutex_unlock(&panel->panel_lock);
	return retval;
}

static int dsi_panel_read_gamma_opt_and_flash(struct dsi_panel *panel,
				struct dsi_display_ctrl *ctrl)
{
	int rc = 0;
	int retval = 0;
	int i = 0;
	int retry_cnt = 0;
	u16 flags = 0;
	struct dsi_display_mode *mode;
	struct gamma_cfg *gamma_cfg;
	struct dsi_cmd_desc *cmds;
	enum dsi_cmd_set_state state;
	u32 count = 0;
	u32 param_index = 0;
	u8 read_param_buf[200] = {0};
	u8 read_fb_buf[16] = {0};
	u8 *tx_buf;
	bool checksum_pass = 0;

	if (!panel || !ctrl) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mode = panel->cur_mode;
	gamma_cfg = &panel->mi_cfg.gamma_cfg;

	/* OTP Read 60hz gamma parameter */
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LEVEL2_KEY_ENABLE);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_LEVEL2_KEY_ENABLE command\n");
		retval = -EAGAIN;
		goto error;
	}

	pr_debug("Gamma 0xC8 OPT Read 135 Parameter (60Hz)\n");
	flags = 0;
	memset(read_param_buf, 0, sizeof(read_param_buf));
	cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_OTP_READ_C8].cmds;
	state = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_OTP_READ_C8].state;
	if (state == DSI_CMD_SET_STATE_LP)
		cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;
	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ | DSI_CTRL_CMD_CUSTOM_DMA_SCHED);
	cmds->msg.rx_buf = read_param_buf;
	cmds->msg.rx_len = sizeof(gamma_cfg->otp_read_c8);
	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &cmds->msg, flags);
	if (rc <= 0) {
		pr_err("Failed to read DSI_CMD_SET_MI_GAMMA_OTP_READ_C8\n");
		retval = -EAGAIN;
		goto error;
	}
	memcpy(gamma_cfg->otp_read_c8, cmds->msg.rx_buf, sizeof(gamma_cfg->otp_read_c8));

	pr_debug("Gamma 0xC9 OPT Read 180 Parameter (60Hz)\n");
	flags = 0;
	memset(read_param_buf, 0, sizeof(read_param_buf));
	cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_OTP_READ_C9].cmds;
	state = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_OTP_READ_C9].state;
	if (state == DSI_CMD_SET_STATE_LP)
		cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;
	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ | DSI_CTRL_CMD_CUSTOM_DMA_SCHED);
	cmds->msg.rx_buf = read_param_buf;
	cmds->msg.rx_len = sizeof(gamma_cfg->otp_read_c9);
	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &cmds->msg, flags);
	if (rc <= 0) {
		pr_err("Failed to read DSI_CMD_SET_MI_GAMMA_OTP_READ_C9\n");
		retval = -EAGAIN;
		goto error;
	}
	memcpy(gamma_cfg->otp_read_c9, cmds->msg.rx_buf, sizeof(gamma_cfg->otp_read_c9));

#if 0
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_OTP_READ_B3_PRE);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_OTP_READ_B3_PRE command\n");
		goto error;
	}
#endif

	pr_debug("Gamma 0xB3 OTP Read 45 Parameter (60Hz)\n");
	flags = 0;
	memset(read_param_buf, 0, sizeof(read_param_buf));
	cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_OTP_READ_B3].cmds;
	state = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_OTP_READ_B3].state;
	if (state == DSI_CMD_SET_STATE_LP)
		cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;
	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ | DSI_CTRL_CMD_CUSTOM_DMA_SCHED);
	cmds->msg.rx_buf = read_param_buf;
	cmds->msg.rx_len = sizeof(gamma_cfg->otp_read_b3) + 2;
	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &cmds->msg, flags);
	if (rc <= 0) {
		pr_err("Failed to read DSI_CMD_SET_MI_GAMMA_OTP_READ_B3\n");
		retval = -EAGAIN;
		goto error;
	}
	memcpy(gamma_cfg->otp_read_b3, &read_param_buf[2], sizeof(gamma_cfg->otp_read_b3));

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LEVEL2_KEY_DISABLE);
	if (rc) {
		pr_err("Failed to send DSI_CMD_SET_MI_LEVEL2_KEY_DISABLE command\n");
		retval = -EAGAIN;
		goto error;
	}
	pr_info("OTP Read 60hz gamma done\n");

	/* Flash Read 90hz gamma parameter */
	do {
		gamma_cfg->gamma_checksum = 0;

		if (retry_cnt > 0) {
			pr_err("Failed to flash read 90hz gamma parameters, retry_cnt = %d\n",
					retry_cnt);
			mdelay(80);
		}

		for(i = 0; i < gamma_cfg->flash_read_total_param; i++)
		{
			cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_FLASH_READ_PRE].cmds;
			count = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_FLASH_READ_PRE].count;
			tx_buf = (u8 *)cmds[gamma_cfg->flash_read_c1_index].msg.tx_buf;
			if (cmds && count >= gamma_cfg->flash_read_c1_index) {
				tx_buf[2] = i >> 8;
				tx_buf[3] = i & 0xFF;
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_GAMMA_FLASH_READ_PRE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_GAMMA_FLASH_READ_PRE command\n");
				retval = -EAGAIN;
				goto error;
			}

			/* 0xFB Read 2 Parameter */
			flags = 0;
			cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_FLASH_READ_FB].cmds;
			state = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_GAMMA_FLASH_READ_FB].state;
			if (state == DSI_CMD_SET_STATE_LP)
				cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;
			if (cmds->last_command) {
				cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
				flags |= DSI_CTRL_CMD_LAST_COMMAND;
			}
			flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ |
						DSI_CTRL_CMD_CUSTOM_DMA_SCHED);
			cmds->msg.rx_buf = read_fb_buf;
			cmds->msg.rx_len = 2;
			rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &cmds->msg, flags);
			if (rc <= 0) {
				pr_err("Failed to read DSI_CMD_SET_MI_GAMMA_FLASH_READ_FB\n");
				retval = -EAGAIN;
				goto error;
			}

			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LEVEL2_KEY_DISABLE);
			if (rc) {
				pr_err("Failed to send DSI_CMD_SET_MI_LEVEL2_KEY_DISABLE command\n");
				retval = -EAGAIN;
				goto error;
			}

			if (i < sizeof(gamma_cfg->flash_read_c8)) {
				gamma_cfg->flash_read_c8[i] = read_fb_buf[1];
			}
			else if (i < (sizeof(gamma_cfg->flash_read_c8) +
						sizeof(gamma_cfg->flash_read_c9))) {
				param_index = i - sizeof(gamma_cfg->flash_read_c8);
				gamma_cfg->flash_read_c9[param_index] = read_fb_buf[1];
			}
			else if (i < (sizeof(gamma_cfg->flash_read_c8) +
					sizeof(gamma_cfg->flash_read_c9) +
					sizeof(gamma_cfg->flash_read_b3))) {
				param_index = i - (sizeof(gamma_cfg->flash_read_c8) +
								sizeof(gamma_cfg->flash_read_c9));
				gamma_cfg->flash_read_b3[param_index] = read_fb_buf[1];
			}

			if (i < (gamma_cfg->flash_read_total_param - 2)) {
				gamma_cfg->gamma_checksum = read_fb_buf[1] + gamma_cfg->gamma_checksum;
			} else {
				if (i == (gamma_cfg->flash_read_total_param - 2))
					gamma_cfg->flash_read_checksum[0] = read_fb_buf[1];
				if (i == (gamma_cfg->flash_read_total_param - 1))
					gamma_cfg->flash_read_checksum[1] = read_fb_buf[1];
			}
		}
		if (gamma_cfg->gamma_checksum == ((gamma_cfg->flash_read_checksum[0] << 8)
				+ gamma_cfg->flash_read_checksum[1])) {
			checksum_pass = 1;
			pr_info("Flash Read 90hz gamma done\n");
		} else {
			checksum_pass = 0;
		}
		retry_cnt++;
	}
	while (!checksum_pass && (retry_cnt < 5));

	if (checksum_pass) {
		gamma_cfg->read_done = 1;
		pr_info("Gamma read done\n");
		retval = 0;
	} else {
		pr_err("Failed to flash read 90hz gamma\n");
		retval = -EAGAIN;
	}

error:
	return retval;

}

int dsi_panel_read_gamma_param(struct dsi_panel *panel)
{
	int rc = 0, ret = 0;
	struct dsi_display *display;
	struct dsi_display_ctrl *ctrl;

	if (!panel || !panel->host) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	if (!panel->mi_cfg.gamma_update_flag) {
		pr_debug("gamma_update_flag is not configed\n");
		return 0;
	}

	display = to_dsi_display(panel->host);
	if (display == NULL)
		return -EINVAL;

	if (!panel->panel_initialized) {
		pr_err("Panel not initialized\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	if (panel->mi_cfg.gamma_cfg.read_done) {
		pr_info("Gamma parameter have read and stored at POWER ON sequence\n");
		goto unlock;
	}

	rc = dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_ON);
	if (rc) {
		pr_err("[%s] failed to enable DSI clocks, rc=%d\n", display->name, rc);
		goto unlock;
	}

	ctrl = &display->ctrl[display->cmd_master_idx];

	rc = dsi_display_cmd_engine_enable(display);
	if (rc) {
		pr_err("[%s] failed to enable cmd engine, rc=%d\n",
		       display->name, rc);
		goto error_disable_clks;
	}

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			pr_err("failed to allocate cmd tx buffer memory\n");
			goto error_disable_cmd_engine;
		}
	}

	rc = dsi_panel_read_gamma_opt_and_flash(panel, ctrl);
	if (rc) {
		pr_err("[%s]failed to get gamma parameter, rc=%d\n",
		       display->name, rc);
		goto error_disable_cmd_engine;
	}

error_disable_cmd_engine:
	ret = dsi_display_cmd_engine_disable(display);
	if (ret) {
		pr_err("[%s]failed to disable DSI cmd engine, rc=%d\n",
				display->name, ret);
	}
error_disable_clks:
	ret = dsi_display_clk_ctrl(display->dsi_clk_handle,
			DSI_ALL_CLKS, DSI_CLK_OFF);
	if (ret) {
		pr_err("[%s] failed to disable all DSI clocks, rc=%d\n",
		       display->name, ret);
	}
unlock:
	mutex_unlock(&panel->panel_lock);

	return rc;
}

ssize_t dsi_panel_print_gamma_param(struct dsi_panel *panel,
				char *buf)
{
	int i = 0;
	ssize_t count = 0;
	struct gamma_cfg *gamma_cfg;
	u8 *buffer = NULL;

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	if (!panel->mi_cfg.gamma_update_flag) {
		pr_err("gamma_update_flag is not configed\n");
		return -EINVAL;
	}

	gamma_cfg = &panel->mi_cfg.gamma_cfg;
	if (!gamma_cfg->read_done) {
		pr_info("Gamma parameter not read at POWER ON sequence\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);

	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma 0xC8 OPT Read %d Parameter (60Hz)\n",
				sizeof(gamma_cfg->otp_read_c8));
	buffer = gamma_cfg->otp_read_c8;
	for (i = 1; i <= sizeof(gamma_cfg->otp_read_c8); i++) {
		if (i%8 && (i != sizeof(gamma_cfg->otp_read_c8))) {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X,",
				gamma_cfg->otp_read_c8[i - 1]);
		} else {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X\n",
				gamma_cfg->otp_read_c8[i - 1]);
		}
	}

	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma 0xC9 OPT Read %d Parameter (60Hz)\n",
				sizeof(gamma_cfg->otp_read_c9));
	buffer = gamma_cfg->otp_read_c9;
	for (i = 1; i <= sizeof(gamma_cfg->otp_read_c9); i++) {
		if (i%8 && (i != sizeof(gamma_cfg->otp_read_c9))) {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X,",
				gamma_cfg->otp_read_c9[i - 1]);
		} else {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X\n",
				gamma_cfg->otp_read_c9[i - 1]);
		}
	}

	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma 0xB3 OPT Read %d Parameter (60Hz)\n",
				sizeof(gamma_cfg->otp_read_b3));
	buffer = gamma_cfg->otp_read_b3;
	for (i = 1; i <= sizeof(gamma_cfg->otp_read_b3); i++) {
		if (i%8 && (i != sizeof(gamma_cfg->otp_read_b3))) {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X,",
				gamma_cfg->otp_read_b3[i - 1]);
		} else {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X\n",
				gamma_cfg->otp_read_b3[i - 1]);
		}
	}

	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma Flash 0xC8 Read %d Parameter (90Hz)\n",
				sizeof(gamma_cfg->flash_read_c8));
	buffer = gamma_cfg->flash_read_c8;
	for (i = 1; i <= sizeof(gamma_cfg->flash_read_c8); i++) {
		if (i%8 && (i != sizeof(gamma_cfg->flash_read_c8))) {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X,",
				gamma_cfg->flash_read_c8[i - 1]);
		} else {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X\n",
				gamma_cfg->flash_read_c8[i - 1]);
		}
	}

	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma Flash 0xC9 Read %d Parameter (90Hz)\n",
				sizeof(gamma_cfg->flash_read_c9));
	buffer = gamma_cfg->flash_read_c9;
	for (i = 1; i <= sizeof(gamma_cfg->flash_read_c9); i++) {
		if (i%8 && (i != sizeof(gamma_cfg->flash_read_c9))) {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X,",
				gamma_cfg->flash_read_c9[i - 1]);
		} else {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X\n",
				gamma_cfg->flash_read_c9[i - 1]);
		}
	}

	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma Flash 0xB3 Read %d Parameter (90Hz)\n",
				sizeof(gamma_cfg->flash_read_b3));
	buffer = gamma_cfg->flash_read_b3;
	for (i = 1; i <= sizeof(gamma_cfg->flash_read_b3); i++) {
		if (i%8 && (i != sizeof(gamma_cfg->flash_read_b3))) {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X,",
				gamma_cfg->flash_read_b3[i - 1]);
		} else {
			count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X\n",
				gamma_cfg->flash_read_b3[i - 1]);
		}
	}

	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma Flash Read Checksum Decimal(%d) (90Hz)\n",
				((gamma_cfg->flash_read_checksum[0] << 8) +
				gamma_cfg->flash_read_checksum[1]));
	count += snprintf(buf + count, PAGE_SIZE - count,
				"Gamma Flash Read 450 Parameter SUM(%d) (90Hz)\n",
				gamma_cfg->gamma_checksum);

	mutex_unlock(&panel->panel_lock);

	return count;
}

int dsi_panel_update_gamma_param(struct dsi_panel *panel)
{
	struct dsi_display *display;
	struct dsi_display_mode *mode;
	struct gamma_cfg *gamma_cfg;
	struct dsi_cmd_desc *cmds;
	int total_modes;
	u32 i, count;
	u8 *tx_buf;
	size_t tx_len;
	u32 param_len;
	int rc;

	if (!panel || !panel->host)
		return -EINVAL;

	display = to_dsi_display(panel->host);
	if (!display)
		return -EINVAL;

	if (!panel->mi_cfg.gamma_update_flag) {
		pr_debug("gamma_update_flag is not configed\n");
		return 0;
	}

	gamma_cfg = &panel->mi_cfg.gamma_cfg;
	if (!gamma_cfg->read_done) {
		pr_err("gamma parameter not ready\n");
		pr_err("gamma parameter should be read and stored at POWER ON sequence\n");
		return -EAGAIN;
	}

	if (!display->modes) {
		rc = dsi_display_get_modes(display, &mode);
		if (rc) {
			pr_err("failed to get display mode for update gamma parameter\n");
			return rc;
		}
	}

	memset(gamma_cmd_set, 0, 2 * sizeof(struct dsi_panel_cmd_set));

	mutex_lock(&display->display_lock);
	total_modes = panel->num_display_modes;
	for (i = 0; i < total_modes; i++) {
		mode = &display->modes[i];
		if (60 == mode->timing.refresh_rate && !gamma_cfg->update_done_60hz) {
			pr_info("Update GAMMA Parameter (60Hz)\n");
			cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_TIMING_SWITCH].cmds;
			count = mode->priv_info->cmd_sets[DSI_CMD_SET_TIMING_SWITCH].count;
			if (cmds && count >= gamma_cfg->update_c8_index &&
				count >= gamma_cfg->update_c9_index &&
				count >= gamma_cfg->update_b3_index) {
				tx_buf = (u8 *)cmds[gamma_cfg->update_c8_index].msg.tx_buf;
				tx_len = cmds[gamma_cfg->update_c8_index].msg.tx_len;
				param_len = min(sizeof(gamma_cfg->otp_read_c8), tx_len - 1);
				memcpy(&tx_buf[1], gamma_cfg->otp_read_c8, param_len);

				tx_buf = (u8 *)cmds[gamma_cfg->update_c9_index].msg.tx_buf;
				tx_len = cmds[gamma_cfg->update_c9_index].msg.tx_len;
				param_len = min(sizeof(gamma_cfg->otp_read_c9), tx_len - 1);
				memcpy(&tx_buf[1], gamma_cfg->otp_read_c9, param_len);

				tx_buf = (u8 *)cmds[gamma_cfg->update_b3_index].msg.tx_buf;
				tx_len = cmds[gamma_cfg->update_b3_index].msg.tx_len;
				param_len = min(sizeof(gamma_cfg->otp_read_b3), tx_len - 1);
				memcpy(&tx_buf[1], gamma_cfg->otp_read_b3, param_len);

				memcpy(&gamma_cmd_set[DSI_CMD_SET_MI_GAMMA_SWITCH_60HZ],
						&mode->priv_info->cmd_sets[DSI_CMD_SET_TIMING_SWITCH],
						sizeof(struct dsi_panel_cmd_set));

				gamma_cfg->update_done_60hz = true;
			} else {
				pr_err("please check gamma update parameter index configuration\n");
			}
		}
		if (90 == mode->timing.refresh_rate && !gamma_cfg->update_done_90hz) {
			pr_info("Update GAMMA Parameter (90Hz)\n");
			cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_TIMING_SWITCH].cmds;
			count = mode->priv_info->cmd_sets[DSI_CMD_SET_TIMING_SWITCH].count;
			if (cmds && count >= gamma_cfg->update_c8_index &&
				count >= gamma_cfg->update_c9_index &&
				count >= gamma_cfg->update_b3_index) {
				tx_buf = (u8 *)cmds[gamma_cfg->update_c8_index].msg.tx_buf;
				tx_len = cmds[gamma_cfg->update_c8_index].msg.tx_len;
				param_len = min(sizeof(gamma_cfg->flash_read_c8), tx_len - 1);
				memcpy(&tx_buf[1], gamma_cfg->flash_read_c8, param_len);

				tx_buf = (u8 *)cmds[gamma_cfg->update_c9_index].msg.tx_buf;
				tx_len = cmds[gamma_cfg->update_c9_index].msg.tx_len;
				param_len = min(sizeof(gamma_cfg->flash_read_c9), tx_len - 1);
				memcpy(&tx_buf[1], gamma_cfg->flash_read_c9, param_len);

				tx_buf = (u8 *)cmds[gamma_cfg->update_b3_index].msg.tx_buf;
				tx_len = cmds[gamma_cfg->update_b3_index].msg.tx_len;
				param_len = min(sizeof(gamma_cfg->flash_read_b3), tx_len - 1);
				memcpy(&tx_buf[1], gamma_cfg->flash_read_b3, param_len);

				memcpy(&gamma_cmd_set[DSI_CMD_SET_MI_GAMMA_SWITCH_90HZ],
						&mode->priv_info->cmd_sets[DSI_CMD_SET_TIMING_SWITCH],
						sizeof(struct dsi_panel_cmd_set));

				gamma_cfg->update_done_90hz = true;
			} else {
				pr_err("please check gamma update parameter index configuration\n");
			}
		}
	}
	mutex_unlock(&display->display_lock);

	return 0;
}

int dsi_panel_read_dc_param(struct dsi_panel *panel)
{
	int rc = 0;
	int retval = 0;
	struct dsi_read_config dc_read;
	struct dc_cfg *dc_cfg;
	struct dsi_panel_cmd_set *cmd_set;
	struct dsi_display_mode_priv_info *priv_info;
	int i, j;
	int retry_cnt = 0;
	u32 checksum1 = 0, checksum2 = 0;

	if (!panel || !panel->cur_mode) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	dc_cfg = &panel->mi_cfg.dc_cfg;
	if (!panel->mi_cfg.dc_update_flag) {
		pr_debug("dc_update_flag is not configed\n");
		return 0;
	}

	mutex_lock(&panel->panel_lock);

	for (retry_cnt = 0; retry_cnt < 5; retry_cnt++) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SWITCH_PAGE4);
		if (rc) {
			pr_err("Failed to send DSI_CMD_SET_MI_SWITCH_PAGE4 command\n");
			retval = -EAGAIN;
			goto error;
		}

		priv_info = panel->cur_mode->priv_info;
		cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_DC_READ];
		dc_read.read_cmd = *cmd_set;
		dc_read.cmds_rlen = sizeof(dc_cfg->exit_dc_lut);
		dc_read.is_read = 1;

		rc = dsi_panel_read_cmd_set(panel, &dc_read);
		if (rc <= 0) {
			pr_err("[%s]failed to read dc, rc=%d\n", panel->name, rc);
			retval = -EAGAIN;
			goto error;
		} else {
			memcpy(dc_cfg->exit_dc_lut, dc_read.rbuf, sizeof(dc_cfg->exit_dc_lut));
			for(i = 0; i < sizeof(dc_cfg->exit_dc_lut); i++)
				checksum1 += dc_cfg->exit_dc_lut[i];
		}

		rc = dsi_panel_read_cmd_set(panel, &dc_read);
		if (rc <= 0) {
			pr_err("[%s]failed to read dc, rc=%d\n", panel->name, rc);
			retval = -EAGAIN;
			goto error;
		} else {
			for(i = 0; i < sizeof(dc_cfg->exit_dc_lut); i++)
				checksum2 += dc_read.rbuf[i];
		}

		if (checksum1 == checksum2) {
			dc_cfg->read_done = true;
			break;
		}
	}

	if (dc_cfg->read_done) {
		for (i = 0; i < sizeof(dc_cfg->enter_dc_lut)/5; i++) {
			for (j = i * 5; j < ((i + 1) * 5) ; j++) {
				dc_cfg->enter_dc_lut[j] = dc_cfg->exit_dc_lut[(i + 1) * 5 -1];
			}
		}
		pr_info("DC parameter read done\n");
		retval = 0;
	} else {
		pr_err("Failed to read DC parameter\n");
		retval = -EAGAIN;
	}

error:
	mutex_unlock(&panel->panel_lock);
	return retval;
}

int dsi_panel_update_dc_param(struct dsi_panel *panel)
{
	struct dsi_display *display;
	struct dsi_display_mode *mode;
	struct dc_cfg *dc_cfg;
	struct dsi_cmd_desc *cmds;
	int total_modes;
	u32 i, count;
	u8 *tx_buf;
	size_t tx_len;
	u32 param_len;
	int rc;

	if (!panel || !panel->host) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	display = to_dsi_display(panel->host);
	if (!display)
		return -EINVAL;

	if (!panel->mi_cfg.dc_update_flag) {
		pr_debug("dc_update_flag is not configed\n");
		return 0;
	}

	dc_cfg = &panel->mi_cfg.dc_cfg;
	if (!dc_cfg->read_done) {
		pr_err("DC parameter not ready\n");
		return -EAGAIN;
	}

	if (!display->modes) {
		rc = dsi_display_get_modes(display, &mode);
		if (rc) {
			pr_err("failed to get display mode for update gamma parameter\n");
			return rc;
		}
	}

	mutex_lock(&display->display_lock);

	total_modes = panel->num_display_modes;
	for (i = 0; i < total_modes; i++) {
		mode = &display->modes[i];
		cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_DC_OFF].cmds;
		count = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_DC_OFF].count;
		if (cmds && count >= dc_cfg->update_d2_index) {
			tx_buf = (u8 *)cmds[dc_cfg->update_d2_index].msg.tx_buf;
			tx_len = cmds[dc_cfg->update_d2_index].msg.tx_len;
			param_len = min(sizeof(dc_cfg->exit_dc_lut), tx_len - 1);
			memcpy(&tx_buf[1], dc_cfg->exit_dc_lut, param_len);
		} else {
			pr_err("please check dc update parameter index configuration\n");
		}

		cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_DC_ON].cmds;
		count = mode->priv_info->cmd_sets[DSI_CMD_SET_MI_DC_ON].count;
		if (cmds && count >= dc_cfg->update_d2_index) {
			tx_buf = (u8 *)cmds[dc_cfg->update_d2_index].msg.tx_buf;
			tx_len = cmds[dc_cfg->update_d2_index].msg.tx_len;
			param_len = min(sizeof(dc_cfg->enter_dc_lut), tx_len - 1);
			memcpy(&tx_buf[1], dc_cfg->enter_dc_lut, param_len);
		} else {
			pr_err("please check dc update parameter index configuration\n");
		}
	}
	dc_cfg->update_done = true;

	mutex_unlock(&display->display_lock);

	return 0;
}

int dsi_panel_switch_disp_rate_gpio(struct dsi_panel *panel)
{
	struct dsi_panel_mi_cfg *mi_cfg;
	struct dsi_display_mode *mode;

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	mi_cfg = &panel->mi_cfg;
	mode = panel->cur_mode;

	if (gpio_is_valid(mi_cfg->disp_rate_gpio)) {
		if (60 == mode->timing.refresh_rate) {
			gpio_set_value(mi_cfg->disp_rate_gpio, 1);
		} else if (90 == mode->timing.refresh_rate) {
			gpio_set_value(mi_cfg->disp_rate_gpio, 1);
		} else {
			pr_info("disp_rate gpio not change\n");
		}
	}
	return 0;
}

int dsi_panel_write_gamma_cmd_set(struct dsi_panel *panel,
				enum dsi_gamma_cmd_set_type type)
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;
	const struct mipi_dsi_host_ops *ops = panel->host->ops;

	if (!panel)
		return -EINVAL;

	if (!panel->mi_cfg.gamma_update_flag) {
		pr_err("gamma_update_flag is not configed\n");
		return 0;
	}

	if (!panel->mi_cfg.gamma_cfg.update_done_60hz ||
		!panel->mi_cfg.gamma_cfg.update_done_90hz) {
		pr_err("gamma parameter not update\n");
		return 0;
	}

	cmds = gamma_cmd_set[type].cmds;
	count = gamma_cmd_set[type].count;
	state = gamma_cmd_set[type].state;

	if (!cmds || count == 0) {
		pr_debug("[%s] No commands to be sent for state\n", panel->name);
		goto error;
	}

	for (i = 0; i < count; i++) {
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			pr_err("failed to set cmds, rc=%d\n", rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000,
					((cmds->post_wait_ms * 1000) + 10));
		cmds++;
	}
error:
	return rc;
}

int dsi_panel_write_cmd_set(struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmd_sets)
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;
	struct dsi_display_mode *mode;
	const struct mipi_dsi_host_ops *ops = panel->host->ops;

	if (!panel || !panel->cur_mode)
		return -EINVAL;

	mode = panel->cur_mode;

	cmds = cmd_sets->cmds;
	count = cmd_sets->count;
	state = cmd_sets->state;

	if (count == 0) {
		pr_debug("[%s] No commands to be sent for state\n", panel->name);
		goto error;
	}

	for (i = 0; i < count; i++) {
		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		if (cmds->last_command)
			cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;

		len = ops->transfer(panel->host, &cmds->msg);
		if (len < 0) {
			rc = len;
			pr_err("failed to set cmds, rc=%d\n", rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000,
					((cmds->post_wait_ms * 1000) + 10));
		cmds++;
	}
error:
	return rc;
}

int dsi_panel_read_cmd_set(struct dsi_panel *panel,
				struct dsi_read_config *read_config)
{
	struct mipi_dsi_host *host;
	struct dsi_display *display;
	struct dsi_display_ctrl *ctrl;
	struct dsi_cmd_desc *cmds;
	enum dsi_cmd_set_state state;
	int i, rc = 0, count = 0;
	u32 flags = 0;

	if (panel == NULL || read_config == NULL)
		return -EINVAL;

	host = panel->host;
	if (host) {
		display = to_dsi_display(host);
		if (display == NULL)
			return -EINVAL;
	} else
		return -EINVAL;

	if (!panel->panel_initialized) {
		pr_info("Panel not initialized\n");
		return -EINVAL;
	}

	if (!read_config->is_read) {
		pr_info("read operation was not permitted\n");
		return -EPERM;
	}

	dsi_display_clk_ctrl(display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_ON);

	ctrl = &display->ctrl[display->cmd_master_idx];

	rc = dsi_display_cmd_engine_enable(display);
	if (rc) {
		pr_err("cmd engine enable failed\n");
		rc = -EPERM;
		goto exit_ctrl;
	}

	if (display->tx_cmd_buf == NULL) {
		rc = dsi_host_alloc_cmd_tx_buffer(display);
		if (rc) {
			pr_err("failed to allocate cmd tx buffer memory\n");
			goto exit;
		}
	}

	count = read_config->read_cmd.count;
	cmds = read_config->read_cmd.cmds;
	state = read_config->read_cmd.state;
	if (count == 0) {
		pr_err("No commands to be sent\n");
		goto exit;
	}
	if (cmds->last_command) {
		cmds->msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
		flags |= DSI_CTRL_CMD_LAST_COMMAND;
	}
	if (state == DSI_CMD_SET_STATE_LP)
		cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;
	flags |= (DSI_CTRL_CMD_FETCH_MEMORY | DSI_CTRL_CMD_READ |
		  DSI_CTRL_CMD_CUSTOM_DMA_SCHED);

	memset(read_config->rbuf, 0x0, sizeof(read_config->rbuf));
	cmds->msg.rx_buf = read_config->rbuf;
	cmds->msg.rx_len = read_config->cmds_rlen;

	rc = dsi_ctrl_cmd_transfer(ctrl->ctrl, &(cmds->msg), flags);
	if (rc <= 0) {
		pr_err("rx cmd transfer failed rc=%d\n", rc);
		goto exit;
	}

	/* for debug log */
	for (i = 0; i < read_config->cmds_rlen; i++)
		pr_debug("[%d] = 0x%02X\n", i, read_config->rbuf[i]);

exit:
	dsi_display_cmd_engine_disable(display);
exit_ctrl:
	dsi_display_clk_ctrl(display->dsi_clk_handle,
		DSI_ALL_CLKS, DSI_CLK_OFF);

	return rc;
}

int dsi_panel_write_mipi_reg(struct dsi_panel *panel,
				char *buf)
{
	struct dsi_panel_cmd_set cmd_sets = {0};
	int retval = 0, dlen = 0;
	u32 packet_count = 0;
	char *token, *input_copy, *input_dup = NULL;
	const char *delim = " ";
	char *buffer = NULL;
	u32 buf_size = 0;
	u32 tmp_data = 0;

	mutex_lock(&panel->panel_lock);

	if (!panel || !panel->panel_initialized) {
		pr_err("Panel not initialized!\n");
		retval = -EAGAIN;
		goto exit_unlock;
	}

	pr_debug("input buffer:{%s}\n", buf);

	input_copy = kstrdup(buf, GFP_KERNEL);
	if (!input_copy) {
		retval = -ENOMEM;
		goto exit_unlock;
	}

	input_dup = input_copy;
	/* removes leading and trailing whitespace from input_copy */
	input_copy = strim(input_copy);

	/* Split a string into token */
	token = strsep(&input_copy, delim);
	if (token) {
		retval = kstrtoint(token, 10, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free0;
		}
		g_dsi_read_cfg.is_read= !!tmp_data;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	token = strsep(&input_copy, delim);
	if (token) {
		retval = kstrtoint(token, 10, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free0;
		}
		if (tmp_data > sizeof(g_dsi_read_cfg.rbuf)) {
			pr_err("read size exceeding the limit %d\n",
					sizeof(g_dsi_read_cfg.rbuf));
			goto exit_free0;
		}
		g_dsi_read_cfg.cmds_rlen = tmp_data;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	buffer = kzalloc(strlen(input_copy), GFP_KERNEL);
	if (!buffer) {
		retval = -ENOMEM;
		goto exit_free0;
	}

	token = strsep(&input_copy, delim);
	while (token) {
		retval = kstrtoint(token, 16, &tmp_data);
		if (retval) {
			pr_err("input buffer conversion failed\n");
			goto exit_free1;
		}
		pr_debug("buffer[%d] = 0x%02x\n", buf_size, tmp_data);
		buffer[buf_size++] = (tmp_data & 0xff);
		/* Removes leading whitespace from input_copy */
		if (input_copy) {
			input_copy = skip_spaces(input_copy);
			token = strsep(&input_copy, delim);
		} else {
			token = NULL;
		}
	}

	retval = dsi_panel_get_cmd_pkt_count(buffer, buf_size, &packet_count);
	if (!packet_count) {
		pr_err("get pkt count failed!\n");
		goto exit_free1;
	}

	retval = dsi_panel_alloc_cmd_packets(&cmd_sets, packet_count);
	if (retval) {
		pr_err("failed to allocate cmd packets, ret=%d\n", retval);
		goto exit_free1;
	}

	retval = dsi_panel_create_cmd_packets(buffer, dlen, packet_count,
						  cmd_sets.cmds);
	if (retval) {
		pr_err("failed to create cmd packets, ret=%d\n", retval);
		goto exit_free2;
	}

	if (g_dsi_read_cfg.is_read) {
		g_dsi_read_cfg.read_cmd = cmd_sets;
		retval = dsi_panel_read_cmd_set(panel, &g_dsi_read_cfg);
		if (retval <= 0) {
			pr_err("[%s]failed to read cmds, rc=%d\n", panel->name, retval);
			goto exit_free3;
		}
	} else {
		g_dsi_read_cfg.read_cmd = cmd_sets;
		retval = dsi_panel_write_cmd_set(panel, &cmd_sets);
		if (retval) {
			pr_err("[%s] failed to send cmds, rc=%d\n", panel->name, retval);
			goto exit_free3;
		}
	}

	pr_debug("[%s]: done!\n", panel->name);
	retval = 0;

exit_free3:
	dsi_panel_destroy_cmd_packets(&cmd_sets);
exit_free2:
	dsi_panel_dealloc_cmd_packets(&cmd_sets);
exit_free1:
	kfree(buffer);
exit_free0:
	kfree(input_dup);
exit_unlock:
	mutex_unlock(&panel->panel_lock);
	return retval;
}

ssize_t dsi_panel_read_mipi_reg(struct dsi_panel *panel, char *buf)
{
	int i = 0;
	ssize_t count = 0;

	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);

	if (g_dsi_read_cfg.is_read) {
		for (i = 0; i < g_dsi_read_cfg.cmds_rlen; i++) {
			if (i == g_dsi_read_cfg.cmds_rlen - 1) {
				count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X\n",
				     g_dsi_read_cfg.rbuf[i]);
			} else {
				count += snprintf(buf + count, PAGE_SIZE - count, "0x%02X,",
				     g_dsi_read_cfg.rbuf[i]);
			}
		}
	}

	mutex_unlock(&panel->panel_lock);

	return count;
}

ssize_t dsi_panel_read_wp_info(struct dsi_panel *panel, char *buf)
{
	int rc = 0;
	int i = 0;
	ssize_t count = 0;
	struct dsi_read_config wp_read_config;
	struct dsi_panel_cmd_set *cmd_set;
	struct dsi_display_mode_priv_info *priv_info;

	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);

	priv_info = panel->cur_mode->priv_info;
	cmd_set = &priv_info->cmd_sets[DSI_CMD_SET_MI_WHITE_POINT_READ];
	wp_read_config.read_cmd = *cmd_set;
	wp_read_config.cmds_rlen = panel->mi_cfg.wp_reg_read_len;
	wp_read_config.is_read = 1;

	rc = dsi_panel_read_cmd_set(panel, &wp_read_config);
	if (rc <= 0) {
		pr_err("[%s]failed to read wp_info, rc=%d\n", panel->name, rc);
		count = -EAGAIN;
	} else {
		for (i = 0; i < panel->mi_cfg.wp_info_len; i++) {
			if (i == panel->mi_cfg.wp_info_len - 1) {
				count += snprintf(buf + count, PAGE_SIZE - count, "%02x\n",
					 wp_read_config.rbuf[panel->mi_cfg.wp_info_index + i]);
			} else {
				count += snprintf(buf + count, PAGE_SIZE - count, "%02x",
					wp_read_config.rbuf[panel->mi_cfg.wp_info_index + i]);
			}
		}
	}

	mutex_unlock(&panel->panel_lock);

	return count;
}

int dsi_panel_set_doze_brightness(struct dsi_panel *panel,
			int doze_brightness, bool need_panel_lock)
{
	int rc = 0;
	struct dsi_panel_mi_cfg *mi_cfg;
	const char *doze_brightness_str[] = {
		[DOZE_TO_NORMAL] = "DOZE_TO_NORMAL",
		[DOZE_BRIGHTNESS_HBM] = "DOZE_BRIGHTNESS_HBM",
		[DOZE_BRIGHTNESS_LBM] = "DOZE_BRIGHTNESS_LBM",
	};

	if (need_panel_lock)
		mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;

	if (!panel->panel_initialized) {
		mi_cfg->unset_doze_brightness = doze_brightness;
		pr_info("Panel not initialized! save unset_doze_brightness = %s",
				doze_brightness_str[mi_cfg->unset_doze_brightness]);
		goto exit;
	}

	if (mi_cfg->fod_hbm_enabled) {
		mi_cfg->unset_doze_brightness = doze_brightness;
		if (mi_cfg->unset_doze_brightness == DOZE_TO_NORMAL) {
			mi_cfg->doze_brightness_state = DOZE_TO_NORMAL;
			mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		}
		pr_info("fod_hbm_enabled set, save unset_doze_brightness = %s",
				doze_brightness_str[mi_cfg->unset_doze_brightness]);
		goto exit;
	}

	if (mi_cfg->in_aod) {
		if (mi_cfg->doze_brightness_state != doze_brightness ||
			mi_cfg->unset_doze_brightness != DOZE_TO_NORMAL) {
			if (mi_cfg->sysfs_fod_unlock_success && doze_brightness != DOZE_TO_NORMAL) {
				pr_info("[%d,%d]Fod fingerprint unlocked successfully, skip to enter aod mode\n",
					mi_cfg->layer_fod_unlock_success, mi_cfg->sysfs_fod_unlock_success);
			} else {
				if (doze_brightness == DOZE_BRIGHTNESS_HBM ||
					mi_cfg->unset_doze_brightness == DOZE_BRIGHTNESS_HBM) {
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
					if (rc)
						pr_err("[%s] failed to send DSI_CMD_SET_MI_DOZE_HBM cmd, rc=%d\n",
							panel->name, rc);
					mi_cfg->aod_backlight = 170;
				} else if (doze_brightness == DOZE_BRIGHTNESS_LBM ||
					mi_cfg->unset_doze_brightness == DOZE_BRIGHTNESS_LBM) {
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
					if (rc)
						pr_err("[%s] failed to send DSI_CMD_SET_MI_DOZE_LBM cmd, rc=%d\n",
							panel->name, rc);
					mi_cfg->aod_backlight = 10;
				}
			}

			mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
			mi_cfg->unset_doze_brightness = DOZE_TO_NORMAL;
			mi_cfg->doze_brightness_state = doze_brightness;
			pr_info("set doze brightness to %s", doze_brightness_str[doze_brightness]);
		} else {
			pr_info("%s has been set, skip\n", doze_brightness_str[doze_brightness]);
		}
	} else {
		mi_cfg->unset_doze_brightness = doze_brightness;
		if (mi_cfg->unset_doze_brightness != DOZE_TO_NORMAL)
			pr_info("Not in Doze mode! save unset_doze_brightness = %s",
					doze_brightness_str[mi_cfg->unset_doze_brightness]);
	}

exit:
	if (need_panel_lock)
		mutex_unlock(&panel->panel_lock);

	return rc;
}

ssize_t dsi_panel_get_doze_brightness(struct dsi_panel *panel, char *buf)
{
	ssize_t count = 0;
	struct dsi_panel_mi_cfg *mi_cfg;

	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	count = snprintf(buf, PAGE_SIZE, "%d\n", mi_cfg->doze_brightness_state);

	mutex_unlock(&panel->panel_lock);

	return count;
}

int dsi_panel_set_disp_param(struct dsi_panel *panel, u32 param)
{
	int rc = 0;
	uint32_t temp = 0;
	u32 fod_backlight = 0;
	struct dsi_panel_mi_cfg *mi_cfg  = NULL;
	struct dsi_cmd_desc *cmds = NULL;
	struct dsi_display_mode_priv_info *priv_info;
	static u8 backlight_delta = 0;
	u32 resend_backlight;
	u32 count;
	u8 *tx_buf;
	char buf[64] = {0};

	if (!panel) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	if (!mi_cfg->mi_feature_enabled) {
		pr_err("mi feature not enable, exit!\n");
		goto exit;
	}

	snprintf(buf, sizeof(buf), "param_type = 0x%X", param);
	display_utc_time_marker(buf);

	if (!panel->panel_initialized
		&& (param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_ON
		&& (param & 0x0F000000) != DISPPARAM_FOD_BACKLIGHT_OFF
		&& param != DISPPARAM_FOD_UNLOCK_SUCCESS
		&& param != DISPPARAM_FOD_UNLOCK_FAIL) {
		pr_err("Panel not initialized!\n");
		goto exit;
	}

	priv_info = panel->cur_mode->priv_info;

	if ((param & 0x00F00000) == 0xD00000) {
		fod_backlight = (param & 0x01FFF);
		param = (param & 0x0FF00000);
	}

	temp = param & 0x0000000F;
	switch (temp) {
	case DISPPARAM_WARM:
	case DISPPARAM_DEFAULT:
	case DISPPARAM_COLD:
	case DISPPARAM_PAPERMODE8:
	case DISPPARAM_PAPERMODE1:
	case DISPPARAM_PAPERMODE2:
	case DISPPARAM_PAPERMODE3:
	case DISPPARAM_PAPERMODE4:
	case DISPPARAM_PAPERMODE5:
	case DISPPARAM_PAPERMODE6:
	case DISPPARAM_PAPERMODE7:
		pr_info("DISPPARAM_WARM[0x1]~DISPPARAM_PAPERMODE7[0xc] not supported!\n");
		break;
	default:
		break;
	}

	temp = param & 0x000000F0;
	switch (temp) {
	case DISPPARAM_CE_ON:
		pr_info("ceon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CEON);
		break;
	case DISPPARAM_CE_OFF:
		pr_info("ceoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CEOFF);
		break;
	default:
		break;
	}

	temp = param & 0x00000F00;
	switch (temp) {
	case DISPPARAM_CABCUI_ON:
		pr_info("cabcuion\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCUION);
		break;
	case DISPPARAM_CABCSTILL_ON:
		pr_info("cabcstillon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCSTILLON);
		break;
	case DISPPARAM_CABCMOVIE_ON:
		pr_info("cabcmovieon\n");
		dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCMOVIEON);
		break;
	case DISPPARAM_CABC_OFF:
		pr_info("cabcoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABCOFF);
		break;
	case DISPPARAM_SKIN_CE_CABCUI_ON:
		pr_info("skince cabcuion\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCUION);
		break;
	case DISPPARAM_SKIN_CE_CABCSTILL_ON:
		pr_info("skince cabcstillon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCSTILLON);
		break;
	case DISPPARAM_SKIN_CE_CABCMOVIE_ON:
		pr_info("skince cabcmovieon\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCMOVIEON);
		break;
	case DISPPARAM_SKIN_CE_CABC_OFF:
		pr_info("skince cabcoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SKINCE_CABCOFF);
		break;
	case DISPPARAM_DIMMING_OFF:
		pr_info("dimmingoff\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
		break;
	case DISPPARAM_DIMMING:
		pr_info("dimmingon\n");
		if (mi_cfg->skip_dimmingon != STATE_DIM_BLOCK) {
			if (ktime_after(ktime_get(), mi_cfg->fod_hbm_off_time)
				&& ktime_after(ktime_get(), mi_cfg->fod_backlight_off_time)) {
				dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGON);
			} else {
				pr_info("skip dimmingon due to hbm off\n");
			}
		} else {
			pr_info("skip dimmingon due to hbm on\n");
		}
		break;
	default:
		break;
	}

	temp = param & 0x0000F000;
	switch (temp) {
	case DISPPARAM_ACL_L1:
		pr_info("acl level 1\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L1);
		break;
	case DISPPARAM_ACL_L2:
		pr_info("acl level 2\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L2);
		break;
	case DISPPARAM_ACL_L3:
		pr_info("acl level 3\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_L3);
		break;
	case DISPPARAM_ACL_OFF:
		pr_info("acl off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ACL_OFF);
		break;
	default:
		break;
	}

	temp = param & 0x000F0000;
	switch (temp) {
	case DISPPARAM_HBM_ON:
		pr_info("hbm on\n");
		mi_cfg->hbm_enabled = true;
		if (!mi_cfg->fod_hbm_enabled)
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_ON);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		break;
	case DISPPARAM_HBM_FOD_ON:
		pr_info("hbm fod on\n");
		cancel_delayed_work(&mi_cfg->enter_aod_delayed_work);
		if (mi_cfg->dc_type == 0 && mi_cfg->fod_on_b2_index) {
			cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_ON].cmds;
			count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_ON].count;
			if (cmds && count >= mi_cfg->fod_on_b2_index) {
				tx_buf = (u8 *)cmds[mi_cfg->fod_on_b2_index].msg.tx_buf;
				/* 0xB2 reg: if DC on use 1 Pulse(0x00); if DC off use 4 Pulse(0x20) */
				if (tx_buf && tx_buf[0] == 0xB2) {
					if (mi_cfg->dc_enable) {
						tx_buf[1] = 0x00;
					} else {
						tx_buf[1] = 0x20;
					}
					pr_info("DSI_CMD_SET_MI_HBM_FOD_ON 0x%02X = 0x%02X\n", tx_buf[0], tx_buf[1]);
				} else {
					if (tx_buf)
						pr_err("tx_buf[0] = 0x%02X, check 0xB2 index\n", tx_buf[0]);
					else
						pr_err("tx_buf is NULL pointer\n");
				}
			}
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_ON);
		if (mi_cfg->dc_type)
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_OFF);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		mi_cfg->fod_hbm_enabled = true;
		break;
	case DISPPARAM_HBM_FOD2NORM:
		pr_info("hbm fod to normal mode\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD2NORM);
		break;
	case DISPPARAM_HBM_FOD_OFF:
		pr_info("hbm fod off\n");
		if (!mi_cfg->hbm_enabled) {
			if (mi_cfg->hbm_51_ctrl_flag) {
				cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].cmds;
				count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_FOD_OFF].count;
				if (cmds && count >= mi_cfg->fod_off_51_index) {
					tx_buf = (u8 *)cmds[mi_cfg->fod_off_51_index].msg.tx_buf;
					if (tx_buf && tx_buf[0] == 0x51) {
						/* lmi panel dc_type = 2, always need to restore to last_bl_level to
						 * avoid flash high brightness while exiting app lock (MIUI-1755728) */
						if (mi_cfg->dc_enable && (mi_cfg->dc_type == 1)) {
							tx_buf[1] = (mi_cfg->dc_threshold >> 8) & 0x07;
							tx_buf[2] = mi_cfg->dc_threshold & 0xff;
						} else {
							tx_buf[1] = (mi_cfg->last_bl_level >> 8) & 0x07;
							tx_buf[2] = mi_cfg->last_bl_level & 0xff;
						}
						pr_info("DSI_CMD_SET_MI_HBM_FOD_OFF 0x%02X = 0x%02X 0x%02X\n",
							tx_buf[0], tx_buf[1], tx_buf[2]);
					} else {
						if (tx_buf)
							pr_err("tx_buf[0] = 0x%02X, check 0x51 index\n", tx_buf[0]);
						else
							pr_err("tx_buf is NULL pointer\n");
					}
				}
				if (mi_cfg->vi_setting_enabled) {
					if (mi_cfg->last_bl_level >= mi_cfg->vi_switch_threshold) {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_HIGH);
					} else {
						rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_LOW);
					}
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_OFF);
			} else {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_OFF);
			}
			mi_cfg->skip_dimmingon = STATE_DIM_RESTORE;
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_ON);
		}
		if (mi_cfg->dc_type == 0 && mi_cfg->dc_enable) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AOD_TO_DC_ON);
			if (rc)
				pr_err("[%s] failed to send DSI_CMD_SET_MI_DC_ON cmd, rc=%d\n",
					panel->name, rc);
		}
		mi_cfg->fod_hbm_enabled = false;
		mi_cfg->fod_hbm_off_time = ktime_add_ms(ktime_get(),
				mi_cfg->fod_off_dimming_delay);

		if (panel->power_mode == SDE_MODE_DPMS_LP1 ||
				panel->power_mode == SDE_MODE_DPMS_LP2) {
			if (mi_cfg->layer_fod_unlock_success || mi_cfg->sysfs_fod_unlock_success) {
				pr_info("[%d,%d]Fod fingerprint unlocked successfully, skip to enter aod mode\n",
					mi_cfg->layer_fod_unlock_success, mi_cfg->sysfs_fod_unlock_success);
			} else {
				pr_info("delayed_work schedule --- delay enter aod mode\n");
				schedule_delayed_work(&mi_cfg->enter_aod_delayed_work,
					msecs_to_jiffies(100));
			}
		}
		break;
	case DISPPARAM_HBM_HDR_ON:
		pr_info("hbm hdr on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_HDR_ON);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		mi_cfg->hbm_enabled = true;
		break;
	case DISPPARAM_HBM_HDR_OFF:
		pr_info("hbm hdr off\n");
		if (mi_cfg->hbm_51_ctrl_flag) {
			cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_HDR_OFF].cmds;
			count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_HDR_OFF].count;
			if (cmds && count >= mi_cfg->hbm_off_51_index) {
				tx_buf = (u8 *)cmds[mi_cfg->hbm_off_51_index].msg.tx_buf;
				if (tx_buf && tx_buf[0] == 0x51) {
					tx_buf[1] = (mi_cfg->last_bl_level >> 8) & 0x07;
					tx_buf[2] = mi_cfg->last_bl_level & 0xff;
					pr_info("DSI_CMD_SET_MI_HBM_HDR_OFF 0x%02X = 0x%02X 0x%02X\n",
							tx_buf[0], tx_buf[1], tx_buf[2]);
				} else {
					if (tx_buf)
						pr_err("tx_buf[0] = 0x%02X, check 0x51 index\n", tx_buf[0]);
					else
						pr_err("tx_buf is NULL pointer\n");
				}
			}
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_HDR_OFF);
		mi_cfg->skip_dimmingon = STATE_DIM_RESTORE;
		mi_cfg->hbm_enabled = false;
		break;
	case DISPPARAM_FOD_UNLOCK_SUCCESS:
		pr_info("Fod fingerprint unlock success\n");
		mi_cfg->sysfs_fod_unlock_success = true;
		break;
	case DISPPARAM_FOD_UNLOCK_FAIL:
		pr_info("Fod fingerprint unlock fail\n");
		mi_cfg->sysfs_fod_unlock_success = false;
		break;
	case DISPPARAM_HBM_OFF:
		pr_info("hbm off\n");
		if (mi_cfg->hbm_51_ctrl_flag) {
			cmds = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_OFF].cmds;
			count = priv_info->cmd_sets[DSI_CMD_SET_MI_HBM_OFF].count;
			if (cmds && count >= mi_cfg->hbm_off_51_index) {
				tx_buf = (u8 *)cmds[mi_cfg->hbm_off_51_index].msg.tx_buf;
				if (tx_buf && tx_buf[0] == 0x51) {
					tx_buf[1] = (mi_cfg->last_bl_level >> 8) & 0x07;
					tx_buf[2] = mi_cfg->last_bl_level & 0xff;
				} else {
					if (tx_buf)
						pr_err("tx_buf[0] = 0x%02X, check 0x51 index\n", tx_buf[0]);
					else
						pr_err("tx_buf is NULL pointer\n");
				}
			}
			if (mi_cfg->vi_setting_enabled) {
				if (mi_cfg->last_bl_level >= mi_cfg->vi_switch_threshold) {
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_HIGH);
				} else {
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_VI_SETTING_LOW);
				}
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_OFF);
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_OFF);
		}
		mi_cfg->skip_dimmingon = STATE_DIM_RESTORE;
		mi_cfg->hbm_enabled = false;
		break;
	case DISPPARAM_DC_ON:
		pr_info("DC on\n");
		if (mi_cfg->dc_type == 0) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_ON);
			if (rc)
				pr_err("[%s] failed to send DSI_CMD_SET_MI_DC_ON cmd, rc=%d\n",
						panel->name, rc);
			else
				rc = dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
		}
		mi_cfg->dc_enable = true;
		break;
	case DISPPARAM_DC_OFF:
		pr_info("DC off\n");
		if (mi_cfg->dc_type == 0) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_OFF);
			if (rc)
				pr_err("[%s] failed to send DSI_CMD_SET_MI_DC_OFF cmd, rc=%d\n",
						panel->name, rc);
			else
				rc = dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
		}
		mi_cfg->dc_enable = false;
		break;
	default:
		break;
	}

	temp = param & 0x00F00000;
	switch (temp) {
	case DISPPARAM_NORMALMODE1:
		pr_info("normal mode1\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_NORMAL1);
		break;
	case DISPPARAM_P3:
		pr_info("dci p3 mode\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_DCIP3);
		break;
	case DISPPARAM_SRGB:
		pr_info("sRGB\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_SRGB);
		break;
	case DISPPARAM_DOZE_BRIGHTNESS_HBM:
#ifdef CONFIG_FACTORY_BUILD
		pr_info("doze hbm On\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
#else
		if (mi_cfg->in_aod) {
			pr_info("doze hbm On\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		}
#endif
		break;
	case DISPPARAM_DOZE_BRIGHTNESS_LBM:
#ifdef CONFIG_FACTORY_BUILD
		pr_info("doze lbm On\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
		mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
#else
		if (mi_cfg->in_aod) {
			pr_info("doze lbm On\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			mi_cfg->skip_dimmingon = STATE_DIM_BLOCK;
		}
#endif
		break;
	case DISPPARAM_DOZE_OFF:
		pr_info("doze Off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
		break;
	case DISPPARAM_HBM_BACKLIGHT_RESEND:
		backlight_delta++;
		if (mi_cfg->last_bl_level >= panel->bl_config.bl_max_level - 1)
			resend_backlight = mi_cfg->last_bl_level -
				((backlight_delta%2 == 0) ? 1 : 2);
		else
			resend_backlight = mi_cfg->last_bl_level +
				((backlight_delta%2 == 0) ? 1 : 2);

		pr_info("backlight resend: last_bl_level = %d; resend_backlight = %d\n",
				mi_cfg->last_bl_level, resend_backlight);
		rc = dsi_panel_update_backlight(panel, resend_backlight);
		break;
	case DISPPARAM_FOD_BACKLIGHT:
		if (fod_backlight == 0x1000) {
			pr_info("FOD backlight restore last_bl_level=%d\n",
				mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
			if (mi_cfg->dc_enable && mi_cfg->dc_type) {
				pr_info("FOD backlight restore dc_threshold=%d",
				mi_cfg->dc_threshold);
				rc = dsi_panel_update_backlight(panel, mi_cfg->dc_threshold);
			} else {
				pr_info("FOD backlight restore last_bl_level=%d",
				mi_cfg->last_bl_level);
				rc = dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
			}
		} else if (fod_backlight >= 0) {
			pr_info("FOD backlight set");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
			rc = dsi_panel_update_backlight(panel, fod_backlight);
			mi_cfg->fod_target_backlight = fod_backlight;
			mi_cfg->skip_dimmingon = STATE_NONE;
		}
		break;
	case DISPPARAM_CRC_OFF:
		pr_info("crc off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CRC_OFF);
		break;
	default:
		break;
	}

	temp = param & 0x0F000000;
	switch (temp) {
	case DISPPARAM_FOD_BACKLIGHT_ON:
		pr_info("fod_backlight_flag on\n");
		mi_cfg->fod_backlight_flag = true;
		break;
	case DISPPARAM_FOD_BACKLIGHT_OFF:
		pr_info("fod_backlight_flag off\n");
		mi_cfg->fod_backlight_flag = false;
		break;
	case DISPPARAM_ELVSS_DIMMING_ON:
		pr_info("elvss dimming on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ELVSS_DIMMING_OFF);
		break;
	case DISPPARAM_ELVSS_DIMMING_OFF:
		pr_info("elvss dimming off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ELVSS_DIMMING_OFF);
		break;
	case DISPPARAM_FLAT_MODE_ON:
		pr_info("flat mode on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
		break;
	case DISPPARAM_FLAT_MODE_OFF:
		pr_info("flat mode off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
		break;
	default:
		break;
	}

exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

