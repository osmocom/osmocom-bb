/* Layer 1 - FCCH and SCH burst handling */

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
#include <errno.h>
#include <inttypes.h>
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
#include <layer1/agc.h>

#include <l1ctl_proto.h>

#define FB0_RETRY_COUNT		3
#define AFC_RETRY_COUNT		30

extern uint16_t rf_arfcn; // TODO

struct mon_state {
	uint32_t fnr_report;	/* frame number when DSP reported it */
	int attempt;		/* which attempt was this ? */

	int16_t toa;
	uint16_t pm;
	uint16_t angle;
	uint16_t snr;

	/* computed values */
	int16_t freq_diff;

	/* Sync Burst (SB) */
	uint8_t bsic;
	struct gsm_time time;
};

struct l1a_fb_state {
	struct mon_state mon;
	struct l1ctl_fbsb_req req;
	int16_t initial_freq_err;
	uint8_t fb_retries;
	uint8_t afc_retries;
};

static struct l1a_fb_state fbs;
static struct mon_state *last_fb = &fbs.mon;

static void dump_mon_state(struct mon_state *fb)
{
#if 0
	printf("(%"PRIu32":%u): TOA=%5u, Power=%4ddBm, Angle=%5dHz, "
		"SNR=%04x(%d.%u) OFFSET=%u SYNCHRO=%u\n",
		fb->fnr_report, fb->attempt, fb->toa,
		agc_inp_dbm8_by_pm(fb->pm)/8, ANGLE_TO_FREQ(fb->angle),
		fb->snr, l1s_snr_int(fb->snr), l1s_snr_fract(fb->snr),
		tpu_get_offset(), tpu_get_synchro());
#else
	printf("(%"PRIu32":%u): TOA=%5u, Power=%4ddBm, Angle=%5dHz\n",
		fb->fnr_report, fb->attempt, fb->toa,
		agc_inp_dbm8_by_pm(fb->pm)/8, ANGLE_TO_FREQ(fb->angle));
#endif
}

static int l1ctl_fbsb_resp(uint8_t res)
{
	struct msgb *msg;
	struct l1ctl_fbsb_conf *resp;

	msg = l1_create_l2_msg(L1CTL_FBSB_CONF, fbs.mon.time.fn,
				l1s_snr_int(fbs.mon.snr),
				fbs.req.band_arfcn);
	if (!msg)
		return -ENOMEM;

	resp = (struct l1ctl_fbsb_conf *) msgb_put(msg, sizeof(*resp));
	resp->initial_freq_err = htons(fbs.initial_freq_err);
	resp->result = res;
	resp->bsic = fbs.mon.bsic;

	/* no need to set BSIC, as it is never used here */
	l1_queue_for_l2(msg);

	return 0;
}

/* SCH Burst Detection ********************************************************/

/* determine the GSM time and BSIC from a Sync Burst */
static uint8_t l1s_decode_sb(struct gsm_time *time, uint32_t sb)
{
	uint8_t bsic = (sb >> 2) & 0x3f;
	uint8_t t3p;

	memset(time, 0, sizeof(*time));

	/* TS 05.02 Chapter 3.3.2.2.1 SCH Frame Numbers */
	time->t1 = ((sb >> 23) & 1) | ((sb >> 7) & 0x1fe) | ((sb << 9) & 0x600);
	time->t2 = (sb >> 18) & 0x1f;
	t3p = ((sb >> 24) & 1) | ((sb >> 15) & 6);
	time->t3 = t3p*10 + 1;

	/* TS 05.02 Chapter 4.3.3 TDMA frame number */
	time->fn = gsm_gsmtime2fn(time);

	time->tc = (time->fn / 51) % 8;

	return bsic;
}

