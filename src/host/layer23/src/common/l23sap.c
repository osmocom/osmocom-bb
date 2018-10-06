/*
 * L23SAP (L2&3 Service Access Point), an interface between
 * L1 implementation and the upper layers (i.e. L2&3).
 *
 * (C) 2011 by Harald Welte <laforge@gnumonks.org>
 * (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <arpa/inet.h>
#include <l1ctl_proto.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/prim.h>
#include <osmocom/core/msgb.h>

#include <osmocom/gsm/lapdm.h>
#include <osmocom/gsm/rsl.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l23sap.h>

extern struct gsmtap_inst *gsmtap_inst;

int l23sap_gsmtap_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl = (struct l1ctl_info_dl *) msg->l1h;
	uint8_t chan_type, chan_ts, chan_ss;
	uint8_t gsmtap_chan_type;
	uint16_t band_arfcn;
	int8_t signal_dbm;
	uint32_t fn;

	/* FDMA / TDMA info indicated by L1 */
	band_arfcn = ntohs(dl->band_arfcn);
	signal_dbm = dl->rx_level - 110;
	fn = ntohl(dl->frame_nr);

	/* Logical channel info */
	rsl_dec_chan_nr(dl->chan_nr, &chan_type, &chan_ss, &chan_ts);
	gsmtap_chan_type = chantype_rsl2gsmtap(chan_type, dl->link_id);

	/* Send to GSMTAP */
	return gsmtap_send(gsmtap_inst, band_arfcn, chan_ts,
		gsmtap_chan_type, chan_ss, fn, signal_dbm,
		dl->snr, msg->l2h, msgb_l2len(msg));
}

int l23sap_gsmtap_data_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) msg->l1h;
	uint8_t chan_type, chan_ts, chan_ss;
	uint8_t gsmtap_chan_type;

	/* send copy via GSMTAP */
	rsl_dec_chan_nr(ul->chan_nr, &chan_type, &chan_ss, &chan_ts);
	gsmtap_chan_type = chantype_rsl2gsmtap(chan_type, ul->link_id);

	/**
	 * Send to GSMTAP
	 *
	 * FIXME: neither FDMA, not TDMA info is known here.
	 * As a possible solution, we can store an UL frame
	 * until RTS (TX confirmation) is received from PHY.
	 * This would also require to add some reference
	 * info to both UL/DL info headers. This is similar
	 * to how SIM-card related messages are handled.
	 */
	return gsmtap_send(gsmtap_inst, 0 | 0x4000, chan_ts,
		gsmtap_chan_type, chan_ss, 0, 127, 255,
		msg->l2h, msgb_l2len(msg));
}

int l23sap_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl = (struct l1ctl_info_dl *) msg->l1h;
	struct osmo_phsap_prim pp;
	struct lapdm_entity *le;

	/* Init a new DATA IND primitive */
	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_DATA,
		PRIM_OP_INDICATION, msg);
	pp.u.data.chan_nr = dl->chan_nr;
	pp.u.data.link_id = dl->link_id;

	/* Determine LAPDm entity based on SACCH or not */
	if (CHAN_IS_SACCH(dl->link_id))
		le = &ms->lapdm_channel.lapdm_acch;
	else
		le = &ms->lapdm_channel.lapdm_dcch;

	/* Send to GSMTAP */
	l23sap_gsmtap_data_ind(ms, msg);

	/* Send it up into LAPDm */
	return lapdm_phsap_up(&pp.oph, le);
}

int l23sap_data_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl = (struct l1ctl_info_dl *) msg->l1h;
	struct osmo_phsap_prim pp;
	struct lapdm_entity *le;

	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_RTS,
		PRIM_OP_INDICATION, msg);

	/* Determine LAPDm entity based on SACCH or not */
	if (CHAN_IS_SACCH(dl->link_id))
		le = &ms->lapdm_channel.lapdm_acch;
	else
		le = &ms->lapdm_channel.lapdm_dcch;

	/* Send it up into LAPDm */
	return lapdm_phsap_up(&pp.oph, le);
}

int l23sap_rach_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl = (struct l1ctl_info_dl *) msg->l1h;
	struct osmo_phsap_prim pp;

	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_RACH,
		PRIM_OP_CONFIRM, msg);
	pp.u.rach_ind.fn = ntohl(dl->frame_nr);

	/* TODO: do we really need this? */
	msg->l2h = msg->l3h = dl->payload;

	/* Send it up into LAPDm */
	return lapdm_phsap_up(&pp.oph,
		&ms->lapdm_channel.lapdm_dcch);
}
