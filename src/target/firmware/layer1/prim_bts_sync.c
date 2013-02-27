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

/* ------------------------------------------------------------------------ */
/* DSP extensions API                                                       */
/* ------------------------------------------------------------------------ */

#define BASE_API		0xFFD00000

#define API_DSP2ARM(x)		(BASE_API + ((x) - 0x0800) * sizeof(uint16_t))

#define BASE_API_EXT_DB0	API_DSP2ARM(0x2000)
#define BASE_API_EXT_DB1	API_DSP2ARM(0x2200)
#define BASE_API_EXT_NDB	API_DSP2ARM(0x2400)

struct dsp_ext_ndb {
	uint16_t active;
	uint16_t tsc;
} __attribute__((packed));

struct dsp_ext_db {
	/* TX command and data ptr */
	struct {
#define DSP_EXT_TX_CMD_NONE	0
#define DSP_EXT_TX_CMD_FB	1
#define DSP_EXT_TX_CMD_SB	2
#define DSP_EXT_TX_CMD_DUMMY	3
#define DSP_EXT_TX_CMD_AB	4
#define DSP_EXT_TX_CMD_NB	5
		uint16_t cmd;
		uint16_t data;
	} tx[8];

	/* RX command and data ptr */
	struct {
#define DSP_EXT_RX_CMD_NONE	0
#define DSP_EXT_RX_CMD_AB	1
#define DSP_EXT_RX_CMD_NB	2
		uint16_t cmd;
		uint16_t data;
	} rx[8];

	/* SCH data */
	uint16_t sch[2];

	/* Generic data array */
	uint16_t data[0];
} __attribute__((packed));

struct dsp_ext_api {
	struct dsp_ext_ndb *ndb;
	struct dsp_ext_db *db[2];
};

static struct dsp_ext_api dsp_ext_api = {
	.ndb = (struct dsp_ext_ndb *) BASE_API_EXT_NDB,
	.db  = {
		(struct dsp_ext_db *) BASE_API_EXT_DB0,
		(struct dsp_ext_db *) BASE_API_EXT_DB1,
	},
};

static inline struct dsp_ext_db *
dsp_ext_get_db(int r_wn /* 0=W, 1=R */)
{
	int idx = r_wn ? dsp_api.r_page : dsp_api.w_page;
	return dsp_ext_api.db[idx];
}



struct l1s_rxnb_state {
	struct l1s_meas_hdr meas[4];

	struct msgb *msg;
	struct l1ctl_info_dl *dl;
	struct l1ctl_data_ind *di;
};

static struct l1s_rxnb_state rxnb;