static void read_sb_result(struct mon_state *st, int attempt)
{
	st->toa = dsp_api.db_r->a_serv_demod[D_TOA];
	st->pm = dsp_api.db_r->a_serv_demod[D_PM]>>3;
	st->angle = dsp_api.db_r->a_serv_demod[D_ANGLE];
	st->snr = dsp_api.db_r->a_serv_demod[D_SNR];

	st->freq_diff = ANGLE_TO_FREQ(st->angle);
	st->fnr_report = l1s.current_time.fn;
	st->attempt = attempt;

	dump_mon_state(st);

	if (st->snr > AFC_SNR_THRESHOLD)
		afc_input(st->freq_diff, rf_arfcn, 1);
	else
		afc_input(st->freq_diff, rf_arfcn, 0);

	dsp_api.r_page_used = 1;
}

/* Note: When we get the SB response, it is 2 TDMA frames after the SB
 * actually happened, as it is a "C W W R" task */
#define SB2_LATENCY	2

static int l1s_sbdet_resp(__unused uint8_t p1, uint8_t attempt,
			  __unused uint16_t p3)
{
	uint32_t sb;
	int qbits, fn_offset;
	struct l1_cell_info *cinfo = &l1s.serving_cell;
	int fnr_delta, bits_delta;

	putchart('s');

	if (dsp_api.db_r->a_sch[0] & (1<<B_SCH_CRC)) {
		/* mark READ page as being used */
		dsp_api.r_page_used = 1;

		/* after 2nd attempt, we failed */
		if (attempt == 2) {
			last_fb->attempt = 13;
			l1s_compl_sched(L1_COMPL_FB);
		}

		/* after 1st attempt, we simply wait for 2nd */
		return 0;
	}

	printf("SB%d ", attempt);
	read_sb_result(last_fb, attempt);

	sb = dsp_api.db_r->a_sch[3] | dsp_api.db_r->a_sch[4] << 16;
	fbs.mon.bsic = l1s_decode_sb(&fbs.mon.time, sb);
	printf("=> SB 0x%08"PRIx32": BSIC=%u ", sb, fbs.mon.bsic);
	l1s_time_dump(&fbs.mon.time);

	l1s.serving_cell.bsic = fbs.mon.bsic;

	/* calculate synchronisation value (TODO: only complete for qbits) */
	last_fb->toa -= 23;
	qbits = last_fb->toa * 4;
	fn_offset = l1s.current_time.fn; // TODO

	if (qbits > QBITS_PER_TDMA) {
		qbits -= QBITS_PER_TDMA;
		fn_offset -= 1;
	} else if (qbits < 0)  {
		qbits += QBITS_PER_TDMA;
		fn_offset += 1;
	}

	fnr_delta = last_fb->fnr_report - attempt;
	bits_delta = fnr_delta * BITS_PER_TDMA;

	cinfo->fn_offset = fnr_delta;
	cinfo->time_alignment = qbits;
	cinfo->arfcn = rf_arfcn;

	if (last_fb->toa > bits_delta)
		printf("=> DSP reports SB in bit that is %d bits in the "
			"future?!?\n", last_fb->toa - bits_delta);
	else
		printf(" qbits=%u\n", qbits);

	synchronize_tdma(&l1s.serving_cell);

	/* if we have recived a SYNC burst, update our local GSM time */
	gsm_fn2gsmtime(&l1s.current_time, fbs.mon.time.fn + SB2_LATENCY);
	/* compute next time from new current time */
	l1s.next_time = l1s.current_time;
	l1s_time_inc(&l1s.next_time, 1);

	/* If we call tdma_sched_reset(), which is only needed if there
	 * are further l1s_sbdet_resp() scheduled, we will bring
	 * dsp_api.db_r and dsp_api.db_w out of sync because we changed
	 * dsp_api.db_w for l1s_sbdet_cmd() and canceled
	 * l1s_sbdet_resp() which would change dsp_api.db_r. The DSP
	 * however expects dsp_api.db_w and dsp_api.db_r to be in sync
	 * (either "0 - 0" or "1 - 1"). So we have to bring dsp_api.db_w
	 * and dsp_api.db_r into sync again, otherwise NB reading will
	 * complain. We probably don't need the Abort command and could
	 * just bring dsp_api.db_w and dsp_api.db_r into sync.  */
	if (attempt != 2) {
		tdma_sched_reset();
		l1s_dsp_abort();
	}

	l1s_reset_hw();
	/* enable the MF Task for BCCH reading */
	mframe_enable(MF_TASK_BCCH_NORM);
	if (l1s.serving_cell.ccch_mode == CCCH_MODE_COMBINED)
		mframe_enable(MF_TASK_CCCH_COMB);
	else if (l1s.serving_cell.ccch_mode == CCCH_MODE_NON_COMBINED)
		mframe_enable(MF_TASK_CCCH);

	l1s_compl_sched(L1_COMPL_FB);

	return 0;
}

