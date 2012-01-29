/* Layer 1 Power Measurement */

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
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/msgb.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>
#include <asm/system.h>

#include <layer1/sync.h>
#include <layer1/agc.h>
#include <layer1/tdma_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>
#include <layer1/prim.h>
#include <rffe.h>

#include <l1ctl_proto.h>

static void l1ddsp_meas_read(uint8_t nbmeas, uint16_t *pm)
{
	uint8_t i;

	for (i = 0; i < nbmeas; i++)
		pm[i] = (uint16_t) ((dsp_api.db_r->a_pm[i] & 0xffff) >> 3);
	dsp_api.r_page_used = 1;
}

/* scheduler callback to issue a power measurement task to the DSP */
static int l1s_pm_cmd(uint8_t num_meas,
		      __unused uint8_t p2, uint16_t arfcn)
{
	putchart('P');

	dsp_api.db_w->d_task_md = num_meas; /* number of measurements */
	dsp_api.ndb->d_fb_mode = 0; /* wideband search */

	/* Tell the RF frontend to set the gain appropriately */
	rffe_compute_gain(-85, CAL_DSP_TGT_BB_LVL);

	/* Program TPU */
	/* FIXME: RXWIN_PW needs to set up multiple times in case
	 * num_meas > 1 */
	l1s_rx_win_ctrl(arfcn, L1_RXWIN_PW, 0);
	//l1s_rx_win_ctrl(arfcn, L1_RXWIN_NB);

	return 0;
}

/* scheduler callback to read power measurement resposnse from the DSP */
static int l1s_pm_resp(uint8_t num_meas, __unused uint8_t p2,
		       uint16_t arfcn)
{
	struct l1ctl_pm_conf *pmr;
	uint16_t pm_level[2];

	putchart('p');

	l1ddsp_meas_read(num_meas, pm_level);

	printf("PM MEAS: ARFCN=%u, %-4d dBm at baseband, %-4d dBm at RF\n",
		arfcn, pm_level[0]/8, agc_inp_dbm8_by_pm(pm_level[0])/8);

	printd("PM MEAS: %-4d dBm, %-4d dBm ARFCN=%u\n",
		agc_inp_dbm8_by_pm(pm_level[0])/8,
		agc_inp_dbm8_by_pm(pm_level[1])/8, arfcn);

	if (!l1s.pm.msg)
		l1s.pm.msg = l1ctl_msgb_alloc(L1CTL_PM_CONF);

	if (msgb_tailroom(l1s.pm.msg) < sizeof(*pmr)) {
		/* flush current msgb */
		l1_queue_for_l2(l1s.pm.msg);
		/* allocate a new msgb and initialize header */
		l1s.pm.msg = l1ctl_msgb_alloc(L1CTL_PM_CONF);
	}

	pmr = msgb_put(l1s.pm.msg, sizeof(*pmr));
	pmr->band_arfcn = htons(arfcn);
	/* FIXME: do this as RxLev rather than DBM8 ? */
	pmr->pm[0] = dbm2rxlev(agc_inp_dbm8_by_pm(pm_level[0])/8);
	if (num_meas > 1)
		pmr->pm[1] = dbm2rxlev(agc_inp_dbm8_by_pm(pm_level[1])/8);
	else
		pmr->pm[1] = 0;

	if (l1s.pm.mode == 1) {
		if (l1s.pm.range.arfcn_next != l1s.pm.range.arfcn_end) {
			/* schedule PM for next ARFCN in range */
			l1s.pm.range.arfcn_next =
				(l1s.pm.range.arfcn_next+1) & 0xfbff;
			l1s_pm_test(1, l1s.pm.range.arfcn_next);
		} else {
			/* we have finished, flush the msgb to L2 */
			struct l1ctl_hdr *l1h = l1s.pm.msg->l1h;
			l1h->flags |= L1CTL_F_DONE;
			l1_queue_for_l2(l1s.pm.msg);
			l1s.pm.msg = NULL;
		}
	}

	return 0;
}

