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
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/core/msgb.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <l1ctl_proto.h>

#include "virtual_um.h"
#include "l1ctl_sock.h"
#include "virt_l1_model.h"
#include "l1ctl_sap.h"
#include "gsmtapl1_if.h"
#include "logging.h"

static struct l1_model_ms *l1_model_ms = NULL;

// for debugging
static const struct value_string gsmtap_channels [22] = {
	{ GSMTAP_CHANNEL_UNKNOWN,	"UNKNOWN" },
	{ GSMTAP_CHANNEL_BCCH,		"BCCH" },
	{ GSMTAP_CHANNEL_CCCH,		"CCCH" },
	{ GSMTAP_CHANNEL_RACH,		"RACH" },
	{ GSMTAP_CHANNEL_AGCH,		"AGCH" },
	{ GSMTAP_CHANNEL_PCH,		"PCH" },
	{ GSMTAP_CHANNEL_SDCCH,		"SDCCH" },
	{ GSMTAP_CHANNEL_SDCCH4,	"SDCCH/4" },
	{ GSMTAP_CHANNEL_SDCCH8,	"SDCCH/8" },
	{ GSMTAP_CHANNEL_TCH_F,		"FACCH/F" },
	{ GSMTAP_CHANNEL_TCH_H,		"FACCH/H" },
	{ GSMTAP_CHANNEL_PACCH,		"PACCH" },
	{ GSMTAP_CHANNEL_CBCH52,    	"CBCH" },
	{ GSMTAP_CHANNEL_PDCH,      	"PDCH" },
	{ GSMTAP_CHANNEL_PTCCH,    	"PTTCH" },
	{ GSMTAP_CHANNEL_CBCH51,    	"CBCH" },
        { GSMTAP_CHANNEL_ACCH|
	  GSMTAP_CHANNEL_SDCCH,		"LSACCH" },
	{ GSMTAP_CHANNEL_ACCH|
	  GSMTAP_CHANNEL_SDCCH4,	"SACCH/4" },
	{ GSMTAP_CHANNEL_ACCH|
	  GSMTAP_CHANNEL_SDCCH8,	"SACCH/8" },
	{ GSMTAP_CHANNEL_ACCH|
	  GSMTAP_CHANNEL_TCH_F,		"SACCH/F" },
	{ GSMTAP_CHANNEL_ACCH|
	  GSMTAP_CHANNEL_TCH_H,		"SACCH/H" },
	{ 0,				NULL },
};
// for debugging
static const struct value_string gsmtap_types [10] = {
	{ GSMTAP_TYPE_UM,		"GSM Um (MS<->BTS)" },
	{ GSMTAP_TYPE_ABIS,		"GSM Abis (BTS<->BSC)" },
	{ GSMTAP_TYPE_UM_BURST,		"GSM Um burst (MS<->BTS)" },
	{ GSMTAP_TYPE_SIM,		"SIM" },
	{ GSMTAP_TYPE_TETRA_I1, 	"TETRA V+D"},
	{ GSMTAP_TYPE_WMX_BURST,	"WiMAX burst" },
	{ GSMTAP_TYPE_GMR1_UM, 		"GMR-1 air interfeace (MES-MS<->GTS)" },
	{ GSMTAP_TYPE_UMTS_RLC_MAC,	"UMTS RLC/MAC" },
	{ GSMTAP_TYPE_UMTS_RRC,		"UMTS RRC" },
	{ 0,				NULL },
};

void gsmtapl1_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
}

/**
 * Replace l11 header of given msgb by a gsmtap header and send it over the virt um.
 */