static int l1s_sbdet_cmd(__unused uint8_t p1, __unused uint8_t p2,
			 __unused uint16_t p3)
{
	putchart('S');

	fbs.mon.bsic = 0;
	fbs.mon.time.fn = 0;

	dsp_api.db_w->d_task_md = SB_DSP_TASK;
	dsp_api.ndb->d_fb_mode = 0; /* wideband search */

	/* Program TPU */
	l1s_rx_win_ctrl(rf_arfcn, L1_RXWIN_SB, 0);

	return 0;
}

/* This is how it is done by the TSM30 */
static const struct tdma_sched_item sb_sched_set[] = {
	SCHED_ITEM_DT(l1s_sbdet_cmd, 0, 0, 1),	SCHED_END_FRAME(),
	SCHED_ITEM_DT(l1s_sbdet_cmd, 0, 0, 2),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sbdet_resp, -4, 0, 1),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sbdet_resp, -4, 0, 2),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

void l1s_sb_test(uint8_t base_fn)
{
	tdma_schedule_set(base_fn, sb_sched_set, 0);
}
/* FCCH Burst *****************************************************************/

static int read_fb_result(struct mon_state *st, int attempt)
{
	st->toa = dsp_api.ndb->a_sync_demod[D_TOA];
	st->pm = dsp_api.ndb->a_sync_demod[D_PM]>>3;
	st->angle = dsp_api.ndb->a_sync_demod[D_ANGLE];
	st->snr = dsp_api.ndb->a_sync_demod[D_SNR];

	//last_fb->angle = clip_int16(last_fb->angle, AFC_MAX_ANGLE);
	st->freq_diff = ANGLE_TO_FREQ(last_fb->angle);
	st->fnr_report = l1s.current_time.fn;
	st->attempt = attempt;

	dump_mon_state(st);

	dsp_api.ndb->d_fb_det = 0;
	dsp_api.ndb->a_sync_demod[D_TOA] = 0; /* TSM30 does it (really needed ?) */

	/* Update AFC with current frequency offset */
	afc_correct(st->freq_diff, rf_arfcn);

	//tpu_dsp_frameirq_enable();
	return 1;
}

static void fbinfo2cellinfo(struct l1_cell_info *cinfo,
			    const struct mon_state *mon)
{
	int ntdma, qbits, fn_offset, fnr_delta, bits_delta;

	/* FIXME: where did this magic 23 come from? */
	last_fb->toa -= 23;

	if (last_fb->toa < 0) {
		qbits = (last_fb->toa + BITS_PER_TDMA) * 4;
		ntdma = -1;
	} else {
		ntdma = (last_fb->toa) / BITS_PER_TDMA;
		qbits = (last_fb->toa - ntdma * BITS_PER_TDMA) * 4;
	}

	fn_offset = l1s.current_time.fn - last_fb->attempt + ntdma;
	fnr_delta = last_fb->fnr_report - last_fb->attempt;
	bits_delta = fnr_delta * BITS_PER_TDMA;

	cinfo->fn_offset = fnr_delta;
	cinfo->time_alignment = qbits;
	cinfo->arfcn = rf_arfcn;

	if (last_fb->toa > bits_delta)
		printf("=> DSP reports FB in bit that is %d bits in "
			"the future?!?\n", last_fb->toa - bits_delta);
	else {
		int fb_fnr = (last_fb->fnr_report - last_fb->attempt)
				+ last_fb->toa/BITS_PER_TDMA;
		printf("=>FB @ FNR %u fn_offset=%d qbits=%u\n",
			fb_fnr, fn_offset, qbits);
	}
}

