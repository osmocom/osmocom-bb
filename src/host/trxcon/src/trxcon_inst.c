/*
 * OsmocomBB <-> SDR connection bridge
 *
 * (C) 2022 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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

#include <stdint.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/talloc.h>

#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/trxcon_fsm.h>
#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>
#include <osmocom/bb/l1gprs.h>

extern int g_logc_l1c;
extern int g_logc_l1d;

void trxcon_set_log_cfg(const int *logc, unsigned int logc_num)
{
	int schc = DLGLOBAL;
	int schd = DLGLOBAL;

	for (unsigned int i = 0; i < logc_num; i++) {
		switch ((enum trxcon_log_cat)i) {
		case TRXCON_LOGC_FSM:
			trxcon_fsm_def.log_subsys = logc[i];
			break;
		case TRXCON_LOGC_L1C:
			g_logc_l1c = logc[i];
			break;
		case TRXCON_LOGC_L1D:
			g_logc_l1d = logc[i];
			break;
		case TRXCON_LOGC_SCHC:
			schc = logc[i];
			break;
		case TRXCON_LOGC_SCHD:
			schd = logc[i];
			break;
		case TRXCON_LOGC_GPRS:
			l1gprs_logging_init(logc[i]);
			break;
		}
	}

	l1sched_logging_init(schc, schd);
}

struct trxcon_inst *trxcon_inst_alloc(void *ctx, unsigned int id, uint32_t fn_advance)
{
	struct trxcon_inst *trxcon;
	struct osmo_fsm_inst *fi;

	fi = osmo_fsm_inst_alloc(&trxcon_fsm_def, ctx, NULL, LOGL_DEBUG, NULL);
	OSMO_ASSERT(fi != NULL);

	trxcon = talloc_zero(fi, struct trxcon_inst);
	OSMO_ASSERT(trxcon != NULL);

	fi->priv = trxcon;
	trxcon->fi = fi;

	osmo_fsm_inst_update_id_f(fi, "%u", id);
	trxcon->id = id;

	/* Logging context to be used by both l1ctl and l1sched modules */
	trxcon->log_prefix = talloc_asprintf(trxcon, "%s: ", osmo_fsm_inst_name(fi));

	/* Init scheduler */
	const struct l1sched_cfg sched_cfg = {
		.fn_advance = fn_advance,
		.log_prefix = trxcon->log_prefix,
	};

	trxcon->sched = l1sched_alloc(trxcon, &sched_cfg, trxcon);
	if (trxcon->sched == NULL) {
		trxcon_inst_free(trxcon);
		return NULL;
	}

	trxcon->phy_quirks.fbsb_extend_fns = 0;

	return trxcon;
}

void trxcon_inst_free(struct trxcon_inst *trxcon)
{
	if (trxcon == NULL || trxcon->fi == NULL)
		return;
	osmo_fsm_inst_term(trxcon->fi, OSMO_FSM_TERM_REQUEST, NULL);
}
