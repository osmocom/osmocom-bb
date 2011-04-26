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

#include <defines.h>
#include <debug.h>
#include <memory.h>
#include <byteorder.h>
#include <asm/system.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/msgb.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>

#include <abb/twl3025.h>

//#define DEBUG_EVERY_TDMA

#include <layer1/sync.h>
#include <layer1/afc.h>
#include <layer1/agc.h>
#include <layer1/apc.h>
#include <layer1/tdma_sched.h>
#include <layer1/mframe_sched.h>
#include <layer1/sched_gsmtime.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>

#include <l1ctl_proto.h>

struct l1s_state l1s;

void l1s_time_inc(struct gsm_time *time, uint32_t delta_fn)
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

void l1s_time_dump(const struct gsm_time *time)
{
	printf("fn=%lu(%u/%2u/%2u)", time->fn, time->t1, time->t2, time->t3);
}

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

#define AFC_MAX_ANGLE		328	/* 0.01 radian in fx1.15 */

/* synchronize the L1S to a new timebase (typically a new cell */
void synchronize_tdma(struct l1_cell_info *cinfo)
{
	int32_t fn_offset;
	uint32_t tpu_shift = cinfo->time_alignment;

	/* NB detection only works if the TOA of the SB
	 * is within 0...8. We have to add 75 to get an SB TOA of 4. */
	tpu_shift += 75;

	tpu_shift = (l1s.tpu_offset + tpu_shift) % QBITS_PER_TDMA;

	fn_offset = cinfo->fn_offset - 1;

	/* if we're already very close to the end of the TPU frame, the
	 * next interrupt will basically occur now and we need to
	 * compensate */
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

void l1s_reset_hw(void)
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
	tpu_enq_sync(l1s.tpu_offset);
	tpu_end_scenario();
}

/* Lost TDMA interrupt detection.  This works by starting a hardware timer
 * that is clocked by the same master clock source (VCTCXO).  We expect
 * 1875 timer ticks in the duration of a TDMA frame (5000 qbits / 1250 bits) */

/* Timer for detecting lost IRQ */
#define TIMER_TICKS_PER_TDMA	1875
#define TIMER_TICK_JITTER	1

static int last_timestamp;

static inline void check_lost_frame(void)
{
	int diff, timestamp = hwtimer_read(1);

	if (last_timestamp < timestamp)
		last_timestamp += (4*TIMER_TICKS_PER_TDMA);

	diff = last_timestamp - timestamp;

	/* allow for a bit of jitter */
	if (diff < TIMER_TICKS_PER_TDMA - TIMER_TICK_JITTER ||
	    diff > TIMER_TICKS_PER_TDMA + TIMER_TICK_JITTER)
		printf("LOST %d!\n", diff);

	last_timestamp = timestamp;
}

/* schedule a completion */
void l1s_compl_sched(enum l1_compl c)
{
	unsigned long flags;

	local_firq_save(flags);
	l1s.scheduled_compl |= (1 << c);
	local_irq_restore(flags);
}

/* main routine for synchronous part of layer 1, called by frame interrupt
 * generated by TPU once every TDMA frame */
static void l1_sync(void)
{
	uint16_t sched_flags;

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

	/* execute the sched_items that have been scheduled for this
	 * TDMA frame (including setup/cleanup steps) */
	sched_flags = tdma_sched_flag_scan();

	if (sched_flags & TDMA_IFLG_TPU)
		l1s_win_init();

	tdma_sched_execute();

	if (dsp_api.r_page_used) {
		/* clear and switch the read page */
		dsp_api_memset((uint16_t *) dsp_api.db_r,
				sizeof(*dsp_api.db_r));

		/* TSM30 does it (really needed ?):
		 * Set crc result as "SB not found". */
		dsp_api.db_r->a_sch[0] = (1<<B_SCH_CRC);   /* B_SCH_CRC =1, BLUD =0 */

		dsp_api.r_page ^= 1;
	}

	if (sched_flags & TDMA_IFLG_DSP)
		dsp_end_scenario();

	if (sched_flags & TDMA_IFLG_TPU)
		tpu_end_scenario();

	/* schedule new / upcoming TDMA items */
	mframe_schedule();
	/* schedule new / upcoming one-shot events */
	sched_gsmtime_execute(l1s.current_time.fn);

	tdma_sched_advance();
}

/* ABORT command ********************************************************/

static int l1s_abort_cmd(__unused uint8_t p1, __unused uint8_t p2,
			 __unused uint16_t p3)
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
	tdma_schedule(0, &l1s_abort_cmd, 0, 0, 0, 10);
}

void l1s_tx_apc_helper(uint16_t arfcn)
{
	int16_t auxapc;
	enum gsm_band band;
	int i;

	/* Get DAC setting */
	band = gsm_arfcn2band(arfcn);
	auxapc = apc_tx_pwrlvl2auxapc(band, l1s.tx_power);

	/* Load the ApcOffset into the DSP */
	#define  MY_OFFSET	4
	dsp_api.ndb->d_apcoff = ABB_VAL(APCOFF, (1 << 6) | MY_OFFSET) | 1; /* 2x slope for the GTA-02 ramp */

	/* Load the TX Power into the DSP */
	/*
	   If the power is too low (below 0 dBm) the ramp is not OK,
	   especially for GSM-1800. However an MS does not send below
	   0dBm anyway.
	*/
	dsp_api.db_w->d_power_ctl = ABB_VAL(AUXAPC, auxapc);

	/* Update the ramp according to the PCL */
	for (i = 0; i < 16; i++)
		dsp_api.ndb->a_ramp[i] = ABB_VAL(APCRAM, twl3025_default_ramp[i]);

	/* The Ramp Table is sent to ABB only once after RF init routine called */
	dsp_api.db_w->d_ctrl_abb |= (1 << B_RAMP) | (1 << B_BULRAMPDEL);
}

/* Interrupt handler */
static void frame_irq(__unused enum irq_nr nr)
{
	l1_sync();
}

/* reset the layer1 as part of synchronizing to a new cell */
void l1s_reset(void)
{
	/* Reset state */
	l1s.fb.mode = 0;
	l1s.tx_power = 7; /* initial power reset */

	/* Leave dedicated mode */
	l1s.dedicated.type = GSM_DCHAN_NONE;

	/* reset scheduler and hardware */
	sched_gsmtime_reset();
	mframe_reset();
	tdma_sched_reset();
	l1s_dsp_abort();

	/* Cipher off */
	dsp_load_ciph_param(0, NULL);
}

void l1s_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(l1s.tx_queue); i++)
		INIT_LLIST_HEAD(&l1s.tx_queue[i]);
	l1s.tx_meas = NULL;

	sched_gsmtime_init();

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

