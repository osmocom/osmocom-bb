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

#include <osmocom/gsm/gsm0808.h>
#include <osmocom/gsm/protocol/gsm_08_08.h>
#include <osmocom/gsm/gsm48.h>

#include <arpa/inet.h>

#define BSSMAP_MSG_SIZE 512
#define BSSMAP_MSG_HEADROOM 128

struct msgb *gsm0808_create_layer3(struct msgb *msg_l3, uint16_t nc, uint16_t cc, int lac, uint16_t _ci)
{
	struct msgb* msg;
	struct {
		uint8_t ident;
		struct gsm48_loc_area_id lai;
		uint16_t ci;
	} __attribute__ ((packed)) lai_ci;

	msg  = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
				   "bssmap cmpl l3");
	if (!msg)
		return NULL;

	/* create layer 3 header */
	msgb_v_put(msg, BSS_MAP_MSG_COMPLETE_LAYER_3);

	/* create the cell header */
	lai_ci.ident = CELL_IDENT_WHOLE_GLOBAL;
	gsm48_generate_lai(&lai_ci.lai, cc, nc, lac);
	lai_ci.ci = htons(_ci);
	msgb_tlv_put(msg, GSM0808_IE_CELL_IDENTIFIER, sizeof(lai_ci),
		     (uint8_t *) &lai_ci);

	/* copy the layer3 data */
	msgb_tlv_put(msg, GSM0808_IE_LAYER_3_INFORMATION,
		     msgb_l3len(msg_l3), msg_l3->l3h);

	/* push the bssmap header */
	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_reset(void)
{
	uint8_t cause = GSM0808_CAUSE_EQUIPMENT_FAILURE;
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: reset");
	if (!msg)
		return NULL;

	msgb_v_put(msg, BSS_MAP_MSG_RESET);
	msgb_tlv_put(msg, GSM0808_IE_CAUSE, 1, &cause);
	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_reset_ack(void)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: reset ack");
	if (!msg)
		return NULL;

	msgb_v_put(msg, BSS_MAP_MSG_RESET_ACKNOWLEDGE);
	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_clear_complete(void)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: clear complete");
	uint8_t val = BSS_MAP_MSG_CLEAR_COMPLETE;
	if (!msg)
		return NULL;

	msg->l3h = msg->data;
	msgb_tlv_put(msg, BSSAP_MSG_BSS_MANAGEMENT, 1, &val);

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
	msgb_v_put(msg, BSS_MAP_MSG_CIPHER_MODE_COMPLETE);

	/* include layer3 in case we have at least two octets */
	if (layer3 && msgb_l3len(layer3) > 2) {
		msg->l4h = msgb_tlv_put(msg, GSM0808_IE_LAYER_3_MESSAGE_CONTENTS,
					msgb_l3len(layer3), layer3->l3h);
	}

	/* and the optional BSS message */
	msgb_tv_put(msg, GSM0808_IE_CHOSEN_ENCR_ALG, alg_id);

	/* pre-pend the header */
	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_cipher_reject(uint8_t cause)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: clear complete");
	if (!msg)
		return NULL;

	msgb_tv_put(msg, BSS_MAP_MSG_CIPHER_MODE_REJECT, cause);

	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_classmark_update(const uint8_t *cm2, uint8_t cm2_len,
					     const uint8_t *cm3, uint8_t cm3_len)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "classmark-update");
	if (!msg)
		return NULL;

	msgb_v_put(msg, BSS_MAP_MSG_CLASSMARK_UPDATE);
	msgb_tlv_put(msg, GSM0808_IE_CLASSMARK_INFORMATION_T2, cm2_len, cm2);
	if (cm3)
		msgb_tlv_put(msg, GSM0808_IE_CLASSMARK_INFORMATION_T3,
			     cm3_len, cm3);

	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_sapi_reject(uint8_t link_id)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: sapi 'n' reject");
	if (!msg)
		return NULL;

	msgb_v_put(msg, BSS_MAP_MSG_SAPI_N_REJECT);
	msgb_v_put(msg, link_id);
	msgb_v_put(msg, GSM0808_CAUSE_BSS_NOT_EQUIPPED);

	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_assignment_completed(uint8_t rr_cause,
						 uint8_t chosen_channel, uint8_t encr_alg_id,
						 uint8_t speech_mode)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
						"bssmap: ass compl");
	if (!msg)
		return NULL;

	msgb_v_put(msg, BSS_MAP_MSG_ASSIGMENT_COMPLETE);

	/* write 3.2.2.22 */
	msgb_tv_put(msg, GSM0808_IE_RR_CAUSE, rr_cause);

	/* write cirtcuit identity  code 3.2.2.2 */
	/* write cell identifier 3.2.2.17 */
	/* write chosen channel 3.2.2.33 when BTS picked it */
	msgb_tv_put(msg, GSM0808_IE_CHOSEN_CHANNEL, chosen_channel);

	/* write chosen encryption algorithm 3.2.2.44 */
	msgb_tv_put(msg, GSM0808_IE_CHOSEN_ENCR_ALG, encr_alg_id);

	/* write circuit pool 3.2.2.45 */
	/* write speech version chosen: 3.2.2.51 when BTS picked it */
	if (speech_mode != 0)
		msgb_tv_put(msg, GSM0808_IE_SPEECH_VERSION, speech_mode);

	/* write LSA identifier 3.2.2.15 */

	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_assignment_failure(uint8_t cause, uint8_t *rr_cause)
{
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "bssmap: ass fail");
	if (!msg)
		return NULL;

	msgb_v_put(msg, BSS_MAP_MSG_ASSIGMENT_FAILURE);
	msgb_tlv_put(msg, GSM0808_IE_CAUSE, 1, &cause);

	/* RR cause 3.2.2.22 */
	if (rr_cause)
		msgb_tv_put(msg, GSM0808_IE_RR_CAUSE, *rr_cause);

	/* Circuit pool 3.22.45 */
	/* Circuit pool list 3.2.2.46 */

	/* update the size */
	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

