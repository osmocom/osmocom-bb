/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2018-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2018 by Harald Welte <laforge@gnumonks.org>
 * (C) 2020-2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/core/utils.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/coding/gsm0503_coding.h>
#include <osmocom/coding/gsm0503_amr_dtx.h>
#include <osmocom/codec/codec.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

/* Burst Payload LENgth (short alias) */
#define BPLEN GSM_NBITS_NB_GMSK_PAYLOAD

/* Burst BUFfer capacity (in BPLEN units) */
#define BUFMAX 24

/* Burst BUFfer position macros */
#define BUFPOS(buf, n) &buf[(n) * BPLEN]
#define BUFTAIL8(buf) BUFPOS(buf, (BUFMAX - 8))

/* 3GPP TS 45.009, table 3.2.1.3-{2,4}: AMR on Downlink TCH/H.
 *
 * +---+---+---+---+---+---+
 * | a | b | c | d | e | f |  Burst 'a' received first
 * +---+---+---+---+---+---+
 *  ^^^^^^^^^^^^^^^^^^^^^^^   FACCH frame  (bursts 'a' .. 'f')
 *  ^^^^^^^^^^^^^^^           Speech frame (bursts 'a' .. 'd')
 *
 * TDMA frame number of burst 'f' is always used as the table index. */
static const uint8_t sched_tchh_dl_amr_cmi_map[26] = {
	[15] = 1, /* TCH/H(0): a=4  / d=10 / f=15 */
	[23] = 1, /* TCH/H(0): a=13 / d=19 / f=23 */
	[6]  = 1, /* TCH/H(0): a=21 / d=2  / f=6 */

	[16] = 1, /* TCH/H(1): a=5  / d=11 / f=16 */
	[24] = 1, /* TCH/H(1): a=14 / d=20 / f=24 */
	[7]  = 1, /* TCH/H(1): a=22 / d=3  / f=7 */
};

/* TDMA frame number of burst 'a' is always used as the table index. */
static const uint8_t sched_tchh_ul_amr_cmi_map[26] = {
	[0]  = 1, /* TCH/H(0): a=0 */
	[8]  = 1, /* TCH/H(0): a=8 */
	[17] = 1, /* TCH/H(0): a=17 */

	[1]  = 1, /* TCH/H(1): a=1 */
	[9]  = 1, /* TCH/H(1): a=9 */
	[18] = 1, /* TCH/H(1): a=18 */
};

static const uint8_t tch_h0_traffic_block_map[3][4] = {
	/* B0(0,2,4,6), B1(4,6,8,10), B2(8,10,0,2) */
	{ 0, 2, 4, 6 },
	{ 4, 6, 8, 10 },
	{ 8, 10, 0, 2 },
};

static const uint8_t tch_h1_traffic_block_map[3][4] = {
	/* B0(1,3,5,7), B1(5,7,9,11), B2(9,11,1,3) */
	{ 1, 3, 5, 7 },
	{ 5, 7, 9, 11 },
	{ 9, 11, 1, 3 },
};

static const uint8_t tch_h0_dl_facch_block_map[3][6] = {
	/* B0(4,6,8,10,13,15), B1(13,15,17,19,21,23), B2(21,23,0,2,4,6) */
	{ 4, 6, 8, 10, 13, 15 },
	{ 13, 15, 17, 19, 21, 23 },
	{ 21, 23, 0, 2, 4, 6 },
};

static const uint8_t tch_h0_ul_facch_block_map[3][6] = {
	/* B0(0,2,4,6,8,10), B1(8,10,13,15,17,19), B2(17,19,21,23,0,2) */
	{ 0, 2, 4, 6, 8, 10 },
	{ 8, 10, 13, 15, 17, 19 },
	{ 17, 19, 21, 23, 0, 2 },
};

static const uint8_t tch_h1_dl_facch_block_map[3][6] = {
	/* B0(5,7,9,11,14,16), B1(14,16,18,20,22,24), B2(22,24,1,3,5,7) */
	{ 5, 7, 9, 11, 14, 16 },
	{ 14, 16, 18, 20, 22, 24 },
	{ 22, 24, 1, 3, 5, 7 },
};

