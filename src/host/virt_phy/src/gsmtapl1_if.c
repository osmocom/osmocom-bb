/* GSMTAP layer1 is transmits gsmtap messages over a virtual layer 1.*/

/* (C) 2016 Sebastian Stumpf
 *
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
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <osmocom/core/gsmtap.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/core/msgb.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <l1ctl_proto.h>
#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/gsmtapl1_if.h>
#include <virtphy/logging.h>
#include <virtphy/virt_l1_sched.h>

static struct l1_model_ms *l1_model_ms = NULL;

// for debugging
static const struct value_string gsmtap_channels[22] = {
        {GSMTAP_CHANNEL_UNKNOWN, "UNKNOWN"},
        {GSMTAP_CHANNEL_BCCH, "BCCH"},
        {GSMTAP_CHANNEL_CCCH, "CCCH"},
        {GSMTAP_CHANNEL_RACH, "RACH"},
        {GSMTAP_CHANNEL_AGCH, "AGCH"},
        {GSMTAP_CHANNEL_PCH, "PCH"},
        {GSMTAP_CHANNEL_SDCCH, "SDCCH"},
        {GSMTAP_CHANNEL_SDCCH4, "SDCCH/4"},
        {GSMTAP_CHANNEL_SDCCH8, "SDCCH/8"},
        {GSMTAP_CHANNEL_TCH_F, "TCH/F/FACCH/F"},
        {GSMTAP_CHANNEL_TCH_H, "TCH/H/FACCH/H"},
        {GSMTAP_CHANNEL_PACCH, "PACCH"},
        {GSMTAP_CHANNEL_CBCH52, "CBCH"},
        {GSMTAP_CHANNEL_PDCH, "PDCH"},
        {GSMTAP_CHANNEL_PTCCH, "PTTCH"},
        {GSMTAP_CHANNEL_CBCH51, "CBCH"},
        {GSMTAP_CHANNEL_ACCH |
        GSMTAP_CHANNEL_SDCCH, "LSACCH"},
        {GSMTAP_CHANNEL_ACCH |
        GSMTAP_CHANNEL_SDCCH4, "SACCH/4"},
        {GSMTAP_CHANNEL_ACCH |
        GSMTAP_CHANNEL_SDCCH8, "SACCH/8"},
        {GSMTAP_CHANNEL_ACCH |
        GSMTAP_CHANNEL_TCH_F, "SACCH/F"},
        {GSMTAP_CHANNEL_ACCH |
        GSMTAP_CHANNEL_TCH_H, "SACCH/H"},
        {0, NULL}, };
// for debugging
static const struct value_string gsmtap_types[10] = {{
        GSMTAP_TYPE_UM,
        "GSM Um (MS<->BTS)"}, {GSMTAP_TYPE_ABIS, "GSM Abis (BTS<->BSC)"}, {
        GSMTAP_TYPE_UM_BURST,
        "GSM Um burst (MS<->BTS)"}, {GSMTAP_TYPE_SIM, "SIM"}, {
        GSMTAP_TYPE_TETRA_I1,
        "TETRA V+D"}, {GSMTAP_TYPE_WMX_BURST, "WiMAX burst"}, {
        GSMTAP_TYPE_GMR1_UM,
        "GMR-1 air interfeace (MES-MS<->GTS)"}, {
        GSMTAP_TYPE_UMTS_RLC_MAC,
        "UMTS RLC/MAC"}, {GSMTAP_TYPE_UMTS_RRC, "UMTS RRC"}, {0, NULL}, };

void gsmtapl1_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
}

/**
 * Replace l11 header of given msgb by a gsmtap header and send it over the virt um.
 */
