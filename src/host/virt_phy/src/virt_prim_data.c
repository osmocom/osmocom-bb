/* Layer 1 normal data burst uplink handling and scheduling */

/* (C) 2010 by Dieter Spaar <spaar@mirider.augusta.de>
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/core/msgb.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/virt_l1_sched.h>
#include <virtphy/logging.h>
#include <virtphy/gsmtapl1_if.h>

#include <l1ctl_proto.h>

static struct l1_model_ms *l1_model_ms = NULL;
static void virt_l1_sched_handler_cb(uint32_t fn, struct msgb * msg);

/**
 * @brief Handler callback function for DATA request.
 *
 * @param [in] fn frame number
 * @param [in] msg the msg to sent over virtual um.
 */
static void virt_l1_sched_handler_cb(uint32_t fn, struct msgb * msg)
{
	gsmtapl1_tx_to_virt_um(fn, msg);
	l1ctl_tx_data_conf(fn, 0, l1_model_ms->state->serving_cell.arfcn);
}

/**
 * @brief Handler for received L1CTL_DATA_REQ from L23.
 *
 * -- data request --
 *
 * @param [in] msg the received message.
 *
 * Transmit message on a signalling channel. FACCH/SDCCH or SACCH depending on the headers set link id (TS 8.58 - 9.3.2).
 *
 * TODO: Check if a msg on FACCH needs special handling.
 */
void l1ctl_rx_data_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_data_ind *data_ind = (struct l1ctl_data_ind *)ul->payload;
	uint8_t rsl_chantype, subslot, timeslot;
	uint32_t fn_sched = sched_fn_ul(l1_model_ms->state->current_time,
	                                ul->chan_nr, ul->link_id);

	rsl_dec_chan_nr(ul->chan_nr, &rsl_chantype, &subslot, &timeslot);
	DEBUGP(DL1C,
	       "Received and handled from l23 - L1CTL_DATA_REQ (link_id=0x%02x, ul=%p, ul->payload=%p, data_ind=%p, data_ind->data=%p l3h=%p)\n",
	       ul->link_id, ul, ul->payload, data_ind, data_ind->data,
	       msg->l3h);

	msg->l2h = data_ind->data;

	virt_l1_sched_schedule(msg, fn_sched, timeslot, &virt_l1_sched_handler_cb);
}

void l1ctl_tx_data_ind(struct msgb *msg, uint16_t arfcn, uint8_t link_id,
                       uint8_t chan_nr, uint32_t fn, uint8_t snr,
                       uint8_t signal_dbm, uint8_t num_biterr, uint8_t fire_crc)
{
	struct msgb *l1ctl_msg = NULL;
	struct l1ctl_data_ind * l1di;
	struct l1ctl_info_dl * l1dl;
	l1ctl_msg = l1ctl_msgb_alloc(L1CTL_DATA_IND);
	l1dl = (struct l1ctl_info_dl *)msgb_put(l1ctl_msg,
	                                        sizeof(struct l1ctl_info_dl));
	l1di = (struct l1ctl_data_ind *)msgb_put(l1ctl_msg,
	                                         sizeof(struct l1ctl_data_ind));

	l1dl->band_arfcn = htons(arfcn);
	l1dl->link_id = link_id;
	l1dl->chan_nr = chan_nr;
	l1dl->frame_nr = htonl(fn);
	l1dl->snr = snr;
	l1dl->rx_level = signal_dbm;
	l1dl->num_biterr = 0; /* no biterrors */
	l1dl->fire_crc = 0;

	/* TODO: data decoding and decryption */

	memcpy(l1di->data, msgb_data(msg), msgb_length(msg));

	DEBUGP(DL1C, "Sending signaling-data to l23.\n");
	l1ctl_sap_tx_to_l23(l1ctl_msg);
}

/**
 * @brief Send a L1CTL_DATA_CONF to L23.
 *
 * @param [in] fn frame number
 * @param [in] snr signal noise ratio
 * @param [in] arfcn arfcn of the cell the message was send on
 *
 */
void l1ctl_tx_data_conf(uint32_t fn, uint16_t snr, uint16_t arfcn)
{
	struct msgb * l1ctl_msg;
	l1ctl_msg = l1ctl_create_l2_msg(L1CTL_DATA_CONF, fn, snr, arfcn);
	/* send confirm to layer23 */
	l1ctl_sap_tx_to_l23(l1ctl_msg);
}

/**
 * @brief Initialize virtual prim data.
 *
 * @param [in] model the l1 model instance
 */
void prim_data_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
}
