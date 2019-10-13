
/* (C) 2010 by Dieter Spaar <spaar@mirider.augusta.de>
 * (C) 2010,2017 by Harald Welte <laforge@gnumonks.org>
 * (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
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

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/core/msgb.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/virt_l1_sched.h>
#include <osmocom/core/gsmtap.h>
#include <virtphy/logging.h>
#include <l1ctl_proto.h>

/**
 * @brief Change the signal strength for a given arfcn.
 *
 * Should be called if a msg is received on the virtual layer. The configured signal level reduction is applied.
 *
 * @param [in] arfcn to change sig str for.
 * @param [in] sig_lev the measured signal level value.
 */
uint16_t prim_pm_set_sig_strength(struct l1_model_ms *ms, uint16_t arfcn, int16_t sig_lev)
{
	struct l1_state_ms *l1s = &ms->state;

	if (l1s->pm.timeout_s > 0 || l1s->pm.timeout_us > 0) {
		osmo_timer_schedule(&l1s->pm.meas.arfcn_sig_lev_timers[arfcn],
				    l1s->pm.timeout_s, l1s->pm.timeout_us);
	}
	l1s->pm.meas.arfcn_sig_lev_dbm[arfcn] = sig_lev - l1s->pm.meas.arfcn_sig_lev_red_dbm[arfcn];
	DEBUGPMS(DL1C, ms, "Power measurement set for arfcn %u. Set signal level to %d (== rxlev: %u).\n",
		arfcn, l1s->pm.meas.arfcn_sig_lev_dbm[arfcn],
		dbm2rxlev(l1s->pm.meas.arfcn_sig_lev_dbm[arfcn]));
	return l1s->pm.meas.arfcn_sig_lev_dbm[arfcn];
}

void prim_pm_timer_cb(void *data)
{
	/* reset the signal level to bad value if no messages have been
	 * received from that rfcn for a given time */
	DEBUGP(DL1C, "Timeout occurred for arfcn, signal level reset to worst value.\n");
	*((int16_t*)data) = MIN_SIG_LEV_DBM;
}

/**
 * @brief Handler for received L1CTL_PM_REQ from L23.
 *
 * -- power measurement request --
 *
 * @param [in] msg the received message.
 *
 * Process power measurement for a given range of arfcns to calculate
 * signal power and connection quality.
 *
 * Note: This should only be called after a certain time so some
 * messages have already been received.
 */
void l1ctl_rx_pm_req(struct l1_model_ms *ms, struct msgb *msg)
{
	struct l1_state_ms *l1s = &ms->state;
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_pm_req *pm_req = (struct l1ctl_pm_req *) l1h->data;
	struct msgb *resp_msg = l1ctl_msgb_alloc(L1CTL_PM_CONF);
	uint16_t arfcn_next;

	/* convert to host order */
	pm_req->range.band_arfcn_from = ntohs(pm_req->range.band_arfcn_from);
	pm_req->range.band_arfcn_to = ntohs(pm_req->range.band_arfcn_to);

	LOGPMS(DL1C, LOGL_INFO, ms, "Rx L1CTL_PM_REQ TYPE=%u, FROM=%d, TO=%d\n",
		pm_req->type, pm_req->range.band_arfcn_from, pm_req->range.band_arfcn_to);

	for (arfcn_next = pm_req->range.band_arfcn_from;
	     arfcn_next <= pm_req->range.band_arfcn_to; ++arfcn_next) {
		struct l1ctl_pm_conf *pm_conf = (struct l1ctl_pm_conf *) msgb_put(resp_msg, sizeof(*pm_conf));
		pm_conf->band_arfcn = htons(arfcn_next);
		/* set min and max to the value calculated for that
		 * arfcn (IGNORE UPLINKK AND  PCS AND OTHER FLAGS) */
		pm_conf->pm[0] = dbm2rxlev(l1s->pm.meas.arfcn_sig_lev_dbm[arfcn_next & ARFCN_NO_FLAGS_MASK]);
		pm_conf->pm[1] = dbm2rxlev(l1s->pm.meas.arfcn_sig_lev_dbm[arfcn_next & ARFCN_NO_FLAGS_MASK]);
		if (arfcn_next == pm_req->range.band_arfcn_to) {
			struct l1ctl_hdr *resp_l1h = msgb_l1(resp_msg);
			resp_l1h->flags |= L1CTL_F_DONE;
		}
		/* no more space to hold more pm info in msgb, flush to l23 */
		if (msgb_tailroom(resp_msg) < sizeof(*pm_conf)) {
			LOGPMS(DL1C, LOGL_INFO, ms, "Tx L1CTL_PM_CONF\n");
			l1ctl_sap_tx_to_l23_inst(ms, resp_msg);
			resp_msg = l1ctl_msgb_alloc(L1CTL_PM_CONF);
		}
	}
	/* transmit the remaining part of pm response to l23 */
	if (resp_msg) {
		LOGPMS(DL1C, LOGL_INFO, ms, "Tx L1CTL_PM_CONF\n");
		l1ctl_sap_tx_to_l23_inst(ms, resp_msg);
	}
}

/**
 * @brief Initialize virtual prim pm.
 *
 * @param [in] model the l1 model instance
 */
void prim_pm_init(struct l1_model_ms *model)
{
	struct l1_state_ms *l1s = &model->state;
	int i;

	/* init the signal level of all arfcns with the lowest value possible */
	memset(l1s->pm.meas.arfcn_sig_lev_dbm, MIN_SIG_LEV_DBM, sizeof (int16_t) * 1024);
	/* init timers */
	for (i = 0; i < 1024; ++i) {
		l1s->pm.meas.arfcn_sig_lev_timers[i].cb = prim_pm_timer_cb;
		l1s->pm.meas.arfcn_sig_lev_timers[i].data = &l1s->pm.meas.arfcn_sig_lev_dbm[i];
	}
}

void prim_pm_exit(struct l1_model_ms *model)
{
	struct l1_state_ms *l1s = &model->state;
	int i;

	for (i = 0; i < 1024; ++i)
		osmo_timer_del(&l1s->pm.meas.arfcn_sig_lev_timers[i]);
}
