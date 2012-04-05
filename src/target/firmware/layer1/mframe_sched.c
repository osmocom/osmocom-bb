/* GSM Multiframe Scheduler Implementation (on top of TDMA sched) */

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

#include <debug.h>

#include <osmocom/gsm/gsm_utils.h>

#include <layer1/prim.h>
#include <layer1/sync.h>
#include <layer1/tdma_sched.h>
#include <layer1/mframe_sched.h>

/* A multiframe operation which can be scheduled for a multiframe */
struct mframe_sched_item {
	/* The TDMA scheduler item that shall be scheduled */
	const struct tdma_sched_item *sched_set;
	/* Which modulo shall be used on the frame number */
	uint16_t modulo;
	/* At which number inside the modulo shall we be scheduled */
	uint16_t frame_nr;
	/* bit-mask of flags */
	uint16_t flags;
};

/* FIXME: properly clean this up */
#define NB_QUAD_DL	nb_sched_set
#define NB_QUAD_FH_DL	NB_QUAD_DL
#define NB_QUAD_UL	nb_sched_set_ul
#define NB_QUAD_FH_UL	NB_QUAD_UL
#define NEIGH_PM	neigh_pm_sched_set

/* BCCH Normal */
static const struct mframe_sched_item mf_bcch_norm[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 2 },
	{ .sched_set = NULL }
};

/* BCCH Extended */
static const struct mframe_sched_item mf_bcch_ext[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 6 },
	{ .sched_set = NULL }
};

/* Full CCCH in a pure BCCH + CCCH C0T0 */
static const struct mframe_sched_item mf_ccch[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 6 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 12 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 16 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 22 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 26 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 32 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 36 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 42 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 46 },
	{ .sched_set = NULL }
};

/* Full CCCH in a combined CCCH on C0T0 */
static const struct mframe_sched_item mf_ccch_comb[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 6 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 12 },
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 16 },
	{ .sched_set = NULL }
};