struct msgb *gsm0808_create_clear_rqst(uint8_t cause)
{
	struct msgb *msg;

	msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
				  "bssmap: clear rqst");
	if (!msg)
		return NULL;

	msgb_v_put(msg, BSS_MAP_MSG_CLEAR_RQST);
	msgb_tlv_put(msg, GSM0808_IE_CAUSE, 1, &cause);
	msg->l3h = msgb_tv_push(msg, BSSAP_MSG_BSS_MANAGEMENT, msgb_length(msg));

	return msg;
}

void gsm0808_prepend_dtap_header(struct msgb *msg, uint8_t link_id)
{
	uint8_t *hh = msgb_push(msg, 3);
	hh[0] = BSSAP_MSG_DTAP;
	hh[1] = link_id;
	hh[2] = msg->len - 3;
}

struct msgb *gsm0808_create_dtap(struct msgb *msg_l3, uint8_t link_id)
{
	struct dtap_header *header;
	uint8_t *data;
	struct msgb *msg = msgb_alloc_headroom(BSSMAP_MSG_SIZE, BSSMAP_MSG_HEADROOM,
					       "dtap");
	if (!msg)
		return NULL;

	/* DTAP header */
	msg->l3h = msgb_put(msg, sizeof(*header));
	header = (struct dtap_header *) &msg->l3h[0];
	header->type = BSSAP_MSG_DTAP;
	header->link_id = link_id;
	header->length = msgb_l3len(msg_l3);

	/* Payload */
	data = msgb_put(msg, header->length);
	memcpy(data, msg_l3->l3h, header->length);

	return msg;
}

