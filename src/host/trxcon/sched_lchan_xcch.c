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

/* Forward declarations */
extern const uint8_t nb_training_bits[8][26];

int sched_send_data_ind(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint8_t *l2, size_t l2_len);

int rx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid,
	sbit_t *bits, int8_t rssi, float toa)
{
	const struct trx_lchan_desc *lchan_desc;
	int n_errors, n_bits_total, rc;
	uint8_t *rssi_num, *toa_num;
	float *rssi_sum, *toa_sum;
	sbit_t *buffer, *offset;
	uint8_t l2[23], *mask;
	uint32_t *first_fn;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];
	first_fn = &lchan->rx_first_fn;
	mask = &lchan->rx_burst_mask;
	buffer = lchan->rx_bursts;

	rssi_sum = &lchan->rssi_sum;
	rssi_num = &lchan->rssi_num;
	toa_sum = &lchan->toa_sum;
	toa_num = &lchan->toa_num;

	LOGP(DSCH, LOGL_DEBUG, "Data received on %s: fn=%u ts=%u bid=%u\n",
		lchan_desc->name, fn, ts->index, bid);

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
			lchan_desc->name);

		return -1;
	}

	/* Attempt to decode */
	rc = gsm0503_xcch_decode(l2, buffer, &n_errors, &n_bits_total);
	if (rc) {
		LOGP(DSCH, LOGL_DEBUG, "Received bad data frame at fn=%u "
			"(%u/%u) for %s\n", *first_fn,
			(*first_fn) % ts->mf_layout->period,
			ts->mf_layout->period,
			lchan_desc->name);
		return rc;
	}

	/* Send a L2 frame to the higher layers */
	sched_send_data_ind(trx, ts, lchan, l2, 23);

	/* TODO: AGC, TA loops */
	return 0;
}

int tx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid)
{
	const struct trx_lchan_desc *lchan_desc;
	struct trx_ts_prim *prim;
	struct l1ctl_info_ul *ul;
	ubit_t burst[GSM_BURST_LEN];
	ubit_t *buffer, *offset;
	uint8_t *mask, *l2;
	const uint8_t *tsc;
	int rc;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];
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

	LOGP(DSCH, LOGL_DEBUG, "Transmitting %s fn=%u ts=%u burst=%u\n",
		lchan_desc->name, fn, ts->index, bid);

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