static const struct tdma_sched_item pm_sched_set[] = {
	SCHED_ITEM_DT(l1s_pm_cmd, 0, 1, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_pm_resp, -4, 1, 0),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

/* Schedule a power measurement test */
void l1s_pm_test(uint8_t base_fn, uint16_t arfcn)
{
	unsigned long flags;

	printd("l1s_pm_test(%u, %u)\n", base_fn, arfcn);

	local_firq_save(flags);
	tdma_schedule_set(base_fn, pm_sched_set, arfcn);
	local_irq_restore(flags);
}

/*
 * perform measurements of neighbour cells
 */

/* scheduler callback to issue a power measurement task to the DSP */
static int l1s_neigh_pm_cmd(uint8_t num_meas,
		      __unused uint8_t p2, __unused uint16_t p3)
{
	uint8_t last_gain = rffe_get_gain();

	dsp_api.db_w->d_task_md = num_meas; /* number of measurements */
//	dsp_api.ndb->d_fb_mode = 0; /* wideband search */

	/* Tell the RF frontend to set the gain appropriately (keep last) */
	rffe_compute_gain(-85, CAL_DSP_TGT_BB_LVL);

	/* Program TPU */
	/* FIXME: RXWIN_PW needs to set up multiple times in case
	 * num_meas > 1 */
	/* do measurement dummy, in case l1s.neigh_pm.n == 0 */
	l1s_rx_win_ctrl((l1s.neigh_pm.n) ?
			l1s.neigh_pm.band_arfcn[l1s.neigh_pm.pos] : 0,
		L1_RXWIN_PW, l1s.neigh_pm.tn[l1s.neigh_pm.pos]);

	/* restore last gain */
	rffe_set_gain(last_gain);

	l1s.neigh_pm.running = 1;

	return 0;
}

/* scheduler callback to read power measurement resposnse from the DSP */
static int l1s_neigh_pm_resp(__unused uint8_t p1, __unused uint8_t p2,
		       __unused uint16_t p3)
{
	uint16_t dbm;
	uint8_t level;

	dsp_api.r_page_used = 1;

	if (l1s.neigh_pm.n == 0 || !l1s.neigh_pm.running)
		goto out;

	dbm = (uint16_t) ((dsp_api.db_r->a_pm[0] & 0xffff) >> 3);
	level = dbm2rxlev(agc_inp_dbm8_by_pm(dbm)/8);

	l1s.neigh_pm.level[l1s.neigh_pm.pos] = level;

	if (++l1s.neigh_pm.pos >= l1s.neigh_pm.n) {
		struct msgb *msg;
		struct l1ctl_neigh_pm_ind *mi;
		int i;

		l1s.neigh_pm.pos = 0;
		/* return result */
		msg = l1ctl_msgb_alloc(L1CTL_NEIGH_PM_IND);
		for (i = 0; i < l1s.neigh_pm.n; i++) {
			if (msgb_tailroom(msg) < (int) sizeof(*mi)) {
				l1_queue_for_l2(msg);
				msg = l1ctl_msgb_alloc(L1CTL_NEIGH_PM_IND);
			}
			mi = (struct l1ctl_neigh_pm_ind *)
				msgb_put(msg, sizeof(*mi));
			mi->band_arfcn = htons(l1s.neigh_pm.band_arfcn[i]);
			mi->tn = l1s.neigh_pm.tn[i];
			mi->pm[0] = l1s.neigh_pm.level[i];
			mi->pm[1] = 0;
		}
		l1_queue_for_l2(msg);
	}

out:
	l1s.neigh_pm.running = 0;

	return 0;
}

const struct tdma_sched_item neigh_pm_sched_set[] = {
	SCHED_ITEM_DT(l1s_neigh_pm_cmd, 0, 1, 0),	SCHED_END_FRAME(),
							SCHED_END_FRAME(),
	SCHED_ITEM(l1s_neigh_pm_resp, -4, 1, 0),	SCHED_END_FRAME(),
	SCHED_END_SET()
};