/* As per 3GPP TS 48.008 version 11.7.0 Release 11 */
static const struct tlv_definition bss_att_tlvdef = {
	.def = {
		[GSM0808_IE_CIRCUIT_IDENTITY_CODE]  = { TLV_TYPE_FIXED, 2 },
		[GSM0808_IE_CONNECTION_RELEASE_RQSTED]	= { TLV_TYPE_TV },
		[GSM0808_IE_RESOURCE_AVAILABLE]		= { TLV_TYPE_FIXED, 21 },
		[GSM0808_IE_CAUSE]			= { TLV_TYPE_TLV },
		[GSM0808_IE_IMSI]		    = { TLV_TYPE_TLV },
		[GSM0808_IE_TMSI]		    = { TLV_TYPE_TLV },
		[GSM0808_IE_NUMBER_OF_MSS]		= { TLV_TYPE_TV },
		[GSM0808_IE_LAYER_3_HEADER_INFORMATION] = { TLV_TYPE_TLV },
		[GSM0808_IE_ENCRYPTION_INFORMATION] = { TLV_TYPE_TLV },
		[GSM0808_IE_CHANNEL_TYPE]	    = { TLV_TYPE_TLV },
		[GSM0808_IE_PERIODICITY]		= { TLV_TYPE_TV },
		[GSM0808_IE_EXTENDED_RESOURCE_INDICATOR]= { TLV_TYPE_TV },
		[GSM0808_IE_TOTAL_RESOURCE_ACCESSIBLE]	= { TLV_TYPE_FIXED, 4 },
		[GSM0808_IE_LSA_IDENTIFIER]		= { TLV_TYPE_TLV },
		[GSM0808_IE_LSA_IDENTIFIER_LIST]	= { TLV_TYPE_TLV },
		[GSM0808_IE_LSA_INFORMATION]	= { TLV_TYPE_TLV },
		[GSM0808_IE_CELL_IDENTIFIER]	    = { TLV_TYPE_TLV },
		[GSM0808_IE_PRIORITY]		    = { TLV_TYPE_TLV },
		[GSM0808_IE_CLASSMARK_INFORMATION_T2] = { TLV_TYPE_TLV },
		[GSM0808_IE_CLASSMARK_INFORMATION_T3] = { TLV_TYPE_TLV },
		[GSM0808_IE_INTERFERENCE_BAND_TO_USE] = { TLV_TYPE_TV },
		[GSM0808_IE_RR_CAUSE]			= { TLV_TYPE_TV },
		[GSM0808_IE_LAYER_3_INFORMATION]    = { TLV_TYPE_TLV },
		[GSM0808_IE_DLCI]			= { TLV_TYPE_TV },
		[GSM0808_IE_DOWNLINK_DTX_FLAG]	    = { TLV_TYPE_TV },
		[GSM0808_IE_CELL_IDENTIFIER_LIST]   = { TLV_TYPE_TLV },
		[GSM0808_IE_CELL_ID_LIST_SEGMENT]	= { TLV_TYPE_TLV },
		[GSM0808_IE_CELL_ID_LIST_SEG_EST_CELLS]	= { TLV_TYPE_TLV },
		[GSM0808_IE_CELL_ID_LIST_SEG_CELLS_TBE]	= { TLV_TYPE_TLV },
		[GSM0808_IE_CELL_ID_LIST_SEG_REL_CELLS]	= { TLV_TYPE_TLV },
		[GSM0808_IE_CELL_ID_LIST_SEG_NE_CELLS]	= { TLV_TYPE_TLV },
		[GSM0808_IE_RESPONSE_RQST]		= { TLV_TYPE_T },
		[GSM0808_IE_RESOURCE_INDICATION_METHOD]	= { TLV_TYPE_TV },
		[GSM0808_IE_CLASSMARK_INFORMATION_TYPE_1] = { TLV_TYPE_TV },
		[GSM0808_IE_CIRCUIT_IDENTITY_CODE_LIST]	= { TLV_TYPE_TLV },
		[GSM0808_IE_DIAGNOSTIC]			= { TLV_TYPE_TLV },
		[GSM0808_IE_CHOSEN_CHANNEL]	    = { TLV_TYPE_TV },
		[GSM0808_IE_CIPHER_RESPONSE_MODE]   = { TLV_TYPE_TV },
		[GSM0808_IE_LAYER_3_MESSAGE_CONTENTS] = { TLV_TYPE_TLV },
		[GSM0808_IE_CHANNEL_NEEDED]	    = { TLV_TYPE_TV },
		[GSM0808_IE_TRACE_TYPE]			= { TLV_TYPE_TV },
		[GSM0808_IE_TRIGGERID]			= { TLV_TYPE_TLV },
		[GSM0808_IE_TRACE_REFERENCE]		= { TLV_TYPE_TV },
		[GSM0808_IE_TRANSACTIONID]		= { TLV_TYPE_TLV },
		[GSM0808_IE_MOBILE_IDENTITY]		= { TLV_TYPE_TLV },
		[GSM0808_IE_OMCID]			= { TLV_TYPE_TLV },
		[GSM0808_IE_FORWARD_INDICATOR]		= { TLV_TYPE_TV },
		[GSM0808_IE_CHOSEN_ENCR_ALG]        = { TLV_TYPE_TV },
		[GSM0808_IE_CIRCUIT_POOL]		= { TLV_TYPE_TV },
		[GSM0808_IE_CIRCUIT_POOL_LIST]		= { TLV_TYPE_TLV },
		[GSM0808_IE_TIME_INDICATION]		= { TLV_TYPE_TV },
		[GSM0808_IE_RESOURCE_SITUATION]		= { TLV_TYPE_TLV },
		[GSM0808_IE_CURRENT_CHANNEL_TYPE_1]	= { TLV_TYPE_TV },
		[GSM0808_IE_QUEUEING_INDICATOR]		= { TLV_TYPE_TV },
		[GSM0808_IE_SPEECH_VERSION]         = { TLV_TYPE_TV },
		[GSM0808_IE_ASSIGNMENT_REQUIREMENT]	= { TLV_TYPE_TV },
		[GSM0808_IE_TALKER_FLAG]	    = { TLV_TYPE_T },
		[GSM0808_IE_GROUP_CALL_REFERENCE]   = { TLV_TYPE_TLV },
		[GSM0808_IE_EMLPP_PRIORITY]	    = { TLV_TYPE_TV },
		[GSM0808_IE_CONFIG_EVO_INDI]	    = { TLV_TYPE_TV },
		[GSM0808_IE_OLD_BSS_TO_NEW_BSS_INFORMATION] = { TLV_TYPE_TLV },
		[GSM0808_IE_LCS_QOS]			= { TLV_TYPE_TLV },
		[GSM0808_IE_LSA_ACCESS_CTRL_SUPPR]  = { TLV_TYPE_TV },
		[GSM0808_IE_LCS_PRIORITY]		= { TLV_TYPE_TLV },
		[GSM0808_IE_LOCATION_TYPE]		= { TLV_TYPE_TLV },
		[GSM0808_IE_LOCATION_ESTIMATE]		= { TLV_TYPE_TLV },
		[GSM0808_IE_POSITIONING_DATA]		= { TLV_TYPE_TLV },
		[GSM0808_IE_LCS_CAUSE]			= { TLV_TYPE_TLV },
		[GSM0808_IE_APDU]			= { TLV_TYPE_TLV },
		[GSM0808_IE_NETWORK_ELEMENT_IDENTITY]	= { TLV_TYPE_TLV },
		[GSM0808_IE_GPS_ASSISTANCE_DATA]	= { TLV_TYPE_TLV },
		[GSM0808_IE_DECIPHERING_KEYS]		= { TLV_TYPE_TLV },
		[GSM0808_IE_RETURN_ERROR_RQST]		= { TLV_TYPE_TLV },
		[GSM0808_IE_RETURN_ERROR_CAUSE]		= { TLV_TYPE_TLV },
		[GSM0808_IE_SEGMENTATION]		= { TLV_TYPE_TLV },
		[GSM0808_IE_SERVICE_HANDOVER]	    = { TLV_TYPE_TLV },
		[GSM0808_IE_SOURCE_RNC_TO_TARGET_RNC_TRANSPARENT_UMTS]		= { TLV_TYPE_TLV },
		[GSM0808_IE_SOURCE_RNC_TO_TARGET_RNC_TRANSPARENT_CDMA2000]	= { TLV_TYPE_TLV },
		[GSM0808_IE_GERAN_CLASSMARK]		= { TLV_TYPE_TLV },
		[GSM0808_IE_GERAN_BSC_CONTAINER]	= { TLV_TYPE_TLV },
		[GSM0808_IE_NEW_BSS_TO_OLD_BSS_INFO]	= { TLV_TYPE_TLV },
		[GSM0800_IE_INTER_SYSTEM_INFO]		= { TLV_TYPE_TLV },
		[GSM0808_IE_SNA_ACCESS_INFO]		= { TLV_TYPE_TLV },
		[GSM0808_IE_VSTK_RAND_INFO]		= { TLV_TYPE_TLV },
		[GSM0808_IE_PAGING_INFO]		= { TLV_TYPE_TV },
		[GSM0808_IE_IMEI]			= { TLV_TYPE_TLV },
		[GSM0808_IE_VELOCITY_ESTIMATE]		= { TLV_TYPE_TLV },
		[GSM0808_IE_VGCS_FEATURE_FLAGS]		= { TLV_TYPE_TLV },
		[GSM0808_IE_TALKER_PRIORITY]		= { TLV_TYPE_TV },
		[GSM0808_IE_EMERGENCY_SET_INDICATION]	= { TLV_TYPE_T },
		[GSM0808_IE_TALKER_IDENTITY]		= { TLV_TYPE_TLV },
		[GSM0808_IE_SMS_TO_VGCS]		= { TLV_TYPE_TLV },
		[GSM0808_IE_VGCS_TALKER_MODE]		= { TLV_TYPE_TLV },
		[GSM0808_IE_VGCS_VBS_CELL_STATUS]	= { TLV_TYPE_TLV },
		[GSM0808_IE_GANSS_ASSISTANCE_DATA]	= { TLV_TYPE_TLV },
		[GSM0808_IE_GANSS_POSITIONING_DATA]	= { TLV_TYPE_TLV },
		[GSM0808_IE_GANSS_LOCATION_TYPE]	= { TLV_TYPE_TLV },
		[GSM0808_IE_APP_DATA]			= { TLV_TYPE_TLV },
		[GSM0808_IE_DATA_IDENTITY]		= { TLV_TYPE_TLV },
		[GSM0808_IE_APP_DATA_INFO]		= { TLV_TYPE_TLV },
		[GSM0808_IE_MSISDN]			= { TLV_TYPE_TLV },
		[GSM0808_IE_AOIP_TRASP_ADDR]		= { TLV_TYPE_TLV },
		[GSM0808_IE_SPEECH_CODEC_LIST]		= { TLV_TYPE_TLV },
		[GSM0808_IE_SPEECH_CODEC]		= { TLV_TYPE_TLV },
		[GSM0808_IE_CALL_ID]			= { TLV_TYPE_FIXED, 4 },
		[GSM0808_IE_CALL_ID_LIST]		= { TLV_TYPE_TLV },
		[GSM0808_IE_A_IF_SEL_FOR_RESET]		= { TLV_TYPE_TV },
		[GSM0808_IE_KC_128]			= { TLV_TYPE_FIXED, 16 },
		[GSM0808_IE_CSG_IDENTIFIER]		= { TLV_TYPE_TLV },
		[GSM0808_IE_REDIR_ATTEMPT_FLAG]		= { TLV_TYPE_T },
		[GSM0808_IE_REROUTE_REJ_CAUSE]		= { TLV_TYPE_TV },
		[GSM0808_IE_SEND_SEQ_NUM]		= { TLV_TYPE_TV },
		[GSM0808_IE_REROUTE_COMPL_OUTCOME]	= { TLV_TYPE_TV },
		[GSM0808_IE_GLOBAL_CALL_REF]		= { TLV_TYPE_TLV },
		[GSM0808_IE_LCLS_CONFIG]		= { TLV_TYPE_TV },
		[GSM0808_IE_LCLS_CONN_STATUS_CTRL]	= { TLV_TYPE_TV },
		[GSM0808_IE_LCLS_CORR_NOT_NEEDED]	= { TLV_TYPE_TV },
		[GSM0808_IE_LCLS_BSS_STATUS]		= { TLV_TYPE_TV },
		[GSM0808_IE_LCLS_BREAK_REQ]		= { TLV_TYPE_TV },
		[GSM0808_IE_CSFB_INDICATION]		= { TLV_TYPE_T },
		[GSM0808_IE_CS_TO_PS_SRVCC]		= { TLV_TYPE_T },
		[GSM0808_IE_SRC_ENB_TO_TGT_ENB_TRANSP]	= { TLV_TYPE_TLV },
		[GSM0808_IE_CS_TO_PS_SRVCC_IND]		= { TLV_TYPE_T },
		[GSM0808_IE_CN_TO_MS_TRANSP_INFO]	= { TLV_TYPE_TLV },
		[GSM0808_IE_SELECTED_PLMN_ID]		= { TLV_TYPE_FIXED, 3 },
		[GSM0808_IE_LAST_USED_EUTRAN_PLMN_ID]	= { TLV_TYPE_FIXED, 3 },
	},
};

