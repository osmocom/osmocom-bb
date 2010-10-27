/* (C) 2009,2010 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009,2010 by On-Waves
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

#include <osmocore/gsm0808.h>
#include <osmocore/protocol/gsm_08_08.h>
#include <osmocore/gsm48.h>

#include <arpa/inet.h>

#define BSSMAP_MSG_SIZE 512
#define BSSMAP_MSG_HEADROOM 128

static void put_data_16(uint8_t *data, const uint16_t val)
{
	memcpy(data, &val, sizeof(val));
}

struct msgb *gsm0808_create_layer3(struct msgb *msg_l3, uint16_t nc, uint16_t cc, int lac, uint16_t _ci)
{
	uint8_t *data;
	uint8_t *ci;
	struct msgb* msg;
	struct gsm48_loc_area_id *lai;

	msg  = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
				   "bssmap cmpl l3");
	if (!msg)
		return NULL;

	/* create the bssmap header */
	msg->l3h = msgb_put(msg, 2);
	msg->l3h[0] = 0x0;

	/* create layer 3 header */
	data = msgb_put(msg, 1);
	data[0] = BSS_MAP_MSG_COMPLETE_LAYER_3;

	/* create the cell header */
	data = msgb_put(msg, 3);
	data[0] = GSM0808_IE_CELL_IDENTIFIER;
	data[1] = 1 + sizeof(*lai) + 2;
	data[2] = CELL_IDENT_WHOLE_GLOBAL;

	lai = (struct gsm48_loc_area_id *) msgb_put(msg, sizeof(*lai));
	gsm48_generate_lai(lai, cc, nc, lac);

	ci = msgb_put(msg, 2);
	put_data_16(ci, htons(_ci));

	/* copy the layer3 data */
	data = msgb_put(msg, msgb_l3len(msg_l3) + 2);
	data[0] = GSM0808_IE_LAYER_3_INFORMATION;
	data[1] = msgb_l3len(msg_l3);
	memcpy(&data[2], msg_l3->l3h, data[1]);

	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;

	return msg;
}

struct msgb *gsm0808_create_reset(void)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: reset");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 6);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0x04;
	msg->l3h[2] = 0x30;
	msg->l3h[3] = 0x04;
	msg->l3h[4] = 0x01;
	msg->l3h[5] = 0x20;
	return msg;
}

struct msgb *gsm0808_create_clear_complete(void)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: clear complete");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 1;
	msg->l3h[2] = BSS_MAP_MSG_CLEAR_COMPLETE;

	return msg;
}

struct msgb *gsm0808_create_clear_command(uint8_t reason)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: clear command");
	if (!msg)
		return NULL;

	msg->l3h = msgb_tv_put(msg, BSSAP_MSG_BSS_MANAGEMENT, 4);
	msgb_v_put(msg, BSS_MAP_MSG_CLEAR_CMD);
	msgb_tlv_put(msg, GSM0808_IE_CAUSE, 1, &reason);
	return msg;
}

struct msgb *gsm0808_create_cipher_complete(struct msgb *layer3, uint8_t alg_id)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "cipher-complete");
	if (!msg)
		return NULL;

        /* send response with BSS override for A5/1... cheating */
	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0xff;
	msg->l3h[2] = BSS_MAP_MSG_CIPHER_MODE_COMPLETE;

	/* include layer3 in case we have at least two octets */
	if (layer3 && msgb_l3len(layer3) > 2) {
		msg->l4h = msgb_put(msg, msgb_l3len(layer3) + 2);
		msg->l4h[0] = GSM0808_IE_LAYER_3_MESSAGE_CONTENTS;
		msg->l4h[1] = msgb_l3len(layer3);
		memcpy(&msg->l4h[2], layer3->l3h, msgb_l3len(layer3));
	}

	/* and the optional BSS message */
	msg->l4h = msgb_put(msg, 2);
	msg->l4h[0] = GSM0808_IE_CHOSEN_ENCR_ALG;
	msg->l4h[1] = alg_id;

	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;
	return msg;
}

struct msgb *gsm0808_create_cipher_reject(uint8_t cause)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: clear complete");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 2;
	msg->l3h[2] = BSS_MAP_MSG_CIPHER_MODE_REJECT;
	msg->l3h[3] = cause;

	return msg;
}

struct msgb *gsm0808_create_classmark_update(const uint8_t *classmark_data, uint8_t length)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "classmark-update");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0xff;
	msg->l3h[2] = BSS_MAP_MSG_CLASSMARK_UPDATE;

	msg->l4h = msgb_put(msg, length);
	memcpy(msg->l4h, classmark_data, length);

	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;
	return msg;
}

struct msgb *gsm0808_create_sapi_reject(uint8_t link_id)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: sapi 'n' reject");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 5);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 3;
	msg->l3h[2] = BSS_MAP_MSG_SAPI_N_REJECT;
	msg->l3h[3] = link_id;
	msg->l3h[4] = GSM0808_CAUSE_BSS_NOT_EQUIPPED;

	return msg;
}

