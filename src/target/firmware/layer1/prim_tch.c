/* Layer 1 - TCH */

/* (C) 2010 by Dieter Spaar <spaar@mirider.augusta.de>
 * (C) 2010 by Sylvain Munaut <tnt@246tnt.com>
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
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/core/msgb.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>

#include <rffe.h>
#include <layer1/sync.h>
#include <layer1/afc.h>
#include <layer1/agc.h>
#include <layer1/toa.h>
#include <layer1/tdma_sched.h>
#include <layer1/mframe_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>
#include <layer1/rfch.h>
#include <layer1/prim.h>

#include <l1ctl_proto.h>


/* This computes various parameters both for the DSP and for
 * our logic. Not all are used all the time, but it's easier
 * to build all in one place */
static void tch_get_params(struct gsm_time *time, uint8_t chan_nr,
                           uint32_t *fn_report, uint8_t *tch_f_hn,
                           uint8_t *tch_sub, uint8_t *tch_mode)
{
	uint8_t tn = chan_nr & 0x07;
	uint8_t cbits = chan_nr >> 3;

	*tch_f_hn = (cbits & 2) ? 0 : 1;

	if (*tch_f_hn) {
		*fn_report = (time->fn - (tn * 13) + 104) % 104;
		*tch_sub = 0;
	} else {
		uint8_t chan_sub = cbits & 1;
		uint8_t tn_report = (tn & ~1) | chan_sub;
		*fn_report = (time->fn - (tn_report * 13) + 104) % 104;
		*tch_sub = chan_sub;
	}

	if (tch_mode) {
		switch (l1s.tch_mode) {
		case GSM48_CMODE_SPEECH_V1:
			*tch_mode = *tch_f_hn ? TCH_FS_MODE : TCH_HS_MODE;
			break;
		case GSM48_CMODE_SPEECH_EFR:
			*tch_mode = *tch_f_hn ? TCH_EFR_MODE : SIG_ONLY_MODE;
			break;
		default:
			*tch_mode = SIG_ONLY_MODE;
		}
	}
}


/* -------------------------------------------------------------------------
 * Shared completion handler
 * ------------------------------------------------------------------------- */

/*
 * FIXME We really need a better way to handle completion, where we can
 *       pass arguments and such ...
 *
 *       Right now, we just 'hope' it gets processed before the next one ...
 */

#define TX_TYPE_SACCH	(1<<0)
#define TX_TYPE_FACCH	(1<<1)
#define TX_TYPE_TRAFFIC	(1<<2)

static uint16_t last_tx_tch_fn;
static uint16_t last_tx_tch_type;

static void l1a_tx_tch_compl(__unused enum l1_compl c)
{
	struct msgb *msg;

	if (last_tx_tch_type & (TX_TYPE_SACCH | TX_TYPE_FACCH)) {
		msg = l1_create_l2_msg(L1CTL_DATA_CONF, last_tx_tch_fn, 0, 0);
		l1_queue_for_l2(msg);
	}

	if (last_tx_tch_type & TX_TYPE_TRAFFIC) {
		msg = l1_create_l2_msg(L1CTL_TRAFFIC_CONF, last_tx_tch_fn, 0, 0);
		l1_queue_for_l2(msg);
	}

	last_tx_tch_type = 0;
}

static __attribute__ ((constructor)) void prim_tch_init(void)
{
	l1s.completion[L1_COMPL_TX_TCH]  = &l1a_tx_tch_compl;
}


/* -------------------------------------------------------------------------
 * TCH: Voice & FACCH
 * ------------------------------------------------------------------------- */

/*
 * Voice and FACCH data are spread in various ways depending on a lot of
 * factors. Trying to handle that with the mframe scheduler is just a mess,
 * so we schedule it burst by burst and handle the complex logic inside the
 * primitive task code itself.
 */


#define FACCH_MEAS_HIST	8	/* Up to 8 bursts history */
struct l1s_rx_tch_state {
	struct l1s_meas_hdr meas[FACCH_MEAS_HIST];
};

static struct l1s_rx_tch_state rx_tch;


