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

const char *cc_state_names[] = {
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

static char strbuf[64];

const char *rr_cause_name(uint8_t cause)
{
	if (cause < ARRAY_SIZE(rr_cause_names) &&
	    rr_cause_names[cause])
		return rr_cause_names[cause];

	snprintf(strbuf, sizeof(strbuf), "0x%02x", cause);
	return strbuf;
}


