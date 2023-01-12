/* Mobile Station */
/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2023 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
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

#include <osmocom/bb/common/ms.h>

struct osmocom_ms *osmocom_ms_alloc(void *ctx)
{
	struct osmocom_ms *ms;

	ms = talloc_zero(ctx, struct osmocom_ms);
	if (!ms)
		return NULL;

	ms->name = talloc_strdup(ms, "1");
	ms->test_arfcn = 871;
	ms->lapdm_channel.lapdm_dcch.l1_ctx = ms;
	ms->lapdm_channel.lapdm_dcch.l3_ctx = ms;
	ms->lapdm_channel.lapdm_acch.l1_ctx = ms;
	ms->lapdm_channel.lapdm_acch.l3_ctx = ms;
	lapdm_channel_init(&ms->lapdm_channel, LAPDM_MODE_MS);
	lapdm_channel_set_l1(&ms->lapdm_channel, l1ctl_ph_prim_cb, ms);

	return ms;
}