static int l1s_tch_resp(__unused uint8_t p1, __unused uint8_t p2, uint16_t p3)
{
	static uint8_t meas_id = 0;
	uint8_t mf_task_id = p3 & 0xff;
	struct gsm_time rx_time;
	uint8_t chan_nr;
	uint16_t arfcn;
	uint8_t tsc, tn;
	uint8_t tch_f_hn, tch_sub;
	uint32_t fn_report;
	int facch_rx_now, traffic_rx_now;

	/* Get/compute various parameters */
	gsm_fn2gsmtime(&rx_time, (l1s.current_time.fn - 1 + GSM_MAX_FN) % GSM_MAX_FN);
	rfch_get_params(&rx_time, &arfcn, &tsc, &tn);
	chan_nr = mframe_task2chan_nr(mf_task_id, tn);
	tch_get_params(&rx_time, chan_nr, &fn_report, &tch_f_hn, &tch_sub, NULL);

	meas_id = (meas_id + 1) % FACCH_MEAS_HIST; /* absolute value doesn't matter */

	/* Collect measurements */
	rx_tch.meas[meas_id].toa_qbit = dsp_api.db_r->a_serv_demod[D_TOA];
	rx_tch.meas[meas_id].pm_dbm8 =
		agc_inp_dbm8_by_pm(dsp_api.db_r->a_serv_demod[D_PM] >> 3);
	rx_tch.meas[meas_id].freq_err =
		ANGLE_TO_FREQ(dsp_api.db_r->a_serv_demod[D_ANGLE]);
	rx_tch.meas[meas_id].snr = dsp_api.db_r->a_serv_demod[D_SNR];

	/* feed computed frequency error into AFC loop */
	if (rx_tch.meas[meas_id].snr > AFC_SNR_THRESHOLD)
		afc_input(rx_tch.meas[meas_id].freq_err, arfcn, 1);
	else
		afc_input(rx_tch.meas[meas_id].freq_err, arfcn, 0);

	/* feed computed TOA into TA loop */
	toa_input(rx_tch.meas[meas_id].toa_qbit << 2, rx_tch.meas[meas_id].snr);

	/* Tell the RF frontend to set the gain appropriately */
	rffe_compute_gain(rx_tch.meas[meas_id].pm_dbm8 / 8,
		CAL_DSP_TGT_BB_LVL);

	/* FACCH Block end ? */
	if (tch_f_hn) {
		/* FACCH/F: B0(0...7),B1(4...11),B2(8...11,0...3) (mod 13) */
		facch_rx_now = ((rx_time.fn % 13) % 4) == 3;
	} else {
		/* FAACH/H: See GSM 05.02 Clause 7 Table 1of9 */
		uint8_t t2_norm = rx_time.t2 - tch_sub;
		facch_rx_now = (t2_norm == 15) ||
		               (t2_norm == 23) ||
		               (t2_norm ==  6);
	}

	if (facch_rx_now && (dsp_api.ndb->a_fd[0] & (1<<B_BLUD))) {
		struct msgb *msg;
		struct l1ctl_info_dl *dl;
		struct l1ctl_data_ind *di;
		uint16_t num_biterr;
		uint32_t avg_snr = 0;
		int32_t avg_dbm8 = 0;
		int i, n;

		/* Allocate msgb */
			/* FIXME: we actually want all allocation out of L1S! */
		msg = l1ctl_msgb_alloc(L1CTL_DATA_IND);
		if(!msg) {
			printf("TCH FACCH: unable to allocate msgb\n");
			goto skip_rx_facch;
		}

		dl = (struct l1ctl_info_dl *) msgb_put(msg, sizeof(*dl));
		di = (struct l1ctl_data_ind *) msgb_put(msg, sizeof(*di));

		/* Fill DL header (should be about the first burst ... here is the last) */
		dl->chan_nr = chan_nr;
		dl->link_id = 0x00;	/* FACCH */
		dl->band_arfcn = htons(arfcn);
		dl->frame_nr = htonl(rx_time.fn);

		/* Average SNR & RX level */
		n = tch_f_hn ? 8 : 6;
		for (i=0; i<n; i++) {
			int j = (meas_id + FACCH_MEAS_HIST - i) % FACCH_MEAS_HIST;
			avg_snr += rx_tch.meas[j].snr;
			avg_dbm8 += rx_tch.meas[j].pm_dbm8;
		}

		dl->snr = avg_snr / n;
		dl->rx_level = dbm2rxlev(avg_dbm8 / (8*n));

		/* Errors & CRC status */
		num_biterr = dsp_api.ndb->a_fd[2] & 0xffff;
		if (num_biterr > 0xff)
			dl->num_biterr = 0xff;
		else
			dl->num_biterr = num_biterr;

		dl->fire_crc = ((dsp_api.ndb->a_fd[0] & 0xffff) & ((1 << B_FIRE1) | (1 << B_FIRE0))) >> B_FIRE0;

		/* Update rx level for pm report */
		pu_update_rx_level(dl->rx_level);

		/* Copy actual data, skipping the information block [0,1,2] */
		dsp_memcpy_from_api(di->data, &dsp_api.ndb->a_fd[3], 23, 0);

		/* Give message to up layer */
		l1_queue_for_l2(msg);

	skip_rx_facch:
		/* Reset A_FD header (needed by DSP) */
		/* B_FIRE1 =1, B_FIRE0 =0 , BLUD =0 */
		dsp_api.ndb->a_fd[0] = (1<<B_FIRE1);
		dsp_api.ndb->a_fd[2] = 0xffff;

		/* Reset A_DD_0 header in NDB (needed by DSP) */
		dsp_api.ndb->a_dd_0[0] = 0;
		dsp_api.ndb->a_dd_0[2] = 0xffff;

		/* Reset A_DD_1 header in NDB (needed by DSP) */
		dsp_api.ndb->a_dd_1[0] = 0;
		dsp_api.ndb->a_dd_1[2] = 0xffff;
	}

	/* Traffic now ? */
	if (tch_f_hn) {
		/* TCH/F: B0(0...7),B1(4...11),B2(8...11,0...3) (mod 13)*/
		traffic_rx_now = ((rx_time.fn % 13) % 4) == 3;
	} else {
		/* TCH/H0: B0(0,2,4,6),B1(4,6,8,10),B2(8,10,0,2) (mod 13) */
		/*     H1: B0(1,3,5,7),B1(5,7,9,11),B2(9,11,1,3) (mod 13) */
		traffic_rx_now = (((rx_time.fn - tch_sub + 13) % 13) % 4) == 2;
	}

	if (traffic_rx_now) {
		volatile uint16_t *traffic_buf;

		traffic_buf = tch_sub ? dsp_api.ndb->a_dd_1 : dsp_api.ndb->a_dd_0;

		if (traffic_buf[0] & (1<<B_BLUD)) {
			/* Send the data to upper layers (if interested and good frame) */
			if ((l1s.audio_mode & AUDIO_RX_TRAFFIC_IND) &&
			    !(dsp_api.ndb->a_dd_0[0] & (1<<B_BFI))) {
				struct msgb *msg;
				struct l1ctl_info_dl *dl;
				struct l1ctl_traffic_ind *ti;

				/* Allocate msgb */
				/* FIXME: we actually want all allocation out of L1S! */
				msg = l1ctl_msgb_alloc(L1CTL_TRAFFIC_IND);
				if(!msg) {
					printf("TCH traffic: unable to allocate msgb\n");
					goto skip_rx_traffic;
				}

				dl = (struct l1ctl_info_dl *) msgb_put(msg, sizeof(*dl));
				ti = (struct l1ctl_traffic_ind *) msgb_put(msg, sizeof(*ti));

				/* Copy actual data, skipping the information block [0,1,2] */
				dsp_memcpy_from_api(ti->data, &traffic_buf[3], 33, 1);

				/* Give message to up layer */
				l1_queue_for_l2(msg);
			}

	skip_rx_traffic:
			/* Reset traffic buffer header in NDB (needed by DSP) */
			traffic_buf[0] = 0;
			traffic_buf[2] = 0xffff;
		}
	}

	/* mark READ page as being used */
	dsp_api.r_page_used = 1;

	return 0;
}

