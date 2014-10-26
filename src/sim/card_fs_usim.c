/* 3GPP USIM specific structures / routines */
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
#include "gsm_int.h"

/* TS 31.102 Version 7.7.0 / Chapter 7.3 */
const struct osim_card_sw ts31_102_sw[] = {
	{
		0x9862, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - Authentication error, incorrect MAC",
	}, {
		0x9864, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - Authentication error, security context not supported",
	}, {
		0x9865, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - Key freshness error",
	},
	OSIM_CARD_SW_LAST
};

static const struct osim_card_sw *usim_card_sws[] = {
	ts31_102_sw,
	ts102221_uicc_sw,
	NULL
};

/* TS 102 221 Chapter 13.1 */
static const struct osim_file_desc uicc_ef_in_mf[] = {
	EF_LIN_FIX_N(0x2f00, SFI_NONE, "EF.DIR", 0, 1, 32,
			"Application directory"),
	EF_TRANSP_N(0x2FE2, SFI_NONE, "EF.ICCID", 0, 10, 10,
			"ICC Identification"),
	EF_TRANSP_N(0x2F05, SFI_NONE, "EF.PL", 0, 2, 20,
			"Preferred Languages"),
	EF_LIN_FIX_N(0x2F06, SFI_NONE, "EF.ARR", F_OPTIONAL, 1, 256,
			"Access Rule Reference"),
};

/* 31.102 Chapter 4.4.3 */
static const struct osim_file_desc usim_ef_in_df_gsm_access[] = {
	EF_TRANSP_N(0x4f20, 0x01, "EF.Kc", 0, 9, 9,
		"Ciphering Key Kc"),
	EF_TRANSP_N(0x4f52, 0x02, "EF.KcGPRS", F_OPTIONAL, 9, 9,
		"GPRS Ciphering key KcGPRS"),
	EF_TRANSP_N(0x4f63, SFI_NONE, "EF.CPBCCH", F_OPTIONAL, 2, 20,
		"CPBCCH Information"),
	EF_TRANSP_N(0x4f64, SFI_NONE, "EF.invSCAN", F_OPTIONAL, 1, 1,
		"Investigation Scan"),
};

