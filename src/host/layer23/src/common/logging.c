/* Logging/Debug support of the layer2/3 stack */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <osmocom/core/logging.h>
#include <osmocom/bb/common/logging.h>

static const struct log_info_cat default_categories[] = {
	[DRSL] = {
		.name = "DRSL",
		.description = "Radio Signalling Link (MS)",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DCS] = {
		.name = "DCS",
		.description = "Cell selection",
		.color = "\033[34m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DNB] = {
		.name = "DNB",
		.description = "Neighbour cell measurement",
		.color = "\033[0;31m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DPLMN] = {
		.name = "DPLMN",
		.description = "PLMN selection",
		.color = "\033[32m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DRR] = {
		.name = "DRR",
		.description = "Radio Resource",
		.color = "\033[1;34m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DMM] = {
		.name = "DMM",
		.description = "Mobility Management",
		.color = "\033[1;32m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DCC] = {
		.name = "DCC",
		.description = "Call Control",
		.color = "\033[1;33m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DSS] = {
		.name = "DSS",
		.description = "Supplenmentary Services",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DSMS] = {
		.name = "DSMS",
		.description = "Short Message Service",
		.color = "\033[1;37m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DMNCC] = {
		.name = "DMNCC",
		.description = "Mobile Network Call Control",
		.color = "\033[1;37m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DMEAS] = {
		.name = "DMEAS",
		.description = "MEasurement Reporting",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DPAG] = {
		.name = "DPAG",
		.description = "Paging",
		.color = "\033[33m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DL1C]	= {
		.name = "DL1C",
		.description = "Layer 1 Control",
		.color = "\033[1;31m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSAP]	= {
		.name = "DSAP",
		.description = "SAP Control",
		.color = "\033[1;31m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DSUM] = {
		.name = "DSUM",
		.description = "Summary of Process",
		.color = "\033[1;37m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DSIM] = {
		.name = "DSIM",
		.description = "SIM client",
		.color = "\033[0;35m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DGPS] = {
		.name = "DGPS",
		.description = "GPS",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
};

const struct log_info log_info = {
	.filter_fn = NULL,
	.cat = default_categories,
	.num_cat = ARRAY_SIZE(default_categories),
};

