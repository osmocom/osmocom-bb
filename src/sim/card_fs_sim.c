/* classic SIM card specific structures/routines */
/*
 * (C) 2012-2014 by Harald Welte <laforge@gnumonks.org>
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
#include <string.h>

#include <osmocom/sim/sim.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gsm/gsm48.h>

#include "sim_int.h"

/* 3GPP TS 51.011 / Chapter 9.4 */
static const struct osim_card_sw ts11_11_sw[] = {
	{
		0x9000, 0xffff, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command",
	}, {
		0x9100, 0xff00, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command - proactive command from SIM pending",
	}, {
		0x9e00, 0xff00, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command - response data for SIM data download",
	}, {
		0x9f00, 0xff00, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command - response data available",
	}, {
		0x9300, 0xffff, SW_TYPE_STR, SW_CLS_POSTP,
		.u.str = "SIM Application Toolkit is busy, command cannot be executed at present",
	}, {
		0x9200, 0xfff0, SW_TYPE_STR, SW_CLS_WARN,
		.u.str = "Memory management - Command successful but after using an internal updat retry X times",
	}, {
		0x9240, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Memory management - Memory problem",
	}, {
		0x9400, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Referencing management - no EF selected",
	}, {
		0x9402, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Referencing management - out of range (invalid address)",
	}, {
		0x9404, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Referencing management - file ID not found / pattern not found",
	}, {
		0x9408, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Referencing management - file is inconsistent with the command",
	}, {
		0x9802, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - no CHV initialized",
	}, {
		0x9804, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - access condition not fulfilled",
	}, {
		0x9808, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - in contradiction with CHV status",
	}, {
		0x9810, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - in contradiction with invalidation status",
	}, {
		0x9840, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - unsuccessful CHV verification, no attempt left",
	}, {
		0x9850, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - increase cannot be performed, max value reached",
	}, {
		0x6700, 0xff00, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application independent - incorrect parameter P3",
	}, {
		0x6b00, 0xff00, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application independent - incorrect parameter P1 or P2",
	}, {
		0x6d00, 0xff00, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application independent - unknown instruction code",
	}, {
		0x6e00, 0xff00, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application independent - wrong instruction class",
	}, {
		0x6f00, 0xff00, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Application independent - technical problem with no diagnostic given",
	},
	OSIM_CARD_SW_LAST
};

static const struct osim_card_sw *sim_card_sws[] = {
	ts11_11_sw,
	NULL
};

static int iccid_decode(struct osim_decoded_data *dd,
			const struct osim_file_desc *desc,
	     		int len, uint8_t *data)
{
	struct osim_decoded_element *elem;

	elem = element_alloc(dd, "ICCID", ELEM_T_BCD, ELEM_REPR_DEC);
	elem->length = len;
	elem->u.buf = talloc_memdup(elem, data, len);

	return 0;
}

static int elp_decode(struct osim_decoded_data *dd,
		      const struct osim_file_desc *desc,
		      int len, uint8_t *data)
{
	int i, num_lp = len / 2;

	for (i = 0; i < num_lp; i++) {
		uint8_t *cur = data + i*2;
		struct osim_decoded_element *elem;
		elem = element_alloc(dd, "Language Code", ELEM_T_STRING, ELEM_REPR_NONE);
		elem->u.buf = (uint8_t *) talloc_strndup(elem, (const char *) cur, 2);
	}

	return 0;
}

/* 10.3.1 */
int gsm_lp_decode(struct osim_decoded_data *dd,
		 const struct osim_file_desc *desc,
		 int len, uint8_t *data)
{
	int i;

	for (i = 0; i < len; i++) {
		struct osim_decoded_element *elem;
		elem = element_alloc(dd, "Language Code", ELEM_T_UINT8, ELEM_REPR_DEC);
		elem->u.u8 = data[i];
	}

	return 0;
}

