/* Layer 1 normal data burst tx handling */

/* (C) 2010 by Dieter Spaar <spaar@mirider.augusta.de>
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
static void virt_l1_sched_handler_cb(struct msgb * msg);

/**
 * @brief Handler callback function for TRAFFIC request.
 *
 * @param [in] msg the msg to sent over virtual um.
 */
static void virt_l1_sched_handler_cb(struct msgb * msg)
{
	gsmtapl1_tx_to_virt_um(msg);
	// send confirm to layer23
	msg = l1ctl_create_l2_msg(L1CTL_TRAFFIC_CONF,
	                          l1_model_ms->state->current_time.fn, 0, 0);
	l1ctl_sap_tx_to_l23(msg);
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
void l1ctl_rx_traffic_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_traffic_req *tr = (struct l1ctl_traffic_req *)ul->payload;
	// TODO: calc the scheduled fn
	uint32_t fn_sched = l1_model_ms->state->current_time.fn;
	uint8_t rsl_chantype, subslot, timeslot;
	rsl_dec_chan_nr(ul->chan_nr, &rsl_chantype, &subslot, &timeslot);

	DEBUGP(DL1C, "Received and handled from l23 - L1CTL_TRAFFIC_REQ\n");

	msg->l2h = tr->data;

	virt_l1_sched_schedule(msg, fn_sched, timeslot,
	                       &virt_l1_sched_handler_cb);
}

/**
 * @brief Initialize virtual prim traffic.
 *
 * @param [in] model the l1 model instance
 */
void prim_traffic_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
}
