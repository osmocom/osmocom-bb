/* Logging/Debug support of the virtual physical layer */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <osmocom/core/utils.h>
#include <osmocom/core/application.h>
#include <virtphy/logging.h>

static const char* l1ctlPrimNames[] = {
        "_L1CTL_NONE",
        "L1CTL_FBSB_REQ",
        "L1CTL_FBSB_CONF",
        "L1CTL_DATA_IND",
        "L1CTL_RACH_REQ",
        "L1CTL_DM_EST_REQ",
        "L1CTL_DATA_REQ",
        "L1CTL_RESET_IND",
        "L1CTL_PM_REQ",
        "L1CTL_PM_CONF",
        "L1CTL_ECHO_REQ",
        "L1CTL_ECHO_CONF",
        "L1CTL_RACH_CONF",
        "L1CTL_RESET_REQ",
        "L1CTL_RESET_CONF",
        "L1CTL_DATA_CONF",
        "L1CTL_CCCH_MODE_REQ",
        "L1CTL_CCCH_MODE_CONF",
        "L1CTL_DM_REL_REQ",
        "L1CTL_PARAM_REQ",
        "L1CTL_DM_FREQ_REQ",
        "L1CTL_CRYPTO_REQ",
        "L1CTL_SIM_REQ",
        "L1CTL_SIM_CONF",
        "L1CTL_TCH_MODE_REQ",
        "L1CTL_TCH_MODE_CONF",
        "L1CTL_NEIGH_PM_REQ",
        "L1CTL_NEIGH_PM_IND",
        "L1CTL_TRAFFIC_REQ",
        "L1CTL_TRAFFIC_CONF",
        "L1CTL_TRAFFIC_IND",
        "L1CTL_BURST_IND",
        "L1CTL_TBF_CFG_REQ",
        "L1CTL_TBF_CFG_CONF",
        "L1CTL_DATA_TBF_REQ",
        "L1CTL_DATA_TBF_CONF"
};

static const struct log_info_cat default_categories[] = {
	[DL1C] = {
		.name = "DL1C",
		.description = "Layer 1 Control",
		.color = "\033[1;31m",
		.enabled = 1,
		.loglevel = LOGL_DEBUG,
	},
	[DL1P] = {
		.name = "DL1P",
		.description = "Layer 1 Data",
		.color = "\033[1;31m",
		.enabled = 1,
		.loglevel = LOGL_DEBUG,
	},
	[DVIRPHY] = {
		.name = "DVIRPHY",
		.description = "Virtual Layer 1 Interface",
		.color = "\033[1;31m",
		.enabled = 1,
		.loglevel = LOGL_DEBUG,
	},
	[DMAIN] = {
		.name = "DMAIN",
		.description = "Main Program / Data Structures",
		.color = "\033[1;32m",
		.enabled = 1,
		.loglevel = LOGL_DEBUG,
	},
};

const struct log_info ms_log_info = {
	.filter_fn = NULL,
	.cat = default_categories,
	.num_cat = ARRAY_SIZE(default_categories),
};

/**
 * Initialize the logging system for the virtual physical layer.
 */
int ms_log_init(char *cat_mask)
{
	struct log_target *stderr_target;

	log_init(&ms_log_info, NULL);
	stderr_target = log_target_create_stderr();
	if (!stderr)
		return -1;

	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);
	//log_set_log_level(stderr_target, 1);
	log_set_print_filename(stderr_target, 1);
	log_set_use_color(stderr_target, 0);
	log_set_print_timestamp(stderr_target, 1);
	log_set_print_category(stderr_target, 1);
	if (cat_mask)
		log_parse_category_mask(stderr_target, cat_mask);

	return 0;
}

const char *getL1ctlPrimName(uint8_t type)
{
	if (type < ARRAY_SIZE(l1ctlPrimNames))
		return l1ctlPrimNames[type];
	else
		return "Unknown Primitive";
}
