/* Synchronous part of GSM Layer 1 */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Dieter Spaar <spaar@mirider.augusta.de>
 * (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include <debug.h>
#include <memory.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>

#include <layer1/sync.h>
#include <layer1/afc.h>
#include <layer1/agc.h>
#include <layer1/tdma_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>

//#define DEBUG_EVERY_TDMA

/* A debug macro to print every TDMA frame */
#ifdef DEBUG_EVERY_TDMA
#define putchart(x) putchar(x)
#else
#define putchart(x)
#endif

struct l1s_state l1s;

static l1s_cb_t l1s_cb = NULL;

void l1s_set_handler(l1s_cb_t cb)
{
	l1s_cb = cb;
}

#define ADD_MODULO(sum, delta, modulo)	do {	\
	if ((sum += delta) >= modulo)		\
		sum -= modulo;			\
	} while (0)

#define GSM_MAX_FN	(26*51*2048)

static void l1s_time_inc(struct gsm_time *time, uint32_t delta_fn)
{
	ADD_MODULO(time->fn, delta_fn, GSM_MAX_FN);

	if (delta_fn == 1) {
		ADD_MODULO(time->t2, 1, 26);
		ADD_MODULO(time->t3, 1, 51);

		/* if the new frame number is a multiple of 51 */
		if (time->t3 == 0) {
			ADD_MODULO(time->tc, 1, 8);

			/* if new FN is multiple of 51 and 26 */
			if (time->t2 == 0)
				ADD_MODULO(time->t1, 1, 2048);
		}
	} else
		gsm_fn2gsmtime(time, time->fn);
}

static void l1s_time_dump(const struct gsm_time *time)
{
	printf("fn=%u(%u/%2u/%2u)", time->fn, time->t1, time->t2, time->t3);
}

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

static int last_task_fnr;

extern uint16_t rf_arfcn; // TODO

/* clip a signed 16bit value at a certain limit */
int16_t clip_int16(int16_t angle, int16_t clip_at)
{
	if (angle > clip_at)
		angle = clip_at;
	else if (angle < -clip_at)
		angle = -clip_at;

	return angle;
}

int16_t l1s_snr_int(uint16_t snr)
{
	return snr >> 10;
}

uint16_t l1s_snr_fract(uint16_t snr)
{
	uint32_t fract = snr & 0x3ff;
	fract = fract * 1000 / (2 << 10);

	return fract & 0xffff;
}

static void l1ddsp_meas_read(uint8_t nbmeas, uint16_t *pm)
{
	uint8_t i;

	for (i = 0; i < nbmeas; i++)
		pm[i] = (uint16_t) ((dsp_api.db_r->a_pm[i] & 0xffff) >> 3);
	dsp_api.r_page_used = 1;
}

/* Convert an angle in fx1.15 notatinon into Hz */
#define BITFREQ_DIV_2PI		43104	/* 270kHz / 2 * pi */
#define ANG2FREQ_SCALING	(2<<15)	/* 2^15 scaling factor for fx1.15 */
#define ANGLE_TO_FREQ(angle)	((int16_t)angle * BITFREQ_DIV_2PI / ANG2FREQ_SCALING)

#define AFC_MAX_ANGLE		328	/* 0.01 radian in fx1.15 */
#define AFC_SNR_THRESHOLD	2560	/* 2.5 dB in fx6.10 */

#define BITS_PER_TDMA		1250
#define QBITS_PER_TDMA		(BITS_PER_TDMA * 4)	/* 5000 */
#define TPU_RANGE		QBITS_PER_TDMA
#define	SWITCH_TIME		(TPU_RANGE-10)


static int fb_once = 0;