/* 31.102 Chapter 4.2 */
static const struct osim_file_desc usim_ef_in_adf_usim[] = {
	EF_TRANSP(0x6F05, 0x02, "EF.LI", 0, 2, 16,
		"Language Indication", &gsm_lp_decode, NULL),
	EF_TRANSP(0x6F07, 0x07, "EF.IMSI", 0, 9, 9,
		"IMSI", &gsm_imsi_decode, NULL),
	EF_TRANSP_N(0x6F08, 0x08, "EF.Keys", 0, 33, 33,
		"Ciphering and Integrity Keys"),
	EF_TRANSP_N(0x6F09, 0x09, "EF.KeysPS", 0, 33, 33,
		"Ciphering and Integrity Keys for Packet Switched domain"),
	EF_TRANSP_N(0x6F60, 0x0A, "EF.PLMNwAcT", F_OPTIONAL, 40, 80,
		"User controlled PLMN Selector with Access Technology"),
	EF_TRANSP(0x6F31, 0x12, "EF.HPPLMN", 0, 1, 1,
		"Higher Priority PLMN search period", &gsm_hpplmn_decode, NULL),
	EF_TRANSP_N(0x6F37, SFI_NONE, "EF.ACMmax", F_OPTIONAL, 3, 3,
		"ACM maximum value"),
	EF_TRANSP_N(0x6F38, 0x04, "EF.UST", 0, 1, 16,
		"USIM Service Table"),
	EF_CYCLIC_N(0x6F39, SFI_NONE, "EF.ACM", F_OPTIONAL, 3, 3,
		"Accumulated call meter"),
	EF_TRANSP_N(0x6F3E, SFI_NONE, "EF.GID1", F_OPTIONAL, 1, 4,
		"Group Identifier Level 1"),
	EF_TRANSP_N(0x6F3F, SFI_NONE, "EF.GID2", F_OPTIONAL, 1, 4,
		"Group Identifier Level 2"),
	EF_TRANSP_N(0x6F46, SFI_NONE, "EF.SPN", F_OPTIONAL, 17, 17,
		"Service Provider Name"),
	EF_TRANSP_N(0x6F41, SFI_NONE, "EF.PUCT", F_OPTIONAL, 5, 5,
		"Price per unit and currency table"),
	EF_TRANSP_N(0x6F45, SFI_NONE, "EF.CBMI", F_OPTIONAL, 2, 32,
		"Cell broadcast massage identifier selection"),
	EF_TRANSP_N(0x6F78, 0x06, "EF.ACC", 0, 2, 2,
		"Access control class"),
	EF_TRANSP_N(0x6F7B, 0x0D, "EF.FPLMN", 0, 12, 36,
		"Forbidden PLMNs"),
	EF_TRANSP_N(0x6F7E, 0x0B, "EF.LOCI", 0, 11, 11,
		"Location information"),
	EF_TRANSP_N(0x6FAD, 0x03, "EF.AD", 0, 5, 16,
		"Administrative data"),
	EF_TRANSP_N(0x6F48, 0x0E, "EF.CBMID", F_OPTIONAL, 2, 32,
		"Cell Broadcast Message Identifier for Data Download"),
	EF_TRANSP_N(0x6FB7, 0x01, "EF.ECC", F_OPTIONAL, 5, 32,
		"Emergency Call Code"),
	EF_TRANSP_N(0x6F50, SFI_NONE, "EF.CBMIR", F_OPTIONAL, 4, 32,
		"Cell broadcast message identifier range selection"),
	EF_TRANSP_N(0x6F73, 0x0C, "EF.PSLOCI", 0, 14, 14,
		"Pacet Switched location information"),
	EF_LIN_FIX_N(0x6F3B, SFI_NONE, "EF.FDN", F_OPTIONAL, 14, 32,
		"Fixed dialling numbers"),
	EF_LIN_FIX_N(0x6F3C, SFI_NONE, "EF.SMS", F_OPTIONAL, 176, 176,
		"Short messages"),
	EF_LIN_FIX_N(0x6F40, SFI_NONE, "EF.MSISDN", F_OPTIONAL, 14, 32,
		"MSISDN"),
	EF_LIN_FIX_N(0x6F42, SFI_NONE, "EF.SMSP", F_OPTIONAL, 28, 64,
		"Short message service parameters"),
	EF_TRANSP_N(0x6F43, SFI_NONE, "EF.SMSS", F_OPTIONAL, 2, 8,
		"SMS Status"),
	EF_LIN_FIX_N(0x6F49, SFI_NONE, "EF.SDN", F_OPTIONAL, 14, 32,
		"Service Dialling Numbers"),
	EF_LIN_FIX_N(0x6F4B, SFI_NONE, "EF.EXT2", F_OPTIONAL, 13, 13,
		"Extension 2"),
	EF_LIN_FIX_N(0x6F4C, SFI_NONE, "EF.EXT3", F_OPTIONAL, 13, 13,
		"Extension 3"),
	EF_LIN_FIX_N(0x6F47, SFI_NONE, "EF.SMSR", F_OPTIONAL, 30, 30,
		"Short message status reports"),
	EF_CYCLIC_N(0x6F80, 0x14, "EF.ICI", F_OPTIONAL, 28, 64,
		"Incoming Calling Information"),
	EF_CYCLIC_N(0x6F81, 0x15, "EF.OCI", F_OPTIONAL, 27, 64,
		"Outgoing Calling Information"),
	EF_CYCLIC_N(0x6F82, SFI_NONE, "EF.ICT", F_OPTIONAL, 3, 3,
		"Incoming Call Timer"),
	EF_CYCLIC_N(0x6F83, SFI_NONE, "EF.OCT", F_OPTIONAL, 3, 3,
		"Outgoing Call Timer"),
	EF_LIN_FIX_N(0x6F4E, SFI_NONE, "EF.EXT5", F_OPTIONAL, 13, 13,
		"Extension 5"),
	EF_LIN_FIX_N(0x6F4F, 0x16, "EF.CCP2", F_OPTIONAL, 15, 32,
		"Capability Configuration Parameters 2"),
	EF_TRANSP_N(0x6FB5, SFI_NONE, "EF.eMLPP", F_OPTIONAL, 2, 2,
		"enhanced Multi Level Precedence and Pre-emption"),
	EF_TRANSP_N(0x6FB6, SFI_NONE, "EF.AAeM", F_OPTIONAL, 1, 1,
		"Automatic Answer for eMLPP Service"),
	EF_TRANSP_N(0x6FC3, SFI_NONE, "EF.Hiddenkey", F_OPTIONAL, 4, 4,
		"Key for hidden phone book entries"),
	EF_LIN_FIX_N(0x6F4D, SFI_NONE, "EF.BDN", F_OPTIONAL, 15, 32,
		"Barred Dialling Numbers"),
	EF_LIN_FIX_N(0x6F4E, SFI_NONE, "EF.EXT4", F_OPTIONAL, 13, 13,
		"Extension 4"),
	EF_LIN_FIX_N(0x6F58, SFI_NONE, "EF.CMI", F_OPTIONAL, 2, 16,
		"Comparison Method Information"),
	EF_TRANSP_N(0x6F56, 0x05, "EF.EST", F_OPTIONAL, 1, 8,
		"Enhanced Services Table"),
	EF_TRANSP_N(0x6F57, SFI_NONE, "EF.ACL", F_OPTIONAL, 2, 256,
		"Access Point Name Control List"),
	EF_TRANSP_N(0x6F2C, SFI_NONE, "EF.DCK", F_OPTIONAL, 16, 16,
		"Depersonalisation Control Keys"),
	EF_TRANSP_N(0x6F32, SFI_NONE, "EF.CNL", F_OPTIONAL, 6, 46,
		"Co-operative Network List"),
	EF_TRANSP_N(0x6F5B, 0x0F, "EF.START-HFN", 0, 6, 6,
		"Initialisation values for Hyperframe number"),
	EF_TRANSP_N(0x6F5C, 0x10, "EF.THRESHOLD", 0, 3, 3,
		"Maximum value of START"),
	EF_TRANSP_N(0x6F61, 0x11, "EF.OPLMNwAcT", F_OPTIONAL, 40, 80,
		"Operator controlled PLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F62, 0x13, "EF.HPLMNwAcT", F_OPTIONAL, 5, 30,
		"HPLMN Selector with Access Technology"),
	EF_LIN_FIX_N(0x6F06, 0x17, "EF.ARR", 0, 1, 256,
		"Access Rule Reference"),
	EF_TRANSP_N(0x6FC4, SFI_NONE, "EF.NETPAR", 0, 46, 256,
		"Network Parameters"),
	EF_LIN_FIX_N(0x6FC5, 0x19, "EF.PNN", F_OPTIONAL, 3, 128,
		"PLMN Network Name"),
	EF_LIN_FIX_N(0x6FC6, 0x1A, "EF.OPL", F_OPTIONAL, 8, 32,
		"Operator PLMN List"),
	EF_LIN_FIX_N(0x6FC7, SFI_NONE, "EF.MBDN", F_OPTIONAL, 14, 32,
		"Mailbox Dialling Numbers"),
	EF_LIN_FIX_N(0x6FC8, SFI_NONE, "EF.EXT6", F_OPTIONAL, 13, 13,
		"Extension 6"),
	EF_LIN_FIX_N(0x6FC9, SFI_NONE, "EF.MBI", F_OPTIONAL, 4, 5,
		"Mailbox Identifier"),
	EF_LIN_FIX_N(0x6FCA, SFI_NONE, "EF.MWIS", F_OPTIONAL, 5, 6,
		"Message Waiting Indication Status"),
	EF_LIN_FIX_N(0x6FCB, SFI_NONE, "EF.CFIS", F_OPTIONAL, 16, 16,
		"Call Forwarding Indication Status"),
	EF_LIN_FIX_N(0x6FCC, SFI_NONE, "EF.EXT7", F_OPTIONAL, 13, 13,
		"Extension 7"),
	EF_TRANSP_N(0x6FCD, 0x1B, "EF.SPDI", F_OPTIONAL, 1, 64,
		"Service Provider Display Information"),
	EF_LIN_FIX_N(0x6FCE, SFI_NONE, "EF.MMSN", F_OPTIONAL, 4, 32,
		"MMS Notification"),
	EF_LIN_FIX_N(0x6FCF, SFI_NONE, "EF.EXT8", F_OPTIONAL, 3, 16,
		"Extension 8"),
	EF_TRANSP_N(0x6FD0, SFI_NONE, "EF.MMSICP", F_OPTIONAL, 3, 256,
		"MMS Issuer Connectivity Parameters"),
	EF_LIN_FIX_N(0x6FD1, SFI_NONE, "EF.MMSUP", F_OPTIONAL, 1, 64,
		"MMS User Preferences"),
	EF_TRANSP_N(0x6FD2, SFI_NONE, "EF.MMSUCP", F_OPTIONAL, 1, 64,
		"MMS User Connectivity Parameters"),
	EF_LIN_FIX_N(0x6FD3, SFI_NONE, "EF.NIA", F_OPTIONAL, 2, 64,
		"Network's Indication of Alerting"),
	EF_TRANSP_N(0x6FB1, SFI_NONE, "EF.VGCS", F_OPTIONAL, 4, 80,
		"Voice Group Call Service"),
	EF_TRANSP_N(0x6FB2, SFI_NONE, "EF.VGCSS", F_OPTIONAL, 7, 7,
		"Voice Group Call Service Status"),
	EF_TRANSP_N(0x6FB3, SFI_NONE, "EF.VBS", F_OPTIONAL, 4, 80,
		"Voice Broadcast Service"),
	EF_TRANSP_N(0x6FB4, SFI_NONE, "EF.VBSS", F_OPTIONAL, 7, 7,
		"Voice Broadcast Service Status"),
	EF_TRANSP_N(0x6FD4, SFI_NONE, "EF.VGCSCA", F_OPTIONAL, 2, 40,
		"Voice Group Call Service Ciphering Algorithm"),
	EF_TRANSP_N(0x6FD5, SFI_NONE, "EF.VBSCA", F_OPTIONAL, 2, 40,
		"Voice Broadcast Service Ciphering Algorithm"),
	EF_TRANSP_N(0x6FD6, SFI_NONE, "EF.GBABP", F_OPTIONAL, 4, 128,
		"GBA Bootstrapping parameters"),
	EF_LIN_FIX_N(0x6FD7, SFI_NONE, "EF.MSK", F_OPTIONAL, 20, 84,
		"MBMS Serviec Key List"),
	EF_LIN_FIX_N(0x6FD8, SFI_NONE, "EF.MUK", F_OPTIONAL, 1, 128,
		"MBMS User Key"),
	EF_LIN_FIX_N(0x6FDA, SFI_NONE, "EF.GBANL", F_OPTIONAL, 1, 128,
		"GBA NAF List"),
	EF_TRANSP_N(0x6FD9, 0x1D, "EF.EHPLMN", F_OPTIONAL, 3, 30,
		"Equivalent HPLMN"),
	EF_TRANSP_N(0x6FDB, SFI_NONE, "EF.EHPLMNPI", F_OPTIONAL, 1, 1,
		"Equivalent HPLMN Presentation Indication"),
	EF_TRANSP_N(0x6FDC, SFI_NONE, "EF.LRPLMNSI", F_OPTIONAL, 1, 1,
		"Last RPLMN Selection Indication"),
	EF_LIN_FIX_N(0x6FDD, SFI_NONE, "EF.NAFKCA", F_OPTIONAL, 1, 128,
		"NAF Key Centre Address"),
	EF_TRANSP_N(0x6FDE, SFI_NONE, "EF.SPNI", F_OPTIONAL, 1, 128,
		"Service Provider Name Icon"),
	EF_LIN_FIX_N(0x6FDF, SFI_NONE, "EF.PNNI", F_OPTIONAL, 1, 128,
		"PLMN Network Name Icon"),
	EF_LIN_FIX_N(0x6FE2, SFI_NONE, "EF.NCP-IP", F_OPTIONAL, 2, 256,
		"Network Connectivity Parameters for USIM IP Connections"),
	EF_TRANSP_N(0x6FE3, 0x1E, "EF.EPSLOCI", F_OPTIONAL, 18, 18,
		"EPS location information"),
	EF_LIN_FIX_N(0x6FE4, 0x18, "EF.EPSNSC", F_OPTIONAL, 54, 128,
		"EPS NAS Security Context"),
	EF_TRANSP_N(0x6FE6, SFI_NONE, "EF.UFC", F_OPTIONAL, 1, 8,
		"USAT Facility Control"),
	EF_TRANSP_N(0x6FE8, SFI_NONE, "EF.NASCONFIG", F_OPTIONAL, 1, 128,
		"Non Access Stratum Configuration"),
	EF_LIN_FIX_N(0x6FE7, SFI_NONE, "EF.UICCIARI", F_OPTIONAL, 1, 32,
		"UICC IARI"),
	EF_TRANSP_N(0x6FEC, SFI_NONE, "EF.PWS", F_OPTIONAL, 1, 32,
		"Public Warning System"),
};



