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
		.enabled = 1, .loglevel = LOGL_NOTICE,
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
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DRR] = {
		.name = "DRR",
		.description = "Radio Resource",
		.color = "\033[1;34m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DMM] = {
		.name = "DMM",
		.description = "Mobility Management",
		.color = "\033[1;32m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DCC] = {
		.name = "DCC",
		.description = "Call Control",
		.color = "\033[1;33m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSS] = {
		.name = "DSS",
		.description = "Supplenmentary Services",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSMS] = {
		.name = "DSMS",
		.description = "Short Message Service",
		.color = "\033[1;37m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DMNCC] = {
		.name = "DMNCC",
		.description = "Mobile Network Call Control",
		.color = "\033[1;37m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DMEAS] = {
		.name = "DMEAS",
		.description = "MEasurement Reporting",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DPAG] = {
		.name = "DPAG",
		.description = "Paging",
		.color = "\033[33m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
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
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSUM] = {
		.name = "DSUM",
		.description = "Summary of Process",
		.color = "\033[1;37m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSIM] = {
		.name = "DSIM",
		.description = "SIM client",
		.color = "\033[0;35m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DGPS] = {
		.name = "DGPS",
		.description = "GPS",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DMOB] = {
		.name = "DMOB",
		.description = "Mobile",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DPRIM] = {
		.name = "DPRIM",
		.description = "PRIM",
		.color = "\033[1;32m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DLUA] = {
		.name = "DLUA",
		.description = "LUA",
		.color = "\033[1;32m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DGAPK] = {
		.name = "DGAPK",
		.description = "GAPK audio",
		.color = "\033[0;36m",
		.enabled = 1, .loglevel = LOGL_DEBUG,
	},
	[DTUN] = {
		.name = "DTUN",
		.description = "Tunnel interface",
		.color = "\033[0;37m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DLLC] = {
		.name = "DLLC",
		.description = "GPRS Logical Link Control Protocol (LLC)",
		.color = "\033[0;38m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSNDCP] = {
		.name = "DSNDCP",
		.description = "GPRS Sub-Network Dependent Control Protocol (SNDCP)",
		.color = "\033[0;39m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
};

const struct log_info log_info = {
	.filter_fn = NULL,
	.cat = default_categories,
	.num_cat = ARRAY_SIZE(default_categories),
};

