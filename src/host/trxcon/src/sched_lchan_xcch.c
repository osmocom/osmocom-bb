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
#include <stdint.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/coding/gsm0503_coding.h>

#include <osmocom/bb/trxcon/l1sched.h>
#include <osmocom/bb/trxcon/logging.h>

int rx_data_fn(struct l1sched_lchan_state *lchan,
	       uint32_t fn, uint8_t bid, const sbit_t *bits,
	       const struct l1sched_meas_set *meas)
{
	const struct l1sched_lchan_desc *lchan_desc;
	uint8_t l2[GSM_MACBLOCK_LEN], *mask;
	int n_errors, n_bits_total, rc;
	sbit_t *buffer, *offset;

	/* Set up pointers */
	lchan_desc = &l1sched_lchan_desc[lchan->type];
	mask = &lchan->rx_burst_mask;
	buffer = lchan->rx_bursts;

	LOGP(DSCHD, LOGL_DEBUG, "Data received on %s: fn=%u ts=%u bid=%u\n",
		lchan_desc->name, fn, lchan->ts->index, bid);

	/* Align to the first burst of a block */
	if (*mask == 0x00 && bid != 0)
		return 0;

	/* Update mask */
	*mask |= (1 << bid);

	/* Store the measurements */
	l1sched_lchan_meas_push(lchan, meas);

	/* Copy burst to buffer of 4 bursts */
	offset = buffer + bid * 116;
	memcpy(offset, bits + 3, 58);
	memcpy(offset + 58, bits + 87, 58);

	/* Wait until complete set of bursts */
	if (bid != 3)
		return 0;

	/* Calculate AVG of the measurements */
	l1sched_lchan_meas_avg(lchan, 4);

	/* Check for complete set of bursts */
	if ((*mask & 0xf) != 0xf) {
		LOGP(DSCHD, LOGL_ERROR, "Received incomplete (%s) data frame at "
			"fn=%u (%u/%u) for %s\n",
			l1sched_burst_mask2str(mask, 4), lchan->meas_avg.fn,
			lchan->meas_avg.fn % lchan->ts->mf_layout->period,
			lchan->ts->mf_layout->period,
			lchan_desc->name);
		/* NOTE: xCCH has an insane amount of redundancy for error
		 * correction, so even just 2 valid bursts might be enough
		 * to reconstruct some L2 frames. This is why we do not
		 * abort here. */
	}

	/* Keep the mask updated */
	*mask = *mask << 4;

	/* Attempt to decode */
	rc = gsm0503_xcch_decode(l2, buffer, &n_errors, &n_bits_total);
	if (rc) {
		LOGP(DSCHD, LOGL_ERROR, "Received bad %s frame (rc=%d, ber=%d/%d) at fn=%u\n",
		     lchan_desc->name, rc, n_errors, n_bits_total, lchan->meas_avg.fn);
	}

	/* Send a L2 frame to the higher layers */
	return l1sched_handle_data_ind(lchan, l2, rc ? 0 : GSM_MACBLOCK_LEN,
				       n_errors, n_bits_total,
				       L1SCHED_DT_SIGNALING);
}

int tx_data_fn(struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	const struct l1sched_lchan_desc *lchan_desc;
	ubit_t *buffer, *offset;
	const uint8_t *tsc;
	uint8_t *mask;
	int rc;

	/* Set up pointers */
	lchan_desc = &l1sched_lchan_desc[lchan->type];
	mask = &lchan->tx_burst_mask;
	buffer = lchan->tx_bursts;

	if (br->bid > 0) {
		/* If we have encoded bursts */
		if (*mask)
			goto send_burst;
		else
			return 0;
	}

	/* Check the prim payload length */
	if (lchan->prim->payload_len != GSM_MACBLOCK_LEN) {
		LOGP(DSCHD, LOGL_ERROR, "Primitive has odd length %zu (expected %u), "
			"so dropping...\n", lchan->prim->payload_len, GSM_MACBLOCK_LEN);

		l1sched_prim_drop(lchan);
		return -EINVAL;
	}

	/* Encode payload */
	rc = gsm0503_xcch_encode(buffer, lchan->prim->payload);
	if (rc) {
		LOGP(DSCHD, LOGL_ERROR, "Failed to encode L2 payload (len=%zu): %s\n",
		     lchan->prim->payload_len, osmo_hexdump(lchan->prim->payload,
							    lchan->prim->payload_len));

		/* Forget this primitive */
		l1sched_prim_drop(lchan);

		return -EINVAL;
	}

send_burst:
	/* Determine which burst should be sent */
	offset = buffer + br->bid * 116;

	/* Update mask */
	*mask |= (1 << br->bid);

	/* Choose proper TSC */
	tsc = l1sched_nb_training_bits[lchan->tsc];

	/* Compose a new burst */
	memset(br->burst, 0, 3); /* TB */
	memcpy(br->burst + 3, offset, 58); /* Payload 1/2 */
	memcpy(br->burst + 61, tsc, 26); /* TSC */
	memcpy(br->burst + 87, offset + 58, 58); /* Payload 2/2 */
	memset(br->burst + 145, 0, 3); /* TB */
	br->burst_len = GSM_BURST_LEN;

	LOGP(DSCHD, LOGL_DEBUG, "Scheduled %s fn=%u ts=%u burst=%u\n",
		lchan_desc->name, br->fn, lchan->ts->index, br->bid);

	/* If we have sent the last (4/4) burst */
	if ((*mask & 0x0f) == 0x0f) {
		/* Confirm data sending */
		l1sched_handle_data_cnf(lchan, br->fn, L1SCHED_DT_SIGNALING);

		/* Forget processed primitive */
		l1sched_prim_drop(lchan);

		/* Reset mask */
		*mask = 0x00;
	}

	return 0;
}