void gsmtapl1_tx_to_virt_um_inst(uint32_t fn, struct virt_um_inst *vui,
                                 struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct gsmtap_hdr *gh;
	struct msgb *outmsg; // msg to send with gsmtap header prepended
	uint16_t arfcn = l1_model_ms->state->serving_cell.arfcn; // arfcn of the cell we currently camp on
	uint8_t signal_dbm = 63; // signal strength, 63 is best
	uint8_t snr = 63; // signal noise ratio, 63 is best
	uint8_t *data = msgb_l2(msg); // data to transmit (whole message without l1 header)
	uint8_t data_len = msgb_l2len(msg); // length of data

	uint8_t rsl_chantype; // rsl chan type (8.58, 9.3.1)
	uint8_t subslot; // multiframe subslot to send msg in (tch -> 0-26, bcch/ccch -> 0-51)
	uint8_t timeslot; // tdma timeslot to send in (0-7)
	uint8_t gsmtap_chan; // the gsmtap channel

	rsl_dec_chan_nr(ul->chan_nr, &rsl_chantype, &subslot, &timeslot);
	gsmtap_chan = chantype_rsl2gsmtap(rsl_chantype, ul->link_id);

	// arfcn needs to be flagged to be able to distinguish between uplink and downlink
	outmsg = gsmtap_makemsg(arfcn | GSMTAP_ARFCN_F_UPLINK, timeslot,
	                        gsmtap_chan, subslot, fn, signal_dbm, snr, data,
	                        data_len);
	if (outmsg) {
		outmsg->l1h = msgb_data(outmsg);
		gh = msgb_l1(outmsg);
		if (virt_um_write_msg(l1_model_ms->vui, outmsg) == -1) {
			LOGP(DVIRPHY,
			     LOGL_ERROR,
			     "Gsmtap msg could not send to virt um - (arfcn=%u, type=%u, subtype=%u, timeslot=%u, subslot=%u)\n",
			     gh->arfcn, gh->type, gh->sub_type, gh->timeslot,
			     gh->sub_slot);
		} else {
			DEBUGP(DVIRPHY,
			       "Sending gsmtap msg to virt um - (arfcn=%u, type=%u, subtype=%u, timeslot=%u, subslot=%u)\n",
			       gh->arfcn, gh->type, gh->sub_type, gh->timeslot,
			       gh->sub_slot);
		}
	} else {
		LOGP(DVIRPHY, LOGL_ERROR, "Gsmtap msg could not be created!\n");
	}

	/* free message */
	msgb_free(msg);
}

/**
 * @see void gsmtapl1_tx_to_virt_um(struct virt_um_inst *vui, uint32_t fn, struct msgb *msg).
 */
void gsmtapl1_tx_to_virt_um(uint32_t fn, struct msgb *msg)
{
	gsmtapl1_tx_to_virt_um_inst(fn, l1_model_ms->vui, msg);
}

/**
 * @see virt_prim_fbsb.c
 */
extern void prim_fbsb_sync(struct msgb *msg);

/**
 * @see virt_prim_pm.c
 */
