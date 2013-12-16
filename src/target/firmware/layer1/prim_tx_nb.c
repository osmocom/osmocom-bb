/* Layer 1 - Transmit Normal Burst */

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

#include <defines.h>
#include <debug.h>
#include <memory.h>
#include <byteorder.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/msgb.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>

#include <layer1/sync.h>
#include <layer1/agc.h>
#include <layer1/tdma_sched.h>
#include <layer1/mframe_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>
#include <layer1/rfch.h>
#include <layer1/prim.h>

#include <l1ctl_proto.h>


static uint32_t last_txnb_fn;

/* p1: type of operation (0: one NB, 1: one RACH burst, 2: four NB) */
static int l1s_tx_resp(__unused uint8_t p1, __unused uint8_t burst_id,
		       __unused uint16_t p3)
{
	putchart('t');

	dsp_api.r_page_used = 1;

	if (burst_id == 3) {
		last_txnb_fn = l1s.current_time.fn - 4;
		l1s_compl_sched(L1_COMPL_TX_NB);
	}

	return 0;
}

/* p1: type of operation (0: one NB, 1: one RACH burst, 2: four NB) */
static int l1s_tx_cmd(uint8_t p1, uint8_t burst_id, uint16_t p3)
{
	uint16_t arfcn;
	uint8_t tsc, tn;
	uint8_t mf_task_flags = p3 >> 8;

	putchart('T');

	/* before sending first of the four bursts, copy data to API ram */
	if (burst_id == 0) {
		uint16_t *info_ptr = dsp_api.ndb->a_cu;
		struct msgb *msg;
		const uint8_t *data;

		/* distinguish between DCCH and ACCH */
		if (mf_task_flags & MF_F_SACCH) {
			msg = msgb_dequeue(&l1s.tx_queue[L1S_CHAN_SACCH]);
			data = msg ? msg->l3h : pu_get_meas_frame();
		} else {
			msg = msgb_dequeue(&l1s.tx_queue[L1S_CHAN_MAIN]);
			data = msg ? msg->l3h : pu_get_idle_frame();
		}

		/* Fill data block Header */
		info_ptr[0] = (1 << B_BLUD);     // 1st word: Set B_BLU bit.
		info_ptr[1] = 0;                 // 2nd word: cleared.
		info_ptr[2] = 0;                 // 3rd word: cleared.

		/* Copy the actual data after the header */
		dsp_memcpy_to_api(&info_ptr[3], data, 23, 0);

		if (msg)
			msgb_free(msg);
	}

	rfch_get_params(&l1s.next_time, &arfcn, &tsc, &tn);

	l1s_tx_apc_helper(arfcn);

	if (p1 == 0)
		/* DUL_DSP_TASK, one normal burst */
		dsp_load_tch_param(&l1s.next_time,
		                   SIG_ONLY_MODE, INVALID_CHANNEL, 0, 0, 0, tn);

	else if (p1 == 2)
		/* DUL_DSP_TASK, four normal bursts */
		dsp_load_tch_param(&l1s.next_time,
		                   SIG_ONLY_MODE, SDCCH_4, 0, 0, 0, tn);

	dsp_load_tx_task(
		dsp_task_iq_swap(DUL_DSP_TASK, arfcn, 1),
		burst_id, tsc
	);

	l1s_tx_win_ctrl(arfcn | ARFCN_UPLINK, L1_TXWIN_NB, 0, 3);

	return 0;
}

/* Asynchronous completion handler for NB transmit */
static void l1a_tx_nb_compl(__unused enum l1_compl c)
{
	struct msgb *msg;

	msg = l1_create_l2_msg(L1CTL_DATA_CONF, last_txnb_fn, 0, 0);
	l1_queue_for_l2(msg);
}

void l1s_tx_test(uint8_t base_fn, uint8_t type)
{
	printf("Starting TX %d\n", type);

	if (type == 0) {// one normal burst
		tdma_schedule(base_fn, &l1s_tx_cmd, 0, 0, 0, 3);
		tdma_schedule(base_fn + 2, &l1s_tx_resp, 0, 0, 0, 3);
	} else if (type == 2) { // four normal bursts
		tdma_schedule(base_fn, &l1s_tx_cmd, 2, 0, 0, 3);
		tdma_schedule(base_fn + 1, &l1s_tx_cmd, 2, 1, 0, 3);
		tdma_schedule(base_fn + 2, &l1s_tx_resp, 2, 0, 0, 3);
		tdma_schedule(base_fn + 2, &l1s_tx_cmd, 2, 2, 0, 3);
		tdma_schedule(base_fn + 3, &l1s_tx_resp, 2, 1, 0, 3);
		tdma_schedule(base_fn + 3, &l1s_tx_cmd, 2, 3, 0, 3);
		tdma_schedule(base_fn + 4, &l1s_tx_resp, 2, 2, 0, 3);
		tdma_schedule(base_fn + 5, &l1s_tx_resp, 2, 3, 0, 3);
	}
}

/* sched sets for uplink */
const struct tdma_sched_item nb_sched_set_ul[] = {
	SCHED_ITEM_DT(l1s_tx_cmd, 3, 2, 0),						SCHED_END_FRAME(),
	SCHED_ITEM_DT(l1s_tx_cmd, 3, 2, 1),						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_tx_resp, -4, 2, 0),	SCHED_ITEM_DT(l1s_tx_cmd, 3, 2, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_tx_resp, -4, 2, 1),	SCHED_ITEM_DT(l1s_tx_cmd, 3, 2, 3),	SCHED_END_FRAME(),
						SCHED_ITEM(l1s_tx_resp, -4, 2, 2),	SCHED_END_FRAME(),
						SCHED_ITEM(l1s_tx_resp, -4, 2, 3),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

static __attribute__ ((constructor)) void prim_tx_nb_init(void)
{
	l1s.completion[L1_COMPL_TX_NB] = &l1a_tx_nb_compl;
}
