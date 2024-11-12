/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2017-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2021-2024 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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

/* ------------------------------------------------------------------
 * 3GPP TS 45.009, table 3.2.1.3-1
 * "TDMA frames for Codec Mode Indication for TCH/AFS, TCH/WFS and O-TCH/WFS" */

/* TDMA frame number (mod 26) of burst 'h' (last) is the table index. */
static const uint8_t sched_tchf_dl_amr_cmi_h_map[26] = {
	[11] = 1, /* TCH/F: a=4  / h=11 */
	[20] = 1, /* TCH/F: a=13 / h=20 */
	[3]  = 1, /* TCH/F: a=21 / h=3 (21+7=28, 25 is idle -> 29. 29%26=3) */
};

/* TDMA frame number (mod 26) of burst 'a' (first) is the table index. */
static const uint8_t sched_tchf_ul_amr_cmi_a_map[26] = {
	[0]  = 1, /* TCH/F: a=0 */
	[8]  = 1, /* TCH/F: a=8 */
	[17] = 1, /* TCH/F: a=17 */
};

static int decode_fr_facch(struct l1sched_lchan_state *lchan)
{
	uint8_t data[GSM_MACBLOCK_LEN];
	int n_errors, n_bits_total;
	int rc;

	rc = gsm0503_tch_fr_facch_decode(&data[0], BUFTAIL8(lchan->rx_bursts),
					 &n_errors, &n_bits_total);
	if (rc != GSM_MACBLOCK_LEN)
		return rc;

	/* calculate AVG of the measurements (FACCH/F takes 8 bursts) */
	l1sched_lchan_meas_avg(lchan, 8);

	l1sched_lchan_emit_data_ind(lchan, &data[0], GSM_MACBLOCK_LEN,
				    n_errors, n_bits_total, false);

	return GSM_MACBLOCK_LEN;
}