/* 31.102 Chapter 4.4.1 */
static const struct osim_file_desc usim_ef_in_solsa[] = {
	EF_TRANSP_N(0x4F30, SFI_NONE, "EF.SAI", F_OPTIONAL, 2, 32,
		"SoLSA Access Indicator"),
	EF_LIN_FIX_N(0x4F31, SFI_NONE, "EF.SLL", F_OPTIONAL, 11, 32,
		"SoLSA LSA List"),
	/* LSA descriptor files 4Fxx, hard to represent here */
};

/* 31.102 Chapter 4.4.4 */
static const struct osim_file_desc usim_ef_in_df_mexe[] = {
	EF_TRANSP_N(0x4F40, SFI_NONE, "EF.MexE-ST", F_OPTIONAL, 1, 4,
		"MexE Service table"),
	EF_LIN_FIX_N(0x4F41, SFI_NONE, "EF.ORPK", F_OPTIONAL, 10, 16,
		"Operator Root Public Key"),
	EF_LIN_FIX_N(0x4F42, SFI_NONE, "EF.ARPK", F_OPTIONAL, 10, 16,
		"Administrator Root Public Key"),
	EF_LIN_FIX_N(0x4F43, SFI_NONE, "EF.TPRPK", F_OPTIONAL, 10, 16,
		"Third Party Root Public Key"),
	/* TKCDF files 4Fxx, hard to represent here */
};

