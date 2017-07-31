/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <errno.h>
#include <string.h>
#include <talloc.h>
#include <stdint.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/coding/gsm0503_coding.h>

#include "l1ctl_proto.h"
#include "scheduler.h"
#include "sched_trx.h"
#include "logging.h"
#include "trx_if.h"
#include "trxcon.h"
#include "l1ctl.h"

/**
 * 41-bit RACH synchronization sequence
 * GSM 05.02 Chapter 5.2.7 Access burst (AB)
 */
static ubit_t rach_synch_seq[] = {
	0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1,
	1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0,
	1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0,
};

/* Obtain a to-be-transmitted RACH burst */
int tx_rach_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid)
{
	struct trx_ts_prim *prim;
	struct l1ctl_rach_req *req;
	uint8_t burst[GSM_BURST_LEN];
	uint8_t payload[36];
	int rc;

	/* Get a message from TX queue */
	prim = llist_entry(ts->tx_prims.next, struct trx_ts_prim, list);
	req = (struct l1ctl_rach_req *) prim->payload;

	/* Delay RACH sending according to offset value */
	if (req->offset-- > 0)
		return 0;

	/* Encode payload */
	rc = gsm0503_rach_encode(payload, &req->ra, trx->bsic);
	if (rc) {
		LOGP(DSCH, LOGL_ERROR, "Could not encode RACH burst\n");
		return rc;
	}

	/* Compose RACH burst */
	memset(burst, 0, 8); /* TB */
	memcpy(burst + 8, rach_synch_seq, 41); /* sync seq */
	memcpy(burst + 49, payload, 36); /* payload */
	memset(burst + 85, 0, 63); /* TB + GP */

	LOGP(DSCH, LOGL_DEBUG, "Transmitting RACH fn=%u\n", fn);

	/* Send burst to transceiver */
	rc = trx_if_tx_burst(trx, ts->index, fn, 10, burst);
	if (rc) {
		LOGP(DSCH, LOGL_ERROR, "Could not send burst to transceiver\n");
		return rc;
	}

	/* Confirm RACH request */
	l1ctl_tx_rach_conf(trx->l1l, fn);

	/* Remove primitive from queue and free memory */
	llist_del(&prim->list);
	talloc_free(prim);

	return 0;
}
