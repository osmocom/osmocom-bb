/* 3GPP USIM specific structures / routines */
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


#include <errno.h>
#include <string.h>

#include <osmocom/sim/sim.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gsm/gsm48.h>

#include "sim_int.h"
#include "gsm_int.h"

/* TS 31.102 Version 7.7.0 / Chapoter 7.3 */
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


static int default_decode(struct osim_decoded_data *dd,
			  const struct osim_file_desc *desc,
			  int len, uint8_t *data)
{
	struct osim_decoded_element *elem;

	elem = element_alloc(dd, "Unknown Payload", ELEM_T_BYTES, ELEM_REPR_HEX);
	elem->u.buf = talloc_memdup(elem, data, len);

	return 0;
}

/* TS 102 221 Chapter 13.1 */
static const struct osim_file_desc uicc_ef_in_mf[] = {
	EF_LIN_FIX_N(0x2f00, "EF.DIR", 0,
			"Application directory"),
	EF_TRANSP_N(0x2FE2, "EF.ICCID", 0,
			"ICC Identification"),
	EF_TRANSP_N(0x2F05, "EF.PL", 0,
			"Preferred Languages"),
	EF_LIN_FIX_N(0x2F06, "EF.ARR", F_OPTIONAL,
			"Access Rule Reference"),
};

static const struct osim_file_desc usim_ef_in_df_gsm_access[] = {
	EF_TRANSP_N(0x4f20, "EF.Kc", 0,
		"Ciphering Key Kc"),
	EF_TRANSP_N(0x4f52, "EF.KcGPRS", 0,
		"GPRS Ciphering key KcGPRS"),
	EF_TRANSP_N(0x4f63, "EF.CPBCCH", F_OPTIONAL,
		"CPBCCH Information"),
	EF_TRANSP_N(0x4f64, "EF.invSCAN", F_OPTIONAL,
		"Investigation Scan"),
};