/* 31.102 Chapter 4.4.5 */
static const struct osim_file_desc usim_ef_in_df_wlan[] = {
	EF_TRANSP_N(0x4F41, 0x01, "EF.Pseudo", F_OPTIONAL, 2, 16,
		"Pseudonym"),
	EF_TRANSP_N(0x4F42, 0x02, "EF.UPLMNWLAN", F_OPTIONAL, 30, 60,
		"User controlled PLMN selector for I-WLAN Access"),
	EF_TRANSP_N(0x4F43, 0x03, "EF.OPLMNWLAN", F_OPTIONAL, 30, 60,
		"Operator controlled PLMN selector for I-WLAN Access"),
	EF_LIN_FIX_N(0x4F44, 0x04, "EF.UWSIDL", F_OPTIONAL, 1, 16,
		"User controlled WLAN Specific Identifier List"),
	EF_LIN_FIX_N(0x4F45, 0x05, "EF.OWSIDL", F_OPTIONAL, 1, 16,
		"Operator controlled WLAN Specific Identifier List"),
	EF_TRANSP_N(0x4F46, 0x06, "EF.WRI", F_OPTIONAL, 1, 64,
		"WLAN Reauthentication Identity"),
	EF_LIN_FIX_N(0x4F47, 0x07, "EF.HWSIDL", F_OPTIONAL, 1, 16,
		"Home I-WLAN Specific Identifier List"),
	EF_TRANSP_N(0x4F48, 0x08, "EF.WEPLMNPI", F_OPTIONAL, 1, 1,
		"I-WLAN Equivalent HPLMN Presentation Indication"),
	EF_TRANSP_N(0x4F49, 0x09, "EF.WHPI", F_OPTIONAL, 1, 1,
		"I-WLAN HPLMN Priority Indiation"),
	EF_TRANSP_N(0x4F4A, 0x0a, "EF.WLRPLMN", F_OPTIONAL, 3, 3,
		"I-WLAN Last Registered PLMN"),
};