const uint8_t tch_h1_ul_facch_block_map[3][6] = {
	/* B0(1,3,5,7,9,11), B1(9,11,14,16,18,20), B2(18,20,22,24,1,3) */
	{ 1, 3, 5, 7, 9, 11 },
	{ 9, 11, 14, 16, 18, 20 },
	{ 18, 20, 22, 24, 1, 3 },
};

/* FACCH/H channel mapping for Downlink (see 3GPP TS 45.002, table 1).
 * This mapping is valid for both FACCH/H(0) and FACCH/H(1).
 * TDMA frame number of burst 'f' is used as the table index. */
static const uint8_t sched_tchh_dl_facch_map[26] = {
	[15] = 1, /* FACCH/H(0): B0(4,6,8,10,13,15) */
	[16] = 1, /* FACCH/H(1): B0(5,7,9,11,14,16) */
	[23] = 1, /* FACCH/H(0): B1(13,15,17,19,21,23) */
	[24] = 1, /* FACCH/H(1): B1(14,16,18,20,22,24) */
	[6]  = 1, /* FACCH/H(0): B2(21,23,0,2,4,6) */
	[7]  = 1, /* FACCH/H(1): B2(22,24,1,3,5,7) */
};

/* 3GPP TS 45.002, table 2 in clause 7: Mapping tables for TCH/H2.4 and TCH/H4.8.
 *
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 * | a | b | c | d | e | f | g | h | i | j | k | l | m | n | o | p | q | r | s | t | u | v |
 * +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
 *
 * TCH/H(0): B0(0,2,4,6,8,10,13,15,17,19,21,23,0,2,4,6,8,10,13,15,17,19)
 * TCH/H(1): B0(1,3,5,7,9,11,14,16,18,20,22,24,1,3,5,7,9,11,14,16,18,20)
 * TCH/H(0): B1(8,10,13,15,17,19,21,23,0,2,4,6,8,10,13,15,17,19,21,23,0,2)
 * TCH/H(1): B1(9,11,14,16,18,20,22,24,1,3,5,7,9,11,14,16,18,20,22,24,1,3)
 * TCH/H(0): B2(17,19,21,23,0,2,4,6,8,10,13,15,17,19,21,23,0,2,4,6,8,10)
 * TCH/H(1): B2(18,20,22,24,1,3,5,7,9,11,14,16,18,20,22,24,1,3,5,7,9,11)
 *
 * TDMA frame number of burst 'a' % 26 is the table index.
 * This mapping is valid for both TCH/H(0) and TCH/H(1). */
static const uint8_t sched_tchh_ul_csd_map[26] = {
	[0]  = 1, /* TCH/H(0): B0(0  ... 19) */
	[1]  = 1, /* TCH/H(1): B0(1  ... 20) */
	[8]  = 1, /* TCH/H(0): B1(8  ... 2) */
	[9]  = 1, /* TCH/H(1): B1(9  ... 3) */
	[17] = 1, /* TCH/H(0): B2(17 ... 10) */
	[18] = 1, /* TCH/H(1): B2(18 ... 11) */
};

/* TDMA frame number of burst 'v' % 26 is the table index.
 * This mapping is valid for both TCH/H(0) and TCH/H(1). */
static const uint8_t sched_tchh_dl_csd_map[26] = {
	[19] = 1, /* TCH/H(0): B0(0  ... 19) */
	[20] = 1, /* TCH/H(1): B0(1  ... 20) */
	[2]  = 1, /* TCH/H(0): B1(8  ... 2) */
	[3]  = 1, /* TCH/H(1): B1(9  ... 3) */
	[10] = 1, /* TCH/H(0): B2(17 ... 10) */
	[11] = 1, /* TCH/H(1): B2(18 ... 11) */
};

