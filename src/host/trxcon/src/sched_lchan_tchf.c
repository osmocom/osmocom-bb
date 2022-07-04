/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2017-2021 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/coding/gsm0503_coding.h>
#include <osmocom/codec/codec.h>

#include <osmocom/bb/trxcon/l1ctl_proto.h>
#include <osmocom/bb/trxcon/l1sched.h>
#include <osmocom/bb/trxcon/logging.h>
#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/l1ctl.h>

int rx_tchf_fn(struct trx_instance *trx, struct l1sched_ts *ts,
	struct l1sched_lchan_state *lchan, uint32_t fn, uint8_t bid,
	const sbit_t *bits, const struct l1sched_meas_set *meas)
{
	const struct l1sched_lchan_desc *lchan_desc;
	int n_errors = -1, n_bits_total, rc;
	sbit_t *buffer, *offset;
	uint8_t l2[128], *mask;
	size_t l2_len;

	/* Set up pointers */
	lchan_desc = &l1sched_lchan_desc[lchan->type];
	mask = &lchan->rx_burst_mask;
	buffer = lchan->rx_bursts;

	LOGP(DSCHD, LOGL_DEBUG, "Traffic received on %s: fn=%u ts=%u bid=%u\n",
		lchan_desc->name, fn, ts->index, bid);

	/* Align to the first burst of a block */
	if (*mask == 0x00 && bid != 0)
		return 0;

	/* Update mask */
	*mask |= (1 << bid);

	/* Store the measurements */
	l1sched_lchan_meas_push(lchan, meas);

	/* Copy burst to end of buffer of 8 bursts */
	offset = buffer + bid * 116 + 464;
	memcpy(offset, bits + 3, 58);
	memcpy(offset + 58, bits + 87, 58);

	/* Wait until complete set of bursts */
	if (bid != 3)
		return 0;

	/* Calculate AVG of the measurements */
	l1sched_lchan_meas_avg(lchan, 8);

	/* Check for complete set of bursts */
	if ((*mask & 0xff) != 0xff) {
		LOGP(DSCHD, LOGL_ERROR, "Received incomplete (%s) traffic frame at "
			"fn=%u (%u/%u) for %s\n",
			l1sched_burst_mask2str(mask, 8), lchan->meas_avg.fn,
			lchan->meas_avg.fn % ts->mf_layout->period,
			ts->mf_layout->period,
			lchan_desc->name);
		/* NOTE: do not abort here, give it a try. Maybe we're lucky ;) */

	}

	/* Keep the mask updated */
	*mask = *mask << 4;

	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1: /* FR */
		rc = gsm0503_tch_fr_decode(l2, buffer,
			1, 0, &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_EFR: /* EFR */
		rc = gsm0503_tch_fr_decode(l2, buffer,
			1, 1, &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_AMR: /* AMR */
		/**
		 * TODO: AMR requires a dedicated loop,
		 * which will be implemented later...
		 */
		LOGP(DSCHD, LOGL_ERROR, "AMR isn't supported yet\n");
		return -ENOTSUP;
	default:
		LOGP(DSCHD, LOGL_ERROR, "Invalid TCH mode: %u\n", lchan->tch_mode);
		return -EINVAL;
	}

	/* Shift buffer by 4 bursts for interleaving */
	memcpy(buffer, buffer + 464, 464);

	/* Check decoding result */
	if (rc < 4) {
		LOGP(DSCHD, LOGL_ERROR, "Received bad %s frame (rc=%d, ber=%d/%d) at fn=%u\n",
		     lchan_desc->name, rc, n_errors, n_bits_total, lchan->meas_avg.fn);

		/* Send BFI */
		goto bfi;
	} else if (rc == GSM_MACBLOCK_LEN) {
		/* FACCH received, forward it to the higher layers */
		l1sched_send_dt_ind(trx, ts, lchan, l2, GSM_MACBLOCK_LEN,
			n_errors, false, false);

		/* Send BFI substituting a stolen TCH frame */
		n_errors = -1; /* ensure fake measurements */
		goto bfi;
	} else {
		/* A good TCH frame received */
		l2_len = rc;
	}

	/* Send a traffic frame to the higher layers */
	return l1sched_send_dt_ind(trx, ts, lchan, l2, l2_len,
		n_errors, false, true);

bfi:
	/* Didn't try to decode, fake measurements */
	if (n_errors < 0) {
		lchan->meas_avg = (struct l1sched_meas_set) {
			.fn = lchan->meas_avg.fn,
			.toa256 = 0,
			.rssi = -110,
		};

		/* No bursts => no errors */
		n_errors = 0;
	}

	/* BFI is not applicable in signalling mode */
	if (lchan->tch_mode == GSM48_CMODE_SIGN)
		return l1sched_send_dt_ind(trx, ts, lchan, NULL, 0,
			n_errors, true, false);

	/* Bad frame indication */
	l2_len = l1sched_bad_frame_ind(l2, lchan);

	/* Send a BFI frame to the higher layers */
	return l1sched_send_dt_ind(trx, ts, lchan, l2, l2_len,
		n_errors, true, true);
}

int tx_tchf_fn(struct trx_instance *trx, struct l1sched_ts *ts,
	       struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	const struct l1sched_lchan_desc *lchan_desc;
	ubit_t *buffer, *offset;
	const uint8_t *tsc;
	uint8_t *mask;
	size_t l2_len;
	int rc;

	/* Set up pointers */
	lchan_desc = &l1sched_lchan_desc[lchan->type];
	mask = &lchan->tx_burst_mask;
	buffer = lchan->tx_bursts;

	/* If we have encoded bursts */
	if (*mask)
		goto send_burst;

	/* Wait until a first burst in period */
	if (br->bid > 0)
		return 0;

	/* Check the current TCH mode */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1: /* FR */
		l2_len = GSM_FR_BYTES;
		break;
	case GSM48_CMODE_SPEECH_EFR: /* EFR */
		l2_len = GSM_EFR_BYTES;
		break;
	case GSM48_CMODE_SPEECH_AMR: /* AMR */
		/**
		 * TODO: AMR requires a dedicated loop,
		 * which will be implemented later...
		 */
		LOGP(DSCHD, LOGL_ERROR, "AMR isn't supported yet, "
			"dropping frame...\n");

		/* Forget this primitive */
		l1sched_prim_drop(lchan);

		return -ENOTSUP;
	default:
		LOGP(DSCHD, LOGL_ERROR, "Invalid TCH mode: %u, "
			"dropping frame...\n", lchan->tch_mode);

		/* Forget this primitive */
		l1sched_prim_drop(lchan);

		return -EINVAL;
	}

	/* Determine and check the payload length */
	if (lchan->prim->payload_len == GSM_MACBLOCK_LEN) {
		l2_len = GSM_MACBLOCK_LEN; /* FACCH */
	} else if (lchan->prim->payload_len != l2_len) {
		LOGP(DSCHD, LOGL_ERROR, "Primitive has odd length %zu "
			"(expected %zu for TCH or %u for FACCH), so dropping...\n",
			lchan->prim->payload_len, l2_len, GSM_MACBLOCK_LEN);

		l1sched_prim_drop(lchan);
		return -EINVAL;
	}

	/* Shift buffer by 4 bursts back for interleaving */
	memcpy(buffer, buffer + 464, 464);

	/* Encode payload */
	rc = gsm0503_tch_fr_encode(buffer, lchan->prim->payload, l2_len, 1);
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
	tsc = l1sched_nb_training_bits[trx->tsc];

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
	if (*mask == 0x0f) {
		/* Confirm data / traffic sending */
		l1sched_send_dt_conf(trx, ts, lchan, br->fn, L1SCHED_PRIM_IS_TCH(lchan->prim));

		/* Forget processed primitive */
		l1sched_prim_drop(lchan);

		/* Reset mask */
		*mask = 0x00;
	}

	return 0;
}