const struct tlv_definition *gsm0808_att_tlvdef(void)
{
	return &bss_att_tlvdef;
}

static const struct value_string gsm0808_msgt_names[] = {
	{ BSS_MAP_MSG_ASSIGMENT_RQST,		"ASSIGNMENT REQ" },
	{ BSS_MAP_MSG_ASSIGMENT_COMPLETE,	"ASSIGNMENT COMPL" },
	{ BSS_MAP_MSG_ASSIGMENT_FAILURE,	"ASSIGNMENT FAIL" },
	{ BSS_MAP_MSG_CHAN_MOD_RQST,		"CHANNEL MODIFY REQUEST" },

	{ BSS_MAP_MSG_HANDOVER_RQST,		"HANDOVER REQ" },
	{ BSS_MAP_MSG_HANDOVER_REQUIRED,	"HANDOVER REQUIRED" },
	{ BSS_MAP_MSG_HANDOVER_RQST_ACKNOWLEDGE,"HANDOVER REQ ACK" },
	{ BSS_MAP_MSG_HANDOVER_CMD,		"HANDOVER CMD" },
	{ BSS_MAP_MSG_HANDOVER_COMPLETE,	"HANDOVER COMPLETE" },
	{ BSS_MAP_MSG_HANDOVER_SUCCEEDED,	"HANDOVER SUCCESS" },
	{ BSS_MAP_MSG_HANDOVER_FAILURE,		"HANDOVER FAILURE" },
	{ BSS_MAP_MSG_HANDOVER_PERFORMED,	"HANDOVER PERFORMED" },
	{ BSS_MAP_MSG_HANDOVER_CANDIDATE_ENQUIRE, "HANDOVER CAND ENQ" },
	{ BSS_MAP_MSG_HANDOVER_CANDIDATE_RESPONSE, "HANDOVER CAND RESP" },
	{ BSS_MAP_MSG_HANDOVER_REQUIRED_REJECT,	"HANDOVER REQ REJ" },
	{ BSS_MAP_MSG_HANDOVER_DETECT,		"HANDOVER DETECT" },
	{ BSS_MAP_MSG_INT_HANDOVER_REQUIRED,	"INT HANDOVER REQ" },
	{ BSS_MAP_MSG_INT_HANDOVER_REQUIRED_REJ,"INT HANDOVER REQ REJ" },
	{ BSS_MAP_MSG_INT_HANDOVER_CMD,		"INT HANDOVER CMD" },
	{ BSS_MAP_MSG_INT_HANDOVER_ENQUIRY,	"INT HANDOVER ENQ" },

	{ BSS_MAP_MSG_CLEAR_CMD,		"CLEAR COMMAND" },
	{ BSS_MAP_MSG_CLEAR_COMPLETE,		"CLEAR COMPLETE" },
	{ BSS_MAP_MSG_CLEAR_RQST,		"CLEAR REQUEST" },
	{ BSS_MAP_MSG_SAPI_N_REJECT,		"SAPI N REJECT" },
	{ BSS_MAP_MSG_CONFUSION,		"CONFUSION" },

	{ BSS_MAP_MSG_SUSPEND,			"SUSPEND" },
	{ BSS_MAP_MSG_RESUME,			"RESUME" },
	{ BSS_MAP_MSG_CONNECTION_ORIENTED_INFORMATION, "CONN ORIENT INFO" },
	{ BSS_MAP_MSG_PERFORM_LOCATION_RQST,	"PERFORM LOC REQ" },
	{ BSS_MAP_MSG_LSA_INFORMATION,		"LSA INFORMATION" },
	{ BSS_MAP_MSG_PERFORM_LOCATION_RESPONSE, "PERFORM LOC RESP" },
	{ BSS_MAP_MSG_PERFORM_LOCATION_ABORT,	"PERFORM LOC ABORT" },
	{ BSS_MAP_MSG_COMMON_ID,		"COMMON ID" },
	{ BSS_MAP_MSG_REROUTE_CMD,		"REROUTE COMMAND" },
	{ BSS_MAP_MSG_REROUTE_COMPLETE,		"REROUTE COMPLETE" },

	{ BSS_MAP_MSG_RESET,			"RESET" },
	{ BSS_MAP_MSG_RESET_ACKNOWLEDGE,	"RESET ACK" },
	{ BSS_MAP_MSG_OVERLOAD,			"OVERLOAD" },
	{ BSS_MAP_MSG_RESET_CIRCUIT,		"RESET CIRCUIT" },
	{ BSS_MAP_MSG_RESET_CIRCUIT_ACKNOWLEDGE, "RESET CIRCUIT ACK" },
	{ BSS_MAP_MSG_MSC_INVOKE_TRACE,		"MSC INVOKE TRACE" },
	{ BSS_MAP_MSG_BSS_INVOKE_TRACE,		"BSS INVOKE TRACE" },
	{ BSS_MAP_MSG_CONNECTIONLESS_INFORMATION, "CONNLESS INFO" },
	{ BSS_MAP_MSG_RESET_IP_RSRC,		"RESET IP RESOURCE" },
	{ BSS_MAP_MSG_RESET_IP_RSRC_ACK,	"RESET IP RESOURCE ACK" },

	{ BSS_MAP_MSG_BLOCK,			"BLOCK" },
	{ BSS_MAP_MSG_BLOCKING_ACKNOWLEDGE,	"BLOCK ACK" },
	{ BSS_MAP_MSG_UNBLOCK,			"UNBLOCK" },
	{ BSS_MAP_MSG_UNBLOCKING_ACKNOWLEDGE,	"UNBLOCK ACK" },
	{ BSS_MAP_MSG_CIRCUIT_GROUP_BLOCK,	"CIRC GROUP BLOCK" },
	{ BSS_MAP_MSG_CIRCUIT_GROUP_BLOCKING_ACKNOWLEDGE, "CIRC GORUP BLOCK ACK" },
	{ BSS_MAP_MSG_CIRCUIT_GROUP_UNBLOCK,	"CIRC GROUP UNBLOCK" },
	{ BSS_MAP_MSG_CIRCUIT_GROUP_UNBLOCKING_ACKNOWLEDGE, "CIRC GROUP UNBLOCK ACK" },
	{ BSS_MAP_MSG_UNEQUIPPED_CIRCUIT,	"UNEQUIPPED CIRCUIT" },
	{ BSS_MAP_MSG_CHANGE_CIRCUIT,		"CHANGE CIRCUIT" },
	{ BSS_MAP_MSG_CHANGE_CIRCUIT_ACKNOWLEDGE, "CHANGE CIRCUIT ACK" },

	{ BSS_MAP_MSG_RESOURCE_RQST,		"RESOURCE REQ" },
	{ BSS_MAP_MSG_RESOURCE_INDICATION,	"RESOURCE IND" },
	{ BSS_MAP_MSG_PAGING,			"PAGING" },
	{ BSS_MAP_MSG_CIPHER_MODE_CMD,		"CIPHER MODE CMD" },
	{ BSS_MAP_MSG_CLASSMARK_UPDATE,		"CLASSMARK UPDATE" },
	{ BSS_MAP_MSG_CIPHER_MODE_COMPLETE,	"CIPHER MODE COMPLETE" },
	{ BSS_MAP_MSG_QUEUING_INDICATION,	"QUEUING INDICATION" },
	{ BSS_MAP_MSG_COMPLETE_LAYER_3,		"COMPLETE LAYER 3" },
	{ BSS_MAP_MSG_CLASSMARK_RQST,		"CLASSMARK REQ" },
	{ BSS_MAP_MSG_CIPHER_MODE_REJECT,	"CIPHER MODE REJECT" },
	{ BSS_MAP_MSG_LOAD_INDICATION,		"LOAD IND" },

	{ BSS_MAP_MSG_VGCS_VBS_SETUP,		"VGCS/VBS SETUP" },
	{ BSS_MAP_MSG_VGCS_VBS_SETUP_ACK,	"VGCS/VBS SETUP ACK" },
	{ BSS_MAP_MSG_VGCS_VBS_SETUP_REFUSE,	"VGCS/VBS SETUP REFUSE" },
	{ BSS_MAP_MSG_VGCS_VBS_ASSIGNMENT_RQST,	"VGCS/VBS ASSIGN REQ" },
	{ BSS_MAP_MSG_VGCS_VBS_ASSIGNMENT_RESULT, "VGCS/VBS ASSIGN RES" },
	{ BSS_MAP_MSG_VGCS_VBS_ASSIGNMENT_FAILURE, "VGCS/VBS ASSIGN FAIL" },
	{ BSS_MAP_MSG_VGCS_VBS_QUEUING_INDICATION, "VGCS/VBS QUEUING IND" },
	{ BSS_MAP_MSG_UPLINK_RQST,		"UPLINK REQ" },
	{ BSS_MAP_MSG_UPLINK_RQST_ACKNOWLEDGE,	"UPLINK REQ ACK" },
	{ BSS_MAP_MSG_UPLINK_RQST_CONFIRMATION,	"UPLINK REQ CONF" },
	{ BSS_MAP_MSG_UPLINK_RELEASE_INDICATION,"UPLINK REL IND" },
	{ BSS_MAP_MSG_UPLINK_REJECT_CMD,	"UPLINK REJ CMD" },
	{ BSS_MAP_MSG_UPLINK_RELEASE_CMD,	"UPLINK REL CMD" },
	{ BSS_MAP_MSG_UPLINK_SEIZED_CMD,	"UPLINK SEIZED CMD" },
	{ BSS_MAP_MSG_VGCS_ADDL_INFO,		"VGCS ADDL INFO" },
	{ BSS_MAP_MSG_NOTIFICATION_DATA,	"NOTIF DATA" },
	{ BSS_MAP_MSG_UPLINK_APP_DATA,		"UPLINK APP DATA" },

	{ BSS_MAP_MSG_LCLS_CONNECT_CTRL,	"LCLS-CONNECT-CONTROL" },
	{ BSS_MAP_MSG_LCLS_CONNECT_CTRL_ACK,	"CLS-CONNECT-CONTROL-ACK" },
	{ BSS_MAP_MSG_LCLS_NOTIFICATION,	"LCLS-NOTIFICATION" },

	{ 0, NULL }
};

const char *gsm0808_bssmap_name(uint8_t msg_type)
{
	return get_value_string(gsm0808_msgt_names, msg_type);
}

static const struct value_string gsm0808_bssap_names[] = {
	{ BSSAP_MSG_BSS_MANAGEMENT, 		"MANAGEMENT" },
	{ BSSAP_MSG_DTAP,			"DTAP" },
};

const char *gsm0808_bssap_name(uint8_t msg_type)
{
	return get_value_string(gsm0808_bssap_names, msg_type);
}