/* 10.3.2 */
int gsm_imsi_decode(struct osim_decoded_data *dd,
		   const struct osim_file_desc *desc,
		   int len, uint8_t *data)
{
	struct osim_decoded_element *elem;

	if (len < 2)
		return -EINVAL;

	elem = element_alloc(dd, "IMSI", ELEM_T_BCD, ELEM_REPR_DEC);
	elem->length = data[0];
	elem->u.buf = talloc_memdup(elem, data+1, len-1);

	return 0;
}

/* 10.3.3 */
static int gsm_kc_decode(struct osim_decoded_data *dd,
			 const struct osim_file_desc *desc,
			 int len, uint8_t *data)
{
	struct osim_decoded_element *kc, *cksn;

	if (len < 9)
		return -EINVAL;

	kc = element_alloc(dd, "Kc", ELEM_T_BYTES, ELEM_REPR_HEX);
	kc->u.buf = talloc_memdup(kc, data, 8);
	cksn = element_alloc(dd, "CKSN", ELEM_T_UINT8, ELEM_REPR_DEC);
	cksn->u.u8 = data[8];

	return 0;
}

/* 10.3.4 */
static int gsm_plmnsel_decode(struct osim_decoded_data *dd,
			      const struct osim_file_desc *desc,
			      int len, uint8_t *data)
{
	int i, n_plmn = len / 3;

	if (n_plmn < 1)
		return -EINVAL;

	for (i = 0; i < n_plmn; i++) {
		uint8_t *cur = data + 3*i;
		struct osim_decoded_element *elem, *mcc, *mnc;
		uint8_t ra_buf[6];
		struct gprs_ra_id ra_id;

		memset(ra_buf, 0, sizeof(ra_buf));
		memcpy(ra_buf, cur, 3);
		gsm48_parse_ra(&ra_id, ra_buf);

		elem = element_alloc(dd, "PLMN", ELEM_T_GROUP, ELEM_REPR_NONE);

		mcc = element_alloc_sub(elem, "MCC", ELEM_T_UINT16, ELEM_REPR_DEC);
		mcc->u.u16 = ra_id.mcc;

		mnc = element_alloc_sub(elem, "MNC", ELEM_T_UINT16, ELEM_REPR_DEC);
		mnc->u.u16 = ra_id.mnc;
	}

	return 0;
}

/* 10.3.5 */
int gsm_hpplmn_decode(struct osim_decoded_data *dd,
		     const struct osim_file_desc *desc,
		     int len, uint8_t *data)
{
	struct osim_decoded_element *elem;

	elem = element_alloc(dd, "Time interval", ELEM_T_UINT8, ELEM_REPR_DEC);
	elem->u.u8 = *data;

	return 0;
}

/* Chapter 10.1. Contents of the EFs at the MF level */
static const struct osim_file_desc sim_ef_in_mf[] = {
	EF_TRANSP(0x2FE2, SFI_NONE, "EF.ICCID", 0, 10, 10,
		  "ICC Identification", &iccid_decode, NULL),
	EF_TRANSP(0x2F05, SFI_NONE, "EF.PL", F_OPTIONAL, 2, 20,
		  "Preferred language", &elp_decode, NULL),
};

