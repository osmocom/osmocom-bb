/* GSM Mobile Radio Interface Layer 3 messages
 * 3GPP TS 04.08 version 7.21.0 Release 1998 / ETSI TS 100 940 V7.21.0 */

/* (C) 2008-2009 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocore/utils.h>
#include <osmocore/tlv.h>
#include <osmocore/gsm48.h>

#include <osmocore/protocol/gsm_04_08.h>

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

static const char *rr_cause_names[] = {
	[GSM48_RR_CAUSE_NORMAL]			= "Normal event",
	[GSM48_RR_CAUSE_ABNORMAL_UNSPEC]	= "Abnormal release, unspecified",
	[GSM48_RR_CAUSE_ABNORMAL_UNACCT]	= "Abnormal release, channel unacceptable",
	[GSM48_RR_CAUSE_ABNORMAL_TIMER]		= "Abnormal release, timer expired",
	[GSM48_RR_CAUSE_ABNORMAL_NOACT]		= "Abnormal release, no activity on radio path",
	[GSM48_RR_CAUSE_PREMPTIVE_REL]		= "Preemptive release",
	[GSM48_RR_CAUSE_HNDOVER_IMP]		= "Handover impossible, timing advance out of range",
	[GSM48_RR_CAUSE_CHAN_MODE_UNACCT]	= "Channel mode unacceptable",
	[GSM48_RR_CAUSE_FREQ_NOT_IMPL]		= "Frequency not implemented",
	[GSM48_RR_CAUSE_CALL_CLEARED]		= "Call already cleared",
	[GSM48_RR_CAUSE_SEMANT_INCORR]		= "Semantically incorrect message",
	[GSM48_RR_CAUSE_INVALID_MAND_INF]	= "Invalid mandatory information",
	[GSM48_RR_CAUSE_MSG_TYPE_N]		= "Message type non-existant or not implemented",
	[GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT]	= "Message type not compatible with protocol state",
	[GSM48_RR_CAUSE_COND_IE_ERROR]		= "Conditional IE error",
	[GSM48_RR_CAUSE_NO_CELL_ALLOC_A]	= "No cell allocation available",
	[GSM48_RR_CAUSE_PROT_ERROR_UNSPC]	= "Protocol error unspecified",
};

const char *cc_state_names[32] = {
	"NULL",
	"INITIATED",
	"illegal state 2",
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

const char *gsm48_cc_msg_names[0x40] = {
	"unknown 0x00",
	"ALERTING",
	"CALL_PROC",
	"PROGRESS",
	"ESTAB",
	"SETUP",
	"ESTAB_CONF",
	"CONNECT",
	"CALL_CONF",
	"START_CC",
	"unknown 0x0a",
	"RECALL",
	"unknown 0x0c",
	"unknown 0x0d",
	"EMERG_SETUP",
	"CONNECT_ACK",
	"USER_INFO",
	"unknown 0x11",
	"unknown 0x12",
	"MODIFY_REJECT",
	"unknown 0x14",
	"unknown 0x15",
	"unknown 0x16",
	"MODIFY",
	"HOLD",
	"HOLD_ACK",
	"HOLD_REJ",
	"unknown 0x1b",
	"RETR",
	"RETR_ACK",
	"RETR_REJ",
	"MODIFY_COMPL",
	"unknown 0x20",
	"unknown 0x21",
	"unknown 0x22",
	"unknown 0x23",
	"unknown 0x24",
	"DISCONNECT",
	"unknown 0x26",
	"unknown 0x27",
	"unknown 0x28",
	"unknown 0x29",
	"RELEASE_COMPL",
	"unknown 0x2b",
	"unknown 0x2c",
	"RELEASE",
	"unknown 0x2e",
	"unknown 0x2f",
	"unknown 0x30",
	"STOP_DTMF",
	"STOP_DTMF_ACK",
	"unknown 0x33",
	"STATUS_ENQ",
	"START_DTMF",
	"START_DTMF_ACK",
	"START_DTMF_REJ",
	"unknown 0x38",
	"CONG_CTRL",
	"FACILITY",
	"unknown 0x3b",
	"STATUS",
	"unknown 0x3d",
	"NOTIFY",
	"unknown 0x3f",
};

static char strbuf[64];

const char *rr_cause_name(uint8_t cause)
{
	if (cause < ARRAY_SIZE(rr_cause_names) &&
	    rr_cause_names[cause])
		return rr_cause_names[cause];

	snprintf(strbuf, sizeof(strbuf), "0x%02x", cause);
	return strbuf;
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
#if 0
	lai48->digits[1] |= bcd[2] << 4;
	lai48->digits[2] = bcd[0] | (bcd[1] << 4);
#else
	lai48->digits[1] |= 0xf << 4;
	lai48->digits[2] = bcd[1] | (bcd[2] << 4);
#endif

	lai48->lac = htons(lac);
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
	buf[2] = char2bcd(imsi[0]) << 4 | GSM_MI_TYPE_IMSI | (odd << 3);

	/* if the length is even we will fill half of the last octet */
	if (odd)
		buf[1] = (length + 1) >> 1;
	else
		buf[1] = (length + 2) >> 1;

	for (i = 1; i < buf[1]; ++i) {
		uint8_t lower, upper;

		lower = char2bcd(imsi[++off]);
		if (!odd && off + 1 == length)
			upper = 0x0f;
		else
			upper = char2bcd(imsi[++off]) & 0x0f;

		buf[2 + i] = (upper << 4) | lower;
	}

	return 2 + buf[1];
}
