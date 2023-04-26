/*
 * OsmocomBB <-> SDR connection bridge
 *
 * (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <osmocom/core/application.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>

#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/logging.h>

static struct log_info_cat trxcon_log_info_cat[] = {
	[DAPP] = {
		.name = "DAPP",
		.description = "Application",
		.color = "\033[1;35m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DL1C] = {
		.name = "DL1C",
		.description = "Layer 1 control interface",
		.color = "\033[1;31m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DL1D] = {
		.name = "DL1D",
		.description = "Layer 1 data",
		.color = "\033[1;31m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DTRXC] = {
		.name = "DTRXC",
		.description = "Transceiver control interface",
		.color = "\033[1;33m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DTRXD] = {
		.name = "DTRXD",
		.description = "Transceiver data interface",
		.color = "\033[1;33m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSCH] = {
		.name = "DSCH",
		.description = "Scheduler management",
		.color = "\033[1;36m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DSCHD] = {
		.name = "DSCHD",
		.description = "Scheduler data",
		.color = "\033[1;36m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
	[DGPRS] = {
		.name = "DGPRS",
		.description = "L1 GPRS (MAC layer)",
		.color = "\033[1;36m",
		.enabled = 1, .loglevel = LOGL_NOTICE,
	},
};

static const struct log_info trxcon_log_info = {
	.cat = trxcon_log_info_cat,
	.num_cat = ARRAY_SIZE(trxcon_log_info_cat),
};

static const int trxcon_log_cfg[] = {
	[TRXCON_LOGC_FSM] = DAPP,
	[TRXCON_LOGC_L1C] = DL1C,
	[TRXCON_LOGC_L1D] = DL1D,
	[TRXCON_LOGC_SCHC] = DSCH,
	[TRXCON_LOGC_SCHD] = DSCHD,
	[TRXCON_LOGC_GPRS] = DGPRS,
};

int trxcon_logging_init(void *tall_ctx, const char *category_mask)
{
	osmo_init_logging2(tall_ctx, &trxcon_log_info);

	if (category_mask)
		log_parse_category_mask(osmo_stderr_target, category_mask);

	trxcon_set_log_cfg(&trxcon_log_cfg[0], ARRAY_SIZE(trxcon_log_cfg));

	return 0;
}