/* 31.102 Chapter 4.4.6 */
static const struct osim_file_desc usim_ef_in_df_hnb[] = {
	EF_LIN_FIX_N(0x4F81, 0x01, "EF.ACSGL", F_OPTIONAL, 1, 128,
		"Allowed CSG Lists"),
	EF_LIN_FIX_N(0x4F82, 0x02, "EF.CSGT", F_OPTIONAL, 1, 64,
		"CSG Type"),
	EF_LIN_FIX_N(0x4F83, 0x03, "EF.HNBN", F_OPTIONAL, 1, 64,
		"Home NodeB Name"),
	EF_LIN_FIX_N(0x4F84, 0x04, "EF.OCSGL", F_OPTIONAL, 1, 64,
		"Operator CSG List"),
	EF_LIN_FIX_N(0x4F85, 0x05, "EF.OCSGT", F_OPTIONAL, 1, 64,
		"Operator CSG Type"),
	EF_LIN_FIX_N(0x4F86, 0x06, "EF.OHNBN", F_OPTIONAL, 1, 64,
		"Oprator Home NodeB Name"),
};

/* Annex E - TS 101 220 */
static const uint8_t adf_usim_aid[] = { 0xA0, 0x00, 0x00, 0x00, 0x87, 0x10, 0x02 };

