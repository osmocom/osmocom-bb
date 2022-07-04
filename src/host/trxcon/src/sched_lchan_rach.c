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
#include <stdbool.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/coding/gsm0503_coding.h>

#include <osmocom/bb/trxcon/l1ctl_proto.h>
#include <osmocom/bb/trxcon/l1sched.h>
#include <osmocom/bb/trxcon/logging.h>
#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/l1ctl.h>

/* 3GPP TS 05.02, section 5.2.7 "Access burst (AB)" */
#define RACH_EXT_TAIL_BITS_LEN	8
#define RACH_SYNCH_SEQ_LEN	41
#define RACH_PAYLOAD_LEN	36

/* Extended tail bits (BN0..BN7) */
static const ubit_t rach_ext_tail_bits[] = {
	0, 0, 1, 1, 1, 0, 1, 0,
};

/* Synchronization (training) sequence types */
enum rach_synch_seq_t {
	RACH_SYNCH_SEQ_UNKNOWN = -1,
	RACH_SYNCH_SEQ_TS0, /* GSM, GMSK (default) */
	RACH_SYNCH_SEQ_TS1, /* EGPRS, 8-PSK */
	RACH_SYNCH_SEQ_TS2, /* EGPRS, GMSK */
	RACH_SYNCH_SEQ_NUM
};

/* Synchronization (training) sequence bits */
static const char rach_synch_seq_bits[RACH_SYNCH_SEQ_NUM][RACH_SYNCH_SEQ_LEN] = {
	[RACH_SYNCH_SEQ_TS0] = "01001011011111111001100110101010001111000",
	[RACH_SYNCH_SEQ_TS1] = "01010100111110001000011000101111001001101",
	[RACH_SYNCH_SEQ_TS2] = "11101111001001110101011000001101101110111",
};

/* Synchronization (training) sequence names */
static struct value_string rach_synch_seq_names[] = {
	{ RACH_SYNCH_SEQ_UNKNOWN, "UNKNOWN" },
	{ RACH_SYNCH_SEQ_TS0, "TS0: GSM, GMSK" },
	{ RACH_SYNCH_SEQ_TS1, "TS1: EGPRS, 8-PSK" },
	{ RACH_SYNCH_SEQ_TS2, "TS2: EGPRS, GMSK" },
	{ 0, NULL },
};

/* Obtain a to-be-transmitted RACH burst */
int tx_rach_fn(struct trx_instance *trx, struct l1sched_ts *ts,
	       struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	struct l1ctl_ext_rach_req *ext_req = NULL;
	struct l1ctl_rach_req *req = NULL;
	enum rach_synch_seq_t synch_seq;
	uint8_t *burst_ptr = br->burst;
	uint8_t payload[36];
	int i, rc;

	/* Is it extended (11-bit) RACH or not? */
	if (L1SCHED_PRIM_IS_RACH11(lchan->prim)) {
		ext_req = (struct l1ctl_ext_rach_req *) lchan->prim->payload;
		synch_seq = ext_req->synch_seq;

		/* Check requested synch. sequence */
		if (synch_seq >= RACH_SYNCH_SEQ_NUM) {
			LOGP(DSCHD, LOGL_ERROR, "Unknown RACH synch. sequence=0x%02x\n", synch_seq);

			/* Forget this primitive */
			l1sched_prim_drop(lchan);
			return -ENOTSUP;
		}

		/* Delay sending according to offset value */
		if (ext_req->offset-- > 0)
			return 0;

		/* Encode extended (11-bit) payload */
		rc = gsm0503_rach_ext_encode(payload, ext_req->ra11, trx->bsic, true);
		if (rc) {
			LOGP(DSCHD, LOGL_ERROR, "Could not encode extended RACH burst "
						"(ra=%u bsic=%u)\n", ext_req->ra11, trx->bsic);

			/* Forget this primitive */
			l1sched_prim_drop(lchan);
			return rc;
		}
	} else if (L1SCHED_PRIM_IS_RACH8(lchan->prim)) {
		req = (struct l1ctl_rach_req *) lchan->prim->payload;
		synch_seq = RACH_SYNCH_SEQ_TS0;

		/* Delay sending according to offset value */
		if (req->offset-- > 0)
			return 0;

		/* Encode regular (8-bit) payload */
		rc = gsm0503_rach_ext_encode(payload, req->ra, trx->bsic, false);
		if (rc) {
			LOGP(DSCHD, LOGL_ERROR, "Could not encode RACH burst "
						"(ra=%u bsic=%u)\n", req->ra, trx->bsic);

			/* Forget this primitive */
			l1sched_prim_drop(lchan);
			return rc;
		}
	} else {
		LOGP(DSCHD, LOGL_ERROR, "Primitive has odd length %zu (expected %zu or %zu), "
			"so dropping...\n", lchan->prim->payload_len,
			sizeof(*req), sizeof(*ext_req));
		l1sched_prim_drop(lchan);
		return -EINVAL;
	}


	/* BN0-7: extended tail bits */
	memcpy(burst_ptr, rach_ext_tail_bits, RACH_EXT_TAIL_BITS_LEN);
	burst_ptr += RACH_EXT_TAIL_BITS_LEN;

	/* BN8-48: chosen synch. (training) sequence */
	for (i = 0; i < RACH_SYNCH_SEQ_LEN; i++)
		*(burst_ptr++) = rach_synch_seq_bits[synch_seq][i] == '1';

	/* BN49-84: encrypted bits (the payload) */
	memcpy(burst_ptr, payload, RACH_PAYLOAD_LEN);
	burst_ptr += RACH_PAYLOAD_LEN;

	/* BN85-156: tail bits & extended guard period */
	memset(burst_ptr, 0, br->burst + GSM_BURST_LEN - burst_ptr);
	br->burst_len = GSM_BURST_LEN;

	LOGP(DSCHD, LOGL_NOTICE, "Scheduled %s RACH (%s) on fn=%u, tn=%u, lchan=%s\n",
		L1SCHED_PRIM_IS_RACH11(lchan->prim) ? "extended (11-bit)" : "regular (8-bit)",
		get_value_string(rach_synch_seq_names, synch_seq), br->fn,
		ts->index, l1sched_lchan_desc[lchan->type].name);

	/* Confirm RACH request */
	l1ctl_tx_rach_conf(trx->l1l, trx->band_arfcn, br->fn);

	/* Optional GSMTAP logging */
	l1sched_gsmtap_send(lchan->type, br->fn, ts->index,
			  trx->band_arfcn | ARFCN_UPLINK, 0, 0,
			  L1SCHED_PRIM_IS_RACH11(lchan->prim) ? (uint8_t *) &ext_req->ra11 : &req->ra,
			  L1SCHED_PRIM_IS_RACH11(lchan->prim) ? 2 : 1);

	/* Forget processed primitive */
	l1sched_prim_drop(lchan);

	return 0;
}
