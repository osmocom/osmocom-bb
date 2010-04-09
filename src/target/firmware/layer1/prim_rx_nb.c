/* Layer 1 - Receiving Normal Bursts */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <osmocore/gsm_utils.h>
#include <osmocore/msgb.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>

#include <layer1/sync.h>
#include <layer1/afc.h>
#include <layer1/tdma_sched.h>
#include <layer1/mframe_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>

#include <l1a_l23_interface.h>


static int l1s_nb_resp(__unused uint8_t p1, uint8_t burst_id, uint16_t p3)
{
	static struct l1_signal _nb_sig, *sig = &_nb_sig;
	uint8_t mf_task_id = p3 & 0xff;
	uint8_t mf_task_flags = p3 >> 8;
	struct msgb *msg;
	uint16_t rf_arfcn = l1s.serving_cell.arfcn;

	putchart('n');

	/* just for debugging, d_task_d should not be 0 */
	if (dsp_api.db_r->d_task_d == 0) {
		puts("EMPTY\n");
		return 0;
	}

	/* DSP burst ID needs to corespond with what we expect */
	if (dsp_api.db_r->d_burst_d != burst_id) {
		printf("BURST ID %u!=%u\n", dsp_api.db_r->d_burst_d, burst_id);
		return 0;
	}

	sig->nb.meas[burst_id].toa_qbit = dsp_api.db_r->a_serv_demod[D_TOA];
	sig->nb.meas[burst_id].pm_dbm8 = dsp_api.db_r->a_serv_demod[D_PM] >> 3;
	sig->nb.meas[burst_id].freq_err =
			ANGLE_TO_FREQ(dsp_api.db_r->a_serv_demod[D_ANGLE]);
	sig->nb.meas[burst_id].snr = dsp_api.db_r->a_serv_demod[D_SNR];

	/* feed computed frequency error into AFC loop */
	if (sig->nb.meas[burst_id].snr > AFC_SNR_THRESHOLD)
		afc_input(sig->nb.meas[burst_id].freq_err, rf_arfcn, 1);
	else
		afc_input(sig->nb.meas[burst_id].freq_err, rf_arfcn, 0);

	/* 4th burst, get frame data */
	if (dsp_api.db_r->d_burst_d == 3) {
		struct l1ctl_hdr *l1h;
		struct l1ctl_info_dl *dl;
		struct l1ctl_data_ind *di;
		uint32_t avg_snr = 0;
		int32_t avg_dbm8 = 0;
		uint8_t i, j;

		sig->signum = L1_SIG_NB;
		sig->nb.num_biterr = dsp_api.ndb->a_cd[2] & 0xffff;
		sig->nb.crc = ((dsp_api.ndb->a_cd[0] & 0xffff) & ((1 << B_FIRE1) | (1 << B_FIRE0))) >> B_FIRE0;
		sig->nb.fire = ((dsp_api.ndb->a_cd[0] & 0xffff) & (1 << B_FIRE1)) >> B_FIRE1;

		/* copy actual data, skipping the information block [0,1,2] */
		for (j = 0,i = 3; i < 15; i++) {
			sig->nb.frame[j++] = dsp_api.ndb->a_cd[i] & 0xFF;
			sig->nb.frame[j++] = (dsp_api.ndb->a_cd[i] >> 8) & 0xFF;
		}

		/* actually issue the signal */
		if (l1s_cb)
			l1s_cb(sig);

		/* place it in the queue for the layer2 */
		msg = l1_create_l2_msg(L1CTL_DATA_IND, l1s.current_time.fn-4,
					0, rf_arfcn);
		l1h = (struct l1ctl_hdr *) msg->l1h;
		dl = (struct l1ctl_info_dl *) l1h->data;
		di = (struct l1ctl_data_ind *) msgb_put(msg, sizeof(*di));

		/* Set Channel Number depending on MFrame Task ID */
		dl->chan_nr = mframe_task2chan_nr(mf_task_id, 0); /* FIXME: TS */

		/* Set SACCH indication in Link IDentifier */
		if (mf_task_flags & MF_F_SACCH)
			dl->link_id = 0x40;
		else
			dl->link_id = 0x00;

		/* compute average snr and rx level */
		for (i = 0; i < 4; ++i) {
			avg_snr += sig->nb.meas[i].snr;
			avg_dbm8 += sig->nb.meas[i].pm_dbm8;
		}
		dl->snr = avg_snr / 4;
		dl->rx_level = (avg_dbm8 / (8*4)) + 110;

		/* copy the actual payload data */
		for (i = 0; i < 23; ++i)
			di->data[i] = sig->nb.frame[i];
		l1_queue_for_l2(msg);

		/* clear downlink task */
		dsp_api.db_w->d_task_d = 0;
	}

	/* mark READ page as being used */
	dsp_api.r_page_used = 1;

	return 0;
}

static int l1s_nb_cmd(__unused uint8_t p1, uint8_t burst_id,
		      __unused uint16_t p3)
{
	uint8_t tsc = l1s.serving_cell.bsic & 0x7;

	putchart('N');
	dsp_load_rx_task(ALLC_DSP_TASK, burst_id, tsc);
	dsp_end_scenario();

	l1s_rx_win_ctrl(l1s.serving_cell.arfcn, L1_RXWIN_NB);
	tpu_end_scenario();

	return 0;
}

const struct tdma_sched_item nb_sched_set[] = {
	SCHED_ITEM(l1s_nb_cmd, 0, 0),					SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_cmd, 0, 1),					SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_resp, 0, 0), SCHED_ITEM(l1s_nb_cmd, 0, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_resp, 0, 1), SCHED_ITEM(l1s_nb_cmd, 0, 3),	SCHED_END_FRAME(),
				       SCHED_ITEM(l1s_nb_resp, 0, 2),	SCHED_END_FRAME(),
				       SCHED_ITEM(l1s_nb_resp, 0, 3),	SCHED_END_FRAME(),
	SCHED_END_SET()
};