/* 31.102 Chapter 4.2 */
static const struct osim_file_desc usim_ef_in_adf_usim[] = {
	EF_TRANSP(0x6F05, "EF.LI", 0,
		"Language Indication", &gsm_lp_decode, NULL),
	EF_TRANSP(0x6F07, "EF.IMSI", 0,
		"IMSI", &gsm_imsi_decode, NULL),
	EF_TRANSP_N(0x6F08, "EF.Keys", 0,
		"Ciphering and Integrity Keys"),
	EF_TRANSP_N(0x6F09, "EF.KeysPS", 0,
		"Ciphering and Integrity Keys for Packet Switched domain"),
	EF_TRANSP_N(0x6F60, "EF.PLMNwAcT", F_OPTIONAL,
		"User controlled PLMN Selector with Access Technology"),
	EF_TRANSP(0x6F31, "EF.HPPLMN", 0,
		"Higher Priority PLMN search period", &gsm_hpplmn_decode, NULL),
	EF_TRANSP_N(0x6F37, "EF.ACMmax", F_OPTIONAL,
		"ACM maximum value"),
	EF_TRANSP_N(0x6F38, "EF.UST", 0,
		"USIM Service Table"),
	EF_CYCLIC_N(0x6F39, "EF.ACM", F_OPTIONAL,
		"Accumulated call meter"),
	EF_TRANSP_N(0x6F3E, "EF.GID1", F_OPTIONAL,
		"Group Identifier Level 1"),
	EF_TRANSP_N(0x6F3F, "EF.GID2", F_OPTIONAL,
		"Group Identifier Level 2"),
	EF_TRANSP_N(0x6F46, "EF.SPN", F_OPTIONAL,
		"Service Provider Name"),
	EF_TRANSP_N(0x6F41, "EF.PUCT", F_OPTIONAL,
		"Price per unit and currency table"),
	EF_TRANSP_N(0x6F45, "EF.CBMI", F_OPTIONAL,
		"Cell broadcast massage identifier selection"),
	EF_TRANSP_N(0x6F78, "EF.ACC", 0,
		"Access control class"),
	EF_TRANSP_N(0x6F7B, "EF.FPLMN", 0,
		"Forbidden PLMNs"),
	EF_TRANSP_N(0x6F7E, "EF.LOCI", 0,
		"Location information"),
	EF_TRANSP_N(0x6FAD, "EF.AD", 0,
		"Administrative data"),
	EF_TRANSP_N(0x6F48, "EF.CBMID", F_OPTIONAL,
		"Cell Broadcast Message Identifier for Data Download"),
	EF_TRANSP_N(0x6FB7, "EF.ECC", F_OPTIONAL,
		"Emergency Call Code"),
	EF_TRANSP_N(0x6F50, "EF.CBMIR", F_OPTIONAL,
		"Cell broadcast message identifier range selection"),
	EF_TRANSP_N(0x6F73, "EF.PSLOCI", 0,
		"Pacet Switched location information"),
	EF_LIN_FIX_N(0x6F3B, "EF.FDN", F_OPTIONAL,
		"Fixed dialling numbers"),
	EF_LIN_FIX_N(0x6F3C, "EF.SMS", F_OPTIONAL,
		"Short messages"),
	EF_LIN_FIX_N(0x6F40, "EF.MSISDN", F_OPTIONAL,
		"MSISDN"),
	EF_LIN_FIX_N(0x6F42, "EF.SMSP", F_OPTIONAL,
		"Short message service parameters"),
	EF_TRANSP_N(0x6F43, "EF.SMSS", F_OPTIONAL,
		"SMS Status"),
	EF_LIN_FIX_N(0x6F49, "EF.SDN", F_OPTIONAL,
		"Service Dialling Numbers"),
	EF_LIN_FIX_N(0x6F4B, "EF.EXT2", F_OPTIONAL,
		"Extension 2"),
	EF_LIN_FIX_N(0x6F4C, "EF.EXT3", F_OPTIONAL,
		"Extension 3"),
	EF_LIN_FIX_N(0x6F47, "EF.SMSR", F_OPTIONAL,
		"Short message status reports"),
	EF_CYCLIC_N(0x6F80, "EF.ICI", F_OPTIONAL,
		"Incoming Calling Information"),
	EF_CYCLIC_N(0x6F81, "EF.OCI", F_OPTIONAL,
		"Outgoing Calling Information"),
	EF_CYCLIC_N(0x6F82, "EF.ICT", F_OPTIONAL,
		"Incoming Call Timer"),
	EF_CYCLIC_N(0x6F83, "EF.OCT", F_OPTIONAL,
		"Outgoing Call Timer"),
	EF_LIN_FIX_N(0x6F4E, "EF.EXT5", F_OPTIONAL,
		"Extension 5"),
	EF_LIN_FIX_N(0x6F4F, "EF.CCP2", F_OPTIONAL,
		"Capability Configuration Parameters 2"),
	EF_TRANSP_N(0x6FB5, "EF.eMLPP", F_OPTIONAL,
		"enhanced Multi Level Precedence and Pre-emption"),
	EF_TRANSP_N(0x6FB6, "EF.AAeM", F_OPTIONAL,
		"Automatic Answer for eMLPP Service"),
	EF_TRANSP_N(0x6FC3, "EF.Hiddenkey", F_OPTIONAL,
		"Key for hidden phone book entries"),
	EF_LIN_FIX_N(0x6F4D, "EF.BDN", F_OPTIONAL,
		"Barred Dialling Numbers"),
	EF_LIN_FIX_N(0x6F4E, "EF.EXT4", F_OPTIONAL,
		"Extension 4"),
	EF_LIN_FIX_N(0x6F58, "EF.CMI", F_OPTIONAL,
		"Comparison Method Information"),
	EF_TRANSP_N(0x6F56, "EF.EST", F_OPTIONAL,
		"Enhanced Services Table"),
	EF_TRANSP_N(0x6F57, "EF.ACL", F_OPTIONAL,
		"Access Point Name Control List"),
	EF_TRANSP_N(0x6F2C, "EF.DCK", F_OPTIONAL,
		"Depersonalisation Control Keys"),
	EF_TRANSP_N(0x6F32, "EF.CNL", F_OPTIONAL,
		"Co-operative Network List"),
	EF_TRANSP_N(0x6F5B, "EF.START-HFN", 0,
		"Initialisation values for Hyperframe number"),
	EF_TRANSP_N(0x6F5C, "EF.THRESHOLD", 0,
		"Maximum value of START"),
	EF_TRANSP_N(0x6F61, "EF.OPLMNwAcT", F_OPTIONAL,
		"Operator controlled PLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F62, "EF.HPLMNwAcT", F_OPTIONAL,
		"HPLMN Selector with Access Technology"),
	EF_LIN_FIX_N(0x6F06, "EF.ARR", 0,
		"Access Rule Reference"),
	EF_TRANSP_N(0x6FC4, "EF.NETPAR", 0,
		"Network Parameters"),
	EF_LIN_FIX_N(0x6FC5, "EF.PNN", F_OPTIONAL,
		"PLMN Network Name"),
	EF_LIN_FIX_N(0x6FC6, "EF.OPL", F_OPTIONAL,
		"Operator PLMN List"),
	EF_LIN_FIX_N(0x6FC7, "EF.MBDN", F_OPTIONAL,
		"Mailbox Dialling Numbers"),
	EF_LIN_FIX_N(0x6FC8, "EF.EXT6", F_OPTIONAL,
		"Extension 6"),
	EF_LIN_FIX_N(0x6FC9, "EF.MBI", F_OPTIONAL,
		"Mailbox Identifier"),
	EF_LIN_FIX_N(0x6FCA, "EF.MWIS", F_OPTIONAL,
		"Message Waiting Indication Status"),
	EF_LIN_FIX_N(0x6FCB, "EF.CFIS", F_OPTIONAL,
		"Call Forwarding Indication Status"),
	EF_LIN_FIX_N(0x6FCC, "EF.EXT7", F_OPTIONAL,
		"Extension 7"),
	EF_TRANSP_N(0x6FCD, "EF.SPDI", F_OPTIONAL,
		"Service Provider Display Information"),
	EF_LIN_FIX_N(0x6FCE, "EF.MMSN", F_OPTIONAL,
		"MMS Notification"),
	EF_LIN_FIX_N(0x6FCF, "EF.EXT8", F_OPTIONAL,
		"Extension 8"),
	EF_TRANSP_N(0x6FD0, "EF.MMSICP", F_OPTIONAL,
		"MMS Issuer Connectivity Parameters"),
	EF_LIN_FIX_N(0x6FD1, "EF.MMSUP", F_OPTIONAL,
		"MMS User Preferences"),
	EF_TRANSP_N(0x6FD2, "EF.MMSUCP", F_OPTIONAL,
		"MMS User Connectivity Parameters"),
	EF_LIN_FIX_N(0x6FD3, "EF.NIA", F_OPTIONAL,
		"Network's Indication of Alerting"),
	EF_TRANSP_N(0x6FB1, "EF.VGCS", F_OPTIONAL,
		"Voice Group Call Service"),
	EF_TRANSP_N(0x6FB2, "EF.VGCSS", F_OPTIONAL,
		"Voice Group Call Service Status"),
	EF_TRANSP_N(0x6FB3, "EF.VBS", F_OPTIONAL,
		"Voice Broadcast Service"),
	EF_TRANSP_N(0x6FB4, "EF.VBSS", F_OPTIONAL,
		"Voice Broadcast Service Status"),
	EF_TRANSP_N(0x6FD4, "EF.VGCSCA", F_OPTIONAL,
		"Voice Group Call Service Ciphering Algorithm"),
	EF_TRANSP_N(0x6FD5, "EF.VBSCA", F_OPTIONAL,
		"Voice Broadcast Service Ciphering Algorithm"),
	EF_TRANSP_N(0x6FD6, "EF.GBABP", F_OPTIONAL,
		"GBA Bootstrapping parameters"),
	EF_LIN_FIX_N(0x6FD7, "EF.MSK", F_OPTIONAL,
		"MBMS Serviec Key List"),
	EF_LIN_FIX_N(0x6FD8, "EF.MUK", F_OPTIONAL,
		"MBMS User Key"),
	EF_LIN_FIX_N(0x6FDA, "EF.GBANL", F_OPTIONAL,
		"GBA NAF List"),
	EF_TRANSP_N(0x6FD9, "EF.EHPLMN", F_OPTIONAL,
		"Equivalent HPLMN"),
};