void gsmtapl1_tx_to_virt_um_inst(struct virt_um_inst *vui, uint8_t tn, uint32_t fn, uint8_t gsmtap_chan, struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	uint8_t ss = 0;
	uint8_t *data = msgb_l2(msg); // data bits to transmit (whole message without l1 header)
	uint8_t data_len = msgb_l2len(msg);
	struct msgb *outmsg;

	outmsg = gsmtap_makemsg(l1_model_ms->state->serving_cell.arfcn, ul->chan_nr, gsmtap_chan,
	                ss, fn, 0, 0, data,
	                data_len);
	if (outmsg) {
		struct gsmtap_hdr *gh = msgb_data(msg);
		virt_um_write_msg(l1_model_ms->vui, outmsg);
		DEBUGP(DVIRPHY,
		                "Sending gsmtap msg to virt um - (arfcn=%u, type=%u, subtype=%u, timeslot=%u, subslot=%u)\n",
		                gh->arfcn, gh->type, gh->sub_type, gh->timeslot,
		                gh->sub_slot);
	} else {
		LOGP(DVIRPHY, LOGL_ERROR, "Gsmtap msg could not be created!\n");
	}

	/* free message */
	msgb_free(msg);
}

/**
 * @see void gsmtapl1_tx_to_virt_um(struct virt_um_inst *vui, uint8_t tn, uint32_t fn, uint8_t gsmtap_chan, struct msgb *msg).
 */
void gsmtapl1_tx_to_virt_um(uint8_t tn, uint32_t fn, uint8_t gsmtap_chan, struct msgb *msg)
{
	gsmtapl1_tx_to_virt_um_inst(l1_model_ms->vui, tn, fn, gsmtap_chan, msg);
}

/* This is the header as it is used by gsmtap peer virtual layer 1.
struct gsmtap_hdr {
	guint8 version;		// version, set to 0x01 currently
	guint8 hdr_len;		// length in number of 32bit words
	guint8 type;		// see GSMTAP_TYPE_*
	guint8 timeslot;	// timeslot (0..7 on Um)
	guint16 arfcn;		// ARFCN (frequency)
	gint8 signal_dbm;	// signal level in dBm
	gint8 snr_db;		// signal/noise ratio in dB
	guint32 frame_number;	// GSM Frame Number (FN)
	guint8 sub_type;	// Type of burst/channel, see above
	guint8 antenna_nr;	// Antenna Number
	guint8 sub_slot;	// sub-slot within timeslot
	guint8 res;		// reserved for future use (RFU)
}
 */

/**
 * Receive a gsmtap message from the virt um.
 */