static int l1s_tch_cmd(__unused uint8_t p1, __unused uint8_t p2, uint16_t p3)
{
	uint8_t mf_task_id = p3 & 0xff;
	uint8_t chan_nr;
	uint16_t arfcn;
	uint8_t tsc, tn;
	uint8_t tch_f_hn, tch_sub, tch_mode;
	uint32_t fn_report;
	uint8_t sync = 0;
	static int icnt;
	int facch_tx_now, traffic_tx_now;

	/* Get/compute various parameters */
	rfch_get_params(&l1s.next_time, &arfcn, &tsc, &tn);
	chan_nr = mframe_task2chan_nr(mf_task_id, tn);
	tch_get_params(&l1s.next_time, chan_nr, &fn_report, &tch_f_hn, &tch_sub, &tch_mode);

	/* Sync & FACCH delay */
	if (l1s.tch_sync) {
		l1s.tch_sync = 0;
		sync = 1;
		icnt = 0;
	} else if (icnt <= 26)
		icnt++;

	/* Load FACCH data if we start a new burst */
	/* (the DSP wants the data on the CMD of the burst _preceding_ the
	 * first burst) */
	if (tch_f_hn) {
		/* FACCH/F: B0(0...7),B1(4...11),B2(8...11,0...3) */
		facch_tx_now = ((l1s.next_time.fn % 13) % 4) == 3;
	} else {
		/* FAACH/H: See GSM 05.02 Clause 7 Table 1of9 */
		uint8_t t2_norm = l1s.next_time.t2 - tch_sub;
		facch_tx_now = (t2_norm == 23) ||
		               (t2_norm ==  6) ||
		               (t2_norm == 15);
	}

	if (facch_tx_now) {
		uint16_t *info_ptr = dsp_api.ndb->a_fu;
		struct msgb *msg;
		const uint8_t *data;

		/* Pull FACCH data (if ready) */
		if (icnt > 26)
			msg = msgb_dequeue(&l1s.tx_queue[L1S_CHAN_MAIN]);
		else
			msg = NULL;

		/* If TX is empty and we're signalling only, use dummy frame */
		if (msg)
			data = msg->l3h;
		else if (tch_mode == SIG_ONLY_MODE)
			data = pu_get_idle_frame();
		else
			data = NULL;

		/* Do we really send something ? */
		if (data) {
			/* Fill data block header */
			info_ptr[0] = (1 << B_BLUD);	/* 1st word: Set B_BLU bit. */
			info_ptr[1] = 0;		/* 2nd word: cleared. */
			info_ptr[2] = 0;		/* 3nd word: cleared. */

			/* Copy the actual data after the header */
			dsp_memcpy_to_api(&info_ptr[3], data, 23, 0);
		}

		/* Indicate completion (FIXME: early but easier this way for now) */
		if (msg) {
			last_tx_tch_fn = l1s.next_time.fn;
			last_tx_tch_type |= TX_TYPE_FACCH;
			l1s_compl_sched(L1_COMPL_TX_TCH);
		}

		/* Free msg now that we're done with it */
		if (msg)
			msgb_free(msg);
	}

	/* Traffic now ? */
	if (tch_f_hn) {
		/* TCH/F: B0(0...7),B1(4...11),B2(8...11,0...3) (mod 13)*/
		traffic_tx_now = ((l1s.next_time.fn % 13) % 4) == 3;
	} else {
		/* TCH/H0: B0(0,2,4,6),B1(4,6,8,10),B2(8,10,0,2) (mod 13) */
		/*     H1: B0(1,3,5,7),B1(5,7,9,11),B2(9,11,1,3) (mod 13) */
		traffic_tx_now = (((l1s.next_time.fn - tch_sub + 13) % 13) % 4) == 2;
	}

	if (traffic_tx_now) {
		volatile uint16_t *traffic_buf;
		struct msgb *msg;
		const uint8_t *data;

		/* Reset play mode */
		dsp_api.ndb->d_tch_mode &= ~B_PLAY_UL;

		/* Check l1s audio mode */
		if (!(l1s.audio_mode & AUDIO_TX_TRAFFIC_REQ))
			goto skip_tx_traffic;

		/* Traffic buffer = !tch_sub */
		traffic_buf = tch_sub ? dsp_api.ndb->a_du_0 : dsp_api.ndb->a_du_1;

		/* Pull Traffic data (if any) */
		msg = msgb_dequeue(&l1s.tx_queue[L1S_CHAN_TRAFFIC]);

		/* Copy actual data, skipping the information block [0,1,2] */
		if (msg) {
			data = msg->l2h;
			dsp_memcpy_to_api(&traffic_buf[3], data, 33, 1);

			traffic_buf[0] = (1 << B_BLUD);	/* 1st word: Set B_BLU bit. */
			traffic_buf[1] = 0;		/* 2nd word: cleared. */
			traffic_buf[2] = 0;		/* 3nd word: cleared. */
		}

		if (msg)
			dsp_api.ndb->d_tch_mode |= B_PLAY_UL;

		/* Indicate completion (FIXME: early but easier this way for now) */
		if (msg) {
			last_tx_tch_fn = l1s.next_time.fn;
			last_tx_tch_type |= TX_TYPE_TRAFFIC;
			l1s_compl_sched(L1_COMPL_TX_TCH);
		}

		/* Free msg now that we're done with it */
		if (msg)
			msgb_free(msg);
	}
skip_tx_traffic:

	/* Configure DSP for TX/RX */
	l1s_tx_apc_helper(arfcn);

	dsp_load_tch_param(
		&l1s.next_time,
		tch_mode, tch_f_hn ? TCH_F : TCH_H, tch_sub,
		0, sync, tn
	);

	dsp_load_rx_task(
		dsp_task_iq_swap(TCHT_DSP_TASK, arfcn, 0),
		0, tsc		/* burst_id unused for TCH */
	);
	l1s_rx_win_ctrl(arfcn, L1_RXWIN_NB, 0);

	dsp_load_tx_task(
		dsp_task_iq_swap(TCHT_DSP_TASK, arfcn, 1),
		0, tsc		/* burst_id unused for TCH */
	);
	l1s_tx_win_ctrl(arfcn | ARFCN_UPLINK, L1_TXWIN_NB, 0, 3);

	return 0;
}