/**
 * Can a TCH/H block transmission be initiated / finished
 * on a given frame number and a given channel type?
 *
 * See GSM 05.02, clause 7, table 1
 *
 * @param  chan   channel type (L1SCHED_TCHH_0 or L1SCHED_TCHH_1)
 * @param  fn     the current frame number
 * @param  ul     Uplink or Downlink?
 * @param  facch  FACCH/H or traffic?
 * @param  start  init or end of transmission?
 * @return        true (yes) or false (no)
 */
bool l1sched_tchh_block_map_fn(enum l1sched_lchan_type chan,
	uint32_t fn, bool ul, bool facch, bool start)
{
	uint8_t fn_mf;
	int i = 0;

	/* Just to be sure */
	OSMO_ASSERT(chan == L1SCHED_TCHH_0 || chan == L1SCHED_TCHH_1);

	/* Calculate a modulo */
	fn_mf = facch ? (fn % 26) : (fn % 13);

#define MAP_GET_POS(map) \
	(start ? 0 : ARRAY_SIZE(map[i]) - 1)

#define BLOCK_MAP_FN(map) \
	do { \
		if (map[i][MAP_GET_POS(map)] == fn_mf) \
			return true; \
	} while (++i < ARRAY_SIZE(map))

	/* Choose a proper block map */
	if (facch) {
		if (ul) {
			if (chan == L1SCHED_TCHH_0)
				BLOCK_MAP_FN(tch_h0_ul_facch_block_map);
			else
				BLOCK_MAP_FN(tch_h1_ul_facch_block_map);
		} else {
			if (chan == L1SCHED_TCHH_0)
				BLOCK_MAP_FN(tch_h0_dl_facch_block_map);
			else
				BLOCK_MAP_FN(tch_h1_dl_facch_block_map);
		}
	} else {
		if (chan == L1SCHED_TCHH_0)
			BLOCK_MAP_FN(tch_h0_traffic_block_map);
		else
			BLOCK_MAP_FN(tch_h1_traffic_block_map);
	}

	return false;
}

static int decode_hr_facch(struct l1sched_lchan_state *lchan)
{
	uint8_t data[GSM_MACBLOCK_LEN];
	int n_errors, n_bits_total;
	int rc;

	rc = gsm0503_tch_hr_facch_decode(&data[0], BUFTAIL8(lchan->rx_bursts),
					 &n_errors, &n_bits_total);
	if (rc != GSM_MACBLOCK_LEN)
		return rc;

	/* calculate AVG of the measurements (FACCH/H takes 6 bursts) */
	l1sched_lchan_meas_avg(lchan, 6);

	l1sched_lchan_emit_data_ind(lchan, &data[0], GSM_MACBLOCK_LEN,
				    n_errors, n_bits_total, false);

	return GSM_MACBLOCK_LEN;
}

