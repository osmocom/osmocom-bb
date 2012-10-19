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
#include <rffe.h>
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
#include <layer1/afc.h>
#include <layer1/toa.h>
#include <layer1/tdma_sched.h>
#include <layer1/mframe_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>
#include <layer1/rfch.h>
#include <layer1/prim.h>
#include <layer1/agc.h>

#include <l1ctl_proto.h>

struct l1s_rxnb_state {
	struct l1s_meas_hdr meas[4];

	struct msgb *msg;
	struct l1ctl_info_dl *dl;
	struct l1ctl_data_ind *di;
};

static struct l1s_rxnb_state rxnb;

static int l1s_nb_resp(__unused uint8_t p1, uint8_t burst_id, uint16_t p3)
{
	struct gsm_time rx_time;
	uint8_t mf_task_id = p3 & 0xff;
	uint8_t mf_task_flags = p3 >> 8;
	uint16_t rf_arfcn;
	uint8_t tsc, tn;

	putchart('n');

	/* just for debugging, d_task_d should not be 0 */
	if (dsp_api.db_r->d_task_d == 0) {
		puts("EMPTY\n");
		return 0;
	}

	/* DSP burst ID needs to correspond with what we expect */
	if (dsp_api.db_r->d_burst_d != burst_id) {
		printf("BURST ID %u!=%u\n", dsp_api.db_r->d_burst_d, burst_id);
		return 0;
	}

	/* get radio parameters for _this_ burst */
	gsm_fn2gsmtime(&rx_time, l1s.current_time.fn - 1);
	rfch_get_params(&rx_time, &rf_arfcn, &tsc, &tn);

	/* collect measurements */
	rxnb.meas[burst_id].toa_qbit = dsp_api.db_r->a_serv_demod[D_TOA];
	rxnb.meas[burst_id].pm_dbm8 =
		agc_inp_dbm8_by_pm(dsp_api.db_r->a_serv_demod[D_PM] >> 3);
	rxnb.meas[burst_id].freq_err =
			ANGLE_TO_FREQ(dsp_api.db_r->a_serv_demod[D_ANGLE]);
	rxnb.meas[burst_id].snr = dsp_api.db_r->a_serv_demod[D_SNR];

	/* feed computed frequency error into AFC loop */
	if (rxnb.meas[burst_id].snr > AFC_SNR_THRESHOLD)
		afc_input(rxnb.meas[burst_id].freq_err, rf_arfcn, 1);
	else
		afc_input(rxnb.meas[burst_id].freq_err, rf_arfcn, 0);

	/* feed computed TOA into TA loop */
	toa_input(rxnb.meas[burst_id].toa_qbit << 2, rxnb.meas[burst_id].snr);

	/* Tell the RF frontend to set the gain appropriately */
	rffe_compute_gain(rxnb.meas[burst_id].pm_dbm8/8, CAL_DSP_TGT_BB_LVL);

	/* 4th burst, get frame data */
	if (dsp_api.db_r->d_burst_d == 3) {
		uint8_t i;
		uint16_t num_biterr;
		uint32_t avg_snr = 0;
		int32_t avg_dbm8 = 0;

		/* Get radio parameters for the first burst */
		gsm_fn2gsmtime(&rx_time, l1s.current_time.fn - 4);
		rfch_get_params(&rx_time, &rf_arfcn, &tsc, &tn);

		/* Set Channel Number depending on MFrame Task ID */
		rxnb.dl->chan_nr = mframe_task2chan_nr(mf_task_id, tn);

		/* Set SACCH indication in Link IDentifier */
		if (mf_task_flags & MF_F_SACCH)
			rxnb.dl->link_id = 0x40;
		else
			rxnb.dl->link_id = 0x00;

		rxnb.dl->band_arfcn = htons(rf_arfcn);

		rxnb.dl->frame_nr = htonl(rx_time.fn);

		/* compute average snr and rx level */
		for (i = 0; i < 4; ++i) {
			avg_snr += rxnb.meas[i].snr;
			avg_dbm8 += rxnb.meas[i].pm_dbm8;
		}
		rxnb.dl->snr = avg_snr / 4;
		rxnb.dl->rx_level = dbm2rxlev(avg_dbm8 / (8*4));

		num_biterr = dsp_api.ndb->a_cd[2] & 0xffff;
		if (num_biterr > 0xff)
			rxnb.dl->num_biterr = 0xff;
		else
			rxnb.dl->num_biterr = num_biterr;

		rxnb.dl->fire_crc = ((dsp_api.ndb->a_cd[0] & 0xffff) & ((1 << B_FIRE1) | (1 << B_FIRE0))) >> B_FIRE0;

		/* update rx level for pm report */
		pu_update_rx_level(rxnb.dl->rx_level);

		/* copy actual data, skipping the information block [0,1,2] */
		dsp_memcpy_from_api(rxnb.di->data, &dsp_api.ndb->a_cd[3], 23, 0);

		l1_queue_for_l2(rxnb.msg);
		rxnb.msg = NULL; rxnb.dl = NULL; rxnb.di = NULL;

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
	uint16_t arfcn;
	uint8_t tsc, tn;

	putchart('N');

	if (burst_id == 1) {
		/* allocate message only at 2nd burst in case of
		 * consecutive/overlapping normal burst RX tasks */
		/* FIXME: we actually want all allocation out of L1S! */
		if (rxnb.msg) {
			/* Can happen when resetting ... */
			printf("nb_cmd(0) and rxnb.msg != NULL\n");
			msgb_free(rxnb.msg);
		}
		/* allocate msgb as needed. FIXME: from L1A ?? */
		rxnb.msg = l1ctl_msgb_alloc(L1CTL_DATA_IND);
		if (!rxnb.msg)
			printf("nb_cmd(0): unable to allocate msgb\n");
		rxnb.dl = (struct l1ctl_info_dl *) msgb_put(rxnb.msg, sizeof(*rxnb.dl));
		rxnb.di = (struct l1ctl_data_ind *) msgb_put(rxnb.msg, sizeof(*rxnb.di));
	}

	rfch_get_params(&l1s.next_time, &arfcn, &tsc, &tn);

	/* DDL_DSP_TASK, four normal bursts */
	dsp_load_tch_param(&l1s.next_time,
	                   SIG_ONLY_MODE, SDCCH_4, 0, 0, 0, tn);

	dsp_load_rx_task(
		dsp_task_iq_swap(ALLC_DSP_TASK, arfcn, 0),
		burst_id, tsc
	);

	l1s_rx_win_ctrl(arfcn, L1_RXWIN_NB, 0);

	return 0;
}

const struct tdma_sched_item nb_sched_set[] = {
	SCHED_ITEM_DT(l1s_nb_cmd, 0, 0, 0),						SCHED_END_FRAME(),
	SCHED_ITEM_DT(l1s_nb_cmd, 0, 0, 1),						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_resp, -4, 0, 0),	SCHED_ITEM_DT(l1s_nb_cmd, 0, 0, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_resp, -4, 0, 1),	SCHED_ITEM_DT(l1s_nb_cmd, 0, 0, 3),	SCHED_END_FRAME(),
						SCHED_ITEM(l1s_nb_resp, -4, 0, 2),	SCHED_END_FRAME(),
						SCHED_ITEM(l1s_nb_resp, -4, 0, 3),	SCHED_END_FRAME(),
	SCHED_END_SET()
};
