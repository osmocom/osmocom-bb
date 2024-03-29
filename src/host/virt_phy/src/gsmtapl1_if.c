/* GSMTAP layer1 is transmits gsmtap messages over a virtual layer 1.*/

/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
 * (C) 2017 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <osmocom/core/gsmtap.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/core/msgb.h>

#include <osmocom/bb/virtphy/virtual_um.h>
#include <osmocom/bb/virtphy/l1ctl_sock.h>
#include <osmocom/bb/virtphy/virt_l1_model.h>
#include <osmocom/bb/virtphy/l1ctl_sap.h>
#include <osmocom/bb/virtphy/gsmtapl1_if.h>
#include <osmocom/bb/virtphy/logging.h>
#include <osmocom/bb/virtphy/virt_l1_sched.h>
#include <osmocom/bb/l1ctl_proto.h>

static char *pseudo_lchan_name(uint16_t arfcn, uint8_t ts, uint8_t ss, uint8_t sub_type)
{
	static char lname[64];
	snprintf(lname, sizeof(lname), "(arfcn=%u,ts=%u,ss=%u,type=%s)",
		arfcn, ts, ss, get_value_string(gsmtap_gsm_channel_names, sub_type));
	return lname;
}

/* Return gsmtap_um_voice_type or -1 on error */
static int get_um_voice_type(enum gsm48_chan_mode tch_mode, uint8_t rsl_chantype)
{
	switch (tch_mode) {
	case GSM48_CMODE_SPEECH_V1:
		switch (rsl_chantype) {
		case RSL_CHAN_Bm_ACCHs:
			return GSMTAP_UM_VOICE_FR;
		case RSL_CHAN_Lm_ACCHs:
			return GSMTAP_UM_VOICE_HR;
		default:
			return -1;
		}
		break;
	case GSM48_CMODE_SPEECH_EFR:
		return GSMTAP_UM_VOICE_EFR;
	case GSM48_CMODE_SPEECH_AMR:
		return GSMTAP_UM_VOICE_AMR;
	default:
		return -1;
	}
}

/**
 * Replace l11 header of given msgb by a gsmtap header and send it over the virt um.
 */
void gsmtapl1_tx_to_virt_um_inst(struct l1_model_ms *ms, uint32_t fn, uint8_t tn, struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul;
	struct gsmtap_hdr *gh;
	struct msgb *outmsg;	/* msg to send with gsmtap header prepended */
	uint16_t arfcn;
	uint8_t signal_dbm = rxlev2dbm(63);	/* signal strength */
	uint8_t snr = 63;	/* signal noise ratio, 63 is best */
	uint8_t *data = msgb_l2(msg);	/* data to transmit (whole message without l1 header) */
	uint8_t data_len = msgb_l2len(msg);	/* length of data */

	uint8_t rsl_chantype;	/* rsl chan type (8.58, 9.3.1) */
	uint8_t subslot;	/* multiframe subslot to send msg in (tch -> 0-26, bcch/ccch -> 0-51) */
	uint8_t timeslot;	/* tdma timeslot to send in (0-7) */
	uint8_t gsmtap_chan;	/* the gsmtap channel */

	if (ms->state.state == MS_STATE_DEDICATED)
		arfcn = ms->state.dedicated.band_arfcn;
	else
		arfcn = ms->state.serving_cell.arfcn;

	switch (l1h->msg_type) {
	case L1CTL_GPRS_UL_BLOCK_REQ:
		gsmtap_chan = GSMTAP_CHANNEL_PDCH;
		timeslot = tn;
		subslot = 0;
		break;
	case L1CTL_TRAFFIC_REQ:
		ul = (struct l1ctl_info_ul *)l1h->data;
		rsl_dec_chan_nr(ul->chan_nr, &rsl_chantype, &subslot, &timeslot);
		gsmtap_chan = chantype_rsl2gsmtap2(rsl_chantype, 0, true);
		/* the first byte indicates the type of voice codec (gsmtap_um_voice_type);
		 * let's first strip any data in front of the l2 header, then push this extra
		 * byte to the front and finally adjust the l2h pointer */
		msgb_pull_to_l2(msg);
		msgb_push_u8(msg, get_um_voice_type(ms->state.tch_mode, rsl_chantype));
		msg->l2h = msg->data;
		data = msgb_l2(msg);
		data_len = msgb_l2len(msg);
		break;
	default:
		ul = (struct l1ctl_info_ul *)l1h->data;
		rsl_dec_chan_nr(ul->chan_nr, &rsl_chantype, &subslot, &timeslot);
		gsmtap_chan = chantype_rsl2gsmtap2(rsl_chantype, ul->link_id, false);
		break;
	}

	/* arfcn needs to be flagged to be able to distinguish between uplink and downlink */
	outmsg = gsmtap_makemsg(arfcn | GSMTAP_ARFCN_F_UPLINK, timeslot,
				gsmtap_chan, subslot, fn, signal_dbm, snr, data,
				data_len);
	if (outmsg) {
		outmsg->l1h = msgb_data(outmsg);
		gh = msgb_l1(outmsg);
		if (virt_um_write_msg(ms->vui, outmsg) == -1) {
			LOGPMS(DVIRPHY, LOGL_ERROR, ms, "%s Tx go GSMTAP failed: %s\n",
				pseudo_lchan_name(gh->arfcn, gh->timeslot, gh->sub_slot, gh->sub_type),
				strerror(errno));
		} else {
			DEBUGPMS(DVIRPHY, ms, "%s: Tx to GSMTAP: %s\n",
				pseudo_lchan_name(gh->arfcn, gh->timeslot, gh->sub_slot, gh->sub_type),
				osmo_hexdump(data, data_len));
		}
	} else
		LOGPMS(DVIRPHY, LOGL_ERROR, ms, "GSMTAP msg could not be created!\n");

	/* free message */
	msgb_free(msg);
}