/* synchronize the L1S to a new timebase (typically a new cell */
static void synchronize_tdma(struct l1_cell_info *cinfo)
{
	int32_t fn_offset;
	uint32_t tpu_shift = cinfo->time_alignment;

	/* NB detection only works if the TOA of the SB
	 * is within 0...8. We have to add 75 to get an SB TOA of 4. */
	tpu_shift += 75;

	tpu_shift = (l1s.tpu_offset + tpu_shift) % QBITS_PER_TDMA;

	fn_offset = cinfo->fn_offset - 1;

	/* if we're already very close to the end of the TPU frame,
	 * the next interrupt will basically occur now and we need to compensate */
	if (tpu_shift < SWITCH_TIME)
		fn_offset++;

#if 0 /* probably wrong as we already added "offset" and "shift" above */
	/* increment the TPU quarter-bit offset */
	l1s.tpu_offset = (l1s.tpu_offset + tpu_shift) % TPU_RANGE;
#else
	l1s.tpu_offset = tpu_shift;
#endif

	puts("Synchronize_TDMA\n");
	/* request the TPU to adjust the SYNCHRO and OFFSET registers */
	tpu_enq_at(SWITCH_TIME);
	tpu_enq_sync(l1s.tpu_offset);
#if 0
	/* FIXME: properly end the TPU window at the emd of l1_sync() */
	tpu_end_scenario();
#endif

	/* Change the current time to reflect the new value */
	l1s_time_inc(&l1s.current_time, fn_offset);
	l1s.next_time = l1s.current_time;
	l1s_time_inc(&l1s.next_time, 1);

	/* The serving cell now no longer has a frame or bit offset */
	cinfo->fn_offset = 0;
	cinfo->time_alignment = 0;
}

static void l1s_reset_hw(void)
{
	dsp_api.w_page = 0;
	dsp_api.r_page = 0;
	dsp_api.r_page_used = 0;
	dsp_api.db_w = (T_DB_MCU_TO_DSP *) BASE_API_W_PAGE_0;
	dsp_api.db_r = (T_DB_DSP_TO_MCU *) BASE_API_R_PAGE_0;
	dsp_api.ndb->d_dsp_page = 0;

	/* we have to really reset the TPU, otherwise FB detection
	 * somtimes returns wrong TOA values. */
	tpu_reset(1);
	tpu_reset(0);
	tpu_rewind();
	tpu_enq_wait(5); /* really needed ? */
	tpu_enq_offset(l1s.tpu_offset);
	tpu_end_scenario();
}

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
		"SNR=%04x(%d.%u) OFFSET=%u SYNCHRO=%u\n", fb->fnr_report, fb->attempt,
		fb->toa, agc_inp_dbm8_by_pm(fb->pm)/8,
		ANGLE_TO_FREQ(fb->angle), fb->snr, l1s_snr_int(fb->snr),
		l1s_snr_fract(fb->snr), tpu_get_offset(), tpu_get_synchro());
#else
	printf("(%u:%u): TOA=%5u, Power=%4ddBm, Angle=%5dHz ", fb->fnr_report, fb->attempt,
		fb->toa, agc_inp_dbm8_by_pm(fb->pm)/8,
		ANGLE_TO_FREQ(fb->angle));
#endif
}

static struct mon_state _last_fb, *last_fb = &_last_fb;

static int read_fb_result(int attempt)
{
	last_fb->toa = dsp_api.ndb->a_sync_demod[D_TOA];
	last_fb->pm = dsp_api.ndb->a_sync_demod[D_PM]>>3;
	last_fb->angle = dsp_api.ndb->a_sync_demod[D_ANGLE];
	last_fb->snr = dsp_api.ndb->a_sync_demod[D_SNR];

	//last_fb->angle = clip_int16(last_fb->angle, AFC_MAX_ANGLE);
	last_fb->freq_diff = ANGLE_TO_FREQ(last_fb->angle);
	last_fb->fnr_report = l1s.current_time.fn;
	last_fb->attempt = attempt;

	dump_mon_state(last_fb);

	dsp_api.ndb->d_fb_det = 0;
	dsp_api.ndb->a_sync_demod[D_TOA] = 0; /* TSM30 does it (really needed ?) */

	/* Update AFC with current frequency offset */
	afc_correct(last_fb->freq_diff, rf_arfcn);

	//tpu_dsp_frameirq_enable();
	return 1;
}

