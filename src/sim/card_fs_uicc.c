/* ETSI UICC specific structures / routines */
/*
 * (C) 2012 by Harald Welte <laforge@gnumonks.org>
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


#include <osmocom/sim/sim.h>
#include <osmocom/gsm/tlv.h>

/* TS 102 221 V10.0.0 / 10.2.1 */
const struct osim_card_sw ts102221_uicc_sw[] = {
	{
		0x9000, 0xffff, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command",
	}, {
		0x9100, 0xff00, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command, extra info proactive",
	}, {
		0x9200, 0xff00, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command, extra info regarding transfer session",
	}, {
		0x9300, 0xff00, SW_TYPE_STR, SW_CLS_POSTP,
		.u.str = "SIM Application Toolkit is busy, command cannot be executed at present",
	}, {
		0x6200, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "No information given, state of non volatile memory unchanged",
	}, {
		0x6281, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "Part of returned data may be corrupted",
	}, {
		0x6282, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "End of file/record reached before reading Le bytes",
	}, {
		0x6283, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "Selected file invalidated",
	}, {
		0x6285, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "Selected file in termination state",
	}, {
		0x62f1, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "More data available",
	}, {
		0x62f2, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "More data available and proactive command pending",
	}, {
		0x62f3, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "Response data available",
	}, {
		0x63f1, 0xffff, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "More data expected",
	}, {
		0x63c0, 0xfff0, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "Verification falied, X retries remaining",
	}, {
		0x6400, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Execution - No information given, state of non-volatile memory unchanged",
	}, {
		0x6500, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Execution - No information given, state of non-volatile memory changed",
	}, {
		0x6581, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Execution - Memory problem",
	}, {
		0x6700, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Checking - Wrong length",
	}, {
		0x6700, 0xff00, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Checking - Command dependent error",
	}, {
		0x6b00, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Checking - Wrong parameter(s) P1-P2",
	}, {
		0x6d00, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Checking - Instruction code not supported or valid",
	}, {
		0x6e00, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Checking - Class not supported",
	}, {
		0x6f00, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Checking - Technical problem, no precise diagnostics",
	}, {
		0x6f00, 0xff00, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Checking - Command dependent error",
	}, {
		0x6800, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Function in CLA not supported - No information given",
	}, {
		0x6881, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Function in CLA not supported - Logical channel not supported",
	}, {
		0x6882, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Function in CLA not supportied - Secure messaging not supported",
	}, {
		0x6900, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - No information given",
	}, {
		0x6981, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - Command incompatible with file structure",
	}, {
		0x6982, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - Security status not satisfied",
	}, {
		0x6983, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - Authentication/PIN method blocked",
	}, {
		0x6984, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - Referenced data invalidated",
	}, {
		0x6985, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - Conditions of use not satisfied",
	}, {
		0x6986, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - Noe EF selected",
	}, {
		0x6989, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Command not allowed - secure channel - security not satisfied",
	}, {
		0x6a80, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - Incorrect parameters in the data field",
	}, {
		0x6a81, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - Function not supported",
	}, {
		0x6a82, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - File not found",
	}, {
		0x6a83, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - Record not found",
	}, {
		0x6a84, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - Not enough memory space",
	}, {
		0x6a86, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - Incorrect parameters P1 to P2",
	}, {
		0x6a87, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - Lc inconsistent with P1 ot P2",
	}, {
		0x6a88, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Wrong parameters - Referenced data not found",
	}, {
		0x9850, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application error - INCREASE cannot be performed, max value reached",
	}, {
		0x9862, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application error - Authentication error, application specific",
	}, {
		0x9863, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application error - Security session or association expired",
	},
	OSIM_CARD_SW_LAST
};

const struct value_string ts102221_fcp_vals[14] = {
	{ UICC_FCP_T_FCP,		"File control parameters" },
	{ UICC_FCP_T_FILE_SIZE,		"File size" },
	{ UICC_FCP_T_TOT_F_SIZE,	"Total size of files" },
	{ UICC_FCP_T_FILE_DESC,		"File descriptor" },
	{ UICC_FCP_T_FILE_ID,		"File identifier" },
	{ UICC_FCP_T_DF_NAME,		"DF name" },
	{ UICC_FCP_T_SFID,		"Short file identifier" },
	{ UICC_FCP_T_LIFEC_STS,		"Lifecycle status integer" },
	{ UICC_FCP_T_SEC_ATTR_REFEXP,	"Security attributes (Referenced/Expanded)" },
	{ UICC_FCP_T_SEC_ATTR_COMP,	"Security attributes (Compact)" },
	{ UICC_FCP_T_PROPRIETARY,	"Proprietary" },
	{ UICC_FCP_T_SEC_ATTR_EXP,	"Security attributes (Expanded)" },
	{ UICC_FCP_T_PIN_STS_DO,	"PIN Status DO" },
	{ 0, NULL }
};

/* FIXME: Ber-TLV ?? */
const struct tlv_definition ts102221_fcp_tlv_def = {
	.def = {
		[UICC_FCP_T_FCP]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_FILE_SIZE]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_TOT_F_SIZE]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_FILE_DESC]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_FILE_ID]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_DF_NAME]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_SFID]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_LIFEC_STS]		= { TLV_TYPE_TLV },
		[UICC_FCP_T_SEC_ATTR_REFEXP]	= { TLV_TYPE_TLV },
		[UICC_FCP_T_SEC_ATTR_COMP]	= { TLV_TYPE_TLV },
		[UICC_FCP_T_PROPRIETARY] 	= { TLV_TYPE_TLV },
		[UICC_FCP_T_SEC_ATTR_EXP]	= { TLV_TYPE_TLV },
		[UICC_FCP_T_PIN_STS_DO]		= { TLV_TYPE_TLV },
	},
};

/* Annex E - TS 101 220 */
static const uint8_t adf_uicc_aid[] = { 0xA0, 0x00, 0x00, 0x00, 0x87, 0x10, 0x01 };
