/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: common routines for lchan handlers
 *
 * (C) 2017-2020 by Vadim Yanitskiy <axilirator@gmail.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <errno.h>
#include <string.h>
#include <talloc.h>
#include <stdint.h>
#include <stdbool.h>

#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/gsmtap.h>

#include <osmocom/codec/codec.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

#include "l1ctl_proto.h"
#include "scheduler.h"
#include "sched_trx.h"
#include "logging.h"
#include "trxcon.h"
#include "trx_if.h"
#include "l1ctl.h"

/* GSM 05.02 Chapter 5.2.3 Normal Burst (NB) */
const uint8_t sched_nb_training_bits[8][26] = {
	{
		0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0,
		0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1,
	},
	{
		0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1,
		1, 1, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1,
	},
	{
		0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1,
		0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0,
	},
	{
		0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 0,
		1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0,
	},
	{
		0, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0,
		1, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 1, 1,
	},
	{
		0, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 1, 0,
		0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 0,
	},
	{
		1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1,
		0, 0, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1,
	},
	{
		1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 1, 0,
		0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0,
	},
};

/* Get a string representation of the burst buffer's completeness.
 * Examples: "  ****.." (incomplete, 4/6 bursts)
 *           "    ****" (complete, all 4 bursts)
 *           "**.***.." (incomplete, 5/8 bursts) */
const char *burst_mask2str(const uint8_t *mask, int bits)
{
	/* TODO: CSD is interleaved over 22 bursts, so the mask needs to be extended */
	static char buf[8 + 1];
	char *ptr = buf;

	OSMO_ASSERT(bits <= 8 && bits > 0);

	while (--bits >= 0)
		*(ptr++) = (*mask & (1 << bits)) ? '*' : '.';
	*ptr = '\0';

	return buf;
}

int sched_gsmtap_send(enum trx_lchan_type lchan_type, uint32_t fn, uint8_t tn,
		      uint16_t band_arfcn, int8_t signal_dbm, uint8_t snr,
		      const uint8_t *data, size_t data_len)
{
	const struct trx_lchan_desc *lchan_desc = &trx_lchan_desc[lchan_type];

	/* GSMTAP logging may not be enabled */
	if (gsmtap == NULL)
		return 0;

	/* Omit frames with unknown channel type */
	if (lchan_desc->gsmtap_chan_type == GSMTAP_CHANNEL_UNKNOWN)
		return 0;

	/* TODO: distinguish GSMTAP_CHANNEL_PCH and GSMTAP_CHANNEL_AGCH */
	return gsmtap_send(gsmtap, band_arfcn, tn, lchan_desc->gsmtap_chan_type,
			   lchan_desc->ss_nr, fn, signal_dbm, snr, data, data_len);
}

int sched_send_dt_ind(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint8_t *l2, size_t l2_len,
	int bit_error_count, bool dec_failed, bool traffic)
{
	const struct trx_meas_set *meas = &lchan->meas_avg;
	const struct trx_lchan_desc *lchan_desc;
	struct l1ctl_info_dl dl_hdr;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];

	/* Fill in known downlink info */
	dl_hdr.chan_nr = lchan_desc->chan_nr | ts->index;
	dl_hdr.link_id = lchan_desc->link_id;
	dl_hdr.band_arfcn = htons(trx->band_arfcn);
	dl_hdr.frame_nr = htonl(lchan->rx_first_fn);
	dl_hdr.num_biterr = bit_error_count;

	/* RX level: 0 .. 63 in typical GSM notation (dBm + 110) */
	dl_hdr.rx_level = dbm2rxlev(meas->rssi);

	/* FIXME: set proper values */
	dl_hdr.snr = 0;

	/* Mark frame as broken if so */
	dl_hdr.fire_crc = dec_failed ? 2 : 0;

	/* Put a packet to higher layers */
	l1ctl_tx_dt_ind(trx->l1l, &dl_hdr, l2, l2_len, traffic);

	/* Optional GSMTAP logging */
	if (l2_len > 0 && (!traffic || lchan_desc->chan_nr == RSL_CHAN_OSMO_PDCH)) {
		sched_gsmtap_send(lchan->type, lchan->rx_first_fn, ts->index,
				  trx->band_arfcn, meas->rssi, 0, l2, l2_len);
	}

	return 0;
}

int sched_send_dt_conf(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, bool traffic)
{
	const struct trx_lchan_desc *lchan_desc;
	struct l1ctl_info_dl dl_hdr;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];

	/* Zero-initialize DL header, because we don't set all fields */
	memset(&dl_hdr, 0x00, sizeof(struct l1ctl_info_dl));

	/* Fill in known downlink info */
	dl_hdr.chan_nr = lchan_desc->chan_nr | ts->index;
	dl_hdr.link_id = lchan_desc->link_id;
	dl_hdr.band_arfcn = htons(trx->band_arfcn);
	dl_hdr.frame_nr = htonl(fn);

	l1ctl_tx_dt_conf(trx->l1l, &dl_hdr, traffic);

	/* Optional GSMTAP logging */
	if (!traffic || lchan_desc->chan_nr == RSL_CHAN_OSMO_PDCH) {
		sched_gsmtap_send(lchan->type, fn, ts->index,
				  trx->band_arfcn | ARFCN_UPLINK,
				  0, 0, lchan->prim->payload,
				  lchan->prim->payload_len);
	}

	return 0;
}

/**
 * Composes a bad frame indication message
 * according to the current tch_mode.
 *
 * @param  l2       Caller-allocated byte array
 * @param  lchan    Logical channel to generate BFI for
 * @return          How much bytes were written
 */
size_t sched_bad_frame_ind(uint8_t *l2, struct trx_lchan_state *lchan)
{
	switch (lchan->tch_mode) {
	case GSM48_CMODE_SPEECH_V1:
		if (lchan->type == TRXC_TCHF) { /* Full Rate */
			memset(l2, 0x00, GSM_FR_BYTES);
			l2[0] = 0xd0;
			return GSM_FR_BYTES;
		} else { /* Half Rate */
			memset(l2 + 1, 0x00, GSM_HR_BYTES);
			l2[0] = 0x70; /* F = 0, FT = 111 */
			return GSM_HR_BYTES + 1;
		}
	case GSM48_CMODE_SPEECH_EFR: /* Enhanced Full Rate */
		memset(l2, 0x00, GSM_EFR_BYTES);
		l2[0] = 0xc0;
		return GSM_EFR_BYTES;
	case GSM48_CMODE_SPEECH_AMR: /* Adaptive Multi Rate */
		/* FIXME: AMR is not implemented yet */
		return 0;
	case GSM48_CMODE_SIGN:
		LOGP(DSCH, LOGL_ERROR, "BFI is not allowed in signalling mode\n");
		return 0;
	default:
		LOGP(DSCH, LOGL_ERROR, "Invalid TCH mode: %u\n", lchan->tch_mode);
		return 0;
	}
}
