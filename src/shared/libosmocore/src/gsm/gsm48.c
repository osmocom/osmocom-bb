/* GSM Mobile Radio Interface Layer 3 messages
 * 3GPP TS 04.08 version 7.21.0 Release 1998 / ETSI TS 100 940 V7.21.0 */

/* (C) 2008-2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2008, 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include <arpa/inet.h>

#include <osmocom/core/utils.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

const struct tlv_definition gsm48_att_tlvdef = {
	.def = {
		[GSM48_IE_MOBILE_ID]	= { TLV_TYPE_TLV },
		[GSM48_IE_NAME_LONG]	= { TLV_TYPE_TLV },
		[GSM48_IE_NAME_SHORT]	= { TLV_TYPE_TLV },
		[GSM48_IE_UTC]		= { TLV_TYPE_TV },
		[GSM48_IE_NET_TIME_TZ]	= { TLV_TYPE_FIXED, 7 },
		[GSM48_IE_LSA_IDENT]	= { TLV_TYPE_TLV },

		[GSM48_IE_BEARER_CAP]	= { TLV_TYPE_TLV },
		[GSM48_IE_CAUSE]	= { TLV_TYPE_TLV },
		[GSM48_IE_CC_CAP]	= { TLV_TYPE_TLV },
		[GSM48_IE_ALERT]	= { TLV_TYPE_TLV },
		[GSM48_IE_FACILITY]	= { TLV_TYPE_TLV },
		[GSM48_IE_PROGR_IND]	= { TLV_TYPE_TLV },
		[GSM48_IE_AUX_STATUS]	= { TLV_TYPE_TLV },
		[GSM48_IE_NOTIFY]	= { TLV_TYPE_TV },
		[GSM48_IE_KPD_FACILITY]	= { TLV_TYPE_TV },
		[GSM48_IE_SIGNAL]	= { TLV_TYPE_TV },
		[GSM48_IE_CONN_BCD]	= { TLV_TYPE_TLV },
		[GSM48_IE_CONN_SUB]	= { TLV_TYPE_TLV },
		[GSM48_IE_CALLING_BCD]	= { TLV_TYPE_TLV },
		[GSM48_IE_CALLING_SUB]	= { TLV_TYPE_TLV },
		[GSM48_IE_CALLED_BCD]	= { TLV_TYPE_TLV },
		[GSM48_IE_CALLED_SUB]	= { TLV_TYPE_TLV },
		[GSM48_IE_REDIR_BCD]	= { TLV_TYPE_TLV },
		[GSM48_IE_REDIR_SUB]	= { TLV_TYPE_TLV },
		[GSM48_IE_LOWL_COMPAT]	= { TLV_TYPE_TLV },
		[GSM48_IE_HIGHL_COMPAT]	= { TLV_TYPE_TLV },
		[GSM48_IE_USER_USER]	= { TLV_TYPE_TLV },
		[GSM48_IE_SS_VERS]	= { TLV_TYPE_TLV },
		[GSM48_IE_MORE_DATA]	= { TLV_TYPE_T },
		[GSM48_IE_CLIR_SUPP]	= { TLV_TYPE_T },
		[GSM48_IE_CLIR_INVOC]	= { TLV_TYPE_T },
		[GSM48_IE_REV_C_SETUP]	= { TLV_TYPE_T },
		[GSM48_IE_REPEAT_CIR]   = { TLV_TYPE_T },
		[GSM48_IE_REPEAT_SEQ]   = { TLV_TYPE_T },
		/* FIXME: more elements */
	},
};

