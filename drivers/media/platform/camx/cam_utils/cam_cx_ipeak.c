// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#if defined(CONFIG_ARCH_SM6150)
#include <soc/qcom/cx_ipeak.h>
#endif
#include "cam_cx_ipeak.h"
#include "cam_soc_util.h"
#include "cam_debug_util.h"

static struct cx_ipeak_client *cam_cx_ipeak;
static int cx_ipeak_level = CAM_NOMINAL_VOTE;
static int cx_default_ipeak_mask;
static int cx_current_ipeak_mask;
static int cam_cx_client_cnt;

int cam_cx_ipeak_register_cx_ipeak(struct cam_hw_soc_info *soc_info)
{
	int rc = 0;

	soc_info->cam_cx_ipeak_enable = true;
	soc_info->cam_cx_ipeak_bit = 1 << cam_cx_client_cnt++;
	cx_default_ipeak_mask |= soc_info->cam_cx_ipeak_bit;

	if (cam_cx_ipeak)
		goto exit;

#if defined(CONFIG_ARCH_SM6150)
	cam_cx_ipeak = cx_ipeak_register(soc_info->dev->of_node,
		"qcom,cam-cx-ipeak");
#endif
	if (cam_cx_ipeak) {
		goto exit;
	} else {
		rc = -EINVAL;
		goto exit;
	}

exit:
	CAM_DBG(CAM_UTIL, "cam_cx_ipeak is enabled for %s\n"
		"mask = %x cx_default_ipeak_mask = %x",
		soc_info->dev_name, soc_info->cam_cx_ipeak_bit,
		cx_default_ipeak_mask);
	return rc;
}

int cam_cx_ipeak_update_vote_cx_ipeak(struct cam_hw_soc_info *soc_info,
	int32_t apply_level)
{
	int32_t soc_cx_ipeak_bit = soc_info->cam_cx_ipeak_bit;
	int ret = 0;

	CAM_DBG(CAM_UTIL, "E: apply_level = %d cx_current_ipeak_mask = %x\n"
			"soc_cx_ipeak_bit = %x",
			apply_level, cx_current_ipeak_mask, soc_cx_ipeak_bit);

	if (apply_level < cx_ipeak_level &&
		(cx_current_ipeak_mask & soc_cx_ipeak_bit)) {
		if (cx_current_ipeak_mask == cx_default_ipeak_mask) {
#if defined(CONFIG_ARCH_SM6150)
			ret = cx_ipeak_update(cam_cx_ipeak, false);
#endif
			if (ret)
				goto exit;
			CAM_DBG(CAM_UTIL,
				"X: apply_level = %d cx_current_ipeak_mask = %x\n"
				"soc_cx_ipeak_bit = %x  %s UNVOTE",
				apply_level, cx_current_ipeak_mask,
				soc_cx_ipeak_bit, soc_info->dev_name);
		}
		cx_current_ipeak_mask &= (~soc_cx_ipeak_bit);
		CAM_DBG(CAM_UTIL,
			"X: apply_level = %d cx_current_ipeak_mask = %x\n"
			"soc_cx_ipeak_bit = %x  %s DISABLE_BIT",
			apply_level, cx_current_ipeak_mask,
			soc_cx_ipeak_bit, soc_info->dev_name);
		goto exit;
	} else if (apply_level < cx_ipeak_level) {
		CAM_DBG(CAM_UTIL,
			"X: apply_level = %d cx_current_ipeak_mask = %x\n"
			"soc_cx_ipeak_bit = %x NO AI",
			apply_level, cx_current_ipeak_mask, soc_cx_ipeak_bit);
		goto exit;
	}

	cx_current_ipeak_mask |= soc_cx_ipeak_bit;
	CAM_DBG(CAM_UTIL,
		"X: apply_level = %d cx_current_ipeak_mask = %x\n"
		"soc_cx_ipeak_bit = %x  %s ENABLE_BIT",
		apply_level, cx_current_ipeak_mask,
		soc_cx_ipeak_bit, soc_info->dev_name);
	if (cx_current_ipeak_mask == cx_default_ipeak_mask) {
#if defined(CONFIG_ARCH_SM6150)
		ret = cx_ipeak_update(cam_cx_ipeak, true);
#endif
		if (ret)
			goto exit;
		CAM_DBG(CAM_UTIL,
			"X: apply_level = %d cx_current_ipeak_mask = %x\n"
			"soc_cx_ipeak_bit = %x  %s VOTE",
			apply_level, cx_current_ipeak_mask,
			soc_cx_ipeak_bit, soc_info->dev_name);
	}

exit:
	return ret;
}

int cam_cx_ipeak_unvote_cx_ipeak(struct cam_hw_soc_info *soc_info)
{
	int32_t soc_cx_ipeak_bit = soc_info->cam_cx_ipeak_bit;

	CAM_DBG(CAM_UTIL, "E:cx_current_ipeak_mask = %x\n"
		"soc_cx_ipeak_bit = %x",
		cx_current_ipeak_mask, soc_cx_ipeak_bit);
	if (cx_current_ipeak_mask == cx_default_ipeak_mask) {
#if defined(CONFIG_ARCH_SM6150)
		if (cam_cx_ipeak)
			cx_ipeak_update(cam_cx_ipeak, false);
#endif
		CAM_DBG(CAM_UTIL, "X:cx_current_ipeak_mask = %x\n"
			"soc_cx_ipeak_bit = %x UNVOTE",
			cx_current_ipeak_mask, soc_cx_ipeak_bit);
	}
	cx_current_ipeak_mask &= (~soc_cx_ipeak_bit);
	CAM_DBG(CAM_UTIL, "X:cx_current_ipeak_mask = %x\n"
		"soc_cx_ipeak_bit = %x",
		cx_current_ipeak_mask, soc_cx_ipeak_bit);

	return 0;
}
