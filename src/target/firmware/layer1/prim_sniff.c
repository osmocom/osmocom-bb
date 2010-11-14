/* Layer 1 - Sniffing Bursts */

/* (C) 2010 by Sylvain Munaut <tnt@246tNt.com>
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

// #define DEBUG
#undef DEBUG	/* Very bw hungry */

#include <stdint.h>
#include <string.h>

#include <defines.h>
#include <byteorder.h>

#include <debug.h>

#include <osmocom/gsm/gsm_utils.h>

#include <calypso/dsp.h>
#include <layer1/agc.h>
#include <layer1/l23_api.h>
#include <layer1/rfch.h>
#include <layer1/sync.h>
#include <layer1/tdma_sched.h>
#include <layer1/tpu_window.h>


/* ------------------------------------------------------------------------ */
/* Sniff TASK API                                                           */
/* ------------------------------------------------------------------------ */

#define SNIFF_DSP_TASK		23

#define BASE_API_RAM            0xffd00000
#define BASE_SNIFF_API_RAM	(BASE_API_RAM + (0x2000 - 0x800) * sizeof(uint16_t))

struct sniff_burst {
	uint16_t toa;
	uint16_t pm;
	uint16_t angle;
	uint16_t snr;
	uint16_t dummy_ind;
	uint16_t bits[29];
};

struct sniff_db {
	uint16_t w_nb;
	uint16_t r_nb;
	struct sniff_burst bursts[4];
};

struct sniff_api {
	struct sniff_db db[2];
	uint16_t db_ptr;
	uint16_t burst_ptr;
};

static inline struct sniff_db *
sniff_get_page(int r_wn /* 0=W, 1=R */)
{
	struct sniff_api *sapi = (void*)BASE_SNIFF_API_RAM;
	int idx = r_wn ? dsp_api.r_page : dsp_api.w_page;
	return &sapi->db[idx];
}


/* ------------------------------------------------------------------------ */
/* Local state                                                              */
/* ------------------------------------------------------------------------ */

struct sniff_local_db {
	uint32_t fn;
	uint8_t w_nb;
	uint8_t r_nb;
};

static struct sniff_local_db _ldbs[2];

static inline struct sniff_local_db *
sniff_get_local_page(int r_wn /* 0=W, 1=R */)
{
	struct sniff_local_db *ldb;
	int idx = r_wn ? dsp_api.r_page : dsp_api.w_page;

	/* Get page */
	ldb = &_ldbs[idx];

	/* Clear page if it's not properly in sync */
	if (!r_wn) {
		if (ldb->fn != l1s.next_time.fn) {
			memset(ldb, 0x00, sizeof(struct sniff_local_db));
			ldb->fn = l1s.next_time.fn;
		}
	}

	return ldb;
}


/* ------------------------------------------------------------------------ */
/* Sniff command & response                                                 */
/* ------------------------------------------------------------------------ */