/* RR elements */
const struct tlv_definition gsm48_rr_att_tlvdef = {
	.def = {
		/* NOTE: Don't add IE 17 = MOBILE_ID here, it already used. */
		[GSM48_IE_VGCS_TARGET]		= { TLV_TYPE_TLV },
		[GSM48_IE_FRQSHORT_AFTER]	= { TLV_TYPE_FIXED, 9 },
		[GSM48_IE_MUL_RATE_CFG]		= { TLV_TYPE_TLV },
		[GSM48_IE_FREQ_L_AFTER]		= { TLV_TYPE_TLV },
		[GSM48_IE_MSLOT_DESC]		= { TLV_TYPE_TLV },
		[GSM48_IE_CHANMODE_2]		= { TLV_TYPE_TV },
		[GSM48_IE_FRQSHORT_BEFORE]	= { TLV_TYPE_FIXED, 9 },
		[GSM48_IE_CHANMODE_3]		= { TLV_TYPE_TV },
		[GSM48_IE_CHANMODE_4]		= { TLV_TYPE_TV },
		[GSM48_IE_CHANMODE_5]		= { TLV_TYPE_TV },
		[GSM48_IE_CHANMODE_6]		= { TLV_TYPE_TV },
		[GSM48_IE_CHANMODE_7]		= { TLV_TYPE_TV },
		[GSM48_IE_CHANMODE_8]		= { TLV_TYPE_TV },
		[GSM48_IE_FREQ_L_BEFORE]	= { TLV_TYPE_TLV },
		[GSM48_IE_CH_DESC_1_BEFORE]	= { TLV_TYPE_FIXED, 3 },
		[GSM48_IE_CH_DESC_2_BEFORE]	= { TLV_TYPE_FIXED, 3 },
		[GSM48_IE_F_CH_SEQ_BEFORE]	= { TLV_TYPE_FIXED, 9 },
		[GSM48_IE_CLASSMARK3]		= { TLV_TYPE_TLV },
		[GSM48_IE_MA_BEFORE]		= { TLV_TYPE_TLV },
		[GSM48_IE_RR_PACKET_UL]		= { TLV_TYPE_TLV },
		[GSM48_IE_RR_PACKET_DL]		= { TLV_TYPE_TLV },
		[GSM48_IE_CELL_CH_DESC]		= { TLV_TYPE_FIXED, 16 },
		[GSM48_IE_CHANMODE_1]		= { TLV_TYPE_TV },
		[GSM48_IE_CHDES_2_AFTER]	= { TLV_TYPE_FIXED, 3 },
		[GSM48_IE_MODE_SEC_CH]		= { TLV_TYPE_TV },
		[GSM48_IE_F_CH_SEQ_AFTER]		= { TLV_TYPE_FIXED, 9 },
		[GSM48_IE_MA_AFTER]		= { TLV_TYPE_TLV },
		[GSM48_IE_BA_RANGE]		= { TLV_TYPE_TLV },
		[GSM48_IE_GROUP_CHDES]		= { TLV_TYPE_TLV },
		[GSM48_IE_BA_LIST_PREF]		= { TLV_TYPE_TLV },
		[GSM48_IE_MOB_OVSERV_DIF]	= { TLV_TYPE_TLV },
		[GSM48_IE_REALTIME_DIFF]	= { TLV_TYPE_TLV },
		[GSM48_IE_START_TIME]		= { TLV_TYPE_FIXED, 2 },
		[GSM48_IE_TIMING_ADVANCE]	= { TLV_TYPE_TV },
		[GSM48_IE_GROUP_CIP_SEQ]	= { TLV_TYPE_SINGLE_TV },
		[GSM48_IE_CIP_MODE_SET]		= { TLV_TYPE_SINGLE_TV },
		[GSM48_IE_GPRS_RESUMPT]		= { TLV_TYPE_SINGLE_TV },
		[GSM48_IE_SYNC_IND]		= { TLV_TYPE_SINGLE_TV },
	},
};

/* MM elements */
const struct tlv_definition gsm48_mm_att_tlvdef = {
	.def = {
		[GSM48_IE_MOBILE_ID]		= { TLV_TYPE_TLV },
		[GSM48_IE_NAME_LONG]		= { TLV_TYPE_TLV },
		[GSM48_IE_NAME_SHORT]		= { TLV_TYPE_TLV },
		[GSM48_IE_UTC]			= { TLV_TYPE_TV },
		[GSM48_IE_NET_TIME_TZ]		= { TLV_TYPE_FIXED, 7 },
		[GSM48_IE_LSA_IDENT]		= { TLV_TYPE_TLV },

		[GSM48_IE_LOCATION_AREA]	= { TLV_TYPE_FIXED, 5 },
		[GSM48_IE_PRIORITY_LEV]		= { TLV_TYPE_SINGLE_TV },
		[GSM48_IE_FOLLOW_ON_PROC]	= { TLV_TYPE_T },
		[GSM48_IE_CTS_PERMISSION]	= { TLV_TYPE_T },
	},
};

