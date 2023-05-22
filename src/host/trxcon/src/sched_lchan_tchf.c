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

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/coding/gsm0503_coding.h>
#include <osmocom/coding/gsm0503_amr_dtx.h>
#include <osmocom/codec/codec.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

/* 3GPP TS 45.009, table 3.2.1.3-{1,3}: AMR on Downlink TCH/F.
 *
 * +---+---+---+---+---+---+---+---+
 * | a | b | c | d | e | f | g | h |  Burst 'a' received first
 * +---+---+---+---+---+---+---+---+
 *  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   Speech/FACCH frame  (bursts 'a' .. 'h')
 *
 * TDMA frame number of burst 'h' is always used as the table index. */
static const uint8_t sched_tchf_dl_amr_cmi_map[26] = {
	[11] = 1, /* TCH/F: a=4  / h=11 */
	[20] = 1, /* TCH/F: a=13 / h=20 */
	[3]  = 1, /* TCH/F: a=21 / h=3 (21+7=28, 25 is idle -> 29. 29%26=3) */
};

/* TDMA frame number of burst 'a' should be used as the table index. */
static const uint8_t sched_tchf_ul_amr_cmi_map[26] = {
	[0]  = 1, /* TCH/F: a=0 */
	[8]  = 1, /* TCH/F: a=8 */
	[17] = 1, /* TCH/F: a=17 */
};

int rx_tchf_fn(struct l1sched_lchan_state *lchan,
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
		/* Shift the burst buffer by 4 bursts leftwards */
		memcpy(&bursts_p[0], &bursts_p[464], 464);
		memset(&bursts_p[464], 0, 464);
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

	/* Copy burst to end of buffer of 8 bursts */
	burst = bursts_p + bi->bid * 116 + 464;
	memcpy(burst, bi->burst + 3, 58);
	memcpy(burst + 58, bi->burst + 87, 58);

	/* Wait until complete set of bursts */
	if (bi->bid != 3)
		return 0;

	/* Calculate AVG of the measurements */
	l1sched_lchan_meas_avg(lchan, 8);

	/* Check for complete set of bursts */
	if ((*mask & 0xff) != 0xff) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received incomplete (%s) traffic frame at fn=%u (%u/%u)\n",
			    l1sched_burst_mask2str(mask, 8), lchan->meas_avg.fn,
			    lchan->meas_avg.fn % lchan->ts->mf_layout->period,
			    lchan->ts->mf_layout->period);
		/* NOTE: do not abort here, give it a try. Maybe we're lucky ;) */

	}

	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1: /* FR */
		rc = gsm0503_tch_fr_decode(&tch_data[0], bursts_p,
			1, 0, &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_EFR: /* EFR */
		rc = gsm0503_tch_fr_decode(&tch_data[0], bursts_p,
			1, 1, &n_errors, &n_bits_total);
		break;
	case GSM48_CMODE_SPEECH_AMR: /* AMR */
		/* we store tch_data + 2 header bytes, the amr variable set to
		 * 2 will allow us to skip the first 2 bytes in case we did
		 * receive an FACCH frame instead of a voice frame (we do not
		 * know this before we actually decode the frame) */
		amr = 2;
		rc = gsm0503_tch_afs_decode_dtx(&tch_data[amr], bursts_p,
						!sched_tchf_dl_amr_cmi_map[bi->fn % 26],
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

static struct msgb *prim_dequeue_tchf(struct l1sched_lchan_state *lchan)
{
	struct msgb *msg_facch;
	struct msgb *msg_tch;

	/* dequeue a pair of TCH and FACCH frames */
	msg_tch = l1sched_lchan_prim_dequeue_tch(lchan, false);
	msg_facch = l1sched_lchan_prim_dequeue_tch(lchan, true);

	/* prioritize FACCH over TCH */
	if (msg_facch != NULL) {
		msgb_free(msg_tch); /* drop one TCH/FS block */
		return msg_facch;
	}

	return msg_tch;
}

int tx_tchf_fn(struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	ubit_t *bursts_p, *burst;
	const uint8_t *tsc;
	uint8_t *mask;
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
	memcpy(&bursts_p[0], &bursts_p[464], 464);
	memset(&bursts_p[464], 0, 464);
	*mask = *mask << 4;

	lchan->prim = prim_dequeue_tchf(lchan);
	if (lchan->prim == NULL)
		lchan->prim = l1sched_lchan_prim_dummy_lapdm(lchan);
	OSMO_ASSERT(lchan->prim != NULL);

	/* populate the buffer with bursts */
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1:
	case GSM48_CMODE_SPEECH_EFR:
		rc = gsm0503_tch_fr_encode(bursts_p,
					   msgb_l2(lchan->prim),
					   msgb_l2len(lchan->prim), 1);
		break;
	case GSM48_CMODE_SPEECH_AMR:
	{
		bool amr_fn_is_cmr = !sched_tchf_ul_amr_cmi_map[br->fn % 26];
		const uint8_t *data = msgb_l2(lchan->prim);
		size_t data_len = msgb_l2len(lchan->prim);

		if (data_len != GSM_MACBLOCK_LEN) { /* TCH/AFS: speech */
			if (!l1sched_lchan_amr_prim_is_valid(lchan, amr_fn_is_cmr))
				goto free_bad_msg;
			/* pull the AMR header - sizeof(struct amr_hdr) */
			data_len -= 2;
			data += 2;
		}

		rc = gsm0503_tch_afs_encode(bursts_p,
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

	/* If we have sent the last (4/4) burst */
	if ((*mask & 0x0f) == 0x0f) {
		/* Confirm data / traffic sending (pass ownership of the prim) */
		l1sched_lchan_emit_data_cnf(lchan, br->fn);
	}

	return 0;
}