/* Chapter 10.3.x Contents of files at the GSM application level */
static const struct osim_file_desc sim_ef_in_gsm[] = {
	EF_TRANSP(0x6F05, SFI_NONE, "EF.LP", 0, 1, 16,
		  "Language preference", &gsm_lp_decode, NULL),
	EF_TRANSP(0x6F07, SFI_NONE, "EF.IMSI", 0, 9, 9,
		  "IMSI", &gsm_imsi_decode, NULL),
	EF_TRANSP(0x6F20, SFI_NONE, "EF.Kc", 0, 9, 9,
		  "Ciphering key Kc", &gsm_kc_decode, NULL),
	EF_TRANSP(0x6F30, SFI_NONE, "EF.PLMNsel", F_OPTIONAL, 24, 72,
		  "PLMN selector", &gsm_plmnsel_decode, NULL),
	EF_TRANSP(0x6F31, SFI_NONE, "EF.HPPLMN", 0, 1, 1,
		  "Higher Priority PLMN search period", &gsm_hpplmn_decode, NULL),
	EF_TRANSP_N(0x6F37, SFI_NONE, "EF.ACMmax", F_OPTIONAL, 3, 3,
		  "ACM maximum value"),
	EF_TRANSP_N(0x6F38, SFI_NONE, "EF.SST", 0, 2, 16,
		  "SIM service table"),
	EF_CYCLIC_N(0x6F39, SFI_NONE, "EF.ACM", F_OPTIONAL, 3, 3,
		  "Accumulated call meter"),
	EF_TRANSP_N(0x6F3E, SFI_NONE, "EF.GID1", F_OPTIONAL, 1, 8,
		  "Group Identifier Level 1"),
	EF_TRANSP_N(0x6F3F, SFI_NONE, "EF.GID2", F_OPTIONAL, 1, 8,
		  "Group Identifier Level 2"),
	EF_TRANSP_N(0x6F46, SFI_NONE, "EF.SPN", F_OPTIONAL, 17, 17,
		  "Service Provider Name"),
	EF_TRANSP_N(0x6F41, SFI_NONE, "EF.PUCT", F_OPTIONAL, 5, 5,
		  "Price per unit and currency table"),
	EF_TRANSP_N(0x6F45, SFI_NONE, "EF.CBMI", F_OPTIONAL, 2, 32,
		  "Cell broadcast massage identifier selection"),
	EF_TRANSP_N(0x6F74, SFI_NONE, "EF.BCCH", 0, 16, 16,
		  "Broadcast control channels"),
	EF_TRANSP_N(0x6F78, SFI_NONE, "EF.ACC", 0, 2, 2,
		  "Access control class"),
	EF_TRANSP_N(0x6F7B, SFI_NONE, "EF.FPLMN", 0, 12, 12,
		  "Forbidden PLMNs"),
	EF_TRANSP_N(0x6F7E, SFI_NONE, "EF.LOCI", 0, 11, 11,
		  "Location information"),
	EF_TRANSP_N(0x6FAD, SFI_NONE, "EF.AD", 0, 3, 8,
		  "Administrative data"),
	EF_TRANSP_N(0x6FAE, SFI_NONE, "EF.Phase", 0, 1, 1,
		  "Phase identification"),
	EF_TRANSP_N(0x6FB1, SFI_NONE, "EF.VGCS", F_OPTIONAL, 4, 80,
		  "Voice Group Call Service"),
	EF_TRANSP_N(0x6FB2, SFI_NONE, "EF.VGCSS", F_OPTIONAL, 7, 7,
		  "Voice Group Call Service Status"),
	EF_TRANSP_N(0x6FB3, SFI_NONE, "EF.VBS", F_OPTIONAL, 4, 80,
		  "Voice Broadcast Service"),
	EF_TRANSP_N(0x6FB4, SFI_NONE, "EF.VBSS", F_OPTIONAL, 7, 7,
		  "Voice Broadcast Service Status"),
	EF_TRANSP_N(0x6FB5, SFI_NONE, "EF.eMLPP", F_OPTIONAL, 2, 2,
		  "enhanced Mult Level Pre-emption and Priority"),
	EF_TRANSP_N(0x6FB6, SFI_NONE, "EF.AAeM", F_OPTIONAL, 1, 1,
		  "Automatic Answer for eMLPP Service"),
	EF_TRANSP_N(0x6F48, SFI_NONE, "EF.CBMID", F_OPTIONAL, 2, 32,
		  "Cell Broadcast Message Identifier for Data Download"),
	EF_TRANSP_N(0x6FB7, SFI_NONE, "EF.ECC", F_OPTIONAL, 3, 15,
		  "Emergency Call Code"),
	EF_TRANSP_N(0x6F50, SFI_NONE, "EF.CBMIR", F_OPTIONAL, 4, 64,
		  "Cell broadcast message identifier range selection"),
	EF_TRANSP_N(0x6F2C, SFI_NONE, "EF.DCK", F_OPTIONAL, 16, 16,
		  "De-personalization Control Keys"),
	EF_TRANSP_N(0x6F32, SFI_NONE, "EF.CNL", F_OPTIONAL, 6, 60,
		  "Co-operative Network List"),
	EF_LIN_FIX_N(0x6F51, SFI_NONE, "EF.NIA", F_OPTIONAL, 1, 17,
		   "Network's Indication of Alerting"),
	EF_TRANSP_N(0x6F52, SFI_NONE, "EF.KcGPRS", F_OPTIONAL, 9, 9,
		  "GPRS Ciphering key KcGPRS"),
	EF_TRANSP_N(0x6F53, SFI_NONE, "EF.LOCIGPRS", F_OPTIONAL, 14, 14,
		  "GPRS location information"),
	EF_TRANSP_N(0x6F54, SFI_NONE, "EF.SUME", F_OPTIONAL, 1, 64,
		  "SetUpMenu Elements"),
	EF_TRANSP_N(0x6F60, SFI_NONE, "EF.PLMNwAcT", F_OPTIONAL, 40, 80,
		  "User controlled PLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F61, SFI_NONE, "EF.OPLMNwAcT", F_OPTIONAL, 40, 80,
		  "Operator controlled PLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F62, SFI_NONE, "EF.HPLMNwAcT", F_OPTIONAL, 5, 20,
		  "HPLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F63, SFI_NONE, "EF.CPBCCH", F_OPTIONAL, 2, 20,
		  "CPBCCH Information"),
	EF_TRANSP_N(0x6F64, SFI_NONE, "EF.InvScan", F_OPTIONAL, 1, 1,
		  "Investigation Scan"),
	EF_LIN_FIX_N(0x6FC5, SFI_NONE, "EF.PNN", F_OPTIONAL, 3, 20,
		  "PLMN Network Name"),
	EF_LIN_FIX_N(0x6FC6, SFI_NONE, "EF.OPL", F_OPTIONAL, 8, 8,
		  "PLMN Operator PLMN List"),
	EF_LIN_FIX_N(0x6FC7, SFI_NONE, "EF.MBDN", F_OPTIONAL, 14, 30,
		  "Mailbox Dialling Number"),
	EF_LIN_FIX_N(0x6FC9, SFI_NONE, "EF.MBI", F_OPTIONAL, 4, 4,
		  "Maibox Identifier"),
	EF_LIN_FIX_N(0x6FCA, SFI_NONE, "EF.MWIS", F_OPTIONAL, 5, 5,
		  "Message Waiting Indication Status"),
	EF_LIN_FIX_N(0x6FCB, SFI_NONE, "EF.CFIS", F_OPTIONAL, 16, 16,
		  "Call Forwarding Indication Status"),
	EF_LIN_FIX_N(0x6FC8, SFI_NONE, "EF.EXT6", F_OPTIONAL, 13, 13,
		  "Extension6 (MBDN)"),
	EF_LIN_FIX_N(0x6FCC, SFI_NONE, "EF.EXT7", F_OPTIONAL, 13, 13,
		  "Extension7 (CFIS)"),
	EF_TRANSP_N(0x6FCD, SFI_NONE, "EF.SPDI", F_OPTIONAL, 1, 32,
		  "Extension7 (CFIS)"),
	EF_LIN_FIX_N(0x6FCE, SFI_NONE, "EF.MMSN", F_OPTIONAL, 4, 32,
		  "MMS Notification"),
	EF_LIN_FIX_N(0x6FCF, SFI_NONE, "EF.EXT8", F_OPTIONAL, 2, 18,
		  "Extension8 (MMSN)"),
	EF_TRANSP_N(0x6FD0, SFI_NONE, "EF.MMSICP", F_OPTIONAL, 1, 64,
		  "MMS Issuer Connectivity Parameters"),
	EF_LIN_FIX_N(0x6FD1, SFI_NONE, "EF.MMSUP", F_OPTIONAL, 1, 64,
		  "MMS User Preferences"),
	EF_TRANSP_N(0x6FD2, SFI_NONE, "EF.MMSUCP", F_OPTIONAL, 1, 64,
		  "MMS User Connectivity Parameters"),
};