struct osim_card_profile *osim_cprof_usim(void *ctx)
{
	struct osim_card_profile *cprof;
	struct osim_file_desc *mf, *uadf;
	int rc;

	cprof = talloc_zero(ctx, struct osim_card_profile);
	cprof->name = "3GPP USIM";
	cprof->sws = usim_card_sws;

	mf = alloc_df(cprof, 0x3f00, "MF");

	cprof->mf = mf;

	/* Core UICC Files */
	add_filedesc(mf, uicc_ef_in_mf, ARRAY_SIZE(uicc_ef_in_mf));

	/* ADF.USIM with its EF siblings */
	uadf = add_adf_with_ef(mf, adf_usim_aid, sizeof(adf_usim_aid),
				"ADF.USIM", usim_ef_in_adf_usim,
				ARRAY_SIZE(usim_ef_in_adf_usim));

	/* DFs under ADF.USIM */
	add_df_with_ef(uadf, 0x5F3A, "DF.PHONEBOOK", NULL, 0);
	add_df_with_ef(uadf, 0x5F3B, "DF.GSM-ACCESS", usim_ef_in_df_gsm_access,
			ARRAY_SIZE(usim_ef_in_df_gsm_access));
	add_df_with_ef(uadf, 0x5F3C, "DF.MexE", usim_ef_in_df_mexe,
			ARRAY_SIZE(usim_ef_in_df_mexe));
	add_df_with_ef(uadf, 0x5F40, "DF.WLAN", usim_ef_in_df_wlan,
			ARRAY_SIZE(usim_ef_in_df_wlan));
	/* Home-NodeB (femtocell) */
	add_df_with_ef(uadf, 0x5F50, "DF.HNB", usim_ef_in_df_hnb,
			ARRAY_SIZE(usim_ef_in_df_hnb));
	/* Support of Localised Service Areas */
	add_df_with_ef(uadf, 0x5F70, "DF.SoLSA", usim_ef_in_solsa,
			ARRAY_SIZE(usim_ef_in_solsa));
	/* OMA BCAST Smart Card Profile */
	add_df_with_ef(uadf, 0x5F80, "DF.BCAST", NULL, 0);

	/* DF.GSM and DF.TELECOM hierarchy as sub-directory of MF */
	rc = osim_int_cprof_add_gsm(mf);
	rc |= osim_int_cprof_add_telecom(mf);
	if (rc != 0) {
		talloc_free(cprof);
		return NULL;
	}

	return cprof;
}
