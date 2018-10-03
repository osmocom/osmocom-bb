/*
 * L23SAP (L2&3 Service Access Point), an interface between
 * L1 implementation and the upper layers (i.e. L2&3).
 *
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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
#include <osmocom/core/signal.h>
#include <osmocom/core/prim.h>
#include <osmocom/core/msgb.h>

#include <osmocom/gsm/lapdm.h>
#include <osmocom/gsm/rsl.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23sap.h>

extern struct gsmtap_inst *gsmtap_inst;

/* Decoder for Osmocom specific chan_nr / link_id values (e.g. CBCH) */
static int l23sap_dec_chan_nr_ext(uint8_t chan_nr, uint8_t link_id,
	uint8_t *chan_type, uint8_t *chan_ts, uint8_t *chan_ss,
	uint8_t *gsmtap_chan_type)
{
	uint8_t cbits = (chan_nr >> 3);

	if ((cbits & 0x1f) == 0x18) {
		*chan_type = GSMTAP_CHANNEL_SDCCH4;
		*chan_ss = 2;
		if (gsmtap_chan_type)
			*gsmtap_chan_type = GSMTAP_CHANNEL_CBCH51;
	} else if ((cbits & 0x1f) == 0x19) {
		*chan_type = GSMTAP_CHANNEL_SDCCH8;
		if (gsmtap_chan_type)
			*gsmtap_chan_type = GSMTAP_CHANNEL_CBCH51;
	} else {
		return -ENODEV;
	}

	*chan_ts = chan_nr & 0x07;
	return 0;
}

/* Safe wrapper around rsl_dec_chan_nr() */
static int l23sap_dec_chan_nr(uint8_t chan_nr, uint8_t link_id,
	uint8_t *chan_type, uint8_t *chan_ts, uint8_t *chan_ss,
	uint8_t *gsmtap_chan_type)
{
	int rc;

	/* Attempt to decode Osmocom specific extensions */
	rc = l23sap_dec_chan_nr_ext(chan_nr, link_id,
		chan_type, chan_ts, chan_ss, gsmtap_chan_type);
	if (!rc) /* Successful decoding */
		return 0;

	/* Attempt to decode according to the specs */
	rc = rsl_dec_chan_nr(chan_nr, chan_type, chan_ss, chan_ts);
	if (rc) {
		LOGP(DL23SAP, LOGL_ERROR, "Failed to decode logical channel "
			"info (chan_nr=0x%02x, link_id=0x%02x)\n", chan_nr, link_id);
		if (gsmtap_chan_type)
			*gsmtap_chan_type = GSMTAP_CHANNEL_UNKNOWN;
		*chan_type = *chan_ss = *chan_ts = 0x00;
		return -EINVAL;
	}

	/* Pick corresponding GSMTAP channel type */
	if (gsmtap_chan_type)
		*gsmtap_chan_type = chantype_rsl2gsmtap(*chan_type, link_id);

	return 0;
}

static int l23sap_check_dl_loss(struct osmocom_ms *ms,
	struct l1ctl_info_dl *dl)
{
	struct rx_meas_stat *meas = &ms->meas;
	uint8_t chan_type, chan_ts, chan_ss;
	int rc;

	/* Update measurements */
	meas->last_fn = ntohl(dl->frame_nr);
	meas->frames++;
	meas->snr += dl->snr;
	meas->berr += dl->num_biterr;
	meas->rxlev += dl->rx_level;

	/* Attempt to decode logical channel info */
	rc = l23sap_dec_chan_nr(dl->chan_nr, dl->link_id,
		&chan_type, &chan_ts, &chan_ss, NULL);
	if (rc)
		return rc;

	/* counting loss criteria */
	if (!CHAN_IS_SACCH(dl->link_id)) {
		switch (chan_type) {
		case RSL_CHAN_PCH_AGCH:
			/* only look at one CCCH frame in each 51 multiframe.
			 * FIXME: implement DRX
			 * - select correct paging block that is for us.
			 * - initialize ds_fail according to BS_PA_MFRMS.
			 */
			if ((meas->last_fn % 51) != 6)
				break;
			if (!meas->ds_fail)
				break;
			if (dl->fire_crc >= 2)
				meas->dsc -= 4;
			else
				meas->dsc += 1;
			if (meas->dsc > meas->ds_fail)
				meas->dsc = meas->ds_fail;
			if (meas->dsc < meas->ds_fail)
				LOGP(DL23SAP, LOGL_INFO, "LOSS counter for CCCH %d\n", meas->dsc);
			if (meas->dsc > 0)
				break;
			meas->ds_fail = 0;
			osmo_signal_dispatch(SS_L1CTL, S_L1CTL_LOSS_IND, ms);
			break;
		}
	} else {
		switch (chan_type) {
		case RSL_CHAN_Bm_ACCHs:
		case RSL_CHAN_Lm_ACCHs:
		case RSL_CHAN_SDCCH4_ACCH:
		case RSL_CHAN_SDCCH8_ACCH:
			if (!meas->rl_fail)
				break;
			if (dl->fire_crc >= 2)
				meas->s -= 1;
			else
				meas->s += 2;
			if (meas->s > meas->rl_fail)
				meas->s = meas->rl_fail;
			if (meas->s < meas->rl_fail)
				LOGP(DL23SAP, LOGL_NOTICE, "LOSS counter for ACCH %d\n", meas->s);
			if (meas->s > 0)
				break;
			meas->rl_fail = 0;
			osmo_signal_dispatch(SS_L1CTL, S_L1CTL_LOSS_IND, ms);
			break;
		}
	}

	if (dl->fire_crc >= 2) {
		LOGP(DL23SAP, LOGL_NOTICE, "Dropping frame with %u bit "
			"errors\n", dl->num_biterr);
		return -EBADMSG;
	}

	return 0;
}

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

	/* Attempt to decode logical channel info */
	l23sap_dec_chan_nr(dl->chan_nr, dl->link_id,
		&chan_type, &chan_ts, &chan_ss,
		&gsmtap_chan_type);

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

	/* Attempt to decode logical channel info */
	l23sap_dec_chan_nr(ul->chan_nr, ul->link_id,
		&chan_type, &chan_ts, &chan_ss,
		&gsmtap_chan_type);

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

	/* Check for decoding errors (path loss) */
	if (l23sap_check_dl_loss(ms, dl)) {
		msgb_free(msg);
		return 0;
	}

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

/* LAPDm wants to send a PH-* primitive to the PHY (L1) */
int l23sap_lapdm_ph_prim_cb(struct osmo_prim_hdr *oph, void *ctx)
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