/* 10.4.1 Contents of the files at the SoLSA level */
static const struct osim_file_desc sim_ef_in_solsa[] = {
	EF_TRANSP_N(0x4F30, SFI_NONE, "EF.SAI", F_OPTIONAL, 1, 32,
		"SoLSA Access Indicator"),
	EF_LIN_FIX_N(0x4F31, SFI_NONE, "EF.SLL", F_OPTIONAL, 1, 32,
		"SoLSA LSA List"),
	/* LSA Descriptor files */
};

/* 10.4.2 Contents of files at the MExE level */
static const struct osim_file_desc sim_ef_in_mexe[] = {
	EF_TRANSP_N(0x4F40, SFI_NONE, "EF.MExE-ST", F_OPTIONAL, 1, 8,
		"MExE Service table"),
	EF_LIN_FIX_N(0x4F41, SFI_NONE, "EF.ORPK", F_OPTIONAL, 11, 32,
		"Operator Root Public Key"),
	EF_LIN_FIX_N(0x4F42, SFI_NONE, "EF.ARPK", F_OPTIONAL, 11, 32,
		"Administrator Root Public Key"),
	EF_LIN_FIX_N(0x4F43, SFI_NONE, "EF.TRPK", F_OPTIONAL, 11, 32,
		"Third Party Root Public Key"),
};

