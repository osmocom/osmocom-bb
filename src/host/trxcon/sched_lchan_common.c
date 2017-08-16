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

#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/bits.h>

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
	struct trx_lchan_state *lchan, uint8_t *l2, size_t l2_len)
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
	data->rx_level = -(lchan->rssi_sum / lchan->rssi_num);

	/* FIXME: set proper values */
	data->num_biterr = 0;
	data->fire_crc = 0;
	data->snr = 0;

	/* Fill in the payload */
	memcpy(data->payload, l2, l2_len);

	/* Put a packet to higher layers */
	l1ctl_tx_data_ind(trx->l1l, data, l2_len == 23 ?
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
	conf_type = l2_len == 23 ?
		L1CTL_DATA_CONF : L1CTL_TRAFFIC_CONF;

	l1ctl_tx_data_conf(trx->l1l, data, conf_type);
	talloc_free(data);

	return 0;
}
