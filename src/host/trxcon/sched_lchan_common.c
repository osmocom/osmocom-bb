/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: common routines for lchan handlers
 *
 * (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <osmocom/codec/codec.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>

#include "l1ctl_proto.h"
#include "scheduler.h"
#include "sched_trx.h"
#include "logging.h"
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

int sched_send_dt_ind(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint8_t *l2, size_t l2_len,
	int bit_error_count, bool dec_failed, bool traffic)
{
	const struct trx_lchan_desc *lchan_desc;
	struct l1ctl_info_dl dl_hdr;
	int dbm_avg;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];

	/* Fill in known downlink info */
	dl_hdr.chan_nr = lchan_desc->chan_nr | ts->index;
	dl_hdr.link_id = lchan_desc->link_id;
	dl_hdr.band_arfcn = htons(trx->band_arfcn);
	dl_hdr.frame_nr = htonl(lchan->rx_first_fn);
	dl_hdr.num_biterr = bit_error_count;

	/* Convert average RSSI to RX level */
	if (lchan->meas.rssi_num) {
		/* RX level: 0 .. 63 in typical GSM notation (dBm + 110) */
		dbm_avg = lchan->meas.rssi_sum / lchan->meas.rssi_num;
		dl_hdr.rx_level = dbm2rxlev(dbm_avg);
	} else {
		/* No measurements, assuming the worst */
		dl_hdr.rx_level = 0;
	}

	/* FIXME: set proper values */
	dl_hdr.snr = 0;

	/* Mark frame as broken if so */
	dl_hdr.fire_crc = dec_failed ? 2 : 0;

	/* Put a packet to higher layers */
	l1ctl_tx_dt_ind(trx->l1l, &dl_hdr, l2, l2_len, traffic);

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