const struct tdma_sched_item tch_sched_set[] = {
	SCHED_ITEM_DT(l1s_tch_cmd, 0, 0, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_tch_resp, 0, 0, -4),	SCHED_END_FRAME(),
	SCHED_END_SET()
};


/* -------------------------------------------------------------------------
 * TCH/H: Dummy
 * ------------------------------------------------------------------------- */

/* This task is needed to perform some operation in the DSP when there is
 * no data to be exchanged */

static int l1s_tch_d_resp(__unused uint8_t p1, __unused uint8_t p2, uint16_t p3)
{
	/* mark READ page as being used */
	dsp_api.r_page_used = 1;

	return 0;
}

static int l1s_tch_d_cmd(__unused uint8_t p1, __unused uint8_t p2, uint16_t p3)
{
	uint8_t mf_task_id = p3 & 0xff;
	uint8_t chan_nr;
	uint8_t tsc, tn;
	uint8_t tch_f_hn, tch_sub, tch_mode;
	uint32_t fn_report;

	/* Get/compute various parameters */
	rfch_get_params(&l1s.next_time, NULL, &tsc, &tn);
	chan_nr = mframe_task2chan_nr(mf_task_id, tn);
	tch_get_params(&l1s.next_time, chan_nr, &fn_report, &tch_f_hn, &tch_sub, &tch_mode);

	/* Configure DSP */
	dsp_load_tch_param(
		&l1s.next_time,
		tch_mode, tch_f_hn ? TCH_F : TCH_H, tch_sub,
		0, 0, tn
	);

	dsp_load_rx_task(TCHD_DSP_TASK, 0, tsc); /* burst_id unused for TCH */
	dsp_load_tx_task(TCHD_DSP_TASK, 0, tsc); /* burst_id unused for TCH */

	return 0;
}