/* 31.102 Chapter 4.4.1 */
static const struct osim_file_desc usim_ef_in_solsa[] = {
	EF_TRANSP_N(0x4F30, "EF.SAI", F_OPTIONAL,
		"SoLSA Access Indicator"),
	EF_LIN_FIX_N(0x4F31, "EF.SLL", F_OPTIONAL,
		"SoLSA LSA List"),
	/* LSA descriptor files 4Fxx, hard to represent here */
};

/* Annex E - TS 101 220 */
static const uint8_t adf_usim_aid[] = { 0xA0, 0x00, 0x00, 0x00, 0x87, 0x10, 0x02 };

struct osim_card_profile *osim_cprof_usim(void *ctx)
{
	struct osim_card_profile *cprof;
	struct osim_file_desc *mf, *gsm, *tc, *uadf;

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
	add_df_with_ef(uadf, 0x5F3C, "DF.MExE", NULL, 0);
	add_df_with_ef(uadf, 0x5F40, "DF.WLAN", NULL, 0);
	add_df_with_ef(uadf, 0x5F70, "DF.SoLSA", usim_ef_in_solsa, ARRAY_SIZE(usim_ef_in_solsa));

#if 0
	/* DF.TELECOM as sub-directory of MF */
	tc = add_df_with_ef(mf, 0x7F10, "DF.TELECOM", sim_ef_in_telecom,
			ARRAY_SIZE(sim_ef_in_telecom));
	add_df_with_ef(tc, 0x5F50, "DF.GRAPHICS", sim_ef_in_graphics,
			ARRAY_SIZE(sim_ef_in_graphics));

	/* DF.GSM for backwards compatibility */
	gsm = add_df_with_ef(mf, 0x7F20, "DF.GSM", sim_ef_in_gsm,
			ARRAY_SIZE(sim_ef_in_gsm));
	/* FIXME: DF's below DF.GSM  (51.011) */
#endif

	return cprof;
}