static int
l1s_sniff_resp(uint8_t ul, uint8_t burst_id, uint16_t p3)
{
	struct sniff_db *sp = sniff_get_page(1);
	struct sniff_local_db *lsp = sniff_get_local_page(1);
	struct sniff_api *sapi = (void*)BASE_SNIFF_API_RAM;

	struct msgb *msg;
	struct l1ctl_burst_ind *bi;

	struct gsm_time rx_time;
	uint16_t rf_arfcn;
	uint8_t mf_task_id = p3 & 0xff;
	uint8_t mf_task_flags = p3 >> 8;
	uint8_t tn;

	int bidx, i;

	/* Debug */
	printd("Fn: %d - %d - lsp->w_nb: %d - lsp->r_nb: %d - sp->w_nb: %d - sp->r_nb: %d\n",
		l1s.current_time.fn-1, ul,
		sp->w_nb, lsp->r_nb,
		sp->w_nb, sp->r_nb
	);
	printd(" -> %d %04hx %04hx | %04hx %04hx %04hx %04hx %04hx\n",
		dsp_api.r_page,
		sapi->db_ptr,
		sapi->burst_ptr,
		sp->bursts[bidx].toa,
		sp->bursts[bidx].pm,
		sp->bursts[bidx].angle,
		sp->bursts[bidx].snr,
		sp->bursts[bidx].dummy_ind
	);

	for (i=0; i<29; i++)
		printd("%04hx%c", sp->bursts[bidx].bits[i], i==28?'\n':' ');

	/* Burst index in DSP response */
	bidx = lsp->r_nb++;

	/* The radio parameters for _this_ burst */
	gsm_fn2gsmtime(&rx_time, l1s.current_time.fn - 1);
	rfch_get_params(&rx_time, &rf_arfcn, NULL, &tn);

	/* Create message */
	msg = l1ctl_msgb_alloc(L1CTL_BURST_IND);
	if (!msg)
		goto exit;

	bi = (struct l1ctl_burst_ind *) msgb_put(msg, sizeof(*bi));

	/* Meta data */
		/* Time */
	bi->frame_nr = htonl(rx_time.fn);

		/* ARFCN */
	if (ul)
		rf_arfcn |= ARFCN_UPLINK;
	bi->band_arfcn = htons(rf_arfcn);

		/* Set Channel Number depending on MFrame Task ID */
	bi->chan_nr = mframe_task2chan_nr(mf_task_id, tn);

		/* Set burst id */
	bi->flags = burst_id;

		/* Set SACCH indication */
	if (mf_task_flags & MF_F_SACCH)
	        bi->flags |= BI_FLG_SACCH;

		/* Set dummy indication */
	if (sp->bursts[bidx].dummy_ind)
		bi->flags |= BI_FLG_DUMMY;

		/* DSP measurements */
	bi->rx_level = dbm2rxlev(agc_inp_dbm8_by_pm(sp->bursts[bidx].pm >> 3)>>3);
	bi->snr = (sp->bursts[bidx].snr - 1) >> 6;

	/* Pack bits */
	memset(bi->bits, 0x00, sizeof(bi->bits));

	for (i=0; i<116; i++)
	{
		int sbit  = 0x0008 << ((3 - (i & 3)) << 2);
		int sword = i >> 2;
		int dbit  = 1 << (7 - (i & 7));
		int dbyte = i >> 3;

		if (sp->bursts[bidx].bits[sword] & sbit)
			bi->bits[dbyte] |= dbit;
	}

	/* Send it ! */
	l1_queue_for_l2(msg);

exit:
	/* mark READ page as being used */
	dsp_api.r_page_used = 1;

	return 0;
}

static int
l1s_sniff_cmd(uint8_t ul, __unused uint8_t burst_id, __unused uint16_t p3)
{
	struct sniff_db *sp = sniff_get_page(0);
	struct sniff_local_db *lsp = sniff_get_local_page(0);
	uint16_t arfcn;
	uint8_t tsc, tn;

	printd("CMD: %d %d %d\n", lsp->w_nb);

	sp->w_nb = ++lsp->w_nb;
	sp->r_nb = 0;

	rfch_get_params(&l1s.next_time, &arfcn, &tsc, &tn);

	dsp_load_rx_task(SNIFF_DSP_TASK, 0, tsc);

		/* enable dummy bursts detection */
	dsp_api.db_w->d_ctrl_system |= (1 << B_BCCH_FREQ_IND);

	if (ul) {
		l1s_rx_win_ctrl(arfcn | ARFCN_UPLINK, L1_RXWIN_NB, 3);
	} else {
		l1s_rx_win_ctrl(arfcn, L1_RXWIN_NB, 0);
	}

	return 0;
}

const struct tdma_sched_item sniff_xcch_dl_sched_set[] = {
						SCHED_ITEM_DT(l1s_sniff_cmd, 0, 0, 0),	SCHED_END_FRAME(),
						SCHED_ITEM_DT(l1s_sniff_cmd, 0, 0, 1),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -5, 0, 0),	SCHED_ITEM_DT(l1s_sniff_cmd, 0, 0, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -5, 0, 1),	SCHED_ITEM_DT(l1s_sniff_cmd, 0, 0, 3),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -5, 0, 2),						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -5, 0, 3),						SCHED_END_FRAME(),
	SCHED_END_SET()
};

const struct tdma_sched_item sniff_xcch_ul_sched_set[] = {
						SCHED_ITEM_DT(l1s_sniff_cmd, 3, 1, 0),	SCHED_END_FRAME(),
						SCHED_ITEM_DT(l1s_sniff_cmd, 3, 1, 1),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -4, 1, 0),	SCHED_ITEM_DT(l1s_sniff_cmd, 3, 1, 2),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -4, 1, 1),	SCHED_ITEM_DT(l1s_sniff_cmd, 3, 1, 3),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -4, 1, 2),						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -4, 1, 3),						SCHED_END_FRAME(),
	SCHED_END_SET()
};

const struct tdma_sched_item sniff_tch_sched_set[] = {
	SCHED_ITEM_DT(l1s_sniff_cmd, 0, 0, 0),	SCHED_ITEM_DT(l1s_sniff_cmd, 3, 1, 0),	SCHED_END_FRAME(),
											SCHED_END_FRAME(),
	SCHED_ITEM(l1s_sniff_resp, -5, 0, 0),	SCHED_ITEM(l1s_sniff_resp, -4, 1, 0),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