/* 10.5 Contents of files at the telecom level */
static const struct osim_file_desc sim_ef_in_telecom[] = {
	EF_LIN_FIX_N(0x6F3A, SFI_NONE, "EF.ADN", F_OPTIONAL, 14, 30,
		"Abbreviated dialling numbers"),
	EF_LIN_FIX_N(0x6F3B, SFI_NONE, "EF.FDN", F_OPTIONAL, 14, 30,
		"Fixed dialling numbers"),
	EF_LIN_FIX_N(0x6F3C, SFI_NONE, "EF.SMS", F_OPTIONAL, 176, 176,
		"Short messages"),
	EF_LIN_FIX_N(0x6F3D, SFI_NONE, "EF.CCP", F_OPTIONAL, 14, 14,
		"Capability configuration parameters"),
	EF_LIN_FIX_N(0x6F4F, SFI_NONE, "EF.ECCP", F_OPTIONAL, 15, 15,
		"Extended Capability configuration parameters"),
	EF_LIN_FIX_N(0x6F40, SFI_NONE, "EF.MSISDN", F_OPTIONAL, 14, 30,
		"MSISDN"),
	EF_LIN_FIX_N(0x6F42, SFI_NONE, "EF.SMSP", F_OPTIONAL, 28, 44,
		"Short message service parameters"),
	EF_TRANSP_N(0x6F43, SFI_NONE, "EF.SMSS", F_OPTIONAL, 2, 3,
		"SMS Status"),
	EF_CYCLIC_N(0x6F44, SFI_NONE, "EF.LND", F_OPTIONAL, 14, 30,
		"Last number dialled"),
	EF_LIN_FIX_N(0x6F49, SFI_NONE, "EF.SDN", F_OPTIONAL, 14, 30,
		"Service Dialling Numbers"),
	EF_LIN_FIX_N(0x6F4A, SFI_NONE, "EF.EXT1", F_OPTIONAL, 13, 13,
		"Extension 1 (ADN/SSC, MSISDN, LND)"),
	EF_LIN_FIX_N(0x6F4B, SFI_NONE, "EF.EXT2", F_OPTIONAL, 13, 13,
		"Extension 2 (FDN/SSC)"),
	EF_LIN_FIX_N(0x6F4C, SFI_NONE, "EF.EXT3", F_OPTIONAL, 13, 13,
		"Extension 3 (SDN)"),
	EF_LIN_FIX_N(0x6F4D, SFI_NONE, "EF.BDN", F_OPTIONAL, 15, 31,
		"Barred dialling numbers"),
	EF_LIN_FIX_N(0x6F4E, SFI_NONE, "EF.EXT4", F_OPTIONAL, 13, 13,
		"Extension 4 (BDN/SSC)"),
	EF_LIN_FIX_N(0x6F47, SFI_NONE, "EF.SMSR", F_OPTIONAL, 30, 30,
		"Short message status reports"),
	EF_LIN_FIX_N(0x6F58, SFI_NONE, "EF.CMI", F_OPTIONAL, 1, 17,
		"Comparison Method Information"),
};