static void read_sb_result(int attempt)
{
	last_fb->toa = dsp_api.db_r->a_serv_demod[D_TOA];
	last_fb->pm = dsp_api.db_r->a_serv_demod[D_PM]>>3;
	last_fb->angle = dsp_api.db_r->a_serv_demod[D_ANGLE];
	last_fb->snr = dsp_api.db_r->a_serv_demod[D_SNR];

	last_fb->freq_diff = ANGLE_TO_FREQ(last_fb->angle);
	last_fb->fnr_report = l1s.current_time.fn;
	last_fb->attempt = attempt;

	dump_mon_state(last_fb);

	if (last_fb->snr > AFC_SNR_THRESHOLD)
		afc_input(last_fb->freq_diff, rf_arfcn, 1);
	else
		afc_input(last_fb->freq_diff, rf_arfcn, 0);

	dsp_api.r_page_used = 1;
}

#define TIMER_TICKS_PER_TDMA	1875

static int last_timestamp;

static inline void check_lost_frame(void)
{
	int diff, timestamp = hwtimer_read(1);

	if (last_timestamp < timestamp)
		last_timestamp += (4*TIMER_TICKS_PER_TDMA);

	diff = last_timestamp - timestamp;
	if (diff != 1875)
		printf("LOST!\n");

	last_timestamp = timestamp;
}

/* main routine for synchronous part of layer 1, called by frame interrupt
 * generated by TPU once every TDMA frame */
static void l1_sync(void)
{
	putchart('+');

	check_lost_frame();

	/* Increment Time */
	l1s.current_time = l1s.next_time;
	l1s_time_inc(&l1s.next_time, 1);
	//l1s_time_dump(&l1s.current_time); putchar(' ');

	dsp_api.frame_ctr++;
	dsp_api.r_page_used = 0;

	/* Update pointers */
	if (dsp_api.w_page == 0)
		dsp_api.db_w = (T_DB_MCU_TO_DSP *) BASE_API_W_PAGE_0;
	else
		dsp_api.db_w = (T_DB_MCU_TO_DSP *) BASE_API_W_PAGE_1;

	if (dsp_api.r_page == 0)
		dsp_api.db_r = (T_DB_DSP_TO_MCU *) BASE_API_R_PAGE_0;
	else
		dsp_api.db_r = (T_DB_DSP_TO_MCU *) BASE_API_R_PAGE_1;

	/* Reset MCU->DSP page */
	dsp_api_memset((uint16_t *) dsp_api.db_w, sizeof(*dsp_api.db_w));

	/* Update AFC */
	afc_load_dsp();

	if (dsp_api.ndb->d_error_status) {
		printf("DSP Error Status: %u\n", dsp_api.ndb->d_error_status);
		dsp_api.ndb->d_error_status = 0;
	}

#if 0
	if (l1s.task != dsp_api.db_r->d_task_md)
		printf("DSP task (%u) and L1S task (%u) disagree\n", dsp_api.db_r->d_task_md, l1s.task);
#endif
	/* execute the sched_items that have been scheduled for this TDMA frame */
	tdma_sched_execute();

	if (dsp_api.r_page_used) {
		/* clear and switch the read page */
		dsp_api_memset((uint16_t *) dsp_api.db_r, sizeof(*dsp_api.db_r));

		/* TSM30 does it (really needed ?):
		 * Set crc result as "SB not found". */
		dsp_api.db_r->a_sch[0] = (1<<B_SCH_CRC);   /* B_SCH_CRC =1, BLUD =0 */

		dsp_api.r_page ^= 1;
	}

	//dsp_end_scenario();
}

/* ABORT command ********************************************************/

