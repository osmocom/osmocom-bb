/* Layer 1 Random Access Channel Burst */

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

/* use if we have a combined uplink (RACH, SDCCH, ...) (see
 * http://www.rfwireless-world.com/Terminology/GSM-combined-channel-configuration.html)
 * if we have no combined channel config, uplink consists of only RACH * */
static const uint8_t t3_to_rach_comb[51] = {
        0, 0, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10,
        11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 25, 25, 25,
        25, 25, 25, 25, 25, 26, 27, 27, 27, 27
};
static const uint8_t rach_to_t3_comb[27] = {
        4, 5, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
        30, 31, 32, 33, 34, 35, 36, 45, 46
};

/**
 * @brief Handler callback function for RACH request.
 *
 * @param [in] msg the msg to sent over virtual um.
 */
static void virt_l1_sched_handler_cb(struct l1_model_ms *ms, uint32_t fn, struct msgb *msg)
{
	gsmtapl1_tx_to_virt_um_inst(ms, fn, msg);
	l1ctl_tx_rach_conf(ms, fn, ms->state.serving_cell.arfcn);
}

/**
 * @brief Handler for received L1CTL_RACH_REQ from L23.
 *
 * -- random access channel request --
 *
 * @param [in] msg the received message.
 *
 * Transmit RACH request on RACH. Refer to 04.08 - 9.1.8 - Channel request.
 *
 */
void l1ctl_rx_rach_req(struct l1_model_ms *ms, struct msgb *msg)
{
	struct l1_state_ms *l1s = &ms->state;
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_rach_req *rach_req = (struct l1ctl_rach_req *) ul->payload;
	uint32_t fn_sched;
	uint8_t ts = 1; /* FIXME mostly, ts 1 is used for rach, where can i get that info? System info? */
	uint16_t offset = ntohs(rach_req->offset);

	DEBUGPMS(DL1C, ms, "Received and handled from l23 - L1CTL_RACH_REQ (ra=0x%02x, offset=%d combined=%d)\n",
		rach_req->ra, offset, rach_req->combined);

	if (rach_req->ra == 0x03)
		fn_sched = 42;

	/* set ra data to msg (8bits, the 11bit option is not used for GSM) */
	msg->l2h = msgb_put(msg, sizeof(uint8_t));
	*msg->l2h = rach_req->ra;

	/* chan_nr need to be encoded here, as it is not set by l23 for
	 * the rach request, but needed by virt um */
	ul->chan_nr = rsl_enc_chan_nr(RSL_CHAN_RACH, 0, ts);
	ul->link_id = LID_DEDIC;

	/* sched fn calculation if we have a combined ccch channel configuration */
	if (rach_req->combined) {
		/* add elapsed RACH slots to offset */
		offset += t3_to_rach_comb[l1s->current_time.t3];
		/* offset is the number of RACH slots in the future */
		fn_sched = l1s->current_time.fn - l1s->current_time.t3;
		fn_sched += offset / 27 * 51;
		fn_sched += rach_to_t3_comb[offset % 27];
	} else
		fn_sched = l1s->current_time.fn + offset;

	virt_l1_sched_schedule(ms, msg, fn_sched, ts, &virt_l1_sched_handler_cb);
}

/**
 * @brief Transmit L1CTL_RACH_CONF to layer 23.
 *
 * -- rach confirm --
 *
 * @param [in] fn the fn on which the rach was sent
 * @param [in] arfcn arfcn on which the rach was sent
 */
void l1ctl_tx_rach_conf(struct l1_model_ms *ms, uint32_t fn, uint16_t arfcn)
{
	struct msgb *msg = l1ctl_create_l2_msg(L1CTL_RACH_CONF, fn, 0, arfcn);

	DEBUGPMS(DL1C, ms, "Sending to l23 - %s (fn: %u, arfcn: %u)\n",
	       getL1ctlPrimName(L1CTL_RACH_CONF), fn, arfcn);
	l1ctl_sap_tx_to_l23_inst(ms, msg);
}
