/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2017-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * Contributions by sysmocom - s.f.m.c. GmbH
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

#include <errno.h>
#include <string.h>
#include <talloc.h>
#include <stdint.h>

#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/coding/gsm0503_coding.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

static void decode_sb(struct gsm_time *time, uint8_t *bsic, uint8_t *sb_info)
{
	uint8_t t3p;
	uint32_t sb;

	sb = ((uint32_t)sb_info[3] << 24)
	   | (sb_info[2] << 16)
	   | (sb_info[1] << 8)
	   | sb_info[0];

	*bsic = (sb >> 2) & 0x3f;

	/* TS 05.02 Chapter 3.3.2.2.1 SCH Frame Numbers */
	time->t1 = ((sb >> 23) & 0x01)
		 | ((sb >> 7) & 0x1fe)
		 | ((sb << 9) & 0x600);

	time->t2 = (sb >> 18) & 0x1f;

	t3p = ((sb >> 24) & 0x01) | ((sb >> 15) & 0x06);
	time->t3 = t3p * 10 + 1;

	/* TS 05.02 Chapter 4.3.3 TDMA frame number */
	time->fn = gsm_gsmtime2fn(time);
}

static int handle_sch_ind(struct l1sched_state *sched, uint32_t fn, uint8_t bsic)
{
	struct l1sched_prim *prim;
	struct msgb *msg;

	msg = l1sched_prim_alloc(L1SCHED_PRIM_T_SCH, PRIM_OP_INDICATION, 0);
	OSMO_ASSERT(msg != NULL);

	prim = l1sched_prim_from_msgb(msg);
	prim->sch_ind.frame_nr = fn;
	prim->sch_ind.bsic = bsic;

	return l1sched_prim_to_user(sched, msg);
}

int rx_sch_fn(struct l1sched_lchan_state *lchan,
	      const struct l1sched_burst_ind *bi)
{
	sbit_t payload[2 * 39];
	struct gsm_time time;
	uint8_t sb_info[4];
	uint8_t bsic;
	int rc;

	/* Obtain payload from burst */
	memcpy(payload, bi->burst + 3, 39);
	memcpy(payload + 39, bi->burst + 3 + 39 + 64, 39);

	/* Attempt to decode */
	rc = gsm0503_sch_decode(sb_info, payload);
	if (rc) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received bad SCH burst at fn=%u\n", bi->fn);
		return rc;
	}

	/* Decode BSIC and TDMA frame number */
	decode_sb(&time, &bsic, sb_info);

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "Received SCH: bsic=%u, fn=%u, sched_fn=%u\n",
		    bsic, time.fn, bi->fn);

	/* Check if decoded frame number matches */
	if (time.fn != bi->fn) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Decoded fn=%u does not match sched_fn=%u\n",
			    time.fn, bi->fn);
		return -EINVAL;
	}

	/* Update BSIC value in the scheduler state */
	lchan->ts->sched->bsic = bsic;

	return handle_sch_ind(lchan->ts->sched, time.fn, bsic);
}