/* scheduler callback to issue a FB detection task to the DSP */
static int l1s_fbdet_cmd(__unused uint8_t p1, __unused uint8_t p2,
			 uint16_t fb_mode)
{
	if (fb_mode == 0) {
		putchart('F');
	} else {
		putchart('V');
	}

	l1s.fb.mode = fb_mode;

	/* Tell the RF frontend to set the gain appropriately */
	rffe_compute_gain(rxlev2dbm(fbs.req.rxlev_exp), CAL_DSP_TGT_BB_LVL);

	/* Program DSP */
	dsp_api.db_w->d_task_md = FB_DSP_TASK;	/* maybe with I/Q swap? */
	dsp_api.ndb->d_fb_mode = fb_mode;

	/* Program TPU */
	l1s_rx_win_ctrl(fbs.req.band_arfcn, L1_RXWIN_FB, 0);

	return 0;
}

#if 0
#define FB0_SNR_THRESH	2000
#define FB1_SNR_THRESH	3000
#else
#define FB0_SNR_THRESH	0
#define FB1_SNR_THRESH	0
#endif

static const struct tdma_sched_item fb_sched_set[];

/* scheduler callback to check for a FB detection response */
static int l1s_fbdet_resp(__unused uint8_t p1, uint8_t attempt,
			  uint16_t fb_mode)
{
	putchart('f');

	if (!dsp_api.ndb->d_fb_det) {
		/* we did not detect a FB */

		/* attempt < 12, do nothing */
		if (attempt < 12)
			return 0;

		/* attempt >= 12, we simply don't find one */

		/* If we don't reset here, we get DSP DMA errors */
		tdma_sched_reset();

		if (fbs.fb_retries < FB0_RETRY_COUNT) {
			/* retry once more */
			tdma_schedule_set(1, fb_sched_set, 0);
			fbs.fb_retries++;
		} else {
			last_fb->attempt = 13;
			l1s_compl_sched(L1_COMPL_FB);
		}

		return 0;
	}

	/* We found a frequency burst, reset everything */
	l1s_reset_hw();

	printf("FB%u ", dsp_api.ndb->d_fb_mode);
	read_fb_result(last_fb, attempt);

	/* if this is the first success, save freq err */
	if (!fbs.initial_freq_err)
		fbs.initial_freq_err = last_fb->freq_diff;

	/* If we don't reset here, we get DSP DMA errors */
	tdma_sched_reset();

	/* Immediately schedule further TDMA tasklets, if requested. Doing
	 * this directly from L1S means we can do this quickly without any
	 * additional delays */
	if (fb_mode == 0) {
		if (fbs.req.flags & L1CTL_FBSB_F_FB1) {
			/* If we don't reset here, we get DSP DMA errors */
			tdma_sched_reset();
			/* FIXME: don't only use the last but an average */
			if (abs(last_fb->freq_diff) < fbs.req.freq_err_thresh1 &&
			    last_fb->snr > FB0_SNR_THRESH) {
				/* continue with FB1 task in DSP */
				tdma_schedule_set(1, fb_sched_set, 1);
			} else {
				if (fbs.afc_retries < AFC_RETRY_COUNT) {
					tdma_schedule_set(1, fb_sched_set, 0);
					fbs.afc_retries++;
				} else {
					/* Abort */
					last_fb->attempt = 13;
					l1s_compl_sched(L1_COMPL_FB);
				}
			}
		} else
			l1s_compl_sched(L1_COMPL_FB);
	} else if (fb_mode == 1) {
		if (fbs.req.flags & L1CTL_FBSB_F_SB) {

	int ntdma, qbits;
	/* FIXME: where did this magic 23 come from? */
	last_fb->toa -= 23;

	if (last_fb->toa < 0) {
		qbits = (last_fb->toa + BITS_PER_TDMA) * 4;
		ntdma = -1;
	} else {
		ntdma = (last_fb->toa) / BITS_PER_TDMA;
		qbits = (last_fb->toa - ntdma * BITS_PER_TDMA) * 4;
	}


			int fn_offset = l1s.current_time.fn - last_fb->attempt + ntdma;
			int delay = fn_offset + 11 - l1s.current_time.fn - 1;
			printf("  fn_offset=%d (fn=%"PRIu32" + attempt=%u + ntdma = %d)\n",
				fn_offset, l1s.current_time.fn, last_fb->attempt, ntdma);
			printf("  delay=%d (fn_offset=%d + 11 - fn=%"PRIu32" - 1\n", delay,
				fn_offset, l1s.current_time.fn);
			printf("  scheduling next FB/SB detection task with delay %u\n", delay);
			if (abs(last_fb->freq_diff) < fbs.req.freq_err_thresh2 &&
			    last_fb->snr > FB1_SNR_THRESH) {
				/* synchronize before reading SB */
				fbinfo2cellinfo(&l1s.serving_cell, last_fb);
				synchronize_tdma(&l1s.serving_cell);
				tdma_schedule_set(delay, sb_sched_set, 0);
			} else
				tdma_schedule_set(delay, fb_sched_set, 1);
		} else
			l1s_compl_sched(L1_COMPL_FB);
	}

	return 0;
}