int rx_tchh_fn(struct l1sched_lchan_state *lchan,
	       const struct l1sched_burst_ind *bi)
{
	int n_errors = -1, n_bits_total = 0, rc;
	sbit_t *bursts_p, *burst;
	uint8_t tch_data[240];
	size_t tch_data_len;
	uint32_t *mask;
	int amr = 0;
	uint8_t ft;

	/* Set up pointers */
	mask = &lchan->rx_burst_mask;
	bursts_p = lchan->rx_bursts;

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "Traffic received: fn=%u bid=%u\n", bi->fn, bi->bid);

	if (bi->bid == 0) {
		/* Shift the burst buffer by 2 bursts leftwards */
		memmove(BUFPOS(bursts_p, 0), BUFPOS(bursts_p, 2), 20 * BPLEN);
		memset(BUFPOS(bursts_p, 20), 0, 2 * BPLEN);
		*mask = *mask << 2;
	}

	if (*mask == 0x00) {
		/* Align to the first burst */
		if (bi->bid > 0)
			return 0;

		/* Align reception of the first FACCH/H frame */
		if (lchan->tch_mode == GSM48_CMODE_SIGN) {
			if (!l1sched_tchh_facch_start(lchan->type, bi->fn, 0))
				return 0;
		}
	}

	/* Update mask */
	*mask |= (1 << bi->bid);

	/* Store the measurements */
	l1sched_lchan_meas_push(lchan, bi);

	/* Copy burst to the end of buffer of 24 bursts */
	burst = BUFPOS(bursts_p, 20 + bi->bid);
	memcpy(burst, bi->burst + 3, 58);
	memcpy(burst + 58, bi->burst + 87, 58);

	/* Wait until the second burst */
	if (bi->bid != 1)
		return 0;

	/* Wait for complete set of bursts */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
		/* FACCH/H is interleaved over 6 bursts */
		if ((*mask & 0x3f) != 0x3f)
			return 0;
		break;
	case GSM48_CMODE_DATA_6k0:
	case GSM48_CMODE_DATA_3k6:
		/* Data (CSD) is interleaved over 22 bursts */
		if ((*mask & 0x3fffff) != 0x3fffff)
			return 0;
		if (!sched_tchh_dl_csd_map[bi->fn % 26])
			return 0; /* CSD: skip decoding attempt, need 2 more bursts */
		break;
	default:
		/* Speech is interleaved over 4 bursts */
		if ((*mask & 0x0f) != 0x0f)
			return 0;
		break;
	}

	/* Skip decoding attempt in case of FACCH/H */
	if (lchan->dl_ongoing_facch) {
		/* Send BFI (DATA.ind without payload) for the 2nd stolen TCH frame */
		l1sched_lchan_meas_avg(lchan, 4);
		l1sched_lchan_emit_data_ind(lchan, NULL, 0, 0, 0, true);
		lchan->dl_ongoing_facch = false;
		return 0;
	}

	/* TCH/H: speech and signalling frames are interleaved over 4 and 6 bursts,
	 * respectively, while CSD frames are interleaved over 22 bursts.  Unless
	 * we're in CSD mode, decode only the last 6 bursts to avoid introducing
	 * additional delays. */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1: /* HR */
		rc = gsm0503_tch_hr_decode(&tch_data[0], BUFTAIL8(bursts_p),
					   !sched_tchh_dl_facch_map[bi->fn % 26],
					   &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_AMR: /* AMR */
		/* See comment in function rx_tchf_fn() */
		amr = 2;
		rc = gsm0503_tch_ahs_decode_dtx(&tch_data[amr], BUFTAIL8(bursts_p),
						!sched_tchh_dl_facch_map[bi->fn % 26],
						!sched_tchh_dl_amr_cmi_map[bi->fn % 26],
						lchan->amr.codec,
						lchan->amr.codecs,
						&lchan->amr.dl_ft,
						&lchan->amr.dl_cmr,
						&n_errors, &n_bits_total,
						&lchan->amr.last_dtx);

		/* only good speech frames get rtp header */
		if (rc != GSM_MACBLOCK_LEN && rc >= 4) {
			if (lchan->amr.last_dtx == AMR_OTHER) {
				ft = lchan->amr.codec[lchan->amr.dl_ft];
			} else {
				/* SID frames will always get Frame Type Index 8 (AMR_SID) */
				ft = AMR_SID;
			}
			rc = osmo_amr_rtp_enc(&tch_data[0],
				lchan->amr.codec[lchan->amr.dl_cmr],
				ft, AMR_GOOD);
			if (rc < 0)
				LOGP_LCHAND(lchan, LOGL_ERROR,
					    "osmo_amr_rtp_enc() returned rc=%d\n", rc);
		}
		break;
	/* CSD (TCH/H4.8): 6.0 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_6k0:
		/* FACCH/H does not steal TCH/H4.8 frames, but only disturbs some bits */
		decode_hr_facch(lchan);
		rc = gsm0503_tch_hr48_decode(&tch_data[0], BUFPOS(bursts_p, 0),
					     &n_errors, &n_bits_total);
		break;
	/* CSD (TCH/H2.4): 3.6 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_3k6:
		/* FACCH/H does not steal TCH/H2.4 frames, but only disturbs some bits */
		decode_hr_facch(lchan);
		rc = gsm0503_tch_hr24_decode(&tch_data[0], BUFPOS(bursts_p, 0),
					     &n_errors, &n_bits_total);
		break;
	default:
		LOGP_LCHAND(lchan, LOGL_ERROR, "Invalid TCH mode: %u\n", lchan->tch_mode);
		return -EINVAL;
	}

	/* Check decoding result */
	if (rc < 4) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received bad frame (rc=%d, ber=%d/%d) at fn=%u\n",
			    rc, n_errors, n_bits_total, lchan->meas_avg.fn);

		/* Send BFI (DATA.ind without payload) */
		tch_data_len = 0;
	} else if (rc == GSM_MACBLOCK_LEN) {
		/* Skip decoding of the next 2 stolen bursts */
		lchan->dl_ongoing_facch = true;

		/* Calculate AVG of the measurements (FACCH/H takes 6 bursts) */
		l1sched_lchan_meas_avg(lchan, 6);

		/* FACCH/H received, forward to the higher layers */
		l1sched_lchan_emit_data_ind(lchan, &tch_data[amr], GSM_MACBLOCK_LEN,
					    n_errors, n_bits_total, false);

		/* Send BFI (DATA.ind without payload) for the 1st stolen TCH frame */
		if (lchan->tch_mode == GSM48_CMODE_SIGN)
			return 0;
		tch_data_len = 0;
	} else {
		/* A good TCH frame received */
		tch_data_len = rc;
	}

	/* Calculate AVG of the measurements (traffic takes 4 bursts) */
	l1sched_lchan_meas_avg(lchan, 4);

	/* Send a traffic frame to the higher layers */
	return l1sched_lchan_emit_data_ind(lchan, &tch_data[0], tch_data_len,
					   n_errors, n_bits_total, true);
}