const struct tdma_sched_item tch_d_sched_set[] = {
	SCHED_ITEM_DT(l1s_tch_d_cmd, 0, 0, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_tch_d_resp, 0, 0, -4),	SCHED_END_FRAME(),
	SCHED_END_SET()
};


/* -------------------------------------------------------------------------
 * TCH: SACCH
 * ------------------------------------------------------------------------- */

/*
 * SACCH data are spread over 4 bursts, however they are so far appart that
 * we can't use the normal scheduler to schedule all them at once in a single
 * set.
 * Therefore, the task code itself decides in which burst it is, if it's the
 * start/end, and act appropriately.
 */


struct l1s_rx_tch_a_state {
	struct l1s_meas_hdr meas[4];

	struct msgb *msg;
	struct l1ctl_info_dl *dl;
	struct l1ctl_data_ind *di;
};

static struct l1s_rx_tch_a_state rx_tch_a;


static int l1s_tch_a_resp(__unused uint8_t p1, __unused uint8_t p2, uint16_t p3)
{
	uint8_t mf_task_id = p3 & 0xff;
	struct gsm_time rx_time;
	uint8_t chan_nr;
	uint16_t arfcn;
	uint8_t tsc, tn;
	uint8_t tch_f_hn, tch_sub;
	uint32_t fn_report;
	uint8_t burst_id;

	/* It may happen we've never gone through cmd(0) yet, skip until then */
	if (!rx_tch_a.msg)
		goto skip;

	/* Get/compute various parameters */
	gsm_fn2gsmtime(&rx_time, (l1s.current_time.fn - 1 + GSM_MAX_FN) % GSM_MAX_FN);
	rfch_get_params(&rx_time, &arfcn, &tsc, &tn);
	chan_nr = mframe_task2chan_nr(mf_task_id, tn);
	tch_get_params(&rx_time, chan_nr, &fn_report, &tch_f_hn, &tch_sub, NULL);
	burst_id = (fn_report - 12) / 26;

	/* Collect measurements */
	rx_tch_a.meas[burst_id].toa_qbit = dsp_api.db_r->a_serv_demod[D_TOA];
	rx_tch_a.meas[burst_id].pm_dbm8 =
		agc_inp_dbm8_by_pm(dsp_api.db_r->a_serv_demod[D_PM] >> 3);
	rx_tch_a.meas[burst_id].freq_err =
		ANGLE_TO_FREQ(dsp_api.db_r->a_serv_demod[D_ANGLE]);
	rx_tch_a.meas[burst_id].snr = dsp_api.db_r->a_serv_demod[D_SNR];

	/* feed computed frequency error into AFC loop */
	if (rx_tch_a.meas[burst_id].snr > AFC_SNR_THRESHOLD)
		afc_input(rx_tch_a.meas[burst_id].freq_err, arfcn, 1);
	else
		afc_input(rx_tch_a.meas[burst_id].freq_err, arfcn, 0);

	/* feed computed TOA into TA loop */
	toa_input(rx_tch_a.meas[burst_id].toa_qbit << 2, rx_tch_a.meas[burst_id].snr);

	/* Tell the RF frontend to set the gain appropriately */
	rffe_compute_gain(rx_tch_a.meas[burst_id].pm_dbm8 / 8,
		CAL_DSP_TGT_BB_LVL);

	/* Last burst, read data & send to the up layer */
	if ((burst_id == 3) && (dsp_api.ndb->a_cd[0] & (1<<B_BLUD))) {
		unsigned int i;
		uint16_t num_biterr;
		uint32_t avg_snr = 0;
		int32_t avg_dbm8 = 0;

		/* Average SNR & RX level + error & crc status */
		for (i=0; i<4; i++) {
			avg_snr += rx_tch_a.meas[i].snr;
			avg_dbm8 += rx_tch_a.meas[i].pm_dbm8;
		}
		rx_tch_a.dl->snr = avg_snr / 4;
		rx_tch_a.dl->rx_level = dbm2rxlev(avg_dbm8 / (8*4));

		num_biterr = dsp_api.ndb->a_cd[2];
		if (num_biterr > 0xff)
			rx_tch_a.dl->num_biterr = 0xff;
		else
			rx_tch_a.dl->num_biterr = num_biterr;

		rx_tch_a.dl->fire_crc = ((dsp_api.ndb->a_cd[0] & 0xffff) & ((1 << B_FIRE1) | (1 << B_FIRE0))) >> B_FIRE0;

		/* Update rx level for pm report */
		pu_update_rx_level(rx_tch_a.dl->rx_level);

		/* Copy actual data, skipping the information block [0,1,2] */
		dsp_memcpy_from_api(rx_tch_a.di->data, &dsp_api.ndb->a_cd[3], 23, 0);

		/* Give message to up layer */
		l1_queue_for_l2(rx_tch_a.msg);
		rx_tch_a.msg = NULL; rx_tch_a.dl = NULL; rx_tch_a.di = NULL;

		/* Reset header */
		dsp_api.ndb->a_cd[0] = (1<<B_FIRE1);
		dsp_api.ndb->a_cd[2] = 0xffff;
	}

skip:
	/* mark READ page as being used */
	dsp_api.r_page_used = 1;

	return 0;
}

