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

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

#include "l1ctl_proto.h"
#include "scheduler.h"
#include "sched_trx.h"
#include "logging.h"
#include "trx_if.h"
#include "trxcon.h"
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

int sched_send_data_ind(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint8_t *l2, size_t l2_len,
	bool dec_failed, int bit_error_count)
{
	const struct trx_lchan_desc *lchan_desc;
	struct l1ctl_info_dl *data;

	/* Allocate memory */
	data = talloc_zero_size(ts, sizeof(struct l1ctl_info_dl) + l2_len);
	if (data == NULL)
		return -ENOMEM;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];

	/* Fill in known downlink info */
	data->chan_nr = lchan_desc->chan_nr | ts->index;
	data->link_id = lchan_desc->link_id;
	data->band_arfcn = htons(trx->band_arfcn);
	data->frame_nr = htonl(lchan->rx_first_fn);
	data->rx_level = -(lchan->meas.rssi_sum / lchan->meas.rssi_num);
	data->num_biterr = bit_error_count;

	/* FIXME: set proper values */
	data->snr = 0;

	if (dec_failed) {
		/* Mark frame as broken */
		data->fire_crc = 2;
	} else {
		/* Fill in the payload */
		memcpy(data->payload, l2, l2_len);
	}

	/* Put a packet to higher layers */
	l1ctl_tx_data_ind(trx->l1l, data, l2_len == GSM_MACBLOCK_LEN ?
		L1CTL_DATA_IND : L1CTL_TRAFFIC_IND);
	talloc_free(data);

	return 0;
}

int sched_send_data_conf(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, size_t l2_len)
{
	const struct trx_lchan_desc *lchan_desc;
	struct l1ctl_info_dl *data;
	uint8_t conf_type;

	/* Allocate memory */
	data = talloc_zero(ts, struct l1ctl_info_dl);
	if (data == NULL)
		return -ENOMEM;

	/* Set up pointers */
	lchan_desc = &trx_lchan_desc[lchan->type];

	/* Fill in known downlink info */
	data->chan_nr = lchan_desc->chan_nr | ts->index;
	data->link_id = lchan_desc->link_id;
	data->band_arfcn = htons(trx->band_arfcn);
	data->frame_nr = htonl(fn);

	/* Choose a confirmation type */
	conf_type = l2_len == GSM_MACBLOCK_LEN ?
		L1CTL_DATA_CONF : L1CTL_TRAFFIC_CONF;

	l1ctl_tx_data_conf(trx->l1l, data, conf_type);
	talloc_free(data);

	return 0;
}

/**
 * Composes a bad frame indication message
 * according to the current tch_mode.
 *
 * @param  l2       Pointer to allocated byte array
 * @param  tch_mode Current TCH mode
 * @return          How much bytes were written
 */
size_t sched_bad_frame_ind(uint8_t *l2, uint8_t rsl_cmode, uint8_t tch_mode)
{
	/* BFI is only required for speech */
	if (rsl_cmode != RSL_CMOD_SPD_SPEECH)
		return 0;

	switch (tch_mode) {
	case GSM48_CMODE_SIGN:
	case GSM48_CMODE_SPEECH_V1: /* Full Rate */
		memset(l2, 0x00, GSM_FR_BYTES);
		l2[0] = 0xd0;
		return GSM_FR_BYTES;
	case GSM48_CMODE_SPEECH_EFR: /* Enhanced Full Rate */
		memset(l2, 0x00, GSM_EFR_BYTES);
		l2[0] = 0xc0;
		return GSM_EFR_BYTES;
	case GSM48_CMODE_SPEECH_AMR: /* Adaptive Multi Rate */
		/* FIXME: AMR is not implemented yet */
		return 0;
	default:
		LOGP(DSCH, LOGL_ERROR, "Invalid TCH mode: %u\n", tch_mode);
		return 0;
	}
}

#define PRIM_IS_FACCH(prim) \
	prim->payload_len == GSM_MACBLOCK_LEN

#define PRIM_IS_TCH(prim) \
	prim->payload_len != GSM_MACBLOCK_LEN

struct trx_ts_prim *sched_dequeue_tch_prim(struct llist_head *queue)
{
	struct trx_ts_prim *a, *b;

	/* Obtain the first prim from TX queue */
	a = llist_entry(queue->next, struct trx_ts_prim, list);

	/* If this is the only one => do nothing... */
	if (queue->next->next == queue)
		return a;

	/* Obtain the second prim from TX queue */
	b = llist_entry(queue->next->next, struct trx_ts_prim, list);

	/* Find and prioritize FACCH  */
	if (PRIM_IS_FACCH(a) && PRIM_IS_TCH(b)) {
		/**
		 * Case 1: first is FACCH, second is TCH:
		 * Prioritize FACCH, dropping TCH
		 */
		llist_del(&b->list);
		talloc_free(b);
		return a;
	} else if (PRIM_IS_TCH(a) && PRIM_IS_FACCH(b)) {
		/**
		 * Case 2: first is TCH, second is FACCH:
		 * Prioritize FACCH, dropping TCH
		 */
		llist_del(&a->list);
		talloc_free(a);
		return b;
	} else {
		/**
		 * Otherwise: both are TCH or FACCH frames:
		 * Nothing to prioritize, return the first one
		 */
		return a;
	}
}
