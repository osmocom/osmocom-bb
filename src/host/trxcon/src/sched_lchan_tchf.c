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
	sbit_t *buffer, *offset;
	uint8_t l2[128], *mask;
	size_t l2_len;
	int amr = 0;
	uint8_t ft;
	bool amr_is_cmr;

	/* Set up pointers */
	mask = &lchan->rx_burst_mask;
	buffer = lchan->rx_bursts;

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "Traffic received: fn=%u bid=%u\n", bi->fn, bi->bid);

	/* Align to the first burst of a block */
	if (*mask == 0x00 && bi->bid != 0)
		return 0;

	/* Update mask */
	*mask |= (1 << bi->bid);

	/* Store the measurements */
	l1sched_lchan_meas_push(lchan, bi);

	/* Copy burst to end of buffer of 8 bursts */
	offset = buffer + bi->bid * 116 + 464;
	memcpy(offset, bi->burst + 3, 58);
	memcpy(offset + 58, bi->burst + 87, 58);

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
		/* the first FN 4,13,21 defines that CMI is included in frame,
		 * the first FN 0,8,17 defines that CMR/CMC is included in frame.
		 * NOTE: A frame ends 7 FN after start.
		 */
		amr_is_cmr = !sched_tchf_dl_amr_cmi_map[bi->fn % 26];

		/* we store tch_data + 2 header bytes, the amr variable set to
		 * 2 will allow us to skip the first 2 bytes in case we did
		 * receive an FACCH frame instead of a voice frame (we do not
		 * know this before we actually decode the frame) */
		amr = 2;
		rc = gsm0503_tch_afs_decode_dtx(l2 + amr, buffer,
			amr_is_cmr, lchan->amr.codec, lchan->amr.codecs, &lchan->amr.dl_ft,
			&lchan->amr.dl_cmr, &n_errors, &n_bits_total, &lchan->amr.last_dtx);

		/* only good speech frames get rtp header */
		if (rc != GSM_MACBLOCK_LEN && rc >= 4) {
			if (lchan->amr.last_dtx == AMR_OTHER) {
				ft = lchan->amr.codec[lchan->amr.dl_ft];
			} else {
				/* SID frames will always get Frame Type Index 8 (AMR_SID) */
				ft = AMR_SID;
			}
			rc = osmo_amr_rtp_enc(l2,
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

	/* Shift buffer by 4 bursts for interleaving */
	memcpy(buffer, buffer + 464, 464);

	/* Check decoding result */
	if (rc < 4) {
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "Received bad frame (rc=%d, ber=%d/%d) at fn=%u\n",
			    rc, n_errors, n_bits_total, lchan->meas_avg.fn);

		/* Send BFI */
		goto bfi;
	} else if (rc == GSM_MACBLOCK_LEN) {
		/* FACCH received, forward it to the higher layers */
		l1sched_handle_data_ind(lchan, l2 + amr, GSM_MACBLOCK_LEN,
					n_errors, n_bits_total,
					L1SCHED_DT_SIGNALING);

		/* Send BFI substituting a stolen TCH frame */
		n_errors = -1; /* ensure fake measurements */
		goto bfi;
	} else {
		/* A good TCH frame received */
		l2_len = rc;
	}

	/* Send a traffic frame to the higher layers */
	return l1sched_handle_data_ind(lchan, l2, l2_len, n_errors, n_bits_total, L1SCHED_DT_TRAFFIC);

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
	if (lchan->tch_mode == GSM48_CMODE_SIGN) {
		return l1sched_handle_data_ind(lchan, NULL, 0,
					       n_errors, n_bits_total,
					       L1SCHED_DT_TRAFFIC);
	}

	/* Bad frame indication */
	l2_len = l1sched_bad_frame_ind(l2, lchan);

	/* Send a BFI frame to the higher layers */
	return l1sched_handle_data_ind(lchan, l2, l2_len,
				       n_errors, n_bits_total,
				       L1SCHED_DT_TRAFFIC);
}

int tx_tchf_fn(struct l1sched_lchan_state *lchan,
	       struct l1sched_burst_req *br)
{
	ubit_t *buffer, *offset;
	const uint8_t *tsc;
	uint8_t *mask;
	size_t l2_len;
	int rc;

	/* Set up pointers */
	mask = &lchan->tx_burst_mask;
	buffer = lchan->tx_bursts;

	/* If we have encoded bursts */
	if (*mask)
		goto send_burst;

	/* Wait until a first burst in period */
	if (br->bid > 0)
		return 0;

	/* Shift buffer by 4 bursts back for interleaving */
	memcpy(buffer, buffer + 464, 464);

