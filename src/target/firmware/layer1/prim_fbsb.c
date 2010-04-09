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
};

static void dump_mon_state(struct mon_state *fb)
{
#if 0
	printf("(%u:%u): TOA=%5u, Power=%4ddBm, Angle=%5dHz, "
		"SNR=%04x(%d.%u) OFFSET=%u SYNCHRO=%u\n",
		fb->fnr_report, fb->attempt, fb->toa,
		agc_inp_dbm8_by_pm(fb->pm)/8, ANGLE_TO_FREQ(fb->angle),
		fb->snr, l1s_snr_int(fb->snr), l1s_snr_fract(fb->snr),
		tpu_get_offset(), tpu_get_synchro());
#else
	printf("(%u:%u): TOA=%5u, Power=%4ddBm, Angle=%5dHz ",
		fb->fnr_report, fb->attempt, fb->toa,
		agc_inp_dbm8_by_pm(fb->pm)/8, ANGLE_TO_FREQ(fb->angle));
#endif
}

static struct mon_state _last_fb, *last_fb = &_last_fb;

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

/* FCCH Burst *****************************************************************/

/* scheduler callback to issue a FB detection task to the DSP */
static int l1s_fbdet_cmd(__unused uint8_t p1, __unused uint8_t fb_mode,
			 __unused uint16_t p3)
{
	if (fb_mode == 0) {
		putchart('F');
	} else {
		putchart('V');
	}

	/* Program DSP */
	dsp_api.db_w->d_task_md = FB_DSP_TASK;	/* maybe with I/Q swap? */
	dsp_api.ndb->d_fb_mode = fb_mode;
	dsp_end_scenario();

	/* Program TPU */
	l1s_rx_win_ctrl(rf_arfcn, L1_RXWIN_FB);
	tpu_end_scenario();

	return 0;
}


/* scheduler callback to check for a FB detection response */
static int l1s_fbdet_resp(__unused uint8_t p1, uint8_t attempt,
			  __unused uint16_t p3)
{
	int ntdma, qbits, fn_offset;

	putchart('f');

	if (!dsp_api.ndb->d_fb_det) {
		/* we did not detect a FB, fall back to mode 0! */
		if (attempt == 12) {
			/* If we don't reset here, we get DSP DMA errors */
			tdma_sched_reset();

			/* if we are already synchronized initially,
			 * code below has set l1s.fb.mode to 1 and
			 * we switch to the more narrow mode 1 */
			l1s_fb_test(1, l1s.fb.mode);
		}
		return 0;
	}

	printf("FB%u ", dsp_api.ndb->d_fb_mode);
	read_fb_result(last_fb, attempt);

	/* FIXME: where did this magic 23 come from? */
	last_fb->toa -= 23;

	if (last_fb->toa < 0) {
		qbits = (last_fb->toa + BITS_PER_TDMA) * 4;
		ntdma = -1;
	} else {
		ntdma = (last_fb->toa) / BITS_PER_TDMA;
		qbits = (last_fb->toa - ntdma * BITS_PER_TDMA) * 4;
	}

	{
		fn_offset = l1s.current_time.fn - attempt + ntdma;
		int fnr_delta = last_fb->fnr_report - attempt;
		int bits_delta = fnr_delta * BITS_PER_TDMA;

		struct l1_cell_info *cinfo = &l1s.serving_cell;

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

	/* We found a frequency burst, reset everything and start next task */
	l1s_reset_hw();
	tdma_sched_reset();

	if (dsp_api.frame_ctr > 500 && l1s.fb.mode == 0) {
		/* We've done more than 500 rounds of FB detection, so
		 * the AGC should be synchronized and we switch to the
		 * more narrow FB detection mode 1 */
		l1s.fb.mode = 1;
		/* Don't synchronize_tdma() yet, it does probably not work
		 * reliable due to the TPU reset) */
	}

#if 1
	/* restart a SB or new FB detection task */
	if (dsp_api.frame_ctr > 1000 && l1s.fb.mode == 1 &&
	    abs(last_fb->freq_diff) < 1000)  {
		int delay;

		/* synchronize before reading SB */
		synchronize_tdma(&l1s.serving_cell);

		delay = fn_offset + 11 - l1s.current_time.fn - 1;
		dsp_api.ndb->d_fb_det = 0;
		dsp_api.ndb->a_sync_demod[D_TOA] = 0; /* TSM30 does it (really needed ?) */
		l1s.fb.mode = 0;
		l1s_sb_test(delay);
	} else
#endif
	{
		/* If we don't reset here, we get DSP DMA errors */
		tdma_sched_reset();
		/* use FB_MODE_1 if we are within certain limits */
		if (abs(last_fb->freq_diff < 2000))
			l1s_fb_test(fn_offset + 10 - l1s.current_time.fn - 1, 1);
		else
			l1s_fb_test(fn_offset + 10 - l1s.current_time.fn - 1, 0);
	}

	return 0;
}

/* we don't really use this because we need to configure the fb_mode! */
static const struct tdma_sched_item fb_sched_set[] = {
	SCHED_ITEM(l1s_fbdet_cmd, 0, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 1),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 3),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 4),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 5),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 6),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 7),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 8),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 9),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 10),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 11),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_fbdet_resp, 0, 12),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

