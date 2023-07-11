/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2018-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2018 by Harald Welte <laforge@gnumonks.org>
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
#include <osmocom/core/utils.h>
#include <osmocom/core/bits.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/coding/gsm0503_coding.h>
#include <osmocom/coding/gsm0503_amr_dtx.h>
#include <osmocom/codec/codec.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

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

/**
 * Calculates a frame number of the first burst
 * using given frame number of the last burst.
 *
 * See GSM 05.02, clause 7, table 1
 *
 * @param  chan      channel type (L1SCHED_TCHH_0 or L1SCHED_TCHH_1)
 * @param  last_fn   frame number of the last burst
 * @param  facch     FACCH/H or traffic?
 * @return           either frame number of the first burst,
 *                   or fn=last_fn if calculation failed
 */
static uint32_t tchh_block_dl_first_fn(const struct l1sched_lchan_state *lchan,
				       uint32_t last_fn, bool facch)
{
	enum l1sched_lchan_type chan = lchan->type;
	uint8_t fn_mf, fn_diff;
	int i = 0;

	/* Just to be sure */
	OSMO_ASSERT(chan == L1SCHED_TCHH_0 || chan == L1SCHED_TCHH_1);

	/* Calculate a modulo */
	fn_mf = facch ? (last_fn % 26) : (last_fn % 13);

#define BLOCK_FIRST_FN(map) \
	do { \
		if (map[i][ARRAY_SIZE(map[i]) - 1] == fn_mf) { \
			fn_diff = GSM_TDMA_FN_DIFF(fn_mf, map[i][0]); \
			return GSM_TDMA_FN_SUB(last_fn, fn_diff); \
		} \
	} while (++i < ARRAY_SIZE(map))

	/* Choose a proper block map */
	if (facch) {
		if (chan == L1SCHED_TCHH_0)
			BLOCK_FIRST_FN(tch_h0_dl_facch_block_map);
		else
			BLOCK_FIRST_FN(tch_h1_dl_facch_block_map);
	} else {
		if (chan == L1SCHED_TCHH_0)
			BLOCK_FIRST_FN(tch_h0_traffic_block_map);
		else
			BLOCK_FIRST_FN(tch_h1_traffic_block_map);
	}

	LOGP_LCHAND(lchan, LOGL_ERROR,
		    "Failed to calculate TDMA frame number of the first burst of %s block, "
		    "using the current fn=%u\n", facch ? "FACCH/H" : "TCH/H", last_fn);

	/* Couldn't calculate the first fn, return the last */
	return last_fn;
}

int rx_tchh_fn(struct l1sched_lchan_state *lchan,
	       const struct l1sched_burst_ind *bi)
{
	int n_errors = -1, n_bits_total = 0, rc;
	sbit_t *bursts_p, *burst;
	uint8_t tch_data[128];
	size_t tch_data_len;
	uint8_t *mask;
	int amr = 0;
	uint8_t ft;

	/* Set up pointers */
	mask = &lchan->rx_burst_mask;
	bursts_p = lchan->rx_bursts;

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "Traffic received: fn=%u bid=%u\n", bi->fn, bi->bid);

	if (bi->bid == 0) {
		/* Shift the burst buffer by 2 bursts leftwards */
		memcpy(&bursts_p[0], &bursts_p[232], 232);
		memcpy(&bursts_p[232], &bursts_p[464], 232);
		memset(&bursts_p[464], 0, 232);
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
		} else { /* or TCH/H traffic frame */
			if (!l1sched_tchh_traffic_start(lchan->type, bi->fn, 0))
				return 0;
		}
	}

	/* Update mask */
	*mask |= (1 << bi->bid);

	/* Store the measurements */
	l1sched_lchan_meas_push(lchan, bi);

	/* Copy burst to the end of buffer of 6 bursts */
	burst = bursts_p + bi->bid * 116 + 464;
	memcpy(burst, bi->burst + 3, 58);
	memcpy(burst + 58, bi->burst + 87, 58);

	/* Wait until the second burst */
	if (bi->bid != 1)
		return 0;

	/* Wait for complete set of bursts */
	if (lchan->tch_mode == GSM48_CMODE_SIGN) {
		/* FACCH/H is interleaved over 6 bursts */
		if ((*mask & 0x3f) != 0x3f)
			goto bfi;
	} else {
		/* Traffic is interleaved over 4 bursts */
		if ((*mask & 0x0f) != 0x0f)
			goto bfi;
	}

	/* Skip decoding attempt in case of FACCH/H */
	if (lchan->dl_ongoing_facch) {
		lchan->dl_ongoing_facch = false;
		goto bfi; /* 2/2 BFI */
	}

	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1: /* HR */
		rc = gsm0503_tch_hr_decode(&tch_data[0], bursts_p,
					   !sched_tchh_dl_facch_map[bi->fn % 26],
					   &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_AMR: /* AMR */
		/* See comment in function rx_tchf_fn() */
		amr = 2;
		rc = gsm0503_tch_ahs_decode_dtx(&tch_data[amr], bursts_p,
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
	default:
		LOGP_LCHAND(lchan, LOGL_ERROR, "Invalid TCH mode: %u\n", lchan->tch_mode);
		return -EINVAL;
	}

	/* Check decoding result */
	if (rc < 4) {
		/* Calculate AVG of the measurements (assuming 4 bursts) */
		l1sched_lchan_meas_avg(lchan, 4);

		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received bad frame (rc=%d, ber=%d/%d) at fn=%u\n",
			    rc, n_errors, n_bits_total, lchan->meas_avg.fn);

		/* Send BFI */
		goto bfi;
	} else if (rc == GSM_MACBLOCK_LEN) {
		/* Skip decoding of the next 2 stolen bursts */
		lchan->dl_ongoing_facch = true;

		/* Calculate AVG of the measurements (FACCH/H takes 6 bursts) */
		l1sched_lchan_meas_avg(lchan, 6);

		/* FACCH/H received, forward to the higher layers */
		l1sched_lchan_emit_data_ind(lchan, &tch_data[amr], GSM_MACBLOCK_LEN,
					    n_errors, n_bits_total, false);

		/* Send BFI substituting 1/2 stolen TCH frames */
		n_errors = -1; /* ensure fake measurements */
		goto bfi;
	} else {
		/* A good TCH frame received */
		tch_data_len = rc;

		/* Calculate AVG of the measurements (traffic takes 4 bursts) */
		l1sched_lchan_meas_avg(lchan, 4);
	}

	/* Send a traffic frame to the higher layers */
	return l1sched_lchan_emit_data_ind(lchan, &tch_data[0], tch_data_len,
					   n_errors, n_bits_total, true);