extern uint16_t prim_pm_set_sig_strength(uint16_t arfcn, int16_t sig_lev);

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
	if (!msg) {
		return;
	}

	struct gsmtap_hdr *gh = msgb_l1(msg);
	uint32_t fn = ntohl(gh->frame_number); // frame number of the rcv msg
	uint16_t arfcn = ntohs(gh->arfcn); // arfcn of the received msg
	uint8_t gsmtap_chantype = gh->sub_type; // gsmtap channel type
	uint8_t signal_dbm = dbm2rxlev(prim_pm_set_sig_strength(arfcn & GSMTAP_ARFCN_MASK, MAX_SIG_LEV_DBM)); // Power measurement with each received massage
	uint8_t snr = gh->snr_db; // signal noise ratio, 63 is best
	uint8_t subslot = gh->sub_slot; // multiframe subslot to send msg in (tch -> 0-26, bcch/ccch -> 0-51)
	uint8_t timeslot = gh->timeslot; // tdma timeslot to send in (0-7)
	uint8_t rsl_chantype; // rsl chan type (8.58, 9.3.1)
	uint8_t link_id; // rsl link id tells if this is an ssociated or dedicated link
	uint8_t chan_nr; // encoded rsl channel type, timeslot and mf subslot

	// generally ignore all uplink messages received
	if (arfcn & GSMTAP_ARFCN_F_UPLINK) {
		LOGP(DVIRPHY, LOGL_NOTICE,
		     "Ignoring gsmtap msg from virt um - uplink flag set!\n");
		goto freemsg;
	}

	// we do not forward messages to l23 if we are in network search state
	if (l1_model_ms->state->state == MS_STATE_IDLE_SEARCHING) {
		goto freemsg;
	}

	// forward downlink msg to fbsb sync routine if we are in sync state
	if (l1_model_ms->state->state == MS_STATE_IDLE_SYNCING) {
		prim_fbsb_sync(msg);
		return;
	}

	// generally ignore all messages coming from another arfcn than the camped one
	if (l1_model_ms->state->serving_cell.arfcn != arfcn) {
		LOGP(DVIRPHY,
		     LOGL_NOTICE,
		     "Ignoring gsmtap msg from virt um - msg arfcn=%d not equal synced arfcn=%d!\n",
		     arfcn,
		     l1_model_ms->state->serving_cell.arfcn);
		goto freemsg;
	}

	msg->l2h = msgb_pull(msg, sizeof(*gh));
	chantype_gsmtap2rsl(gsmtap_chantype, &rsl_chantype, &link_id);
	// see GSM 8.58 -> 9.3.1 for channel number encoding
	chan_nr = rsl_enc_chan_nr(rsl_chantype, subslot, timeslot);

	gsm_fn2gsmtime(&l1_model_ms->state->downlink_time, fn);
	virt_l1_sched_sync_time(l1_model_ms->state->downlink_time, 0);
	virt_l1_sched_execute(fn);

	DEBUGP(DVIRPHY,
	       "Receiving gsmtap msg from virt um - (arfcn=%u, framenumber=%u, type=%s, subtype=%s, timeslot=%u, subslot=%u, rsl_chan_type=0x%2x, link_id=0x%2x, chan_nr=0x%2x)\n",
	       arfcn, fn, get_value_string(gsmtap_types, gh->type),
	       get_value_string(gsmtap_channels, gsmtap_chantype), timeslot,
	       subslot, rsl_chantype, link_id, chan_nr);

	// switch case with removed acch flag
	switch (gsmtap_chantype & ~GSMTAP_CHANNEL_ACCH & 0xff) {
	case GSMTAP_CHANNEL_TCH_H:
	case GSMTAP_CHANNEL_TCH_F:
#if(0)
		// TODO: handle msgs on TCH that are neither FACCH nor TCH/ACCH
		if(!facch && ! tch_acch) {
			l1ctl_tx_traffic_ind(msg, arfcn, link_id, chan_nr, fn,
					snr, signal_dbm, 0, 0);
		}
#endif
	case GSMTAP_CHANNEL_SDCCH4:
	case GSMTAP_CHANNEL_SDCCH8:
		// only forward messages on dedicated channels to l2, if the timeslot and subslot is fitting
		if(l1_model_ms->state->dedicated.tn == timeslot && l1_model_ms->state->dedicated.subslot == subslot) {
			l1ctl_tx_data_ind(msg, arfcn, link_id, chan_nr, fn, snr,
					                  signal_dbm, 0, 0);
		}
		break;
	case GSMTAP_CHANNEL_AGCH:
	case GSMTAP_CHANNEL_PCH:
	case GSMTAP_CHANNEL_BCCH:
		// save to just forward here, as upper layer ignores messages that do not fit the current state (e.g. gsm48_rr.c:2159)
		l1ctl_tx_data_ind(msg, arfcn, link_id, chan_nr, fn, snr,
						  signal_dbm, 0, 0);
		break;
	case GSMTAP_CHANNEL_RACH:
		LOGP(DVIRPHY,
		     LOGL_NOTICE,
		     "Ignoring gsmtap msg from virt um - channel type is uplink only!\n");
		break;
	case GSMTAP_CHANNEL_SDCCH:
	case GSMTAP_CHANNEL_CCCH:
	case GSMTAP_CHANNEL_PACCH:
	case GSMTAP_CHANNEL_PDCH:
	case GSMTAP_CHANNEL_PTCCH:
	case GSMTAP_CHANNEL_CBCH51:
	case GSMTAP_CHANNEL_CBCH52:
		LOGP(DVIRPHY,
		     LOGL_NOTICE,
		     "Ignoring gsmtap msg from virt um - channel type not supported!\n");
		break;
	default:
		LOGP(DVIRPHY,
		     LOGL_NOTICE,
		     "Ignoring gsmtap msg from virt um - channel type unknown.\n");
		break;
	}

	freemsg:
	// handle memory deallocation
	talloc_free(msg);
}

/**
 * @see void gsmtapl1_rx_from_virt_um_cb(struct virt_um_inst *vui, struct msgb msg).
 */
void gsmtapl1_rx_from_virt_um(struct msgb *msg)
{
	gsmtapl1_rx_from_virt_um_inst_cb(l1_model_ms->vui, msg);
}