int tx_tchh_fn(struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	struct msgb *msg_facch, *msg_tch, *msg;
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

	if (*mask == 0x00) {
		/* Align transmission of the first frame */
		switch (lchan->tch_mode) {
		case GSM48_CMODE_SIGN:
			if (!l1sched_tchh_facch_start(lchan->type, br->fn, 1))
				return 0;
			break;
		case GSM48_CMODE_DATA_6k0:
		case GSM48_CMODE_DATA_3k6:
			if (!sched_tchh_ul_csd_map[br->fn % 26])
				return 0;
			break;
		}
	}

	/* Shift the burst buffer by 2 bursts leftwards for interleaving */
	memmove(BUFPOS(bursts_p, 0), BUFPOS(bursts_p, 2), 20 * BPLEN);
	memset(BUFPOS(bursts_p, 20), 0, 2 * BPLEN);
	*mask = *mask << 2;

	/* If FACCH/H blocks are still pending */
	if (lchan->ul_facch_blocks > 2) {
		struct msgb *msg = l1sched_lchan_prim_dequeue_tch(lchan, false);
		msgb_free(msg); /* drop 2nd TCH/HS block */
		goto send_burst;
	}

	switch (lchan->tch_mode) {
	case GSM48_CMODE_DATA_6k0:
	case GSM48_CMODE_DATA_3k6:
		/* CSD: skip dequeueing/encoding, send 2 more bursts */
		if (!sched_tchh_ul_csd_map[br->fn % 26])
			goto send_burst;
		break;
	}

	/* dequeue a pair of TCH and FACCH frames */
	msg_tch = l1sched_lchan_prim_dequeue_tch(lchan, false);
	if (l1sched_tchh_facch_start(lchan->type, br->fn, true))
		msg_facch = l1sched_lchan_prim_dequeue_tch(lchan, true);
	else
		msg_facch = NULL;
	/* prioritize FACCH over TCH */
	msg = (msg_facch != NULL) ? msg_facch : msg_tch;

	/* populate the buffer with bursts */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
		if (!l1sched_tchh_facch_start(lchan->type, br->fn, 1))
			goto send_burst; /* XXX: should not happen */
		if (msg == NULL)
			msg = l1sched_lchan_prim_dummy_lapdm(lchan);
		/* fall-through */
	case GSM48_CMODE_SPEECH_V1:
		if (msg == NULL) {
			/* transmit a dummy speech block with inverted CRC3 */
			gsm0503_tch_hr_encode(bursts_p, NULL, 0);
			goto send_burst;
		}
		rc = gsm0503_tch_hr_encode(BUFPOS(bursts_p, 0),
					   msgb_l2(msg),
					   msgb_l2len(msg));
		break;
	case GSM48_CMODE_SPEECH_AMR:
	{
		bool amr_fn_is_cmr = !sched_tchh_ul_amr_cmi_map[br->fn % 26];
		const uint8_t *data = msg ? msgb_l2(msg) : NULL;
		size_t data_len = msg ? msgb_l2len(msg) : 0;

		if (msg != NULL && msg != msg_facch) { /* TCH/AHS: speech */
			if (!l1sched_lchan_amr_prim_is_valid(lchan, msg, amr_fn_is_cmr))
				goto free_bad_msg;
			/* pull the AMR header - sizeof(struct amr_hdr) */
			data_len -= 2;
			data += 2;
		}

		/* if msg == NULL, transmit a dummy speech block with inverted CRC6 */
		rc = gsm0503_tch_ahs_encode(BUFPOS(bursts_p, 0),
					    data, data_len,
					    amr_fn_is_cmr,
					    lchan->amr.codec,
					    lchan->amr.codecs,
					    lchan->amr.ul_ft,
					    lchan->amr.ul_cmr);
		break;
	}
	/* CSD (TCH/H4.8): 6.0 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_6k0:
		if ((msg = msg_tch) != NULL) {
			OSMO_ASSERT(msgb_l2len(msg) == 4 * 60);
			gsm0503_tch_hr48_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm data sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		} else {
			ubit_t idle[4 * 60];
			memset(&idle[0], 0x01, sizeof(idle));
			gsm0503_tch_hr48_encode(BUFPOS(bursts_p, 0), &idle[0]);
		}
		if ((msg = msg_facch) != NULL) {
			gsm0503_tch_hr_facch_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm FACCH sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		}
		goto send_burst;
	/* CSD (TCH/H2.4): 3.6 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_3k6:
		if ((msg = msg_tch) != NULL) {
			OSMO_ASSERT(msgb_l2len(msg) == 4 * 36);
			gsm0503_tch_hr24_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm data sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		} else {
			ubit_t idle[4 * 36];
			memset(&idle[0], 0x01, sizeof(idle));
			gsm0503_tch_hr24_encode(BUFPOS(bursts_p, 0), &idle[0]);
		}
		if ((msg = msg_facch) != NULL) {
			gsm0503_tch_hr_facch_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm FACCH sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		}
		goto send_burst;
	default:
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "TCH mode %s is unknown or not supported\n",
			    gsm48_chan_mode_name(lchan->tch_mode));
		goto free_bad_msg;
	}

	if (rc) {
		LOGP_LCHAND(lchan, LOGL_ERROR, "Failed to encode L2 payload (len=%u): %s\n",
			    msgb_l2len(msg), msgb_hexdump_l2(msg));
free_bad_msg:
		msgb_free(msg_facch);
		msgb_free(msg_tch);
		return -EINVAL;
	}

	if (msgb_l2len(msg) == GSM_MACBLOCK_LEN)
		lchan->ul_facch_blocks = 6;

	/* Confirm data / traffic sending (pass ownership of the msgb/prim) */
	l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
	msgb_free((msg == msg_facch) ? msg_tch : msg_facch);

send_burst:
	/* Determine which burst should be sent */
	burst = BUFPOS(bursts_p, br->bid);

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

	LOGP_LCHAND(lchan, LOGL_DEBUG, "Scheduled fn=%u burst=%u\n", br->fn, br->bid);

	/* In case of a FACCH/H frame, one block less */
	if (lchan->ul_facch_blocks)
		lchan->ul_facch_blocks--;

	return 0;
}