/* SDCCH/4 in a combined CCCH on C0T0, cannot be FH */
static const struct mframe_sched_item mf_sdcch4_0[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 22 },
	{ .sched_set = NB_QUAD_UL, .modulo = 51, .frame_nr = 22+15 },
	{ .sched_set = NB_QUAD_DL, .modulo = 2*51, .frame_nr = 42,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_UL, .modulo = 2*51, .frame_nr = 42+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch4_1[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 26 },
	{ .sched_set = NB_QUAD_UL, .modulo = 51, .frame_nr = 26+15 },
	{ .sched_set = NB_QUAD_DL, .modulo = 2*51, .frame_nr = 46,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_UL, .modulo = 2*51, .frame_nr = 46+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch4_2[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 32 },
	{ .sched_set = NB_QUAD_UL, .modulo = 51, .frame_nr = 32+15 },
	{ .sched_set = NB_QUAD_DL, .modulo = 2*51, .frame_nr = 51+42,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_UL, .modulo = 2*51, .frame_nr = 51+42+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch4_3[] = {
	{ .sched_set = NB_QUAD_DL, .modulo = 51, .frame_nr = 36 },
	{ .sched_set = NB_QUAD_UL, .modulo = 51, .frame_nr = 36+15 },
	{ .sched_set = NB_QUAD_DL, .modulo = 2*51, .frame_nr = 51+46,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_UL, .modulo = 2*51, .frame_nr = 51+46+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};

/* SDCCH/8, can be frequency hopping (FH) */
static const struct mframe_sched_item mf_sdcch8_0[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 0 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 0+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 32,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 32+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch8_1[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 4 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 4+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 36,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 36+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch8_2[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 8 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 8+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 40,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 40+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch8_3[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 12 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 12+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 44,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 44+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch8_4[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 16 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 16+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 51+32,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 51+32+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch8_5[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 20 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 20+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 51+36,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 51+36+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch8_6[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 24 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 24+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 51+40,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 51+40+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_sdcch8_7[] = {
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 51, .frame_nr = 28 },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 51, .frame_nr = 28+15 },
	{ .sched_set = NB_QUAD_FH_DL, .modulo = 2*51, .frame_nr = 51+44,
	  .flags = MF_F_SACCH },
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 2*51, .frame_nr = 51+44+15,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};

/* Measurement for MF 51 C0 */
static const struct mframe_sched_item mf_neigh_pm51_c0t0[] = {
	{ .sched_set = NEIGH_PM   , .modulo = 51, .frame_nr = 0 },
	{ .sched_set = NEIGH_PM   , .modulo = 51, .frame_nr = 10 },
	{ .sched_set = NEIGH_PM   , .modulo = 51, .frame_nr = 20 },
	{ .sched_set = NEIGH_PM   , .modulo = 51, .frame_nr = 30 },
	{ .sched_set = NEIGH_PM   , .modulo = 51, .frame_nr = 40 },
	{ .sched_set = NULL }
};

/* Measurement for MF 51 */
static const struct mframe_sched_item mf_neigh_pm51[] = {
	{ .sched_set = NEIGH_PM   , .modulo = 51, .frame_nr = 50 },
	{ .sched_set = NULL }
};

/* TCH */
#define TCH	tch_sched_set
#define TCH_A	tch_a_sched_set
#define TCH_D	tch_d_sched_set

static const struct mframe_sched_item mf_tch_f_even[] = {
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  0 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  1 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  2 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  3 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  4 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  5 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  6 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  7 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  8 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  9 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr = 10 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr = 11 },
	{ .sched_set = TCH_A, .modulo = 26, .frame_nr = 12,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};

static const struct mframe_sched_item mf_tch_f_odd[] = {
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  0 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  1 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  2 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  3 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  4 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  5 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  6 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  7 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  8 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  9 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr = 10 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr = 11 },
	{ .sched_set = TCH_A, .modulo = 26, .frame_nr = 25,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};

static const struct mframe_sched_item mf_tch_h_0[] = {
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  0 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  1 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  2 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  3 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  4 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  5 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  6 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  7 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  8 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  9 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr = 10 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr = 11 },
	{ .sched_set = TCH_A, .modulo = 26, .frame_nr = 12,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};

static const struct mframe_sched_item mf_tch_h_1[] = {
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  0 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  1 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  2 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  3 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  4 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  5 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  6 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  7 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr =  8 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr =  9 },
	{ .sched_set = TCH_D, .modulo = 13, .frame_nr = 10 },
	{ .sched_set = TCH,   .modulo = 13, .frame_nr = 11 },
	{ .sched_set = TCH_A, .modulo = 26, .frame_nr = 25,
	  .flags = MF_F_SACCH },
	{ .sched_set = NULL }
};

/* Measurement for MF 26 */
static const struct mframe_sched_item mf_neigh_pm26_even[] = {
	{ .sched_set = NEIGH_PM   , .modulo = 26, .frame_nr = 25 },
	{ .sched_set = NULL }
};
static const struct mframe_sched_item mf_neigh_pm26_odd[] = {
	{ .sched_set = NEIGH_PM   , .modulo = 26, .frame_nr = 12 },
	{ .sched_set = NULL }
};

/* Test TX */
static const struct mframe_sched_item mf_tx_all_nb[] = {
	{ .sched_set = NB_QUAD_FH_UL, .modulo = 4, .frame_nr = 0 },
	{ .sched_set = NULL }
};

static const struct mframe_sched_item *sched_set_for_task[32] = {
	[MF_TASK_BCCH_NORM] = mf_bcch_norm,
	[MF_TASK_BCCH_EXT] = mf_bcch_ext,
	[MF_TASK_CCCH] = mf_ccch,
	[MF_TASK_CCCH_COMB] = mf_ccch_comb,

	[MF_TASK_SDCCH4_0] = mf_sdcch4_0,
	[MF_TASK_SDCCH4_1] = mf_sdcch4_1,
	[MF_TASK_SDCCH4_2] = mf_sdcch4_2,
	[MF_TASK_SDCCH4_3] = mf_sdcch4_3,

	[MF_TASK_SDCCH8_0] = mf_sdcch8_0,
	[MF_TASK_SDCCH8_1] = mf_sdcch8_1,
	[MF_TASK_SDCCH8_2] = mf_sdcch8_2,
	[MF_TASK_SDCCH8_3] = mf_sdcch8_3,
	[MF_TASK_SDCCH8_4] = mf_sdcch8_4,
	[MF_TASK_SDCCH8_5] = mf_sdcch8_5,
	[MF_TASK_SDCCH8_6] = mf_sdcch8_6,
	[MF_TASK_SDCCH8_7] = mf_sdcch8_7,

	[MF_TASK_TCH_F_EVEN] = mf_tch_f_even,
	[MF_TASK_TCH_F_ODD]  = mf_tch_f_odd,
	[MF_TASK_TCH_H_0]    = mf_tch_h_0,
	[MF_TASK_TCH_H_1]    = mf_tch_h_1,

	[MF_TASK_NEIGH_PM51_C0T0] = mf_neigh_pm51_c0t0,
	[MF_TASK_NEIGH_PM51] = mf_neigh_pm51,
	[MF_TASK_NEIGH_PM26E] = mf_neigh_pm26_even,
	[MF_TASK_NEIGH_PM26O] = mf_neigh_pm26_odd,

	[MF_TASK_UL_ALL_NB] = mf_tx_all_nb,
};

/* encodes a channel number according to 08.58 Chapter 9.3.1 */
uint8_t mframe_task2chan_nr(enum mframe_task mft, uint8_t ts)
{
	uint8_t cbits;

	switch (mft) {
	case MF_TASK_BCCH_NORM:
	case MF_TASK_BCCH_EXT:
		cbits = 0x10;
		break;
	case MF_TASK_CCCH:
	case MF_TASK_CCCH_COMB:
		cbits = 0x12;
		break;
	case MF_TASK_SDCCH4_0:
		cbits = 0x04 + 0;
		break;
	case MF_TASK_SDCCH4_1:
		cbits = 0x04 + 1;
		break;
	case MF_TASK_SDCCH4_2:
		cbits = 0x04 + 2;
		break;
	case MF_TASK_SDCCH4_3:
		cbits = 0x04 + 3;
		break;
	case MF_TASK_SDCCH8_0:
		cbits = 0x08 + 0;
		break;
	case MF_TASK_SDCCH8_1:
		cbits = 0x08 + 1;
		break;
	case MF_TASK_SDCCH8_2:
		cbits = 0x08 + 2;
		break;
	case MF_TASK_SDCCH8_3:
		cbits = 0x08 + 3;
		break;
	case MF_TASK_SDCCH8_4:
		cbits = 0x08 + 4;
		break;
	case MF_TASK_SDCCH8_5:
		cbits = 0x08 + 5;
		break;
	case MF_TASK_SDCCH8_6:
		cbits = 0x08 + 6;
		break;
	case MF_TASK_SDCCH8_7:
		cbits = 0x08 + 7;
		break;
	case MF_TASK_TCH_F_EVEN:
	case MF_TASK_TCH_F_ODD:
		cbits = 0x01;
		break;
	case MF_TASK_TCH_H_0:
		cbits = 0x02 + 0;
		break;
	case MF_TASK_TCH_H_1:
		cbits = 0x02 + 1;
		break;
	case MF_TASK_UL_ALL_NB:
		/* ERROR: cannot express as channel number */
		cbits = 0;
		break;
	}

	return (cbits << 3) | (ts & 0x7);
}

/* how many TDMA frame ticks should we schedule events ahead? */
#define SCHEDULE_AHEAD	2

/* how long do we need to tell the DSP in advance what we want to do? */
#define SCHEDULE_LATENCY	1

/* (test and) schedule one particular sched_item_set by means of the TDMA scheduler */
static void mframe_schedule_set(enum mframe_task task_id)
{
	const struct mframe_sched_item *set = sched_set_for_task[task_id];
	const struct mframe_sched_item *si;

	for (si = set; si->sched_set != NULL; si++) {
		unsigned int trigger = si->frame_nr % si->modulo;
		unsigned int current = (l1s.current_time.fn + SCHEDULE_AHEAD) % si->modulo;
		if (current == trigger) {
			uint32_t fn;
			int rv;

			/* Schedule the set */
			/* FIXME: what to do with SACCH Flag etc? */
			rv = tdma_schedule_set(SCHEDULE_AHEAD-SCHEDULE_LATENCY,
					  si->sched_set, task_id | (si->flags<<8));

			/* Compute the next safe time to queue a DSP command */
			fn = l1s.current_time.fn;
			ADD_MODULO(fn, rv - 2, GSM_MAX_FN); /* -2 = worst case last dsp command */
			if ((fn > l1s.mframe_sched.safe_fn) ||
			    (l1s.mframe_sched.safe_fn >= GSM_MAX_FN))
				l1s.mframe_sched.safe_fn = fn;
		}
	}
}

/* Enable a specific task */
void mframe_enable(enum mframe_task task_id)
{
	l1s.mframe_sched.tasks_tgt |= (1 << task_id);
}

/* Disable a specific task */
void mframe_disable(enum mframe_task task_id)
{
	l1s.mframe_sched.tasks_tgt &= ~(1 << task_id);
}

/* Replace the current active set by the new one */
void mframe_set(uint32_t tasks)
{
	l1s.mframe_sched.tasks_tgt = tasks;
}

/* Schedule mframe_sched_items according to current MF TASK list */
void mframe_schedule(void)
{
	unsigned int i;
	int fn_diff;

	/* Try to enable/disable task to meet target bitmap */
	fn_diff = l1s.mframe_sched.safe_fn - l1s.current_time.fn;
	if ((fn_diff <= 0) || (fn_diff >= (GSM_MAX_FN>>1)) ||
	    (l1s.mframe_sched.safe_fn >= GSM_MAX_FN))
		/* If nothing is in the way, enable new tasks */
		l1s.mframe_sched.tasks = l1s.mframe_sched.tasks_tgt;
	else
		/* Else, Disable only */
		l1s.mframe_sched.tasks &= l1s.mframe_sched.tasks_tgt;

	/* Schedule any active pending set */
	for (i = 0; i < 32; i++) {
		if (l1s.mframe_sched.tasks & (1 << i))
			mframe_schedule_set(i);
	}
}

/* reset the scheduler, disabling all tasks */
void mframe_reset(void)
{
	l1s.mframe_sched.tasks = 0;
	l1s.mframe_sched.tasks_tgt = 0;
	l1s.mframe_sched.safe_fn = -1UL;	/* Force safe */
}