struct msgb *gsm0808_create_assignment_completed(uint8_t rr_cause,
						 uint8_t chosen_channel, uint8_t encr_alg_id,
						 uint8_t speech_mode)
{
	uint8_t *data;

	struct msgb *msg = msgb_alloc(35, "bssmap: ass compl");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 3);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0xff;
	msg->l3h[2] = BSS_MAP_MSG_ASSIGMENT_COMPLETE;

	/* write 3.2.2.22 */
	data = msgb_put(msg, 2);
	data[0] = GSM0808_IE_RR_CAUSE;
	data[1] = rr_cause;

	/* write cirtcuit identity  code 3.2.2.2 */
	/* write cell identifier 3.2.2.17 */
	/* write chosen channel 3.2.2.33 when BTS picked it */
	data = msgb_put(msg, 2);
	data[0] = GSM0808_IE_CHOSEN_CHANNEL;
	data[1] = chosen_channel;

	/* write chosen encryption algorithm 3.2.2.44 */
	data = msgb_put(msg, 2);
	data[0] = GSM0808_IE_CHOSEN_ENCR_ALG;
	data[1] = encr_alg_id;

	/* write circuit pool 3.2.2.45 */
	/* write speech version chosen: 3.2.2.51 when BTS picked it */
	if (speech_mode != 0) {
		data = msgb_put(msg, 2);
		data[0] = GSM0808_IE_SPEECH_VERSION;
		data[1] = speech_mode;
	}

	/* write LSA identifier 3.2.2.15 */


	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;
	return msg;
}

struct msgb *gsm0808_create_assignment_failure(uint8_t cause, uint8_t *rr_cause)
{
	uint8_t *data;
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: ass fail");
	if (!msg)
		return NULL;

	msg->l3h = msgb_put(msg, 6);
	msg->l3h[0] = BSSAP_MSG_BSS_MANAGEMENT;
	msg->l3h[1] = 0xff;
	msg->l3h[2] = BSS_MAP_MSG_ASSIGMENT_FAILURE;
	msg->l3h[3] = GSM0808_IE_CAUSE;
	msg->l3h[4] = 1;
	msg->l3h[5] = cause;

	/* RR cause 3.2.2.22 */
	if (rr_cause) {
		data = msgb_put(msg, 2);
		data[0] = GSM0808_IE_RR_CAUSE;
		data[1] = *rr_cause;
	}

	/* Circuit pool 3.22.45 */
	/* Circuit pool list 3.2.2.46 */

	/* update the size */
	msg->l3h[1] = msgb_l3len(msg) - 2;
	return msg;
}

void gsm0808_prepend_dtap_header(struct msgb *msg, uint8_t link_id)
{
	uint8_t *hh = msgb_push(msg, 3);
	hh[0] = BSSAP_MSG_DTAP;
	hh[1] = link_id;
	hh[2] = msg->len - 3;
}

static const struct tlv_definition bss_att_tlvdef = {
	.def = {
		[GSM0808_IE_IMSI]		    = { TLV_TYPE_TLV },
		[GSM0808_IE_TMSI]		    = { TLV_TYPE_TLV },
		[GSM0808_IE_CELL_IDENTIFIER_LIST]   = { TLV_TYPE_TLV },
		[GSM0808_IE_CHANNEL_NEEDED]	    = { TLV_TYPE_TV },
		[GSM0808_IE_EMLPP_PRIORITY]	    = { TLV_TYPE_TV },
		[GSM0808_IE_CHANNEL_TYPE]	    = { TLV_TYPE_TLV },
		[GSM0808_IE_PRIORITY]		    = { TLV_TYPE_TLV },
		[GSM0808_IE_CIRCUIT_IDENTITY_CODE]  = { TLV_TYPE_FIXED, 2 },
		[GSM0808_IE_DOWNLINK_DTX_FLAG]	    = { TLV_TYPE_TV },
		[GSM0808_IE_INTERFERENCE_BAND_TO_USE] = { TLV_TYPE_TV },
		[GSM0808_IE_CLASSMARK_INFORMATION_T2] = { TLV_TYPE_TLV },
		[GSM0808_IE_GROUP_CALL_REFERENCE]   = { TLV_TYPE_TLV },
		[GSM0808_IE_TALKER_FLAG]	    = { TLV_TYPE_T },
		[GSM0808_IE_CONFIG_EVO_INDI]	    = { TLV_TYPE_TV },
		[GSM0808_IE_LSA_ACCESS_CTRL_SUPPR]  = { TLV_TYPE_TV },
		[GSM0808_IE_SERVICE_HANDOVER]	    = { TLV_TYPE_TLV },
		[GSM0808_IE_ENCRYPTION_INFORMATION] = { TLV_TYPE_TLV },
		[GSM0808_IE_CIPHER_RESPONSE_MODE]   = { TLV_TYPE_TV },
		[GSM0808_IE_CELL_IDENTIFIER]	    = { TLV_TYPE_TLV },
		[GSM0808_IE_CHOSEN_CHANNEL]	    = { TLV_TYPE_TV },
		[GSM0808_IE_LAYER_3_INFORMATION]    = { TLV_TYPE_TLV },
		[GSM0808_IE_SPEECH_VERSION]         = { TLV_TYPE_TV },
		[GSM0808_IE_CHOSEN_ENCR_ALG]        = { TLV_TYPE_TV },
	},
};

const struct tlv_definition *gsm0808_att_tlvdef()
{
	return &bss_att_tlvdef;
}
