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

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/fsm.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/coding/gsm0503_coding.h>

#include "l1ctl_proto.h"
#include "scheduler.h"
#include "sched_trx.h"
#include "logging.h"
#include "trx_if.h"
#include "trxcon.h"
#include "l1ctl.h"

static void decode_sb(struct gsm_time *time, uint8_t *bsic, uint8_t *sb_info)
{
	uint8_t t3p;
	uint32_t sb;

	sb = (sb_info[3] << 24)
	   | (sb_info[2] << 16)
	   | (sb_info[1] << 8)
	   | sb_info[0];

	*bsic = (sb >> 2) & 0x3f;

	/* TS 05.02 Chapter 3.3.2.2.1 SCH Frame Numbers */
	time->t1 = ((sb >> 23) & 0x01)
		 | ((sb >> 7) & 0x1fe)
		 | ((sb << 9) & 0x600);

	time->t2 = (sb >> 18) & 0x1f;

	t3p = ((sb >> 24) & 0x01) | ((sb >> 15) & 0x06);
	time->t3 = t3p * 10 + 1;

	/* TS 05.02 Chapter 4.3.3 TDMA frame number */
	time->fn = gsm_gsmtime2fn(time);
}

int rx_sch_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan, uint8_t bid,
	sbit_t *bits, uint16_t nbits, int8_t rssi, float toa)
{
	sbit_t payload[2 * 39];
	struct gsm_time time;
	uint8_t sb_info[4];
	uint8_t bsic;
	int rc;

	/* Obtain payload from burst */
	memcpy(payload, bits + 3, 39);
	memcpy(payload + 39, bits + 3 + 39 + 64, 39);

	/* Attempt to decode */
	rc = gsm0503_sch_decode(sb_info, payload);
	if (rc) {
		LOGP(DSCH, LOGL_DEBUG, "Received bad SCH burst at fn=%u\n", fn);
		return rc;
	}

	/* Decode BSIC and TDMA frame number */
	decode_sb(&time, &bsic, sb_info);

	LOGP(DSCH, LOGL_DEBUG, "Received SCH: bsic=%u, fn=%u, sched_fn=%u\n",
		bsic, time.fn, trx->sched.fn_counter_proc);

	/* Check if decoded frame number matches */
	if (time.fn != fn) {
		LOGP(DSCH, LOGL_ERROR, "Decoded fn=%u does not match "
			"fn=%u provided by scheduler\n", time.fn, fn);
		return -EINVAL;
	}

	/* We don't need to send L1CTL_FBSB_CONF */
	if (trx->l1l->fbsb_conf_sent)
		return 0;

	/* Send L1CTL_FBSB_CONF to higher layers */
	struct l1ctl_info_dl *data;
	data = talloc_zero_size(ts, sizeof(struct l1ctl_info_dl));
	if (data == NULL)
		return -ENOMEM;

	/* Fill in some downlink info */
	data->chan_nr = trx_lchan_desc[chan].chan_nr | ts->index;
	data->link_id = trx_lchan_desc[chan].link_id;
	data->band_arfcn = htons(trx->band_arfcn);
	data->frame_nr = htonl(fn);
	data->rx_level = -rssi;

	/* FIXME: set proper values */
	data->num_biterr = 0;
	data->fire_crc = 0;
	data->snr = 0;

	l1ctl_tx_fbsb_conf(trx->l1l, 0, data, bsic);

	/* Update BSIC value of trx_instance */
	trx->bsic = bsic;

	return 0;
}
