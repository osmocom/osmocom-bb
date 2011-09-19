/* Glue code between L1CTL and LAPDm */

/* (C) 2011 by Harald Welte <laforge@gnumonks.org>
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

#include <l1ctl_proto.h>

#include <osmocom/gsm/prim.h>

#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/gsm/lapdm.h>

/* LAPDm wants to send a PH-* primitive to the physical layer (L1) */
int l1ctl_ph_prim_cb(struct osmo_prim_hdr *oph, void *ctx)
{
	struct osmocom_ms *ms = ctx;
	struct osmo_phsap_prim *pp = (struct osmo_phsap_prim *) oph;
	int rc = 0;

	if (oph->sap != SAP_GSM_PH)
		return -ENODEV;

	if (oph->operation != PRIM_OP_REQUEST)
		return -EINVAL;

	switch (oph->primitive) {
	case PRIM_PH_DATA:
		rc = l1ctl_tx_data_req(ms, oph->msg, pp->u.data.chan_nr,
					pp->u.data.link_id);
		break;
	case PRIM_PH_RACH:
		l1ctl_tx_param_req(ms, pp->u.rach_req.ta,
				   pp->u.rach_req.tx_power);
		rc = l1ctl_tx_rach_req(ms, pp->u.rach_req.ra,
				       pp->u.rach_req.offset,
				       pp->u.rach_req.is_combined_ccch);
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}