/**
 * @see virt_prim_fbsb.c
 */
extern void prim_fbsb_sync(struct l1_model_ms *ms, struct msgb *msg);

/**
 * @see virt_prim_pm.c
 */
extern uint16_t prim_pm_set_sig_strength(struct l1_model_ms *ms, uint16_t arfcn, int16_t sig_lev);

static void l1ctl_from_virt_um(struct l1ctl_sock_client *lsc, struct msgb *msg, uint32_t fn,
				uint16_t arfcn, uint8_t timeslot, uint8_t subslot,
				uint8_t gsmtap_chantype, uint8_t chan_nr, uint8_t link_id,
				uint8_t snr_db)
{
	struct l1_model_ms *ms = lsc->priv;
	uint8_t rxlev = dbm2rxlev(prim_pm_set_sig_strength(ms, arfcn & GSMTAP_ARFCN_MASK, MAX_SIG_LEV_DBM));

	gsm_fn2gsmtime(&ms->state.downlink_time, fn);

	switch (ms->state.state) {
	case MS_STATE_IDLE_SEARCHING:
		/* we do not forward messages to l23 if we are in network search state */
		return;
	case MS_STATE_IDLE_SYNCING:
		/* forward downlink msg to fbsb sync routine if we are in sync state */
		prim_fbsb_sync(ms, msg);
		return;
	case MS_STATE_DEDICATED:
		/* generally ignore all messages coming from another arfcn than the camped one */
		if (arfcn != ms->state.dedicated.band_arfcn)
			return;
		break;
	default:
		/* generally ignore all messages coming from another arfcn than the camped one */
		if (arfcn != ms->state.serving_cell.arfcn)
			return;
		break;
	}

	virt_l1_sched_sync_time(ms, ms->state.downlink_time, 0);
	virt_l1_sched_execute(ms, fn);

	/* switch case with removed ACCH flag */
	switch (gsmtap_chantype & ~GSMTAP_CHANNEL_ACCH & 0xff) {
	case GSMTAP_CHANNEL_TCH_H:
	case GSMTAP_CHANNEL_TCH_F:
		/* This is TCH signalling, for voice frames see GSMTAP_CHANNEL_VOICE */
	case GSMTAP_CHANNEL_SDCCH4:
	case GSMTAP_CHANNEL_SDCCH8:
		/* only forward messages on dedicated channels to l2, if
		 * the timeslot and subslot is fitting */
		if (ms->state.dedicated.tn == timeslot
		    && ms->state.dedicated.subslot == subslot) {
			l1ctl_tx_data_ind(ms, msg, arfcn, link_id, chan_nr, fn, snr_db, rxlev, 0, 0);
		}
		break;
	case GSMTAP_CHANNEL_VOICE_F:
	case GSMTAP_CHANNEL_VOICE_H:
		/* only forward messages on dedicated channels to l2, if
		 * the timeslot and subslot is fitting */
		if (ms->state.dedicated.tn == timeslot
		    && ms->state.dedicated.subslot == subslot) {
			l1ctl_tx_traffic_ind(ms, msg, arfcn, link_id, chan_nr, fn,
					     snr_db, rxlev, 0, 0);
		}
		break;
	case GSMTAP_CHANNEL_CBCH51:
		/* only pass CBCH data if the user application actually indicated that a CBCH
		 * is present */
		if (ms->state.serving_cell.ccch_mode != CCCH_MODE_COMBINED_CBCH)
			break;
	case GSMTAP_CHANNEL_AGCH:
	case GSMTAP_CHANNEL_PCH:
	case GSMTAP_CHANNEL_BCCH:
	case GSMTAP_CHANNEL_CBCH52:
		/* save to just forward here, as upper layer ignores messages that
		 * do not fit the current state (e.g.  gsm48_rr.c:2159) */
		l1ctl_tx_data_ind(ms, msg, arfcn, link_id, chan_nr, fn, snr_db, rxlev, 0, 0);
		break;
	case GSMTAP_CHANNEL_RACH:
		LOGPMS(DVIRPHY, LOGL_NOTICE, ms, "Ignoring unexpected RACH in downlink ?!?\n");
		break;
	case GSMTAP_CHANNEL_PTCCH:
	case GSMTAP_CHANNEL_PACCH:
	case GSMTAP_CHANNEL_PDCH:
		l1ctl_tx_gprs_dl_block_ind(ms, msg, fn, timeslot, rxlev);
		break;
	case GSMTAP_CHANNEL_SDCCH:
	case GSMTAP_CHANNEL_CCCH:
		LOGPMS(DVIRPHY, LOGL_NOTICE, ms, "Ignoring unsupported channel type %s\n",
			get_value_string(gsmtap_gsm_channel_names, gsmtap_chantype));
		break;
	default:
		LOGPMS(DVIRPHY, LOGL_NOTICE, ms, "Ignoring unknown channel type %s\n",
			get_value_string(gsmtap_gsm_channel_names, gsmtap_chantype));
		break;
	}
}

