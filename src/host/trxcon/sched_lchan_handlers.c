/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
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

#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/fsm.h>

#include <osmocom/coding/gsm0503_coding.h>

#include "l1ctl_proto.h"
#include "scheduler.h"
#include "sched_trx.h"
#include "logging.h"
#include "trx_if.h"
#include "trxcon.h"

extern struct osmo_fsm_inst *trxcon_fsm;

int rx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan, uint8_t bid,
	sbit_t *bits, uint16_t nbits, int8_t rssi, float toa)
{
	int n_errors, n_bits_total, rc;
	struct trx_lchan_state *lchan;
	uint8_t *rssi_num, *toa_num;
	float *rssi_sum, *toa_sum;
	sbit_t *buffer, *offset;
	uint8_t l2[23], *mask;
	uint32_t *first_fn;

	LOGP(DSCH, LOGL_DEBUG, "Data received on %s: fn=%u ts=%u bid=%u\n",
		trx_lchan_desc[chan].name, fn, ts->index, bid);

	/* Find required channel state */
	lchan = sched_trx_find_lchan(ts, chan);
	if (lchan == NULL)
		return -EINVAL;

	/* Set up pointers */
	first_fn = &lchan->rx_first_fn;
	mask = &lchan->rx_burst_mask;
	buffer = lchan->rx_bursts;

	rssi_sum = &lchan->rssi_sum;
	rssi_num = &lchan->rssi_num;
	toa_sum = &lchan->toa_sum;
	toa_num = &lchan->toa_num;

	/* Clear buffer & store frame number of first burst */
	if (bid == 0) {
		memset(buffer, 0, 464);

		*first_fn = fn;
		*mask = 0x0;

		*rssi_sum = 0;
		*rssi_num = 0;
		*toa_sum = 0;
		*toa_num = 0;
	}

	/* Update mask and RSSI */
	*mask |= (1 << bid);
	*rssi_sum += rssi;
	(*rssi_num)++;
	*toa_sum += toa;
	(*toa_num)++;

	/* Copy burst to buffer of 4 bursts */
	offset = buffer + bid * 116;
	memcpy(offset, bits + 3, 58);
	memcpy(offset + 58, bits + 87, 58);

	/* Wait until complete set of bursts */
	if (bid != 3)
		return 0;

	/* Check for complete set of bursts */
	if ((*mask & 0xf) != 0xf) {
		LOGP(DSCH, LOGL_DEBUG, "Received incomplete data frame at "
			"fn=%u (%u/%u) for %s\n", *first_fn,
			(*first_fn) % ts->mf_layout->period,
			ts->mf_layout->period,
			trx_lchan_desc[chan].name);

		/* We require first burst to have correct FN */
		if (!(*mask & 0x1)) {
			*mask = 0x0;
			return 0;
		}

		/* FIXME: return from here? */
	}

	/* Attempt to decode */
	rc = gsm0503_xcch_decode(l2, buffer, &n_errors, &n_bits_total);
	if (rc) {
		LOGP(DSCH, LOGL_DEBUG, "Received bad data frame at fn=%u "
			"(%u/%u) for %s\n", *first_fn,
			(*first_fn) % ts->mf_layout->period,
			ts->mf_layout->period,
			trx_lchan_desc[chan].name);
		return rc;
	}

	/* Compose a message to the higher layers */
	struct l1ctl_info_dl *data;
	data = talloc_zero_size(ts, sizeof(struct l1ctl_info_dl) + 23);
	if (data == NULL)
		return -ENOMEM;

	/* Fill in some downlink info */
	data->chan_nr = trx_lchan_desc[chan].chan_nr | ts->index;
	data->link_id = trx_lchan_desc[chan].link_id;
	data->band_arfcn = htons(trx->band_arfcn);
	data->frame_nr = htonl(*first_fn);
	data->rx_level = -(*rssi_sum / *rssi_num);

	/* FIXME: set proper values */
	data->num_biterr = n_errors;
	data->fire_crc = 0;
	data->snr = 0;

	/* Fill in decoded payload */
	memcpy(data->payload, l2, 23);

	/* Raise an event to trxcon */
	osmo_fsm_inst_dispatch(trxcon_fsm, SCH_EVENT_DATA, data);

	/* TODO: AGC, TA loops */
	return 0;
}
