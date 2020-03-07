/* Layer 1 normal data burst tx handling */

/* (C) 2010 by Dieter Spaar <spaar@mirider.augusta.de>
 * (C) 2010,2017 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/core/msgb.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/virt_l1_sched.h>
#include <virtphy/logging.h>
#include <virtphy/gsmtapl1_if.h>

#include <l1ctl_proto.h>

/**
 * @brief Handler callback function for TRAFFIC request.
 *
 * @param [in] fn frame number
 * @param [in] msg the msg to sent over virtual um.
 */
static void virt_l1_sched_handler_cb(struct l1_model_ms *ms, uint32_t fn, uint8_t tn, struct msgb * msg)
{
	gsmtapl1_tx_to_virt_um_inst(ms, fn, tn, msg);
	l1ctl_tx_traffic_conf(ms, fn, 0, ms->state.serving_cell.arfcn);
}

/**
 * @brief Handler for received L1CTL_TRAFFIC_REQ from L23.
 *
 * -- traffic request --
 *
 * @param [in] msg the received message.
 *
 * Enqueue the message (traffic frame) to the L1 state machine's transmit queue. In virtual layer1 just submit it to the virt um.
 *
 */
void l1ctl_rx_traffic_req(struct l1_model_ms *ms, struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_traffic_req *tr = (struct l1ctl_traffic_req *) ul->payload;
	uint8_t rsl_chantype, subslot, timeslot;
	uint32_t fn_sched = sched_fn_ul(ms->state.current_time, ul->chan_nr, ul->link_id);

	rsl_dec_chan_nr(ul->chan_nr, &rsl_chantype, &subslot, &timeslot);
	DEBUGPMS(DL1P, ms, "Rx L1CTL_TRAFFIC_REQ (chan_nr=0x%02x, link_id=0x%02x)\n",
		 ul->chan_nr, ul->link_id);

	msg->l2h = tr->data;

	virt_l1_sched_schedule(ms, msg, fn_sched, timeslot, &virt_l1_sched_handler_cb);
}

void l1ctl_tx_traffic_ind(struct l1_model_ms *ms, struct msgb *msg, uint16_t arfcn, uint8_t link_id,
			  uint8_t chan_nr, uint32_t fn, uint8_t snr, uint8_t signal_dbm,
			  uint8_t num_biterr, uint8_t fire_crc)
{
	struct msgb *l1ctl_msg = NULL;
	struct l1ctl_traffic_ind * l1ti;
	struct l1ctl_info_dl * l1dl;
	uint8_t *frame;
	int frame_len;
	uint8_t rsl_chan_type, subchan, timeslot;
	l1ctl_msg = l1ctl_msgb_alloc(L1CTL_TRAFFIC_IND);
	l1dl = (struct l1ctl_info_dl *) msgb_put(l1ctl_msg, sizeof(*l1dl));
	l1ti = (struct l1ctl_traffic_ind *) msgb_put(l1ctl_msg, sizeof(*l1ti));

	rsl_dec_chan_nr(chan_nr, &rsl_chan_type, &subchan, &timeslot);

	l1dl->band_arfcn = htons(arfcn);
	l1dl->link_id = link_id;
	l1dl->chan_nr = chan_nr;
	l1dl->frame_nr = htonl(fn);
	l1dl->snr = snr;
	l1dl->rx_level = signal_dbm;
	l1dl->num_biterr = 0; /* no biterrors */
	l1dl->fire_crc = 0;

	/* The first byte indicates the type of voice frame (enum gsmtap_um_voice_type),
	 * which we simply ignore here and pass on the frame without that byte.
	 * TODO: Check for consistency with ms->state.tch_mode ? */
	frame_len = msgb_length(msg) - 1;
	if (frame_len < 0) {
		msgb_free(l1ctl_msg);
		return;
	}
	frame = (uint8_t *) msgb_put(l1ctl_msg, frame_len);
	memcpy(frame, msgb_data(msg)+1, frame_len);

	DEBUGPMS(DL1P, ms, "Tx L1CTL_TRAFFIC_IND (chan_nr=0x%02x, link_id=0x%02x)\n", chan_nr, link_id);
	l1ctl_sap_tx_to_l23_inst(ms, l1ctl_msg);
}

/**
 * @brief Send a L1CTL_TRAFFIC_CONF to L23.
 *
 * @param [in] fn frame number
 * @param [in] snr signal noise ratio
 * @param [in] arfcn arfcn of the cell the message was send on
 *
 */
void l1ctl_tx_traffic_conf(struct l1_model_ms *ms, uint32_t fn, uint16_t snr, uint16_t arfcn)
{
	struct msgb * l1ctl_msg;
	l1ctl_msg = l1ctl_create_l2_msg(L1CTL_TRAFFIC_CONF, fn, snr, arfcn);
	/* send confirm to layer23 */
	DEBUGPMS(DL1P, ms, "Tx L1CTL_TRAFFIC_CONF\n");
	l1ctl_sap_tx_to_l23_inst(ms, l1ctl_msg);
}