void l1s_fb_test(uint8_t base_fn, uint8_t fb_mode)
{
#if 1
	int i;
	/* schedule the FB detection command */
	tdma_schedule(base_fn, &l1s_fbdet_cmd, 0, fb_mode, 0);

	/* schedule 12 attempts to read the result */
	for (i = 1; i <= 12; i++) {
		uint8_t fn = base_fn + 1 + i;
		tdma_schedule(fn, &l1s_fbdet_resp, 0, i, 0);
	}
#else
	/* use the new scheduler 'set' and simply schedule the whole set */
	/* WARNING: we cannot set FB_MODE_1 this way !!! */
	tdma_schedule_set(base_fn, fb_sched_set, 0);
#endif
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
	uint8_t bsic;
	static struct gsm_time sb_time;
	int qbits, fn_offset;
	struct l1_cell_info *cinfo = &l1s.serving_cell;
	int fnr_delta, bits_delta;
	struct l1ctl_sync_new_ccch_resp *l1;
	struct msgb *msg;

	putchart('s');

	if (dsp_api.db_r->a_sch[0] & (1<<B_SCH_CRC)) {
		/* after 2nd attempt, restart */
		if (attempt == 2)
			l1s_sb_test(2);

		/* mark READ page as being used */
		dsp_api.r_page_used = 1;

		return 0;
	}

	l1s.sb.count++;

	printf("SB%d ", attempt);
	read_sb_result(last_fb, dsp_api.frame_ctr);

	sb = dsp_api.db_r->a_sch[3] | dsp_api.db_r->a_sch[4] << 16;
	bsic = l1s_decode_sb(&sb_time, sb);
	printf("=> SB 0x%08x: BSIC=%u ", sb, bsic);
	l1s_time_dump(&sb_time);

	l1s.serving_cell.bsic = bsic;

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

	if (l1s.sb.count > 5 && l1s.sb.synced == 0) {
		synchronize_tdma(&l1s.serving_cell);
		l1s.sb.synced = 1;
	}

	/* if we have recived a SYNC burst, update our local GSM time */
	gsm_fn2gsmtime(&l1s.current_time, sb_time.fn + SB2_LATENCY);
	/* compute next time from new current time */
	l1s.next_time = l1s.current_time;
	l1s_time_inc(&l1s.next_time, 1);

	/* place it in the queue for the layer2 */
	msg = l1_create_l2_msg(L1CTL_NEW_CCCH_RESP, sb_time.fn,
				last_fb->snr, rf_arfcn);
	l1 = (struct l1ctl_sync_new_ccch_resp *) msgb_put(msg, sizeof(*l1));
	l1->bsic = bsic;
	l1_queue_for_l2(msg);

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

	if (l1s.sb.count > 10 && sb_time.t3 == 41) {
		l1s_reset_hw();
		/* enable the MF Task for BCCH reading */
		l1s.mf_tasks |= (1 << MF_TASK_BCCH_NORM);
		l1s.mf_tasks |= (1 << MF_TASK_CCCH_COMB);
	} else {
		/* We have just seen a SCH burst, we know the next one
		 * is not in less than 7 TDMA frames from now */
		l1s_sb_test(7);
	}

	return 0;
}

static int l1s_sbdet_cmd(__unused uint8_t p1, __unused uint8_t p2,
			 __unused uint16_t p3)
{
	putchart('S');

	dsp_api.db_w->d_task_md = SB_DSP_TASK;
	dsp_api.ndb->d_fb_mode = 0; /* wideband search */
	dsp_end_scenario();

	/* Program TPU */
	l1s_rx_win_ctrl(rf_arfcn, L1_RXWIN_SB);
	tpu_end_scenario();

	return 0;
}

void l1s_sb_test(uint8_t base_fn)
{
#if 1
	/* This is how it is done by the TSM30 */
	tdma_schedule(base_fn, &l1s_sbdet_cmd, 0, 1, 0);
	tdma_schedule(base_fn + 1, &l1s_sbdet_cmd, 0, 2, 0);
	tdma_schedule(base_fn + 3, &l1s_sbdet_resp, 0, 1, 0);
	tdma_schedule(base_fn + 4, &l1s_sbdet_resp, 0, 2, 0);
#else
	tdma_schedule(base_fn, &l1s_sbdet_cmd, 0, 1, 0);
	tdma_schedule(base_fn + 1, &l1s_sbdet_resp, 0, 1, 0);
	tdma_schedule(base_fn + 2, &l1s_sbdet_resp, 0, 2, 0);
#endif
}


