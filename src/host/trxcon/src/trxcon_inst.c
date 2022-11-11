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

	return trxcon;
}

void trxcon_inst_free(struct trxcon_inst *trxcon)
{
	if (trxcon == NULL || trxcon->fi == NULL)
		return;
	osmo_fsm_inst_term(trxcon->fi, OSMO_FSM_TERM_REQUEST, NULL);
}