static const struct value_string rr_cause_names[] = {
	{ GSM48_RR_CAUSE_NORMAL,		"Normal event" },
	{ GSM48_RR_CAUSE_ABNORMAL_UNSPEC,	"Abnormal release, unspecified" },
	{ GSM48_RR_CAUSE_ABNORMAL_UNACCT,	"Abnormal release, channel unacceptable" },
	{ GSM48_RR_CAUSE_ABNORMAL_TIMER,	"Abnormal release, timer expired" },
	{ GSM48_RR_CAUSE_ABNORMAL_NOACT,	"Abnormal release, no activity on radio path" },
	{ GSM48_RR_CAUSE_PREMPTIVE_REL,		"Preemptive release" },
	{ GSM48_RR_CAUSE_HNDOVER_IMP,		"Handover impossible, timing advance out of range" },
	{ GSM48_RR_CAUSE_CHAN_MODE_UNACCT,	"Channel mode unacceptable" },
	{ GSM48_RR_CAUSE_FREQ_NOT_IMPL,		"Frequency not implemented" },
	{ GSM48_RR_CAUSE_CALL_CLEARED,		"Call already cleared" },
	{ GSM48_RR_CAUSE_SEMANT_INCORR,		"Semantically incorrect message" },
	{ GSM48_RR_CAUSE_INVALID_MAND_INF,	"Invalid mandatory information" },
	{ GSM48_RR_CAUSE_MSG_TYPE_N,		"Message type non-existant or not implemented" },
	{ GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT,	"Message type not compatible with protocol state" },
	{ GSM48_RR_CAUSE_COND_IE_ERROR,		"Conditional IE error" },
	{ GSM48_RR_CAUSE_NO_CELL_ALLOC_A,	"No cell allocation available" },
	{ GSM48_RR_CAUSE_PROT_ERROR_UNSPC,	"Protocol error unspecified" },
	{ 0,					NULL },
};

/* FIXME: convert to value_string */
static const char *cc_state_names[32] = {
	"NULL",
	"INITIATED",
	"MM_CONNECTION_PEND",
	"MO_CALL_PROC",
	"CALL_DELIVERED",
	"illegal state 5",
	"CALL_PRESENT",
	"CALL_RECEIVED",
	"CONNECT_REQUEST",
	"MO_TERM_CALL_CONF",
	"ACTIVE",
	"DISCONNECT_REQ",
	"DISCONNECT_IND",
	"illegal state 13",
	"illegal state 14",
	"illegal state 15",
	"illegal state 16",
	"illegal state 17",
	"illegal state 18",
	"RELEASE_REQ",
	"illegal state 20",
	"illegal state 21",
	"illegal state 22",
	"illegal state 23",
	"illegal state 24",
	"illegal state 25",
	"MO_ORIG_MODIFY",
	"MO_TERM_MODIFY",
	"CONNECT_IND",
	"illegal state 29",
	"illegal state 30",
	"illegal state 31",
};

const char *gsm48_cc_state_name(uint8_t state)
{
	if (state < ARRAY_SIZE(cc_state_names))
		return cc_state_names[state];

	return "invalid";
}

static const struct value_string cc_msg_names[] = {
	{ GSM48_MT_CC_ALERTING,		"ALERTING" },
	{ GSM48_MT_CC_CALL_PROC,	"CALL_PROC" },
	{ GSM48_MT_CC_PROGRESS,		"PROGRESS" },
	{ GSM48_MT_CC_ESTAB,		"ESTAB" },
	{ GSM48_MT_CC_SETUP,		"SETUP" },
	{ GSM48_MT_CC_ESTAB_CONF,	"ESTAB_CONF" },
	{ GSM48_MT_CC_CONNECT,		"CONNECT" },
	{ GSM48_MT_CC_CALL_CONF,	"CALL_CONF" },
	{ GSM48_MT_CC_START_CC,		"START_CC" },
	{ GSM48_MT_CC_RECALL,		"RECALL" },
	{ GSM48_MT_CC_EMERG_SETUP,	"EMERG_SETUP" },
	{ GSM48_MT_CC_CONNECT_ACK,	"CONNECT_ACK" },
	{ GSM48_MT_CC_USER_INFO,	"USER_INFO" },
	{ GSM48_MT_CC_MODIFY_REJECT,	"MODIFY_REJECT" },
	{ GSM48_MT_CC_MODIFY,		"MODIFY" },
	{ GSM48_MT_CC_HOLD,		"HOLD" },
	{ GSM48_MT_CC_HOLD_ACK,		"HOLD_ACK" },
	{ GSM48_MT_CC_HOLD_REJ,		"HOLD_REJ" },
	{ GSM48_MT_CC_RETR,		"RETR" },
	{ GSM48_MT_CC_RETR_ACK,		"RETR_ACK" },
	{ GSM48_MT_CC_RETR_REJ,		"RETR_REJ" },
	{ GSM48_MT_CC_MODIFY_COMPL,	"MODIFY_COMPL" },
	{ GSM48_MT_CC_DISCONNECT,	"DISCONNECT" },
	{ GSM48_MT_CC_RELEASE_COMPL,	"RELEASE_COMPL" },
	{ GSM48_MT_CC_RELEASE,		"RELEASE" },
	{ GSM48_MT_CC_STOP_DTMF,	"STOP_DTMF" },
	{ GSM48_MT_CC_STOP_DTMF_ACK,	"STOP_DTMF_ACK" },
	{ GSM48_MT_CC_STATUS_ENQ,	"STATUS_ENQ" },
	{ GSM48_MT_CC_START_DTMF,	"START_DTMF" },
	{ GSM48_MT_CC_START_DTMF_ACK,	"START_DTMF_ACK" },
	{ GSM48_MT_CC_START_DTMF_REJ,	"START_DTMF_REJ" },
	{ GSM48_MT_CC_CONG_CTRL,	"CONG_CTRL" },
	{ GSM48_MT_CC_FACILITY,		"FACILITY" },
	{ GSM48_MT_CC_STATUS,		"STATUS" },
	{ GSM48_MT_CC_NOTIFY,		"NOTFIY" },
	{ 0,				NULL }
};

