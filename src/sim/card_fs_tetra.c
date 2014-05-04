/* TETRA SIM card specific structures/routines */
/*
 * (C) 2014 by Harald Welte <laforge@gnumonks.org>
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

/* EN 300 812 V2.1.1 (2001-12) 9.4 */
static const struct osim_card_sw tsim_sw[] = {
	{
		0x9000, 0xffff, SW_TYPE_STR, SW_CLS_OK,
		.u.str = "Normal ending of the command",
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
		0x9860, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - manipulation flag set",
	}, {
		0x9870, 0xffff, SW_TYPE_STR, SW_CLS_ERROR,
		.u.str = "Security management - SwMI authentication unsuccessful",
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

static const struct osim_card_sw *tsim_card_sws[] = {
	tsim1_sw,
	NULL
};

/* Chapter 10.2.x */
static const struct osim_file_desc sim_ef_in_mf[] = {
	EF_TRANSP(0x2FE2, "EF.ICCID", 0, 10, 10,
		  "ICC Identification", NULL, NULL),
	EF_TRANSP(0x2F00, "EF.DIR", F_OPTIONAL, 8, 54,
		  "Application directory", NULL, NULL),
	EF_TRANSP(0x2F05, "EF.LP", 0, 2, 32,
		  "Language preference", NULL, NULL),
};

////////////////////////////

/* Chapter 10.3.x */
static const struct osim_file_desc sim_ef_in_tetra[] = {
	EF_TRANSP_N(0x6F01, "EF.SST", 0, 4, 8,
		"SIM service table"),
	EF_TRANSP_N(0x6F02, "EF.ITSI", 0, 6, 6,
		"ITSI", NULL, NULL),
	EF_TRANSP_N(0x6F03, "EF.ITSIDIS", 0, 1, 1,
		"ITSI Disable");
	EF_TRANSP_N(0x6F05, "EF.SCT", 0, 4, 4,
		"Subscriber Class Table");
	EF_TRANSP_N(0x6F06, "EF.PHASE", 0, 1, 1,
		"Phase identification");
	EF_KEY_N(0x6F07, "EF.CCK", F_OPTIONAL, 12, 12,
		"Common Cipher Key");
	EF_TRANSP_N(0x6F08, "EF.CCKLOC", F_OPTIONAL, 31, 31,
		"CCK location areas");
	EF_KEY_N(0x6F09, "EF.SCK", F_OPTIONAL, 12, 12,
		"Static Cipher Keys");
	/* X+4 for each record, suggested 1 to 10 */
	EF_LIN_FIX_N(0x6F0A, "EF.GSSIS", 0, 5, 21,
		"Static Cipher Keys");
	/* 2 for each record, one for each recod in GSSIS */
	EF_LIN_FIX_N(0x6F0B, "EF.GRDS", 0, 2, 2,
		"Group related data for static GSSIS");
	EF_LIN_FIX_N(0x6F0C, "EF.GSSID", 0, 5, 21,
		"Dynamic GSSIs");
	/* 2 for each record, one for each recod in GSSID */
	EF_LIN_FIX_N(0x6F0D, "EF.GRDD", 0, 2, 2,
		"Dynamic GSSIs");
	EF_LIN_FIX_N(0x6F0E, "EF.GCK", F_OPTIONAL, 12, 12,
		"Group Cipher Keys");
	EF_KEY_N(0x6F0F, "EF.MGCK", F_OPTIONAL, 12, 12,
		"Modified Group Cipher Keys");
	EF_TRANSP_N(0x6F10, "EF.GINFO", 0, 9, 9,
		"User's group information");
	EF_TRANSP_N(0x6F11, "EF.SEC", 0, 1, 1,
		"Security settings");
	EF_CYCLIC_N(0x6F12, "EF.FORBID", 0, 3, 3,
		"Security settings");
	EF_CYCLIC_N(0x6F13, "EF.PREF", F_OPTIONAL, 3, 3,
		"Preferred networks");
	EF_TRANSP_N(0x6F14, "EF.SPN", F_OPTIONAL, 17, 17,
		"Service Provider Name");
	EF_TRANSP_N(0x6F15, "EF.LOCI", F_OPTIONAL, 7, 7,
		"Location Information");
	EF_TRANSP_N(0x6F16, "EF.DNWRK", 0, 3, 3,
		"Broadcast network information");
	EF_LIN_FIX_N(0x6F17, "EF.NWT", 0, 5, 5,
		"Network table");
	EF_LIN_FIX_N(0x6F18, "EF.GWT", F_OPTIONAL, 13, 13,
		"Gateway table");
	EF_LIN_FIX_N(0x6F19, "EF.CMT", F_OPTIONAL, 5, 20,
		"Call Modifier Table");
	EF_LIN_FIX_N(0x6F1A, "EF.ADNGWT", F_OPTIONAL, 13, 28,
		"Abbreviated Dialling Number with Gateways");
	EF_LIN_FIX_N(0x6F1C, "EF.ADNTETRA", F_OPTIONAL, 9, 23,
		"Abbreviated dialling numbers for TETRA network");
	EF_LIN_FIX_N(0x6F1D, "EF.EXTA", F_OPTIONAL, 20, 20,
		"Extension A");
	EF_LIN_FIX_N(0x6F1E, "EF.FDNGWT", F_OPTIONAL, 13, 28,
		"Fixed dialling numbers with Gateways");
	EF_LIN_FIX_N(0x6F1F, "EF.GWTEXT2", F_OPTIONAL, 13, 13,
		"Gateway Extension2");
	EF_LIN_FIX_N(0x6F20, "EF.FDNTETRA", F_OPTIONAL, 9, 25,
		"Fixed dialling numbers for TETRA network");
	EF_LIN_FIX_N(0x6F21, "EF.EXTB", F_OPTIONAL, 20, 20,
		"Extension B");
	EF_LIN_FIX_N(0x6F22, "EF.LNDGWT", F_OPTIONAL, 13, 28,
		"Last number dialled with Gateways");
	EF_CYCLIC_N(0x6F23, "EF.LNDTETRA", F_OPTIONAL, 9, 23,
		"Last numbers dialled for TETRA network");
	EF_LIN_FIX_N(0x6F24, "EF.SDNGWT", F_OPTIONAL, 13, 28,
		"Service Dialling Numbers with gateway");
	EF_LIN_FIX_N(0x6F25, "EF.GWTEXT3", F_OPTIONAL, 13, 13,
		"Gateway Extension3");
	EF_LIN_FIX_N(0x6F26, "EF.SDNTETRA", F_OPTIONAL, 8, 22,
		"Service Dialling Nubers for TETRA network");
	EF_LIN_FIX_N(0x6F27, "EF.STXT", F_OPTIONAL, 5, 18,
		"Status message texts");
	EF_LIN_FIX_N(0x6F28, "EF.MSGTXT", F_OPTIONAL, 5, 18,
		"SDS-1 message texts");
	EF_LIN_FIX_N(0x6F29, "EF.SDS123", F_OPTIONAL, 46, 46,
		"Status and SDS type 1, 2 and 3 message storage");
	EF_LIN_FIX_N(0x6F2A, "EF.SDS4", F_OPTIONAL, 255, 255,
		"Status and SDS type 4 message storage");
	EF_LIN_FIX_N(0x6F2B, "EF.MSGEXT", F_OPTIONAL, 16, 16,
		"Message Extension");
	EF_LIN_FIX_N(0x6F2C, "EF.EADDR", 0, 17, 17,
		"Emergency adresses");
	EF_TRANSP_N(0x6F2D, "EF.EINFO", 0, 2, 2,
		"Emergency call information");
	EF_LIN_FIX_N(0x6F2E, "EF.DMOCh", F_OPTIONAL, 4, 4,
		"DMO channel information");
	EF_TRANSP_N(0x6F2F, "EF.MSCh", F_OPTIONAL, 1, 16,
		"MS allocation of DMO channels");
	EF_TRANSP_N(0x6F30, "EF.KH", F_OPTIONAL, 6, 6,
		"List of Key Holders");
	EF_LIN_FIX_N(0x6F31, "EF.REPGATE", F_OPTIONAL, 2, 2,
		"DMO repeater and gateway list");
	EF_TRANSP_N(0x6F32, "EF.AD", F_OPTIONAL, 2, 2,
		"DMO repeater and gateway list");
	EF_TRANSP_N(0x6F33, "EF.PREF_LA", F_OPTIONAL, 2, 2,
		"Preferred location areas");
	EF_CYCLIC_N(0x6F34, "EF.LNDComp", F_OPTIONAL, 3, 3,
		"Composite LND file");
	EF_TRANSP_N(0x6F35, "EF.DFLTSTSTGGT", F_OPTIONAL, 16, 16,
		"Status Default Target");
	EF_TRANSP_N(0x6F36, "EF.SDSMEM_STATUS", F_OPTIONAL, 7, 7,
		"SDS Memory Status");
	EF_TRANSP_N(0x6F37, "EF.WELCOME", F_OPTIONAL, 32, 32,
		"Welcome Message");
	EF_LIN_FIX_N(0x6F38, "EF.SDSR", F_OPTIONAL, 2, 2,
		"SDS delivery report");
	EF_LIN_FIX_N(0x6F39, "EF.SDSP", F_OPTIONAL, 20, 35,
		"SDS parameters");
	EF_TRANSP_N(0x6F46, "EF.DIALSC", 0, 5, 5,
		"Dialling schemes for TETRA network");
	EF_TRANSP_N(0x6F3E, "EF.APN", F_OPTIONAL, 65, 65,
		"APN table");
	EF_LIN_FIX_N(0x6FC0, "EF.PNI", F_OPTIONAL, 14, 14,
		"Private Number Information");
};

////////////////////////////

/* 10.5. */
static const struct osim_file_desc sim_ef_in_telecom[] = {
	EF_LIN_FIX_N(0x6F3A, "EF.ADN", F_OPTIONAL,
		"Abbreviated dialling numbers"),
	EF_LIN_FIX_N(0x6F3B, "EF.FDN", F_OPTIONAL,
		"Fixed dialling numbers"),
	EF_LIN_FIX_N(0x6F3C, "EF.SMS", F_OPTIONAL,
		"Short messages"),
	EF_LIN_FIX_N(0x6F3D, "EF.CCP", F_OPTIONAL,
		"Capability configuration parameters"),
	EF_LIN_FIX_N(0x6F4F, "EF.ECCP", F_OPTIONAL,
		"Extended Capability configuration parameters"),
	EF_LIN_FIX_N(0x6F40, "EF.MSISDN", F_OPTIONAL,
		"MSISDN"),
	EF_LIN_FIX_N(0x6F42, "EF.SMSP", F_OPTIONAL,
		"Short message service parameters"),
	EF_TRANSP_N(0x6F43, "EF.SMSS", F_OPTIONAL,
		"SMS Status"),
	EF_CYCLIC_N(0x6F44, "EF.LND", F_OPTIONAL,
		"Last number dialled"),
	EF_LIN_FIX_N(0x6F4A, "EF.EXT1", F_OPTIONAL,
		"Extension 1"),
	EF_LIN_FIX_N(0x6F4B, "EF.EXT2", F_OPTIONAL,
		"Extension 2"),
	EF_LIN_FIX_N(0x6F4C, "EF.EXT3", F_OPTIONAL,
		"Extension 3"),
	EF_LIN_FIX_N(0x6F4D, "EF.BDN", F_OPTIONAL,
		"Barred dialling numbers"),
	EF_LIN_FIX_N(0x6F4E, "EF.EXT4", F_OPTIONAL,
		"Extension 4"),
	EF_LIN_FIX_N(0x6F47, "EF.SMSR", F_OPTIONAL,
		"Short message status reports"),
	EF_LIN_FIX_N(0x6F58, "EF.CMI", F_OPTIONAL,
		"Comparison Method Information"),
};


/* 10.6. */
static const struct osim_file_desc sim_ef_in_graphics[] = {
	EF_LIN_FIX_N(0x4F20, "EF.IMG", F_OPTIONAL,
		"Image"),
};

struct osim_card_profile *osim_cprof_sim(void *ctx)
{
	struct osim_card_profile *cprof;
	struct osim_file_desc *mf, *gsm, *tc;

	cprof = talloc_zero(ctx, struct osim_card_profile);
	cprof->name = "GSM SIM";
	cprof->sws = sim_card_sws;

	mf = alloc_df(cprof, 0x3f00, "MF");

	cprof->mf = mf;

	add_filedesc(mf, sim_ef_in_mf, ARRAY_SIZE(sim_ef_in_mf));
	gsm = add_df_with_ef(mf, 0x7F20, "DF.GSM", sim_ef_in_gsm,
			ARRAY_SIZE(sim_ef_in_gsm));
	add_df_with_ef(gsm, 0x5F30, "DF.IRIDIUM", NULL, 0);
	add_df_with_ef(gsm, 0x5F31, "DF.GLOBST", NULL, 0);
	add_df_with_ef(gsm, 0x5F32, "DF.ICO", NULL, 0);
	add_df_with_ef(gsm, 0x5F33, "DF.ACeS", NULL, 0);
	add_df_with_ef(gsm, 0x5F40, "DF.ACeS", NULL, 0);
	add_df_with_ef(gsm, 0x5F60, "DF.CTS", NULL, 0);
	add_df_with_ef(gsm, 0x5F70, "DF.SoLSA", NULL, 0);
	tc = add_df_with_ef(mf, 0x7F10, "DF.TELECOM", sim_ef_in_telecom,
			ARRAY_SIZE(sim_ef_in_telecom));
	add_df_with_ef(tc, 0x5F50, "DF.GRAPHICS", sim_ef_in_graphics,
			ARRAY_SIZE(sim_ef_in_graphics));

	return cprof;
}