static int l1s_abort_cmd(uint16_t p1, uint16_t p2)
{
	putchart('A');

	/* similar to l1s_reset_hw() without touching the TPU */

	dsp_api.w_page = 0;
	dsp_api.r_page = 0;
	dsp_api.r_page_used = 0;
	dsp_api.db_w = (T_DB_MCU_TO_DSP *) BASE_API_W_PAGE_0;
	dsp_api.db_r = (T_DB_DSP_TO_MCU *) BASE_API_R_PAGE_0;

	/* Reset task commands. */
	dsp_api.db_w->d_task_d  = NO_DSP_TASK; /* Init. RX task to NO TASK */
	dsp_api.db_w->d_task_u  = NO_DSP_TASK; /* Init. TX task to NO TASK */
	dsp_api.db_w->d_task_ra = NO_DSP_TASK; /* Init. RA task to NO TASK */
	dsp_api.db_w->d_task_md = NO_DSP_TASK; /* Init. MONITORING task to NO TASK */
	dsp_api.ndb->d_dsp_page = 0;

	/* Set "b_abort" to TRUE, dsp will reset current and pending tasks */
	dsp_api.db_w->d_ctrl_system |= (1 << B_TASK_ABORT);
	return 0;
}

void l1s_dsp_abort(void)
{
	/* abort right now */
	tdma_schedule(0, &l1s_abort_cmd, 0, 0);
}

/* FCCH Burst *****************************************************************/

/* scheduler callback to issue a FB detection task to the DSP */
static int l1s_fbdet_cmd(uint16_t p1, uint16_t fb_mode)
{
	if (fb_mode == 0) {
		putchart('F');
	} else {
		putchart('V');
	}

	/* Program DSP */
	l1s.task = dsp_api.db_w->d_task_md = FB_DSP_TASK;	/* maybe with I/Q swap? */
	dsp_api.ndb->d_fb_mode = fb_mode;
	dsp_end_scenario();
	last_task_fnr = dsp_api.frame_ctr;

	/* Program TPU */
	l1s_rx_win_ctrl(rf_arfcn, L1_RXWIN_FB);
	tpu_end_scenario();

	return 0;
}


/* scheduler callback to check for a FB detection response */
static int l1s_fbdet_resp(uint16_t p1, uint16_t attempt)
{
	int ntdma, qbits, fn_offset;

	putchart('f');

	if (!dsp_api.ndb->d_fb_det) {
		/* we did not detect a FB, fall back to mode 0! */
		if (attempt == 12) {
			/* If we don't reset here, we get DSP DMA errors */
			tdma_sched_reset();

			/* if we are already synchronized initially */
			if (fb_once == 1)
				l1s_fb_test(1, 1);
			else
				l1s_fb_test(1, 0);
		}
		return 0;
	}

	printf("FB%u ", dsp_api.ndb->d_fb_mode);
	read_fb_result(attempt);

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
			printf("=> DSP reports FB in bit that is %d bits in the future?!?\n",
				last_fb->toa - bits_delta);
		else {
			int fb_fnr = last_task_fnr + last_fb->toa/BITS_PER_TDMA;
			printf("=>FB @ FNR %u fn_offset=%d qbits=%u\n", fb_fnr, fn_offset, qbits);
		}
	}

	if (dsp_api.frame_ctr > 500 && fb_once == 0) {
		/* Don't synchronize_tdma() yet, it does probably not work
		 * reliable due to the TPU reset) */
		l1s_reset_hw();
		tdma_sched_reset();
		fb_once = 1;
	} else {
		/* We found a frequency burst, reset everything and start next task */
		l1s_reset_hw();
		tdma_sched_reset();
	}