static int l1s_tch_a_cmd(__unused uint8_t p1, __unused uint8_t p2, uint16_t p3)
{
	uint8_t mf_task_id = p3 & 0xff;
	uint8_t chan_nr;
	uint16_t arfcn;
	uint8_t tsc, tn;
	uint8_t tch_f_hn, tch_sub, tch_mode;
	uint32_t fn_report;
	uint8_t burst_id;

	/* Get/compute various parameters */
	rfch_get_params(&l1s.next_time, &arfcn, &tsc, &tn);
	chan_nr = mframe_task2chan_nr(mf_task_id, tn);
	tch_get_params(&l1s.next_time, chan_nr, &fn_report, &tch_f_hn, &tch_sub, &tch_mode);
	burst_id = (fn_report - 12) / 26;

	/* Load SACCH data if we start a new burst */
	if (burst_id == 0) {
		uint16_t *info_ptr = dsp_api.ndb->a_cu;
		struct msgb *msg;
		const uint8_t *data;

		/* If the TX queue is empty, send dummy measurement */
		msg = msgb_dequeue(&l1s.tx_queue[L1S_CHAN_SACCH]);
		data = msg ? msg->l3h : pu_get_meas_frame();

		/* Fill data block header */
		info_ptr[0] = (1 << B_BLUD);	/* 1st word: Set B_BLU bit. */
		info_ptr[1] = 0;		/* 2nd word: cleared. */
		info_ptr[2] = 0;		/* 3nd word: cleared. */

		/* Copy the actual data after the header */
		dsp_memcpy_to_api(&info_ptr[3], data, 23, 0);

		/* Indicate completion (FIXME: early but easier this way for now) */
		if (msg) {
			last_tx_tch_fn = l1s.next_time.fn;
			last_tx_tch_type |= TX_TYPE_SACCH;
			l1s_compl_sched(L1_COMPL_TX_TCH);
		}

		/* Free msg now that we're done with it */
		if (msg)
			msgb_free(msg);
	}

	/* Allocate RX burst */
	if (burst_id == 0) {
		/* Clear 'dangling' msgb */
		if (rx_tch_a.msg) {
			/* Can happen if the task was shutdown in the middle of
			 * 4 bursts ... */
			msgb_free(rx_tch_a.msg);
		}

		/* Allocate burst */
			/* FIXME: we actually want all allocation out of L1S! */
		rx_tch_a.msg = l1ctl_msgb_alloc(L1CTL_DATA_IND);
		if (!rx_tch_a.msg)
			printf("tch_a_cmd(0): unable to allocate msgb\n");

		rx_tch_a.dl = (struct l1ctl_info_dl *) msgb_put(rx_tch_a.msg, sizeof(*rx_tch_a.dl));
		rx_tch_a.di = (struct l1ctl_data_ind *) msgb_put(rx_tch_a.msg, sizeof(*rx_tch_a.di));

		/* Pre-fill DL header with some info about burst(0) */
		rx_tch_a.dl->chan_nr = chan_nr;
		rx_tch_a.dl->link_id = 0x40;	/* SACCH */
		rx_tch_a.dl->band_arfcn = htons(arfcn);
		rx_tch_a.dl->frame_nr = htonl(l1s.next_time.fn);
	}

	/* Configure DSP for TX/RX */
	l1s_tx_apc_helper(arfcn);

	dsp_load_tch_param(
		&l1s.next_time,
		tch_mode, tch_f_hn ? TCH_F : TCH_H, tch_sub,
		0, 0, tn
	);

	dsp_load_rx_task(
		dsp_task_iq_swap(TCHA_DSP_TASK, arfcn, 0),
		0, tsc		/* burst_id unused for TCHA */
	);
	l1s_rx_win_ctrl(arfcn, L1_RXWIN_NB, 0);

	dsp_load_tx_task(
		dsp_task_iq_swap(TCHA_DSP_TASK, arfcn, 1),
		0, tsc		/* burst_id unused for TCHA */
	);
	l1s_tx_win_ctrl(arfcn | ARFCN_UPLINK, L1_TXWIN_NB, 0, 3);

	return 0;
}


const struct tdma_sched_item tch_a_sched_set[] = {
	SCHED_ITEM_DT(l1s_tch_a_cmd, 0, 0, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_tch_a_resp, 0, 0, -4),	SCHED_END_FRAME(),
	SCHED_END_SET()
};
