/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2018-2021 by Vadim Yanitskiy <axilirator@gmail.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/coding/gsm0503_coding.h>

#include "l1ctl_proto.h"
#include "scheduler.h"
#include "sched_trx.h"
#include "logging.h"
#include "trx_if.h"
#include "l1ctl.h"

int rx_pdtch_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid,
	const sbit_t *bits, const struct trx_meas_set *meas)
{
	const struct trx_lchan_desc *lchan_desc;
	uint8_t l2[GPRS_L2_MAX_LEN], *mask;
	int n_errors, n_bits_total, rc;
	sbit_t *buffer, *offset;
	size_t l2_len;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];
	mask = &lchan->rx_burst_mask;
	buffer = lchan->rx_bursts;

	LOGP(DSCHD, LOGL_DEBUG, "Packet data received on %s: "
		"fn=%u ts=%u bid=%u\n", lchan_desc->name, fn, ts->index, bid);

	/* Align to the first burst of a block */
	if (*mask == 0x00 && bid != 0)
		return 0;

	/* Update mask */
	*mask |= (1 << bid);

	/* Store the measurements */
	sched_trx_meas_push(lchan, meas);

	/* Copy burst to buffer of 4 bursts */
	offset = buffer + bid * 116;
	memcpy(offset, bits + 3, 58);
	memcpy(offset + 58, bits + 87, 58);

	/* Wait until complete set of bursts */
	if (bid != 3)
		return 0;

	/* Calculate AVG of the measurements */
	sched_trx_meas_avg(lchan, 4);

	/* Check for complete set of bursts */
	if ((*mask & 0xf) != 0xf) {
		LOGP(DSCHD, LOGL_ERROR, "Received incomplete (%s) data frame at "
			"fn=%u (%u/%u) for %s\n",
			burst_mask2str(mask, 4), lchan->meas_avg.fn,
			lchan->meas_avg.fn % ts->mf_layout->period,
			ts->mf_layout->period,
			lchan_desc->name);
		/* NOTE: do not abort here, give it a try. Maybe we're lucky ;) */
	}

	/* Keep the mask updated */
	*mask = *mask << 4;

	/* Attempt to decode */
	rc = gsm0503_pdtch_decode(l2, buffer,
		NULL, &n_errors, &n_bits_total);
	if (rc < 0) {
		LOGP(DSCHD, LOGL_ERROR, "Received bad %s frame (rc=%d, ber=%d/%d) at fn=%u\n",
		     lchan_desc->name, rc, n_errors, n_bits_total, lchan->meas_avg.fn);
	}

	/* Determine L2 length */
	l2_len = rc > 0 ? rc : 0;

	/* Send a L2 frame to the higher layers */
	sched_send_dt_ind(trx, ts, lchan,
		l2, l2_len, n_errors, rc < 0, true);

	return 0;
}


int tx_pdtch_fn(struct trx_instance *trx, struct trx_ts *ts,
		struct trx_lchan_state *lchan,
		struct sched_burst_req *br)
{
	const struct trx_lchan_desc *lchan_desc;
	ubit_t *buffer, *offset;
	const uint8_t *tsc;
	uint8_t *mask;
	int rc;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];
	mask = &lchan->tx_burst_mask;
	buffer = lchan->tx_bursts;

	if (br->bid > 0) {
		/* If we have encoded bursts */
		if (*mask)
			goto send_burst;
		else
			return 0;
	}

	/* Encode payload */
	rc = gsm0503_pdtch_encode(buffer, lchan->prim->payload,
		lchan->prim->payload_len);
	if (rc < 0) {
		LOGP(DSCHD, LOGL_ERROR, "Failed to encode L2 payload (len=%zu): %s\n",
		     lchan->prim->payload_len, osmo_hexdump(lchan->prim->payload,
							    lchan->prim->payload_len));

		/* Forget this primitive */
		sched_prim_drop(lchan);

		return -EINVAL;
	}

send_burst:
	/* Determine which burst should be sent */
	offset = buffer + br->bid * 116;

	/* Update mask */
	*mask |= (1 << br->bid);

	/* Choose proper TSC */
	tsc = sched_nb_training_bits[trx->tsc];

	/* Compose a new burst */
	memset(br->burst, 0, 3); /* TB */
	memcpy(br->burst + 3, offset, 58); /* Payload 1/2 */
	memcpy(br->burst + 61, tsc, 26); /* TSC */
	memcpy(br->burst + 87, offset + 58, 58); /* Payload 2/2 */
	memset(br->burst + 145, 0, 3); /* TB */
	br->burst_len = GSM_BURST_LEN;

	LOGP(DSCHD, LOGL_DEBUG, "Scheduled %s fn=%u ts=%u burst=%u\n",
		lchan_desc->name, br->fn, ts->index, br->bid);

	/* If we have sent the last (4/4) burst */
	if ((*mask & 0x0f) == 0x0f) {
		/* Confirm data / traffic sending */
		sched_send_dt_conf(trx, ts, lchan, br->fn, true);

		/* Forget processed primitive */
		sched_prim_drop(lchan);

		/* Reset mask */
		*mask = 0x00;
	}

	return 0;
}