void gsmtapl1_rx_from_virt_um_inst_cb(struct virt_um_inst *vui,
                                      struct msgb *msg)
{
	if (msg) {
		// we assume we only receive msgs if we actually camp on a cell
		if (l1_model_ms->state->camping) {
			struct gsmtap_hdr *gh;
			struct l1ctl_info_dl *l1dl;
			struct msgb *l1ctl_msg = NULL;
			struct l1ctl_data_ind * l1di;

			msg->l1h = msgb_data(msg);
			msg->l2h = msgb_pull(msg, sizeof(*gh));
			gh = msgb_l1(msg);

			DEBUGP(DVIRPHY,
					"Receiving gsmtap msg from virt um - (arfcn=%u, framenumber=%u, type=%s, subtype=%s, timeslot=%u, subslot=%u)\n",
					ntohs(gh->arfcn), ntohl(gh->frame_number), get_value_string(gsmtap_types, gh->type), get_value_string(gsmtap_channels, gh->sub_type), gh->timeslot,
					gh->sub_slot);

			// compose the l1ctl message for layer 2
			switch (gh->sub_type) {
			case GSMTAP_CHANNEL_RACH:
				LOGP(DL1C, LOGL_NOTICE,
						"Ignoring gsmtap msg from virt um - channel type is uplink only!\n");
				break;
			case GSMTAP_CHANNEL_TCH_F:
				l1ctl_msg = l1ctl_msgb_alloc(L1CTL_TRAFFIC_IND);
				// TODO: implement channel handling
				break;
			case GSMTAP_CHANNEL_SDCCH:
			case GSMTAP_CHANNEL_SDCCH4:
			case GSMTAP_CHANNEL_SDCCH8:
				// TODO: we might need to implement own channel handling for standalone dedicated channels
			case GSMTAP_CHANNEL_AGCH:
			case GSMTAP_CHANNEL_PCH:
			case GSMTAP_CHANNEL_BCCH:
				l1ctl_msg = l1ctl_msgb_alloc(L1CTL_DATA_IND);
				l1dl = (struct l1ctl_info_dl *) msgb_put(l1ctl_msg, sizeof(struct l1ctl_info_dl));
				l1di = (struct l1ctl_data_ind *) msgb_put(l1ctl_msg, sizeof(struct l1ctl_data_ind));

				l1dl->band_arfcn = htons(ntohs(gh->arfcn));
				l1dl->link_id = gh->timeslot;
				// see GSM 8.58 -> 9.3.1 for channel number encoding
				l1dl->chan_nr = rsl_enc_chan_nr(chantype_gsmtap2rsl(gh->sub_type), gh->sub_slot, gh->timeslot);
				l1dl->frame_nr = htonl(ntohl(gh->frame_number));
				l1dl->snr = gh->snr_db;
				l1dl->rx_level = gh->signal_dbm;
				l1dl->num_biterr = 0;
				l1dl->fire_crc = 0;

				memcpy(l1di->data, msgb_data(msg), msgb_length(msg));

				break;
			case GSMTAP_CHANNEL_CCCH:
			case GSMTAP_CHANNEL_TCH_H:
			case GSMTAP_CHANNEL_PACCH:
			case GSMTAP_CHANNEL_PDCH:
			case GSMTAP_CHANNEL_PTCCH:
			case GSMTAP_CHANNEL_CBCH51:
			case GSMTAP_CHANNEL_CBCH52:
				LOGP(DL1C, LOGL_NOTICE,
						"Ignoring gsmtap msg from virt um - channel type not supported!\n");
				break;
			default:
				LOGP(DL1C, LOGL_NOTICE,
						"Ignoring gsmtap msg from virt um - channel type unknown.\n");
				break;
			}

			/* forward l1ctl message to l2 */
			if(l1ctl_msg) {
				l1ctl_sap_tx_to_l23(l1ctl_msg);
			}
		}
		// handle memory deallocation
		talloc_free(msg);
	}
}

/**
 * @see void gsmtapl1_rx_from_virt_um_cb(struct virt_um_inst *vui, struct msgb msg).
 */
void gsmtapl1_rx_from_virt_um(struct msgb *msg)
{
	gsmtapl1_rx_from_virt_um_inst_cb(l1_model_ms->vui, msg);
}

/*! \brief convert GSMTAP channel type to RSL channel number
 *  \param[in] gsmtap_chantype GSMTAP channel type
 *  \returns RSL channel type
 */
uint8_t chantype_gsmtap2rsl(uint8_t gsmtap_chantype)
{
	// TODO: proper retval for unknown channel
	uint8_t ret = 0;

	switch (gsmtap_chantype) {
	case GSMTAP_CHANNEL_TCH_F:
		ret = RSL_CHAN_Bm_ACCHs;
		break;
	case GSMTAP_CHANNEL_TCH_H:
		ret = RSL_CHAN_Lm_ACCHs;
		break;
	case GSMTAP_CHANNEL_SDCCH4:
		ret = RSL_CHAN_SDCCH4_ACCH;
		break;
	case GSMTAP_CHANNEL_SDCCH8:
		ret = RSL_CHAN_SDCCH8_ACCH;
		break;
	case GSMTAP_CHANNEL_BCCH:
		ret = RSL_CHAN_BCCH;
		break;
	case GSMTAP_CHANNEL_RACH:
		ret = RSL_CHAN_RACH;
		break;
	case GSMTAP_CHANNEL_PCH:
	case GSMTAP_CHANNEL_AGCH:
		ret = RSL_CHAN_PCH_AGCH;
		break;
	}

	// TODO: check how to handle this...
//	if (link_id & 0x40)
//		ret |= GSMTAP_CHANNEL_ACCH;

	return ret;
}