int rx_tchf_fn(struct l1sched_lchan_state *lchan,
	       const struct l1sched_burst_ind *bi)
{
	int n_errors = -1, n_bits_total = 0, rc;
	sbit_t *bursts_p, *burst;
	uint8_t tch_data[290];
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
		/* Shift the burst buffer by 4 bursts leftwards */
		memmove(BUFPOS(bursts_p, 0), BUFPOS(bursts_p, 4), 20 * BPLEN);
		memset(BUFPOS(bursts_p, 20), 0, 4 * BPLEN);
		*mask = *mask << 4;
	} else {
		/* Align to the first burst of a block */
		if (*mask == 0x00)
			return 0;
	}

	/* Update mask */
	*mask |= (1 << bi->bid);

	/* Store the measurements */
	l1sched_lchan_meas_push(lchan, bi);

	/* Copy burst to end of buffer of 24 bursts */
	burst = BUFPOS(bursts_p, 20 + bi->bid);
	memcpy(burst, bi->burst + 3, 58);
	memcpy(burst + 58, bi->burst + 87, 58);

	/* Wait until complete set of bursts */
	if (bi->bid != 3)
		return 0;

	/* Calculate AVG of the measurements */
	l1sched_lchan_meas_avg(lchan, 8); // XXX

	/* Check for complete set of bursts */
	if ((*mask & 0xff) != 0xff) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received incomplete (%s) traffic frame at fn=%u (%u/%u)\n",
			    l1sched_burst_mask2str(mask, 8), lchan->meas_avg.fn,
			    lchan->meas_avg.fn % lchan->ts->mf_layout->period,
			    lchan->ts->mf_layout->period);
		/* NOTE: do not abort here, give it a try. Maybe we're lucky ;) */

	}

	/* TCH/F: speech and signalling frames are interleaved over 8 bursts, while
	 * CSD frames are interleaved over 22 bursts.  Unless we're in CSD mode,
	 * decode only the last 8 bursts to avoid introducing additional delays. */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1: /* FR */
		rc = gsm0503_tch_fr_decode(&tch_data[0], BUFTAIL8(bursts_p),
					   1, 0, &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_EFR: /* EFR */
		rc = gsm0503_tch_fr_decode(&tch_data[0], BUFTAIL8(bursts_p),
					   1, 1, &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_AMR: /* AMR */
		/* we store tch_data + 2 header bytes, the amr variable set to
		 * 2 will allow us to skip the first 2 bytes in case we did
		 * receive an FACCH frame instead of a voice frame (we do not
		 * know this before we actually decode the frame) */
		amr = 2;
		rc = gsm0503_tch_afs_decode_dtx(&tch_data[amr], BUFTAIL8(bursts_p),
						!sched_tchf_dl_amr_cmi_h_map[bi->fn % 26],
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
	/* CSD (TCH/F14.4): 14.5 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_14k5:
		/* FACCH/F does not steal TCH/F14.4 frames, but only disturbs some bits */
		decode_fr_facch(lchan);
		rc = gsm0503_tch_fr144_decode(&tch_data[0], BUFPOS(bursts_p, 0),
					      &n_errors, &n_bits_total);
		break;
	/* CSD (TCH/F9.6): 12.0 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_12k0:
		/* FACCH/F does not steal TCH/F9.6 frames, but only disturbs some bits */
		decode_fr_facch(lchan);
		rc = gsm0503_tch_fr96_decode(&tch_data[0], BUFPOS(bursts_p, 0),
					     &n_errors, &n_bits_total);
		break;
	/* CSD (TCH/F4.8): 6.0 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_6k0:
		/* FACCH/F does not steal TCH/F4.8 frames, but only disturbs some bits */
		decode_fr_facch(lchan);
		rc = gsm0503_tch_fr48_decode(&tch_data[0], BUFPOS(bursts_p, 0),
					     &n_errors, &n_bits_total);
		break;
	/* CSD (TCH/F2.4): 3.6 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_3k6:
		/* TCH/F2.4 employs the same interleaving as TCH/FS (8 bursts),
		 * so FACCH/F *does* steal TCH/F2.4 frames completely. */
		if (decode_fr_facch(lchan) == GSM_MACBLOCK_LEN)
			return 0; /* TODO: emit BFI? */
		rc = gsm0503_tch_fr24_decode(&tch_data[0], BUFTAIL8(bursts_p),
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
		/* FACCH received, forward it to the higher layers */
		l1sched_lchan_emit_data_ind(lchan, &tch_data[amr], GSM_MACBLOCK_LEN,
					    n_errors, n_bits_total, false);

		/* Send BFI (DATA.ind without payload) */
		if (lchan->tch_mode == GSM48_CMODE_SIGN)
			return 0;
		tch_data_len = 0;
	} else {
		/* A good TCH frame received */
		tch_data_len = rc;
	}

	/* Send a traffic frame to the higher layers */
	return l1sched_lchan_emit_data_ind(lchan, &tch_data[0], tch_data_len,
					   n_errors, n_bits_total, true);
}

int tx_tchf_fn(struct l1sched_lchan_state *lchan,
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

	/* Shift the burst buffer by 4 bursts leftwards for interleaving */
	memmove(BUFPOS(bursts_p, 0), BUFPOS(bursts_p, 4), 20 * BPLEN);
	memset(BUFPOS(bursts_p, 20), 0, 4 * BPLEN);
	*mask = *mask << 4;

	/* dequeue a pair of TCH and FACCH frames */
	msg_tch = l1sched_lchan_prim_dequeue_tch(lchan, false);
	msg_facch = l1sched_lchan_prim_dequeue_tch(lchan, true);
	/* prioritize FACCH over TCH */
	msg = (msg_facch != NULL) ? msg_facch : msg_tch;

	/* populate the buffer with bursts */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
		if (msg == NULL)
			msg = l1sched_lchan_prim_dummy_lapdm(lchan);
		/* fall-through */
	case GSM48_CMODE_SPEECH_V1:
	case GSM48_CMODE_SPEECH_EFR:
		/* if msg == NULL, transmit a dummy speech block with inverted CRC3 */
		rc = gsm0503_tch_fr_encode(BUFPOS(bursts_p, 0),
					   msg ? msgb_l2(msg) : NULL,
					   msg ? msgb_l2len(msg) : 0, 1);
		/* confirm traffic sending (pass ownership of the msgb/prim) */
		if (OSMO_LIKELY(rc == 0))
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		else /* unlikely: encoding failed, drop msgb/prim */
			msgb_free(msg);
		/* drop the other msgb/prim  */
		msgb_free((msg == msg_facch) ? msg_tch : msg_facch);
		break;
	case GSM48_CMODE_SPEECH_AMR:
	{
		bool amr_fn_is_cmr = !sched_tchf_ul_amr_cmi_a_map[br->fn % 26];
		unsigned int offset = 0;

		if (msg != NULL && msg != msg_facch) { /* TCH/AFS: speech */
			if (!l1sched_lchan_amr_prim_is_valid(lchan, msg, amr_fn_is_cmr)) {
				msgb_free(msg);
				msg_tch = NULL;
				msg = NULL;
			}
			/* pull the AMR header - sizeof(struct amr_hdr) */
			offset = 2;
		}

		/* if msg == NULL, transmit a dummy speech block with inverted CRC6 */
		rc = gsm0503_tch_afs_encode(BUFPOS(bursts_p, 0),
					    msg ? msgb_l2(msg) + offset : NULL,
					    msg ? msgb_l2len(msg) - offset : 0,
					    amr_fn_is_cmr,
					    lchan->amr.codec,
					    lchan->amr.codecs,
					    lchan->amr.ul_ft,
					    lchan->amr.ul_cmr);
		/* confirm traffic sending (pass ownership of the msgb/prim) */
		if (OSMO_LIKELY(rc == 0))
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		else /* unlikely: encoding failed, drop prim */
			msgb_free(msg);
		/* drop the other primitive */
		msgb_free((msg == msg_facch) ? msg_tch : msg_facch);
		break;
	}
	/* CSD (TCH/F14.4): 14.5 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_14k5:
		if ((msg = msg_tch) != NULL) {
			OSMO_ASSERT(msgb_l2len(msg) == 290);
			gsm0503_tch_fr144_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm data sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		} else {
			ubit_t idle[290];
			memset(&idle[0], 0x01, sizeof(idle));
			gsm0503_tch_fr144_encode(BUFPOS(bursts_p, 0), &idle[0]);
		}
		if ((msg = msg_facch) != NULL) {
			gsm0503_tch_fr_facch_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm FACCH sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		}
		break;
	/* CSD (TCH/F9.6): 12.0 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_12k0:
		if ((msg = msg_tch) != NULL) {
			OSMO_ASSERT(msgb_l2len(msg) == 4 * 60);
			gsm0503_tch_fr96_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm data sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		} else {
			ubit_t idle[4 * 60];
			memset(&idle[0], 0x01, sizeof(idle));
			gsm0503_tch_fr96_encode(BUFPOS(bursts_p, 0), &idle[0]);
		}
		if ((msg = msg_facch) != NULL) {
			gsm0503_tch_fr_facch_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm FACCH sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		}
		break;
	/* CSD (TCH/F4.8): 6.0 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_6k0:
		if ((msg = msg_tch) != NULL) {
			OSMO_ASSERT(msgb_l2len(msg) == 2 * 60);
			gsm0503_tch_fr48_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm data sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		} else {
			ubit_t idle[2 * 60];
			memset(&idle[0], 0x01, sizeof(idle));
			gsm0503_tch_fr48_encode(BUFPOS(bursts_p, 0), &idle[0]);
		}
		if ((msg = msg_facch) != NULL) {
			gsm0503_tch_fr_facch_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			/* Confirm FACCH sending (pass ownership of the msgb/prim) */
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		}
		break;
	/* CSD (TCH/F2.4): 3.6 kbit/s radio interface rate */
	case GSM48_CMODE_DATA_3k6:
		if ((msg = msg_facch) != NULL) {
			/* FACCH/F does steal a TCH/F2.4 frame completely */
			gsm0503_tch_fr_facch_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
			msgb_free(msg_tch);
		} else if ((msg = msg_tch) != NULL) {
			OSMO_ASSERT(msgb_l2len(msg) == 2 * 36);
			gsm0503_tch_fr24_encode(BUFPOS(bursts_p, 0), msgb_l2(msg));
			l1sched_lchan_emit_data_cnf(lchan, msg, br->fn);
		} else {
			ubit_t idle[2 * 36];
			memset(&idle[0], 0x01, sizeof(idle));
			gsm0503_tch_fr24_encode(BUFPOS(bursts_p, 0), &idle[0]);
		}
		break;
	default:
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "TCH mode %s is unknown or not supported\n",
			    gsm48_chan_mode_name(lchan->tch_mode));
		msgb_free(msg_facch);
		msgb_free(msg_tch);
		break;
	}

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

	return 0;
}