static int l1s_nb_resp(__unused uint8_t p1, __unused uint8_t p2, uint16_t p3)
{
	struct gsm_time rx_time;
	uint16_t rf_arfcn;
	uint8_t tsc, tn;
	int i;

#if 1
	putchart('n');

	/* just for debugging, d_task_d should not be 0 */
	if (dsp_api.db_r->d_task_d == 0) {
		puts("EMPTY\n");
		return 0;
	}

	/* DSP burst ID needs to correspond with what we expect */
	if (dsp_api.db_r->d_burst_d != 0) {
		printf("BURST ID %u!=0\n", dsp_api.db_r->d_burst_d);
		return 0;
	}

	/* get radio parameters for _this_ burst */
	gsm_fn2gsmtime(&rx_time, l1s.current_time.fn - 1);
	rfch_get_params(&rx_time, &rf_arfcn, &tsc, &tn);

	/* collect measurements */
	rxnb.meas[0].toa_qbit = dsp_api.db_r->a_serv_demod[D_TOA];
	rxnb.meas[0].pm_dbm8 =
		agc_inp_dbm8_by_pm(dsp_api.db_r->a_serv_demod[D_PM] >> 3);
	rxnb.meas[0].freq_err =
			ANGLE_TO_FREQ(dsp_api.db_r->a_serv_demod[D_ANGLE]);
	rxnb.meas[0].snr = dsp_api.db_r->a_serv_demod[D_SNR];

	/* feed computed frequency error into AFC loop */
	if (rxnb.meas[0].snr > AFC_SNR_THRESHOLD)
		afc_input(rxnb.meas[0].freq_err, rf_arfcn, 1);
	else
		afc_input(rxnb.meas[0].freq_err, rf_arfcn, 0);

	/* feed computed TOA into TA loop */
	printf("TOA=%d snr=%d (thres %d) freq_err=%d pm=%d\n",
		rxnb.meas[0].toa_qbit, rxnb.meas[0].snr, 2560,
		rxnb.meas[0].freq_err, rxnb.meas[0].pm_dbm8 / 8);
	for (i = 0; i < 10; i++)
		toa_input(rxnb.meas[0].toa_qbit << 2, rxnb.meas[0].snr);

	/* Tell the RF frontend to set the gain appropriately */
	rffe_compute_gain(rxnb.meas[0].pm_dbm8/8, CAL_DSP_TGT_BB_LVL);

	{
		/* Get radio parameters for the first burst */
		gsm_fn2gsmtime(&rx_time, l1s.current_time.fn - 4);
		rfch_get_params(&rx_time, &rf_arfcn, &tsc, &tn);

		rxnb.dl->chan_nr = 0x80;
		rxnb.dl->link_id = 0x00;

		rxnb.dl->band_arfcn = htons(rf_arfcn);

		rxnb.dl->frame_nr = htonl(rx_time.fn);

		rxnb.dl->snr = rxnb.meas[0].snr;
		rxnb.dl->rx_level = dbm2rxlev(rxnb.meas[0].pm_dbm8 / 8);

		/* update rx level for pm report */
		pu_update_rx_level(rxnb.dl->rx_level);

		l1_queue_for_l2(rxnb.msg);
		rxnb.msg = NULL; rxnb.dl = NULL; rxnb.di = NULL;

		/* clear downlink task */
		dsp_api.db_w->d_task_d = 0;
	}

	/* mark READ page as being used */
	dsp_api.r_page_used = 1;
#else
	struct dsp_ext_db *db = dsp_ext_get_db(1);
	uint16_t *d = &db->data[32];

	/* get radio parameters for _this_ burst */
	gsm_fn2gsmtime(&rx_time, l1s.current_time.fn - 1);
	rfch_get_params(&rx_time, &rf_arfcn, &tsc, &tn);

	/* collect measurements */
	rxnb.meas[0].toa_qbit = d[D_TOA];
	rxnb.meas[0].pm_dbm8 =
		agc_inp_dbm8_by_pm(d[D_PM] >> 3);
	rxnb.meas[0].freq_err =
			ANGLE_TO_FREQ(d[D_ANGLE]);
	rxnb.meas[0].snr = d[D_SNR];

	/* feed computed frequency error into AFC loop */
	if (rxnb.meas[0].snr > AFC_SNR_THRESHOLD)
		afc_input(rxnb.meas[0].freq_err, rf_arfcn, 1);
	else
		afc_input(rxnb.meas[0].freq_err, rf_arfcn, 0);

	/* feed computed TOA into TA loop */
	printf("arfcn=%d tn=%d TOA=%d snr=%d (thres %d) freq_err=%d pm=%d\n",
		rf_arfcn, tn,
		rxnb.meas[0].toa_qbit, rxnb.meas[0].snr, 2560,
		rxnb.meas[0].freq_err, rxnb.meas[0].pm_dbm8 / 8);
	for (i = 0; i < 10; i++)
		toa_input(rxnb.meas[0].toa_qbit << 2, rxnb.meas[0].snr);

	/* Tell the RF frontend to set the gain appropriately */
	rffe_compute_gain(rxnb.meas[0].pm_dbm8/8, CAL_DSP_TGT_BB_LVL);

	{
		rxnb.dl->chan_nr = 0x80;
		rxnb.dl->link_id = 0x00;

		rxnb.dl->band_arfcn = htons(rf_arfcn);

		rxnb.dl->frame_nr = htonl(rx_time.fn);

		rxnb.dl->snr = rxnb.meas[0].snr;
		rxnb.dl->rx_level = dbm2rxlev(rxnb.meas[0].pm_dbm8 / 8);

		/* update rx level for pm report */
		pu_update_rx_level(rxnb.dl->rx_level);

		l1_queue_for_l2(rxnb.msg);
		rxnb.msg = NULL; rxnb.dl = NULL; rxnb.di = NULL;
	}

	/* We're done with this */
	dsp_api.r_page_used = 1;
#endif

	return 0;
}

static int l1s_nb_cmd(__unused uint8_t p1, __unused uint8_t p2,
		      __unused uint16_t p3)
{
	uint16_t arfcn;
	uint8_t tsc, tn;

	putchart('N');

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

	rfch_get_params(&l1s.next_time, &arfcn, &tsc, &tn);

	dsp_ext_api.ndb->tsc = tsc;
#if 1
	/* DDL_DSP_TASK, four normal bursts */
	dsp_load_tch_param(&l1s.next_time,
	                   SIG_ONLY_MODE, SDCCH_4, 0, 0, 0, tn);

	dsp_load_rx_task( dsp_task_iq_swap(ALLC_DSP_TASK, arfcn, 0), 0, tsc);
#else
	struct dsp_ext_db *db = dsp_ext_get_db(0);

	/* Enable extensions */
	dsp_ext_api.ndb->active = 1;

	db->rx[0].data = 0x000;
	db->rx[0].cmd = DSP_EXT_RX_CMD_NB;

	/* Enable dummy bursts detection */
	dsp_api.db_w->d_ctrl_system |= (1 << B_BCCH_FREQ_IND);

	/* Enable task */
	dsp_api.db_w->d_task_d = 23;
#endif

	l1s_rx_win_ctrl(arfcn, L1_RXWIN_NB, 0);

	return 0;
}

const struct tdma_sched_item bts_sync_sched_set[] = {
	SCHED_ITEM_DT(l1s_nb_cmd, 2, 0, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_resp, -5, 0, 0),	SCHED_END_FRAME(),
	SCHED_END_SET()
};