const char *gsm48_cc_msg_name(uint8_t msgtype)
{
	return get_value_string(cc_msg_names, msgtype);
}

const char *rr_cause_name(uint8_t cause)
{
	return get_value_string(rr_cause_names, cause);
}

static void to_bcd(uint8_t *bcd, uint16_t val)
{
	bcd[2] = val % 10;
	val = val / 10;
	bcd[1] = val % 10;
	val = val / 10;
	bcd[0] = val % 10;
	val = val / 10;
}

void gsm48_generate_lai(struct gsm48_loc_area_id *lai48, uint16_t mcc,
			uint16_t mnc, uint16_t lac)
{
	uint8_t bcd[3];

	to_bcd(bcd, mcc);
	lai48->digits[0] = bcd[0] | (bcd[1] << 4);
	lai48->digits[1] = bcd[2];

	to_bcd(bcd, mnc);
	/* FIXME: do we need three-digit MNC? See Table 10.5.3 */
	if (mnc > 99) {
		lai48->digits[1] |= bcd[2] << 4;
		lai48->digits[2] = bcd[0] | (bcd[1] << 4);
	} else {
		lai48->digits[1] |= 0xf << 4;
		lai48->digits[2] = bcd[1] | (bcd[2] << 4);
	}

	lai48->lac = htons(lac);
}

/* Attention: this function retunrs true integers, not hex! */
int gsm48_decode_lai(struct gsm48_loc_area_id *lai, uint16_t *mcc,
		     uint16_t *mnc, uint16_t *lac)
{
	*mcc = (lai->digits[0] & 0x0f) * 100
	     + (lai->digits[0] >> 4) * 10
	     + (lai->digits[1] & 0x0f);

	if ((lai->digits[1] & 0xf0) == 0xf0) {
		*mnc = (lai->digits[2] & 0x0f) * 10
		     + (lai->digits[2] >> 4);
	} else {
		*mnc = (lai->digits[2] & 0x0f) * 100
		     + (lai->digits[2] >> 4) * 10
		     + (lai->digits[1] >> 4);
	}
	*lac = ntohs(lai->lac);

	return 0;
}

int gsm48_generate_mid_from_tmsi(uint8_t *buf, uint32_t tmsi)
{
	uint32_t *tptr = (uint32_t *) &buf[3];

	buf[0] = GSM48_IE_MOBILE_ID;
	buf[1] = GSM48_TMSI_LEN;
	buf[2] = 0xf0 | GSM_MI_TYPE_TMSI;
	*tptr = htonl(tmsi);

	return 7;
}

int gsm48_generate_mid_from_imsi(uint8_t *buf, const char *imsi)
{
	unsigned int length = strlen(imsi), i, off = 0;
	uint8_t odd = (length & 0x1) == 1;

	buf[0] = GSM48_IE_MOBILE_ID;
	buf[2] = osmo_char2bcd(imsi[0]) << 4 | GSM_MI_TYPE_IMSI | (odd << 3);

	/* if the length is even we will fill half of the last octet */
	if (odd)
		buf[1] = (length + 1) >> 1;
	else
		buf[1] = (length + 2) >> 1;

	for (i = 1; i < buf[1]; ++i) {
		uint8_t lower, upper;

		lower = osmo_char2bcd(imsi[++off]);
		if (!odd && off + 1 == length)
			upper = 0x0f;
		else
			upper = osmo_char2bcd(imsi[++off]) & 0x0f;

		buf[2 + i] = (upper << 4) | lower;
	}

	return 2 + buf[1];
}

