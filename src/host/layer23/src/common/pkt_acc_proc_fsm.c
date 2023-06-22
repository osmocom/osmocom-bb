/* Packet Access Procedure FSM
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/lienses/>.
 *
 */

#include <stdbool.h>

/* gsm48_rr.c:
 * gsm48_rr_est_req()
 *	gsm48_rr_chan_req()
 *		gsm48_rr_tx_rand_acc()
 */


int pkt_acc_proc_start(struct pkt_acc_proc_fsm_ctx *ctx, uint8_t chan_req)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	OSMO_ASSERT(rr->state == GSM48_RR_ST_IDLE);

	if (!cs->sel_si.si1 || !cs->sel_si.si13)
		return -EAGAIN;
	if (!cs->sel_si.gprs.supported)
		return -ENOTSUP;

	rr->cr_ra = chan_req;
	memset(&rr->cr_hist[0], 0x00, sizeof(rr->cr_hist));

	LOGP(DRR, LOGL_NOTICE, "Sending CHANNEL REQUEST (0x%02x)\n", rr->cr_ra);
	l1ctl_tx_rach_req(ms, RSL_CHAN_RACH, 0x00, rr->cr_ra, 0,
			  cs->ccch_mode == CCCH_MODE_COMBINED);

	rr->state = GSM48_RR_ST_CONN_PEND;
}