/* FB detection */
static const struct tdma_sched_item fb_sched_set[] = {
	SCHED_ITEM_DT(l1s_fbdet_cmd, 0, 0, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 1),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 3),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 4),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 5),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 6),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 7),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 8),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 9),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 10),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 11),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, -4, 0, 12),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

/* Asynchronous completion handler for FB detection */
static void l1a_fb_compl(__unused enum l1_compl c)
{
	if (last_fb->attempt >= 13) {
		/* FB detection failed, signal this via L1CTL */
		l1ctl_fbsb_resp(255);
		return;
	}

	/* FIME: use l1s.neigh_cell[fbs.cinfo_idx] */
	fbinfo2cellinfo(&l1s.serving_cell, last_fb);

	/* send FBSB_CONF success message via L1CTL */
	l1ctl_fbsb_resp(0);
}

void l1s_fbsb_req(uint8_t base_fn, struct l1ctl_fbsb_req *req)
{
	/* copy + endian convert request data */
	fbs.req.band_arfcn = ntohs(req->band_arfcn);
	fbs.req.timeout = ntohs(req->timeout);
	fbs.req.freq_err_thresh1 = ntohs(req->freq_err_thresh1);
	fbs.req.freq_err_thresh2 = ntohs(req->freq_err_thresh2);
	fbs.req.num_freqerr_avg = req->num_freqerr_avg;
	fbs.req.flags = req->flags;
	fbs.req.sync_info_idx = req->sync_info_idx;
	fbs.req.rxlev_exp = req->rxlev_exp;

	/* clear initial frequency error */
	fbs.initial_freq_err = 0;
	fbs.fb_retries = 0;
	fbs.afc_retries = 0;

	/* Make sure we start at a 'center' AFCDAC output value */
	afc_reset();

	/* Reset the TOA loop counters */
	toa_reset();

	if (fbs.req.flags & L1CTL_FBSB_F_FB0)
		tdma_schedule_set(base_fn, fb_sched_set, 0);
	else if (fbs.req.flags & L1CTL_FBSB_F_FB1)
		tdma_schedule_set(base_fn, fb_sched_set, 0);
	else if (fbs.req.flags & L1CTL_FBSB_F_SB)
		tdma_schedule_set(base_fn, sb_sched_set, 0);

}

static __attribute__ ((constructor)) void l1s_prim_fbsb_init(void)
{
	l1s.completion[L1_COMPL_FB] = &l1a_fb_compl;
}