	/* populate the buffer with bursts */
	if (L1SCHED_PRIM_IS_FACCH(lchan->prim)) {
		/* Encode payload */
		rc = gsm0503_tch_fr_encode(buffer, lchan->prim->payload, GSM_MACBLOCK_LEN, 1);
	} else if (lchan->tch_mode == GSM48_CMODE_SPEECH_AMR) {
		int len;
		uint8_t cmr_codec;
		int ft, cmr, i;
		enum osmo_amr_type ft_codec;
		enum osmo_amr_quality bfi;
		int8_t sti, cmi;
		bool amr_fn_is_cmr;
		/* the first FN 0,8,17 defines that CMI is included in frame,
		 * the first FN 4,13,21 defines that CMR is included in frame.
		 */
		amr_fn_is_cmr = !sched_tchf_ul_amr_cmi_map[br->fn % 26];

		len = osmo_amr_rtp_dec(lchan->prim->payload, lchan->prim->payload_len,
				&cmr_codec, &cmi, &ft_codec,
				&bfi, &sti);
		if (len < 0) {
			LOGP_LCHAND(lchan, LOGL_ERROR, "Cannot send invalid AMR payload (%zu): %s\n",
				lchan->prim->payload_len, osmo_hexdump(lchan->prim->payload, lchan->prim->payload_len));
			goto free_bad_msg;
		}
		ft = -1;
		cmr = -1;
		for (i = 0; i < lchan->amr.codecs; i++) {
			if (lchan->amr.codec[i] == ft_codec)
				ft = i;
			if (lchan->amr.codec[i] == cmr_codec)
				cmr = i;
		}
		if (ft < 0) {
			LOGP_LCHAND(lchan, LOGL_ERROR,
				    "Codec (FT = %d) of RTP frame not in list\n", ft_codec);
			goto free_bad_msg;
		}
		if (amr_fn_is_cmr && lchan->amr.ul_ft != ft) {
			LOGP_LCHAND(lchan, LOGL_ERROR,
				    "Codec (FT = %d) of RTP cannot be changed now, but in next frame\n",
				    ft_codec);
			goto free_bad_msg;
		}
		lchan->amr.ul_ft = ft;
		if (cmr < 0) {
			LOGP_LCHAND(lchan, LOGL_ERROR,
				    "Codec (CMR = %d) of RTP frame not in list\n", cmr_codec);
		} else {
			lchan->amr.ul_cmr = cmr;
		}
		rc = gsm0503_tch_afs_encode(buffer, lchan->prim->payload + 2,
			lchan->prim->payload_len - 2, amr_fn_is_cmr,
			lchan->amr.codec, lchan->amr.codecs,
			lchan->amr.ul_ft,
			lchan->amr.ul_cmr);
	} else {
		/* Determine and check the payload length */
		switch (lchan->tch_mode) {
		case GSM48_CMODE_SIGN:
		case GSM48_CMODE_SPEECH_V1: /* FR */
			l2_len = GSM_FR_BYTES;
			break;
		case GSM48_CMODE_SPEECH_EFR: /* EFR */
			l2_len = GSM_EFR_BYTES;
			break;
		default:
			LOGP_LCHAND(lchan, LOGL_ERROR,
				    "Invalid TCH mode: %u, dropping frame...\n",
				    lchan->tch_mode);
			/* Forget this primitive */
			l1sched_prim_drop(lchan);
			return -EINVAL;
		}
		if (lchan->prim->payload_len != l2_len) {
			LOGP_LCHAND(lchan, LOGL_ERROR, "Primitive has odd length %zu "
				    "(expected %zu for TCH or %u for FACCH), so dropping...\n",
				    lchan->prim->payload_len, l2_len, GSM_MACBLOCK_LEN);

			l1sched_prim_drop(lchan);
			return -EINVAL;
		}
		rc = gsm0503_tch_fr_encode(buffer, lchan->prim->payload, l2_len, 1);
	}

	if (rc) {
		LOGP_LCHAND(lchan, LOGL_ERROR, "Failed to encode L2 payload (len=%zu): %s\n",
			    lchan->prim->payload_len, osmo_hexdump(lchan->prim->payload,
								   lchan->prim->payload_len));
free_bad_msg:
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
	br->burst_len = GSM_NBITS_NB_GMSK_BURST;

	LOGP_LCHAND(lchan, LOGL_DEBUG, "Scheduled fn=%u burst=%u\n", br->fn, br->bid);

	/* If we have sent the last (4/4) burst */
	if (*mask == 0x0f) {
		/* Confirm data / traffic sending */
		enum l1sched_data_type dt = L1SCHED_PRIM_IS_TCH(lchan->prim) ?
						L1SCHED_DT_TRAFFIC : L1SCHED_DT_SIGNALING;
		l1sched_handle_data_cnf(lchan, br->fn, dt);

		/* Forget processed primitive */
		l1sched_prim_drop(lchan);

		/* Reset mask */
		*mask = 0x00;
	}

	return 0;
}