#if 1
	/* restart a SB or new FB detection task */
	if (dsp_api.frame_ctr > 1000 && fb_once == 1 &&
	    abs(last_fb->freq_diff) < 1000)  {
		int delay;

		/* synchronize before reading SB */
		synchronize_tdma(&l1s.serving_cell);

		delay = fn_offset + 11 - l1s.current_time.fn - 1;
		dsp_api.ndb->d_fb_det = 0;
		dsp_api.ndb->a_sync_demod[D_TOA] = 0; /* TSM30 does it (really needed ?) */
		fb_once = 0;
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

#define SCHED_ITEM(x, y, z)		{ .cb = x, .p1 = y, .p2 = z }
#define SCHED_END_FRAME()		{ .cb = NULL, .p1 = 0, .p2 = 0 }

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
};

void l1s_fb_test(uint8_t base_fn, uint8_t fb_mode)
{
#if 1
	int i;
	/* schedule the FB detection command */
	tdma_schedule(base_fn, &l1s_fbdet_cmd, 0, fb_mode);

	/* schedule 12 attempts to read the result */
	for (i = 1; i <= 12; i++) {
		uint8_t fn = base_fn + 1 + i;
		tdma_schedule(fn, &l1s_fbdet_resp, 0, i);
	}
#else
	/* use the new scheduler 'set' and simply schedule the whole set */
	/* WARNING: we cannot set FB_MODE_1 this way !!! */
	tdma_schedule_set(base_fn, fb_sched_set, ARRAY_SIZE(fb_sched_set));
#endif
}

/* SCH Burst Detection ********************************************************/

static int sb_once = 0;

static uint8_t sb_cnt;

/* Note: When we get the SB response, it is 2 TDMA frames after the SB
 * actually happened, as it is a "C W W R" task */
#define SB2_LATENCY	2

static int l1s_sbdet_resp(uint16_t p1, uint16_t attempt)
{
	uint32_t sb;
	uint8_t bsic;
	static struct gsm_time sb_time;
	int qbits, fn_offset;
	struct l1_cell_info *cinfo = &l1s.serving_cell;
	int fnr_delta, bits_delta;
	struct l1_sync_new_ccch_resp *l1;
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

	sb_cnt++;

	printf("SB%d ", attempt);
	read_sb_result(dsp_api.frame_ctr);

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
		printf("=> DSP reports SB in bit that is %d bits in the future?!?\n",
			last_fb->toa - bits_delta);
	else
		printf(" qbits=%u\n", qbits);

	if (sb_cnt > 5 && sb_once == 0) {
		synchronize_tdma(&l1s.serving_cell);
		sb_once = 1;
	}

	/* if we have recived a SYNC burst, update our local GSM time */
	gsm_fn2gsmtime(&l1s.current_time, sb_time.fn + SB2_LATENCY);
	/* compute next time from new current time */
	l1s.next_time = l1s.current_time;
	l1s_time_inc(&l1s.next_time, 1);

	/* place it in the queue for the layer2 */
	msg = l1_create_l2_msg(SYNC_NEW_CCCH_RESP, sb_time.fn, last_fb->snr);
	l1 = (struct l1_sync_new_ccch_resp *) msgb_put(msg, sizeof(*l1));
	l1->bsic = bsic;
	l1_queue_for_l2(msg);

#if 0
	tdma_sched_reset();
#else
	/*
		If we call tdma_sched_reset(), which is only needed if there are
		further l1s_sbdet_resp() scheduled, we will bring dsp_api.db_r and
		dsp_api.db_w out of sync because we changed dsp_api.db_w for l1s_sbdet_cmd()
		and canceled l1s_sbdet_resp() which would change dsp_api.db_r. The DSP
		however expects dsp_api.db_w and dsp_api.db_r to be in sync (either 
		"0 - 0" or "1 - 1"). So we have to bring dsp_api.db_w and dsp_api.db_r
		into sync again, otherwise NB reading will complain. We probably don't
		need the Abort command and could just bring dsp_api.db_w and dsp_api.db_r
		into sync.
	*/
	if (attempt != 2) {
		tdma_sched_reset();
		l1s_dsp_abort();
	}
#endif
	if (sb_cnt > 10 && sb_time.t3 == 41) {
		l1s_reset_hw();
		/* current t3 == 43, need to start NB detection in t3 = 1, difference is 9 */
		l1s_nb_test(9);
	} else {
		/* We have just seen a SCH burst, we know the next one is not in
		 * less than 7 TDMA frames from now */
		l1s_sb_test(7);
	}

	return 0;
}