bfi:
	/* Didn't try to decode, fake measurements */
	if (n_errors < 0) {
		lchan->meas_avg = (struct l1sched_meas_set) {
			.fn = tchh_block_dl_first_fn(lchan, bi->fn, false),
			.toa256 = 0,
			.rssi = -110,
		};

		/* No bursts => no errors */
		n_errors = 0;
	}

	/* BFI is not applicable in signalling mode */
	if (lchan->tch_mode == GSM48_CMODE_SIGN) {
		return l1sched_lchan_emit_data_ind(lchan, NULL, 0,
						   n_errors, n_bits_total, false);
	}

	/* Bad frame indication */
	tch_data_len = l1sched_bad_frame_ind(&tch_data[0], lchan);

	/* Send a BFI frame to the higher layers */
	return l1sched_lchan_emit_data_ind(lchan, &tch_data[0], tch_data_len,
					   n_errors, n_bits_total, true);
}

int tx_tchh_fn(struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	ubit_t *bursts_p, *burst;
	const uint8_t *tsc;
	uint8_t *mask;
	int rc;

	/* Set up pointers */
	mask = &lchan->tx_burst_mask;
	bursts_p = lchan->tx_bursts;

	if (br->bid > 0)
		goto send_burst;

	if (*mask == 0x00) {
		/* Align transmission of the first FACCH/H frame */
		if (lchan->tch_mode == GSM48_CMODE_SIGN)
			if (!l1sched_tchh_facch_start(lchan->type, br->fn, 1))
				return 0;
	}

	/* Shift the burst buffer by 2 bursts leftwards for interleaving */
	memcpy(&bursts_p[0], &bursts_p[232], 232);
	memcpy(&bursts_p[232], &bursts_p[464], 232);
	memset(&bursts_p[464], 0, 232);
	*mask = *mask << 2;

	/* If FACCH/H blocks are still pending */
	if (lchan->ul_facch_blocks > 2)
		goto send_burst;

	if (msgb_l2len(lchan->prim) == GSM_MACBLOCK_LEN)
		lchan->ul_facch_blocks = 6;

	/* populate the buffer with bursts */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1:
		rc = gsm0503_tch_hr_encode(bursts_p,
					   msgb_l2(lchan->prim),
					   msgb_l2len(lchan->prim));
		break;
	case GSM48_CMODE_SPEECH_AMR:
	{
		bool amr_fn_is_cmr = !sched_tchh_ul_amr_cmi_map[br->fn % 26];
		const uint8_t *data = msgb_l2(lchan->prim);
		size_t data_len = msgb_l2len(lchan->prim);

		if (data_len != GSM_MACBLOCK_LEN) { /* TCH/AHS: speech */
			if (!l1sched_lchan_amr_prim_is_valid(lchan, amr_fn_is_cmr))
				goto free_bad_msg;
			/* pull the AMR header - sizeof(struct amr_hdr) */
			data_len -= 2;
			data += 2;
		}

		rc = gsm0503_tch_ahs_encode(bursts_p,
					    data, data_len,
					    amr_fn_is_cmr,
					    lchan->amr.codec,
					    lchan->amr.codecs,
					    lchan->amr.ul_ft,
					    lchan->amr.ul_cmr);
		break;
	}
	default:
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "TCH mode %s is unknown or not supported\n",
			    gsm48_chan_mode_name(lchan->tch_mode));
		goto free_bad_msg;
	}

	if (rc) {
		LOGP_LCHAND(lchan, LOGL_ERROR, "Failed to encode L2 payload (len=%u): %s\n",
			    msgb_l2len(lchan->prim), msgb_hexdump_l2(lchan->prim));
free_bad_msg:
		l1sched_lchan_prim_drop(lchan);
		return -EINVAL;
	}

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

	LOGP_LCHAND(lchan, LOGL_DEBUG, "Scheduled fn=%u burst=%u\n", br->fn, br->bid);

	/* In case of a FACCH/H frame, one block less */
	if (lchan->ul_facch_blocks)
		lchan->ul_facch_blocks--;

	if ((*mask & 0x0f) == 0x0f) {
		/* Confirm data / traffic sending (pass ownership of the prim) */
		if (!lchan->ul_facch_blocks)
			l1sched_lchan_emit_data_cnf(lchan, br->fn);
		else /* do not confirm dropped prims */
			l1sched_lchan_prim_drop(lchan);
	}

	return 0;
}
