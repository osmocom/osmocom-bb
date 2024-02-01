/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2018-2022 by Vadim Yanitskiy <axilirator@gmail.com>
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
#include <stdint.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/gsm0502.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/coding/gsm0503_coding.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

int rx_pdtch_fn(struct l1sched_lchan_state *lchan,
		const struct l1sched_burst_ind *bi)
{
	uint8_t l2[GPRS_L2_MAX_LEN];
	int n_errors, n_bits_total, rc;
	sbit_t *bursts_p, *burst;
	size_t l2_len;
	uint32_t *mask;

	/* Set up pointers */
	mask = &lchan->rx_burst_mask;
	bursts_p = lchan->rx_bursts;

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "Packet data received: fn=%u bid=%u\n", bi->fn, bi->bid);

	/* Align to the first burst of a block */
	if (*mask == 0x00 && bi->bid != 0)
		return 0;

	/* Update mask */
	*mask |= (1 << bi->bid);

	/* Store the measurements */
	l1sched_lchan_meas_push(lchan, bi);

	/* Copy burst to buffer of 4 bursts */
	burst = bursts_p + bi->bid * 116;
	memcpy(burst, bi->burst + 3, 58);
	memcpy(burst + 58, bi->burst + 87, 58);

	/* Wait until complete set of bursts */
	if (bi->bid != 3)
		return 0;

	/* Calculate AVG of the measurements */
	l1sched_lchan_meas_avg(lchan, 4);

	/* Check for complete set of bursts */
	if ((*mask & 0xf) != 0xf) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received incomplete (%s) packet data at fn=%u (%u/%u)\n",
			    l1sched_burst_mask2str(mask, 4), lchan->meas_avg.fn,
			    lchan->meas_avg.fn % lchan->ts->mf_layout->period,
			    lchan->ts->mf_layout->period);
		/* NOTE: do not abort here, give it a try. Maybe we're lucky ;) */
	}

	/* Keep the mask updated */
	*mask = *mask << 4;

	/* Attempt to decode */
	rc = gsm0503_pdtch_decode(l2, bursts_p,
		NULL, &n_errors, &n_bits_total);
	if (rc < 0) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received bad frame (rc=%d, ber=%d/%d) at fn=%u\n",
			    rc, n_errors, n_bits_total, lchan->meas_avg.fn);
	}

	/* Determine L2 length */
	l2_len = rc > 0 ? rc : 0;

	/* Send a L2 frame to the higher layers */
	l1sched_lchan_emit_data_ind(lchan, l2, l2_len, n_errors, n_bits_total, true);

	return 0;
}

static struct msgb *prim_dequeue_pdtch(struct l1sched_lchan_state *lchan, uint32_t fn)
{
	while (!llist_empty(&lchan->tx_prims)) {
		struct msgb *msg = llist_first_entry(&lchan->tx_prims, struct msgb, list);
		const struct l1sched_prim *prim = l1sched_prim_from_msgb(msg);
		int ret = gsm0502_fncmp(prim->data_req.frame_nr, fn);

		if (OSMO_LIKELY(ret == 0)) { /* it's a match! */
			llist_del(&msg->list);
			return msg;
		} else if (ret > 0) { /* not now, come back later */
			break;
		} /* else: the ship has sailed, drop your ticket */

		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "%s(): dropping stale Tx prim (current Fn=%u, prim Fn=%u): %s\n",
			    __func__, fn, prim->data_req.frame_nr, msgb_hexdump_l2(msg));
		llist_del(&msg->list);
		msgb_free(msg);
	}

	return NULL;
}

int tx_pdtch_fn(struct l1sched_lchan_state *lchan,
		struct l1sched_burst_req *br)
{
	ubit_t *bursts_p, *burst;
	const uint8_t *tsc;
	uint32_t *mask;
	int rc;

	/* Set up pointers */
	mask = &lchan->tx_burst_mask;
	bursts_p = lchan->tx_bursts;

	if (br->bid > 0) {
		if ((*mask & 0x01) != 0x01)
			return -ENOENT;
		goto send_burst;
	}

	*mask = *mask << 4;

	struct msgb *msg = prim_dequeue_pdtch(lchan, br->fn);
	if (msg == NULL)
		return -ENOENT;

	/* Encode payload */
	rc = gsm0503_pdtch_encode(bursts_p, msgb_l2(msg), msgb_l2len(msg));
	if (rc < 0) {
		LOGP_LCHAND(lchan, LOGL_ERROR, "Failed to encode L2 payload (len=%u): %s\n",
			    msgb_l2len(msg), msgb_hexdump_l2(msg));
		msgb_free(msg);
		return -EINVAL;
	}

	/* Cache the prim, so that we can confirm it later (see below) */
	OSMO_ASSERT(lchan->prim == NULL);
	lchan->prim = msg;

send_burst:
	/* Determine which burst should be sent */
	burst = bursts_p + br->bid * 116;

	/* Update mask */
	*mask |= (1 << br->bid);

	/* Choose proper TSC */
	tsc = l1sched_nb_training_bits[lchan->tsc];

	/* Compose a new burst */
	memset(br->burst, 0, 3); /* TB */
	memcpy(br->burst + 3, burst, 58); /* Payload 1/2 */
	memcpy(br->burst + 61, tsc, 26); /* TSC */
	memcpy(br->burst + 87, burst + 58, 58); /* Payload 2/2 */
	memset(br->burst + 145, 0, 3); /* TB */
	br->burst_len = GSM_NBITS_NB_GMSK_BURST;

	LOGP_LCHAND(lchan, LOGL_DEBUG, "Scheduled at fn=%u burst=%u\n", br->fn, br->bid);

	if (br->bid == 3) {
		/* Confirm data / traffic sending (pass ownership of the msgb/prim) */
		l1sched_lchan_emit_data_cnf(lchan, lchan->prim,
					    GSM_TDMA_FN_SUB(br->fn, 3));
		lchan->prim = NULL;
	}

	return 0;
}
