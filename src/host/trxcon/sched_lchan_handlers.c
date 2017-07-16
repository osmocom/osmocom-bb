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

extern struct osmo_fsm_inst *trxcon_fsm;

/* GSM 05.02 Chapter 5.2.3 Normal Burst (NB) */
static const uint8_t nb_training_bits[8][26] = {
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

	/* Put a packet to higher layers */
	l1ctl_tx_data_ind(trx->l1l, data);
	talloc_free(data);

	/* TODO: AGC, TA loops */
	return 0;
}

int tx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan,
	uint8_t bid, uint16_t *nbits)
{
	struct trx_lchan_state *lchan;
	struct trx_ts_prim *prim;
	struct l1ctl_info_ul *ul;
	ubit_t burst[GSM_BURST_LEN];
	ubit_t *buffer, *offset;
	uint8_t *mask, *l2;
	const uint8_t *tsc;
	int rc;

	/* Find required channel state */
	lchan = sched_trx_find_lchan(ts, chan);
	if (lchan == NULL)
		return -EINVAL;

	/* Set up pointers */
	mask = &lchan->tx_burst_mask;
	buffer = lchan->tx_bursts;

	if (bid > 0) {
		/* If we have encoded bursts */
		if (*mask)
			goto send_burst;
		else
			return 0;
	}

	/* Encode payload if not yet */

	/* Get a message from TX queue */
	prim = llist_entry(ts->tx_prims.next, struct trx_ts_prim, list);
	ul = (struct l1ctl_info_ul *) prim->payload;
	l2 = (uint8_t *) ul->payload;

	/* Encode bursts */
	rc = gsm0503_xcch_encode(buffer, l2);
	if (rc) {
		LOGP(DSCH, LOGL_ERROR, "Failed to encode L2 payload\n");

		/* Remove primitive from queue and free memory */
		llist_del(&prim->list);
		talloc_free(prim);

		return -EINVAL;
	}

send_burst:
	/* Determine which burst should be sent */
	offset = buffer + bid * 116;

	/* Update mask */
	*mask |= (1 << bid);

	/* Choose proper TSC */
	tsc = nb_training_bits[trx->tsc];

	/* Compose a new burst */
	memset(burst, 0, 3); /* TB */
	memcpy(burst + 3, offset, 58); /* Payload 1/2 */
	memcpy(burst + 61, tsc, 26); /* TSC */
	memcpy(burst + 87, offset + 58, 58); /* Payload 2/2 */
	memset(burst + 145, 0, 3); /* TB */

	if (nbits)
		*nbits = GSM_BURST_LEN;

	LOGP(DSCH, LOGL_DEBUG, "Transmitting %s fn=%u ts=%u burst=%u\n",
		trx_lchan_desc[chan].name, fn, ts->index, bid);

	/* Send burst to transceiver */
	rc = trx_if_tx_burst(trx, ts->index, fn, 10, burst);
	if (rc) {
		LOGP(DSCH, LOGL_ERROR, "Could not send burst to transceiver\n");

		/* Remove primitive from queue and free memory */
		prim = llist_entry(ts->tx_prims.next, struct trx_ts_prim, list);
		llist_del(&prim->list);
		talloc_free(prim);

		/* Reset mask */
		*mask = 0x00;

		return rc;
	}

	/* If we have sent the last (4/4) burst */
	if ((*mask & 0x0f) == 0x0f) {
		/* Remove primitive from queue and free memory */
		prim = llist_entry(ts->tx_prims.next, struct trx_ts_prim, list);
		llist_del(&prim->list);
		talloc_free(prim);

		/* Reset mask */
		*mask = 0x00;

		/* Confirm data sending */
		l1ctl_tx_data_conf(trx->l1l);
	}

	return 0;
}

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

/**
 * 41-bit RACH synchronization sequence
 * GSM 05.02 Chapter 5.2.7 Access burst (AB)
 */
static ubit_t rach_synch_seq[] = {
	0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1,
	1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0,
	1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0,
};

/* Obtain a to-be-transmitted RACH burst */
int tx_rach_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan,
	uint8_t bid, uint16_t *nbits)
{
	struct trx_ts_prim *prim;
	struct l1ctl_rach_req *req;
	uint8_t burst[GSM_BURST_LEN];
	uint8_t payload[36];
	int rc;

	/* Get a message from TX queue */
	prim = llist_entry(ts->tx_prims.next, struct trx_ts_prim, list);
	req = (struct l1ctl_rach_req *) prim->payload;

	/* Delay RACH sending according to offset value */
	if (req->offset-- > 0)
		return 0;

	/* Encode payload */
	rc = gsm0503_rach_encode(payload, &req->ra, trx->bsic);
	if (rc) {
		LOGP(DSCH, LOGL_ERROR, "Could not encode RACH burst\n");
		return rc;
	}

	/* Compose RACH burst */
	memset(burst, 0, 8); /* TB */
	memcpy(burst + 8, rach_synch_seq, 41); /* sync seq */
	memcpy(burst + 49, payload, 36); /* payload */
	memset(burst + 85, 0, 63); /* TB + GP */

	LOGP(DSCH, LOGL_DEBUG, "Transmitting RACH fn=%u\n", fn);

	/* Send burst to transceiver */
	rc = trx_if_tx_burst(trx, ts->index, fn, 10, burst);
	if (rc) {
		LOGP(DSCH, LOGL_ERROR, "Could not send burst to transceiver\n");
		return rc;
	}

	/* Confirm RACH request */
	l1ctl_tx_rach_conf(trx->l1l, fn);

	/* Remove primitive from queue and free memory */
	llist_del(&prim->list);
	talloc_free(prim);

	return 0;
}