/* Convert Mobile Identity (10.5.1.4) to string */
int gsm48_mi_to_string(char *string, const int str_len, const uint8_t *mi,
		       const int mi_len)
{
	int i;
	uint8_t mi_type;
	char *str_cur = string;
	uint32_t tmsi;

	mi_type = mi[0] & GSM_MI_TYPE_MASK;

	switch (mi_type) {
	case GSM_MI_TYPE_NONE:
		break;
	case GSM_MI_TYPE_TMSI:
		/* Table 10.5.4.3, reverse generate_mid_from_tmsi */
		if (mi_len == GSM48_TMSI_LEN && mi[0] == (0xf0 | GSM_MI_TYPE_TMSI)) {
			memcpy(&tmsi, &mi[1], 4);
			tmsi = ntohl(tmsi);
			return snprintf(string, str_len, "%u", tmsi);
		}
		break;
	case GSM_MI_TYPE_IMSI:
	case GSM_MI_TYPE_IMEI:
	case GSM_MI_TYPE_IMEISV:
		*str_cur++ = osmo_bcd2char(mi[0] >> 4);

                for (i = 1; i < mi_len; i++) {
			if (str_cur + 2 >= string + str_len)
				return str_cur - string;
			*str_cur++ = osmo_bcd2char(mi[i] & 0xf);
			/* skip last nibble in last input byte when GSM_EVEN */
			if( (i != mi_len-1) || (mi[0] & GSM_MI_ODD))
				*str_cur++ = osmo_bcd2char(mi[i] >> 4);
		}
		break;
	default:
		break;
	}
	*str_cur++ = '\0';

	return str_cur - string;
}

void gsm48_parse_ra(struct gprs_ra_id *raid, const uint8_t *buf)
{
	raid->mcc = (buf[0] & 0xf) * 100;
	raid->mcc += (buf[0] >> 4) * 10;
	raid->mcc += (buf[1] & 0xf) * 1;

	/* I wonder who came up with the stupidity of encoding the MNC
	 * differently depending on how many digits its decimal number has! */
	if ((buf[1] >> 4) == 0xf) {
		raid->mnc = (buf[2] & 0xf) * 10;
		raid->mnc += (buf[2] >> 4) * 1;
	} else {
		raid->mnc = (buf[2] & 0xf) * 100;
		raid->mnc += (buf[2] >> 4) * 10;
		raid->mnc += (buf[1] >> 4) * 1;
	}

	raid->lac = ntohs(*(uint16_t *)(buf + 3));
	raid->rac = buf[5];
}

int gsm48_construct_ra(uint8_t *buf, const struct gprs_ra_id *raid)
{
	uint16_t mcc = raid->mcc;
	uint16_t mnc = raid->mnc;
	uint16_t _lac;

	buf[0] = ((mcc / 100) % 10) | (((mcc / 10) % 10) << 4);
	buf[1] = (mcc % 10);

	/* I wonder who came up with the stupidity of encoding the MNC
	 * differently depending on how many digits its decimal number has! */
	if (mnc < 100) {
		buf[1] |= 0xf0;
		buf[2] = ((mnc / 10) % 10) | ((mnc % 10) << 4);
	} else {
		buf[1] |= (mnc % 10) << 4;
		buf[2] = ((mnc / 100) % 10) | (((mnc / 10) % 10) << 4);
	}

	_lac = htons(raid->lac);
	memcpy(buf + 3, &_lac, 2);

	buf[5] = raid->rac;

	return 6;
}

/* From Table 10.5.33 of GSM 04.08 */
int gsm48_number_of_paging_subchannels(struct gsm48_control_channel_descr *chan_desc)
{
	unsigned int n_pag_blocks = gsm0502_get_n_pag_blocks(chan_desc);

	if (chan_desc->ccch_conf == RSL_BCCH_CCCH_CONF_1_C)
		return OSMO_MAX(1, n_pag_blocks) * (chan_desc->bs_pa_mfrms + 2);
	else
		return n_pag_blocks * (chan_desc->bs_pa_mfrms + 2);
}