/* 10.6.1 Contents of files at the telecom graphics level */
const struct osim_file_desc sim_ef_in_graphics[] = {
	EF_LIN_FIX_N(0x4F20, SFI_NONE, "EF.IMG", F_OPTIONAL, 11, 38,
		"Image"),
};

int osim_int_cprof_add_gsm(struct osim_file_desc *mf)
{
	struct osim_file_desc *gsm;

	gsm = add_df_with_ef(mf, 0x7F20, "DF.GSM", sim_ef_in_gsm,
			ARRAY_SIZE(sim_ef_in_gsm));
	/* Chapter 10.2: DFs at the GSM Application Level */
	add_df_with_ef(gsm, 0x5F30, "DF.IRIDIUM", NULL, 0);
	add_df_with_ef(gsm, 0x5F31, "DF.GLOBALSTAR", NULL, 0);
	add_df_with_ef(gsm, 0x5F32, "DF.ICO", NULL, 0);
	add_df_with_ef(gsm, 0x5F33, "DF.ACeS", NULL, 0);
	add_df_with_ef(gsm, 0x5F3C, "DF.MExE", sim_ef_in_mexe,
			ARRAY_SIZE(sim_ef_in_mexe));
	add_df_with_ef(gsm, 0x5F40, "DF.EIA/TIA-533", NULL, 0);
	add_df_with_ef(gsm, 0x5F60, "DF.CTS", NULL, 0);
	add_df_with_ef(gsm, 0x5F70, "DF.SoLSA", sim_ef_in_solsa,
			ARRAY_SIZE(sim_ef_in_solsa));

	return 0;
}

int osim_int_cprof_add_telecom(struct osim_file_desc *mf)
{
	struct osim_file_desc *tc;

	tc = add_df_with_ef(mf, 0x7F10, "DF.TELECOM", sim_ef_in_telecom,
			ARRAY_SIZE(sim_ef_in_telecom));
	add_df_with_ef(tc, 0x5F50, "DF.GRAPHICS", sim_ef_in_graphics,
			ARRAY_SIZE(sim_ef_in_graphics));
	add_df_with_ef(mf, 0x7F22, "DF.IS-41", NULL, 0);
	add_df_with_ef(mf, 0x7F23, "DF.FP-CTS", NULL, 0);	/* TS 11.19 */

	return 0;
}

struct osim_card_profile *osim_cprof_sim(void *ctx)
{
	struct osim_card_profile *cprof;
	struct osim_file_desc *mf;
	int rc;

	cprof = talloc_zero(ctx, struct osim_card_profile);
	cprof->name = "GSM SIM";
	cprof->sws = sim_card_sws;

	mf = alloc_df(cprof, 0x3f00, "MF");

	cprof->mf = mf;

	/* According to Figure 8 */
	add_filedesc(mf, sim_ef_in_mf, ARRAY_SIZE(sim_ef_in_mf));

	rc = osim_int_cprof_add_gsm(mf);
	rc |= osim_int_cprof_add_telecom(mf);
	if (rc != 0) {
		talloc_free(cprof);
		return NULL;
	}

	return cprof;
}