/**
 * Receive a gsmtap message from the virt um.
 *
 * As we do not have a downlink scheduler, but not all dl messages must be processed and thus forwarded to l2, this function also implements some message filtering.
 * E.g. we do not forward:
 * - uplink messages
 * - messages with a wrong arfcn
 * - if in MS_STATE_IDLE_SEARCHING
 */
void gsmtapl1_rx_from_virt_um_inst_cb(struct virt_um_inst *vui,
				      struct msgb *msg)
{
	struct l1ctl_sock_inst *lsi = vui->priv;
	struct l1ctl_sock_client *lsc;

	if (!msg)
		return;

	struct gsmtap_hdr *gh = msgb_l1(msg);
	uint32_t fn = ntohl(gh->frame_number);	/* frame number of the rcv msg */
	uint16_t arfcn = ntohs(gh->arfcn);	/* arfcn of the received msg */
	uint8_t gsmtap_chantype = gh->sub_type;	/* gsmtap channel type */
	uint8_t snr = gh->snr_db;	/* signal noise ratio */
	uint8_t subslot = gh->sub_slot;	/* multiframe subslot to send msg in (tch -> 0-26, bcch/ccch -> 0-51) */
	uint8_t timeslot = gh->timeslot;	/* tdma timeslot to send in (0-7) */
	uint8_t rsl_chantype;	/* rsl chan type (8.58, 9.3.1) */
	uint8_t link_id;	/* rsl link id tells if this is an ssociated or dedicated link */
	uint8_t chan_nr;	/* encoded rsl channel type, timeslot and mf subslot */
	struct gsm_time gtime;

	msg->l2h = msgb_pull(msg, sizeof(*gh));
	chantype_gsmtap2rsl(gsmtap_chantype, &rsl_chantype, &link_id);
	/* see TS 08.58 -> 9.3.1 for channel number encoding */
	chan_nr = rsl_enc_chan_nr(rsl_chantype, subslot, timeslot);

	gsm_fn2gsmtime(&gtime, fn);

	DEBUGP(DVIRPHY, "%s Rx from VirtUM: FN=%s chan_nr=0x%02x link_id=0x%02x\n",
		pseudo_lchan_name(arfcn, timeslot, subslot, gsmtap_chantype),
		osmo_dump_gsmtime(&gtime), chan_nr, link_id);

	/* generally ignore all uplink messages received */
	if (arfcn & GSMTAP_ARFCN_F_UPLINK) {
		LOGP(DVIRPHY, LOGL_NOTICE, "Ignoring unexpected uplink message in downlink!\n");
		goto freemsg;
	}

	/* dispatch the incoming DL message from GSMTAP to each of the registered L1CTL instances */
	llist_for_each_entry(lsc, &lsi->clients, list) {
		l1ctl_from_virt_um(lsc, msg, fn, arfcn, timeslot, subslot, gsmtap_chantype,
				   chan_nr, link_id, snr);
	}

freemsg:
	talloc_free(msg);
}