static int l1s_sbdet_cmd(uint16_t p1, uint16_t p2)
{
	putchart('S');

	l1s.task = dsp_api.db_w->d_task_md = SB_DSP_TASK;
	dsp_api.ndb->d_fb_mode = 0; /* wideband search */
	dsp_end_scenario();

	last_task_fnr = dsp_api.frame_ctr;

	/* Program TPU */
	l1s_rx_win_ctrl(rf_arfcn, L1_RXWIN_SB);
	tpu_end_scenario();

	return 0;
}

void l1s_sb_test(uint8_t base_fn)
{
#if 1
	/* This is how it is done by the TSM30 */
	tdma_schedule(base_fn, &l1s_sbdet_cmd, 0, 1);
	tdma_schedule(base_fn + 1, &l1s_sbdet_cmd, 0, 2);
	tdma_schedule(base_fn + 3, &l1s_sbdet_resp, 0, 1);
	tdma_schedule(base_fn + 4, &l1s_sbdet_resp, 0, 2);
#else
	tdma_schedule(base_fn, &l1s_sbdet_cmd, 0, 1);
	tdma_schedule(base_fn + 1, &l1s_sbdet_resp, 0, 1);
	tdma_schedule(base_fn + 2, &l1s_sbdet_resp, 0, 2);
#endif
}

/* Power Measurement **********************************************************/

/* scheduler callback to issue a power measurement task to the DSP */
static int l1s_pm_cmd(uint16_t p1, uint16_t arfcn)
{
	putchart('P');

	l1s.task = dsp_api.db_w->d_task_md = 2;
	dsp_api.ndb->d_fb_mode = 0; /* wideband search */
	dsp_end_scenario();
	last_task_fnr = dsp_api.frame_ctr;

	/* Program TPU */
	//l1s_rx_win_ctrl(arfcn, L1_RXWIN_PW);
	l1s_rx_win_ctrl(arfcn, L1_RXWIN_NB);
	tpu_end_scenario();

	return 0;
}

/* scheduler callback to read power measurement resposnse from the DSP */
static int l1s_pm_resp(uint16_t p1, uint16_t p2)
{
	uint16_t pm_level[2];
	struct l1_signal sig;

	putchart('p');

	l1ddsp_meas_read(2, pm_level);

	printd("PM MEAS: %-4d dBm, %-4d dBm ARFCN=%u\n", 
		agc_inp_dbm8_by_pm(pm_level[0])/8,
		agc_inp_dbm8_by_pm(pm_level[1])/8, rf_arfcn);

	/* build and deliver signal */
	sig.signum = L1_SIG_PM;
	sig.arfcn = rf_arfcn;
	sig.pm.dbm8[0] = agc_inp_dbm8_by_pm(pm_level[0]);
	sig.pm.dbm8[1] = agc_inp_dbm8_by_pm(pm_level[1]);

	if (l1s_cb)
		l1s_cb(&sig);

	return 0;
}

void l1s_pm_test(uint8_t base_fn, uint16_t arfcn)
{
	tdma_schedule(base_fn, &l1s_pm_cmd, 0, arfcn);
	tdma_schedule(base_fn + 2, &l1s_pm_resp, 0, 0);
}

/* Normal Burst ***************************************************************/

