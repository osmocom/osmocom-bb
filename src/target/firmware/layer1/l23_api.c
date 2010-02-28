/* Synchronous part of GSM Layer 1: API to Layer2+ */

/* (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include <comm/msgb.h>
#include <comm/sercomm.h>

#include <layer1/sync.h>
#include <layer1/async.h>

#include <l1a_l23_interface.h>

/* the size we will allocate struct msgb* for HDLC */
#define L3_MSG_SIZE (sizeof(struct l1_ccch_info_ind) + 4)
#define L3_MSG_HEAD 4

void l1_queue_for_l2(struct msgb *msg)
{
	/* forward via serial for now */
	sercomm_sendmsg(SC_DLCI_L1A_L23, msg);
}

struct msgb *l1_create_l2_msg(int msg_type, uint32_t fn, uint16_t snr)
{
	struct l1_info_dl *dl;
	struct msgb *msg;

	msg = msgb_alloc_headroom(L3_MSG_SIZE, L3_MSG_HEAD, "l1_burst");
	if (!msg) {
		while (1) {
			puts("OOPS. Out of buffers...\n");
		}

		return NULL;
	}

	dl = (struct l1_info_dl *) msgb_put(msg, sizeof(*dl));
	dl->msg_type = msg_type;
	/* FIXME: we may want to compute T1/T2/T3 in L23 */
	gsm_fn2gsmtime(&dl->time, fn);
	dl->snr[0] = snr;

	return msg;
}

/* callbakc from SERCOMM when L2 sends a message to L1 */
static void l1a_l23_rx_cb(uint8_t dlci, struct msgb *msg)
{
	struct l1_info_ul *ul = msg->data;
	struct l1_sync_new_ccch_req *sync_req;
	struct l1_rach_req *rach_req;
	struct l1_dedic_mode_est_req *est_req;

	if (sizeof(*ul) > msg->len) {
		printf("la1_l23_cb: Short message. %u\n", msg->len);
		goto exit;
	}

	switch (ul->msg_type) {
	case SYNC_NEW_CCCH_REQ:
		if (sizeof(*ul) + sizeof(*sync_req) > msg->len) {
			printf("Short sync msg. %u\n", msg->len);
			break;
		}

		sync_req = (struct l1_sync_new_ccch_req *) (&msg->data[0] + sizeof(*ul));
		printf("Asked to tune to frequency: %u\n", sync_req->band_arfcn);

		/* reset scheduler and hardware */
		tdma_sched_reset();
		l1s_dsp_abort();

		/* tune to specified frequency */
		trf6151_rx_window(0, sync_req->band_arfcn, 40, 0);
		tpu_end_scenario();

		puts("Starting FCCH Recognition\n");
		l1s_fb_test(1, 0);
		break;
	case DEDIC_MODE_EST_REQ:
		est_req = (struct l1_dedic_mode_est_req *) ul->payload;
		/* FIXME: ARFCN! */
		/* figure out which MF tasks to enable, depending on channel number */
		break;
	case CCCH_RACH_REQ:
		rach_req = (struct l1_rach_req *) ul->payload;
		l1a_rach_req(27, rach_req->ra);
		break;
	}

exit:
	msgb_free(msg);
}

void l1a_l23api_init(void)
{
	sercomm_register_rx_cb(SC_DLCI_L1A_L23, l1a_l23_rx_cb);
}
