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

#define DEBUG

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <byteorder.h>

#include <osmocore/msgb.h>
#include <comm/sercomm.h>

#include <layer1/sync.h>
#include <layer1/async.h>
#include <layer1/mframe_sched.h>
#include <layer1/tpu_window.h>

#include <rf/trf6151.h>

#include <l1a_l23_interface.h>

/* the size we will allocate struct msgb* for HDLC */
#define L3_MSG_HEAD 4
#define L3_MSG_SIZE (sizeof(struct l1ctl_info_dl)+sizeof(struct l1ctl_data_ind) + L3_MSG_HEAD)

void l1_queue_for_l2(struct msgb *msg)
{
	/* forward via serial for now */
	sercomm_sendmsg(SC_DLCI_L1A_L23, msg);
}

static enum mframe_task chan_nr2mf_task(uint8_t chan_nr)
{
	uint8_t cbits = chan_nr >> 3;
	uint8_t lch_idx;

	if (cbits == 0x01) {
		lch_idx = 0;
		/* FIXME: TCH/F */
	} else if ((cbits & 0x1e) == 0x02) {
		lch_idx = cbits & 0x1;
		/* FIXME: TCH/H */
	} else if ((cbits & 0x1c) == 0x04) {
		lch_idx = cbits & 0x3;
		return MF_TASK_SDCCH4_0 + lch_idx;
	} else if ((cbits & 0x18) == 0x08) {
		lch_idx = cbits & 0x7;
		return MF_TASK_SDCCH8_0 + lch_idx;
#if 0
	} else if (cbits == 0x10) {
		/* FIXME: when to do extended BCCH? */
		return MF_TASK_BCCH_NORM;
	} else if (cbits == 0x11 || cbits == 0x12) {
		/* FIXME: how to decide CCCH norm/extd? */
		return MF_TASK_BCCH_CCCH;
#endif
	}
	return 0;
}

struct msgb *l1ctl_msgb_alloc(uint8_t msg_type)
{
	struct msgb *msg;
	struct l1ctl_hdr *l1h;

	msg = msgb_alloc_headroom(L3_MSG_SIZE, L3_MSG_HEAD, "l1ctl");
	if (!msg) {
		while (1) {
			puts("OOPS. Out of buffers...\n");
		}

		return NULL;
	}
	l1h = (struct l1ctl_hdr *) msgb_put(msg, sizeof(*l1h));
	l1h->msg_type = msg_type;
	l1h->flags = 0;

	msg->l1h = l1h;

	return msg;
}

struct msgb *l1_create_l2_msg(int msg_type, uint32_t fn, uint16_t snr,
			      uint16_t arfcn)
{
	struct l1ctl_info_dl *dl;
	struct msgb *msg = l1ctl_msgb_alloc(msg_type);

	dl = (struct l1ctl_info_dl *) msgb_put(msg, sizeof(*dl));
	dl->frame_nr = htonl(fn);
	dl->snr = snr;
	dl->band_arfcn = htons(arfcn);

	return msg;
}

/* receive a L1CTL_PM_REQ from L23 */
void l1ctl_rx_pm_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_pm_req *pm_req = (struct l1ctl_pm_req *) l1h->data;

	switch (pm_req->type) {
	case 1:
		l1s.pm.mode = 1;
		l1s.pm.range.arfcn_start =
				ntohs(pm_req->range.band_arfcn_from);
		l1s.pm.range.arfcn_next =
				ntohs(pm_req->range.band_arfcn_from);
		l1s.pm.range.arfcn_end =
				ntohs(pm_req->range.band_arfcn_to);
		printf("L1CTL_PM_REQ start=%u end=%u\n",
			l1s.pm.range.arfcn_start, l1s.pm.range.arfcn_end);
		break;
	}

	l1s_pm_test(1, l1s.pm.range.arfcn_next);
}

/* callback from SERCOMM when L2 sends a message to L1 */
static void l1a_l23_rx_cb(uint8_t dlci, struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_fbsb_req *sync_req;
	struct l1ctl_rach_req *rach_req;
	struct l1ctl_dm_est_req *est_req;
	struct l1ctl_data_ind *data_ind;
	struct llist_head *tx_queue;

	{
		int i;
		printf("l1a_l23_rx_cb (%u): ", msg->len);
		for (i = 0; i < msg->len; i++)
			printf("%02x ", msg->data[i]);
		puts("\n");
	}

	msg->l1h = msg->data;

	if (sizeof(*l1h) > msg->len) {
		printf("l1a_l23_cb: Short message. %u\n", msg->len);
		goto exit_msgbfree;
	}

	switch (l1h->msg_type) {
	case L1CTL_FBSB_REQ:
		if (sizeof(*sync_req) > msg->len) {
			printf("Short sync msg. %u\n", msg->len);
			break;
		}

		sync_req = (struct l1ctl_fbsb_req *) l1h->data;
		printd("L1CTL_FBSB_REQ (arfcn=%u)\n", ntohs(sync_req->band_arfcn));

		/* reset scheduler and hardware */
		l1s_reset();

		/* tune to specified frequency */
		trf6151_rx_window(0, ntohs(sync_req->band_arfcn), 40, 0);
		tpu_end_scenario();

		printd("Starting FCCH Recognition\n");
		l1s_fbsb_req(1, sync_req);
		break;
	case L1CTL_DM_EST_REQ:
		est_req = (struct l1ctl_dm_est_req *) ul->payload;
		printd("L1CTL_DM_EST_REQ (arfcn=%u, chan_nr=0x%02x)\n",
			ntohs(est_req->band_arfcn), ul->chan_nr);
		if (ntohs(est_req->band_arfcn) != l1s.serving_cell.arfcn) {
			/* FIXME: ARFCN */
			puts("We don't support ARFCN switches yet\n");
			break;
		}
		if (ul->chan_nr & 0x7) {
			/* FIXME: Timeslot */
			puts("We don't support non-0 TS yet\n");
			break;
		}
		if (est_req->h0.h) {
			puts("We don't support frequency hopping yet\n");
			break;
		}
		/* FIXME: set TSC of ded chan according to est_req.h0.tsc */
		/* figure out which MF tasks to enable */
		l1a_mftask_set(1 << chan_nr2mf_task(ul->chan_nr));
		break;
	case L1CTL_RACH_REQ:
		rach_req = (struct l1ctl_rach_req *) ul->payload;
		printd("L1CTL_RACH_REQ (ra=0x%02x)\n", rach_req->ra);
		l1a_rach_req(27, rach_req->ra);
		break;
	case L1CTL_DATA_REQ:
		data_ind = (struct l1ctl_data_ind *) ul->payload;
		printd("L1CTL_DATA_REQ (link_id=0x%02x)\n", ul->link_id);
		if (ul->link_id & 0x40)
			tx_queue = &l1s.tx_queue[L1S_CHAN_SACCH];
		else
			tx_queue = &l1s.tx_queue[L1S_CHAN_MAIN];
		msg->l3h = data_ind->data;
		printd("ul=%p, ul->payload=%p, data_ind=%p, data_ind->data=%p l3h=%p\n",
			ul, ul->payload, data_ind, data_ind->data, msg->l3h);
		l1a_txq_msgb_enq(tx_queue, msg);
		/* we have to keep the msgb, not free it! */
		goto exit_nofree;
	case L1CTL_PM_REQ:
		l1ctl_rx_pm_req(msg);
		break;
	}

exit_msgbfree:
	msgb_free(msg);
exit_nofree:
	return;
}

void l1a_l23api_init(void)
{
	sercomm_register_rx_cb(SC_DLCI_L1A_L23, l1a_l23_rx_cb);
}