static int l1s_nb_resp(uint16_t p1, uint16_t burst_id)
{
	static struct l1_signal _nb_sig, *sig = &_nb_sig;
	struct msgb *msg;

	putchart('n');

	/* just for debugging, d_task_d should not be 0 */
	if (dsp_api.db_r->d_task_d == 0) {
		puts("EMPTY\n");
		return 0;
	}

	/* DSP burst ID needs to corespond with what we expect */
	if (dsp_api.db_r->d_burst_d != burst_id) {
		puts("BURST ID\n");
		return 0;
	}

	sig->nb.meas[burst_id].toa_qbit = dsp_api.db_r->a_serv_demod[D_TOA];
	sig->nb.meas[burst_id].pm_dbm8 = dsp_api.db_r->a_serv_demod[D_PM] >> 3;
	sig->nb.meas[burst_id].freq_err = ANGLE_TO_FREQ(dsp_api.db_r->a_serv_demod[D_ANGLE]);
	sig->nb.meas[burst_id].snr = dsp_api.db_r->a_serv_demod[D_SNR];

	/* feed computed frequency error into AFC loop */
	if (sig->nb.meas[burst_id].snr > AFC_SNR_THRESHOLD)
		afc_input(sig->nb.meas[burst_id].freq_err, rf_arfcn, 1);
	else
		afc_input(sig->nb.meas[burst_id].freq_err, rf_arfcn, 0);

	/* 4th burst, get frame data */
	if (dsp_api.db_r->d_burst_d == 3) {
		struct l1_info_dl *dl;
		struct l1_ccch_info_ind *l1;
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
		msg = l1_create_l2_msg(CCCH_INFO_IND, l1s.current_time.fn-4, last_fb->snr);
		dl = (struct l1_info_dl *) msg->data;
		l1 = (struct l1_ccch_info_ind *) msgb_put(msg, sizeof(*l1));

		/* copy the snr and data */
		for (i = 0; i < 3; ++i)
			dl->snr[i] = sig->nb.meas[i].snr;
		for (i = 0; i < 23; ++i)
			l1->data[i] = sig->nb.frame[i];
		l1_queue_for_l2(msg);

		/* clear downlink task */
		l1s.task = dsp_api.db_w->d_task_d = 0;

		l1s_sb_test(4);
	}

	/* mark READ page as being used */
	dsp_api.r_page_used = 1;

	return 0;
}

static int l1s_nb_cmd(uint16_t p1, uint16_t burst_id)
{
	uint8_t tsc = l1s.serving_cell.bsic & 0x7;

	putchart('N');
	dsp_load_rx_task(ALLC_DSP_TASK, burst_id, tsc);
	dsp_end_scenario();

	l1s_rx_win_ctrl(rf_arfcn, L1_RXWIN_NB);
	tpu_end_scenario();

	return 0;
}

static const struct tdma_sched_item nb_sched_set[] = {
	SCHED_ITEM(l1s_nb_cmd, 0, 0),					SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_cmd, 0, 1),					SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_resp, 0, 0), SCHED_ITEM(l1s_nb_cmd, 0, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_nb_resp, 0, 1), SCHED_ITEM(l1s_nb_cmd, 0, 3),	SCHED_END_FRAME(),
				       SCHED_ITEM(l1s_nb_resp, 0, 2),	SCHED_END_FRAME(),
				       SCHED_ITEM(l1s_nb_resp, 0, 3),	SCHED_END_FRAME(),
};

void l1s_nb_test(uint8_t base_fn)
{
	puts("Starting NB\n");
	tdma_schedule_set(base_fn, nb_sched_set, ARRAY_SIZE(nb_sched_set));
}

/* Interrupt handler */
static void frame_irq(enum irq_nr nr)
{
	l1_sync();
}

void l1s_init(void)
{
	/* register FRAME interrupt as FIQ so it can interrupt normal IRQs */
	irq_register_handler(IRQ_TPU_FRAME, &frame_irq);
	irq_config(IRQ_TPU_FRAME, 1, 1, 0);
	irq_enable(IRQ_TPU_FRAME);

	/* configure timer 1 to be auto-reload and have a prescale of 12 (13MHz/12 == qbit clock) */
	hwtimer_enable(1, 1);
	hwtimer_load(1, (1875*4)-1);
	hwtimer_config(1, 0, 1);
	hwtimer_enable(1, 1);
}

