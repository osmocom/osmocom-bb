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
#include <stdbool.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/coding/gsm0503_coding.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

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
int tx_rach_fn(struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	const uint8_t bsic = lchan->ts->sched->bsic;
	struct l1sched_prim *prim;
	uint8_t *burst_ptr = br->burst;
	uint8_t payload[36];
	int i, rc;

	prim = l1sched_prim_from_msgb(lchan->prim);

	/* Delay sending according to offset value */
	if (prim->rach_req.offset-- > 0)
		return 0;

	/* Check requested synch. sequence */
	if (prim->rach_req.synch_seq >= RACH_SYNCH_SEQ_NUM) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Unknown RACH synch. sequence=0x%02x\n",
			    prim->rach_req.synch_seq);
		l1sched_lchan_prim_drop(lchan);
		return -ENOTSUP;
	}

	/* Encode the payload */
	rc = gsm0503_rach_ext_encode(payload, prim->rach_req.ra,
				     bsic, prim->rach_req.is_11bit);
	if (rc) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Could not encode %s-bit RACH burst (ra=%u bsic=%u)\n",
			    prim->rach_req.is_11bit ? "11" : "8",
			    prim->rach_req.ra, bsic);
		l1sched_lchan_prim_drop(lchan);
		return rc;
	}

	/* BN0-7: extended tail bits */
	memcpy(burst_ptr, rach_ext_tail_bits, RACH_EXT_TAIL_BITS_LEN);
	burst_ptr += RACH_EXT_TAIL_BITS_LEN;

	/* BN8-48: chosen synch. (training) sequence */
	for (i = 0; i < RACH_SYNCH_SEQ_LEN; i++)
		*(burst_ptr++) = rach_synch_seq_bits[prim->rach_req.synch_seq][i] == '1';

	/* BN49-84: encrypted bits (the payload) */
	memcpy(burst_ptr, payload, RACH_PAYLOAD_LEN);
	burst_ptr += RACH_PAYLOAD_LEN;

	/* BN85-156: tail bits & extended guard period */
	memset(burst_ptr, 0, br->burst + GSM_NBITS_NB_GMSK_BURST - burst_ptr);
	br->burst_len = GSM_NBITS_NB_GMSK_BURST;

	LOGP_LCHAND(lchan, LOGL_NOTICE, "Scheduled %s-bit RACH (%s) at fn=%u\n",
		    prim->rach_req.is_11bit ? "11" : "8",
		    get_value_string(rach_synch_seq_names, prim->rach_req.synch_seq), br->fn);

	/* Confirm RACH request (pass ownership of the prim) */
	l1sched_lchan_emit_data_cnf(lchan, br->fn);

	return 0;
}
