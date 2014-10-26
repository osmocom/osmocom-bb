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
#include "gsm_int.h"

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
	tsim_sw,
	NULL
};

/* Chapter 10.2.x */
static const struct osim_file_desc sim_ef_in_mf[] = {
	EF_TRANSP_N(0x2FE2, SFI_NONE, "EF.ICCID", 0, 10, 10,
		  "ICC Identification"),
	EF_TRANSP_N(0x2F00, SFI_NONE, "EF.DIR", F_OPTIONAL, 8, 54,
		  "Application directory"),
	EF_TRANSP_N(0x2F05, SFI_NONE, "EF.LP", 0, 2, 32,
		  "Language preference"),
};

////////////////////////////

/* Chapter 10.3.x */
static const struct osim_file_desc sim_ef_in_tetra[] = {
	EF_TRANSP_N(0x6F01, SFI_NONE, "EF.SST", 0, 4, 8,
		"SIM service table"),
	EF_TRANSP_N(0x6F02, SFI_NONE, "EF.ITSI", 0, 6, 6,
		"ITSI"),
	EF_TRANSP_N(0x6F03, SFI_NONE, "EF.ITSIDIS", 0, 1, 1,
		"ITSI Disable"),
	EF_TRANSP_N(0x6F05, SFI_NONE, "EF.SCT", 0, 4, 4,
		"Subscriber Class Table"),
	EF_TRANSP_N(0x6F06, SFI_NONE, "EF.PHASE", 0, 1, 1,
		"Phase identification"),
	EF_KEY_N(0x6F07, SFI_NONE, "EF.CCK", F_OPTIONAL, 12, 12,
		"Common Cipher Key"),
	EF_TRANSP_N(0x6F08, SFI_NONE, "EF.CCKLOC", F_OPTIONAL, 31, 31,
		"CCK location areas"),
	EF_KEY_N(0x6F09, SFI_NONE, "EF.SCK", F_OPTIONAL, 12, 12,
		"Static Cipher Keys"),
	/* X+4 for each record, suggested 1 to 10 */
	EF_LIN_FIX_N(0x6F0A, SFI_NONE, "EF.GSSIS", 0, 5, 21,
		"Static Cipher Keys"),
	/* 2 for each record, one for each recod in GSSIS */
	EF_LIN_FIX_N(0x6F0B, SFI_NONE, "EF.GRDS", 0, 2, 2,
		"Group related data for static GSSIS"),
	EF_LIN_FIX_N(0x6F0C, SFI_NONE, "EF.GSSID", 0, 5, 21,
		"Dynamic GSSIs"),
	/* 2 for each record, one for each recod in GSSID */
	EF_LIN_FIX_N(0x6F0D, SFI_NONE, "EF.GRDD", 0, 2, 2,
		"Dynamic GSSIs"),
	EF_LIN_FIX_N(0x6F0E, SFI_NONE, "EF.GCK", F_OPTIONAL, 12, 12,
		"Group Cipher Keys"),
	EF_KEY_N(0x6F0F, SFI_NONE, "EF.MGCK", F_OPTIONAL, 12, 12,
		"Modified Group Cipher Keys"),
	EF_TRANSP_N(0x6F10, SFI_NONE, "EF.GINFO", 0, 9, 9,
		"User's group information"),
	EF_TRANSP_N(0x6F11, SFI_NONE, "EF.SEC", 0, 1, 1,
		"Security settings"),
	EF_CYCLIC_N(0x6F12, SFI_NONE, "EF.FORBID", 0, 3, 3,
		"Security settings"),
	EF_CYCLIC_N(0x6F13, SFI_NONE, "EF.PREF", F_OPTIONAL, 3, 3,
		"Preferred networks"),
	EF_TRANSP_N(0x6F14, SFI_NONE, "EF.SPN", F_OPTIONAL, 17, 17,
		"Service Provider Name"),
	EF_TRANSP_N(0x6F15, SFI_NONE, "EF.LOCI", F_OPTIONAL, 7, 7,
		"Location Information"),
	EF_TRANSP_N(0x6F16, SFI_NONE, "EF.DNWRK", 0, 3, 3,
		"Broadcast network information"),
	EF_LIN_FIX_N(0x6F17, SFI_NONE, "EF.NWT", 0, 5, 5,
		"Network table"),
	EF_LIN_FIX_N(0x6F18, SFI_NONE, "EF.GWT", F_OPTIONAL, 13, 13,
		"Gateway table"),
	EF_LIN_FIX_N(0x6F19, SFI_NONE, "EF.CMT", F_OPTIONAL, 5, 20,
		"Call Modifier Table"),
	EF_LIN_FIX_N(0x6F1A, SFI_NONE, "EF.ADNGWT", F_OPTIONAL, 13, 28,
		"Abbreviated Dialling Number with Gateways"),
	EF_LIN_FIX_N(0x6F1C, SFI_NONE, "EF.ADNTETRA", F_OPTIONAL, 9, 23,
		"Abbreviated dialling numbers for TETRA network"),
	EF_LIN_FIX_N(0x6F1D, SFI_NONE, "EF.EXTA", F_OPTIONAL, 20, 20,
		"Extension A"),
	EF_LIN_FIX_N(0x6F1E, SFI_NONE, "EF.FDNGWT", F_OPTIONAL, 13, 28,
		"Fixed dialling numbers with Gateways"),
	EF_LIN_FIX_N(0x6F1F, SFI_NONE, "EF.GWTEXT2", F_OPTIONAL, 13, 13,
		"Gateway Extension2"),
	EF_LIN_FIX_N(0x6F20, SFI_NONE, "EF.FDNTETRA", F_OPTIONAL, 9, 25,
		"Fixed dialling numbers for TETRA network"),
	EF_LIN_FIX_N(0x6F21, SFI_NONE, "EF.EXTB", F_OPTIONAL, 20, 20,
		"Extension B"),
	EF_LIN_FIX_N(0x6F22, SFI_NONE, "EF.LNDGWT", F_OPTIONAL, 13, 28,
		"Last number dialled with Gateways"),
	EF_CYCLIC_N(0x6F23, SFI_NONE, "EF.LNDTETRA", F_OPTIONAL, 9, 23,
		"Last numbers dialled for TETRA network"),
	EF_LIN_FIX_N(0x6F24, SFI_NONE, "EF.SDNGWT", F_OPTIONAL, 13, 28,
		"Service Dialling Numbers with gateway"),
	EF_LIN_FIX_N(0x6F25, SFI_NONE, "EF.GWTEXT3", F_OPTIONAL, 13, 13,
		"Gateway Extension3"),
	EF_LIN_FIX_N(0x6F26, SFI_NONE, "EF.SDNTETRA", F_OPTIONAL, 8, 22,
		"Service Dialling Nubers for TETRA network"),
	EF_LIN_FIX_N(0x6F27, SFI_NONE, "EF.STXT", F_OPTIONAL, 5, 18,
		"Status message texts"),
	EF_LIN_FIX_N(0x6F28, SFI_NONE, "EF.MSGTXT", F_OPTIONAL, 5, 18,
		"SDS-1 message texts"),
	EF_LIN_FIX_N(0x6F29, SFI_NONE, "EF.SDS123", F_OPTIONAL, 46, 46,
		"Status and SDS type 1, 2 and 3 message storage"),
	EF_LIN_FIX_N(0x6F2A, SFI_NONE, "EF.SDS4", F_OPTIONAL, 255, 255,
		"Status and SDS type 4 message storage"),
	EF_LIN_FIX_N(0x6F2B, SFI_NONE, "EF.MSGEXT", F_OPTIONAL, 16, 16,
		"Message Extension"),
	EF_LIN_FIX_N(0x6F2C, SFI_NONE, "EF.EADDR", 0, 17, 17,
		"Emergency adresses"),
	EF_TRANSP_N(0x6F2D, SFI_NONE, "EF.EINFO", 0, 2, 2,
		"Emergency call information"),
	EF_LIN_FIX_N(0x6F2E, SFI_NONE, "EF.DMOCh", F_OPTIONAL, 4, 4,
		"DMO channel information"),
	EF_TRANSP_N(0x6F2F, SFI_NONE, "EF.MSCh", F_OPTIONAL, 1, 16,
		"MS allocation of DMO channels"),
	EF_TRANSP_N(0x6F30, SFI_NONE, "EF.KH", F_OPTIONAL, 6, 6,
		"List of Key Holders"),
	EF_LIN_FIX_N(0x6F31, SFI_NONE, "EF.REPGATE", F_OPTIONAL, 2, 2,
		"DMO repeater and gateway list"),
	EF_TRANSP_N(0x6F32, SFI_NONE, "EF.AD", F_OPTIONAL, 2, 2,
		"Administrative Data"),
	EF_TRANSP_N(0x6F33, SFI_NONE, "EF.PREF_LA", F_OPTIONAL, 2, 2,
		"Preferred location areas"),
	EF_CYCLIC_N(0x6F34, SFI_NONE, "EF.LNDComp", F_OPTIONAL, 3, 3,
		"Composite LND file"),
	EF_TRANSP_N(0x6F35, SFI_NONE, "EF.DFLTSTSTGGT", F_OPTIONAL, 16, 16,
		"Status Default Target"),
	EF_TRANSP_N(0x6F36, SFI_NONE, "EF.SDSMEM_STATUS", F_OPTIONAL, 7, 7,
		"SDS Memory Status"),
	EF_TRANSP_N(0x6F37, SFI_NONE, "EF.WELCOME", F_OPTIONAL, 32, 32,
		"Welcome Message"),
	EF_LIN_FIX_N(0x6F38, SFI_NONE, "EF.SDSR", F_OPTIONAL, 2, 2,
		"SDS delivery report"),
	EF_LIN_FIX_N(0x6F39, SFI_NONE, "EF.SDSP", F_OPTIONAL, 20, 35,
		"SDS parameters"),
	EF_TRANSP_N(0x6F46, SFI_NONE, "EF.DIALSC", 0, 5, 5,
		"Dialling schemes for TETRA network"),
	EF_TRANSP_N(0x6F3E, SFI_NONE, "EF.APN", F_OPTIONAL, 65, 65,
		"APN table"),
	EF_LIN_FIX_N(0x6FC0, SFI_NONE, "EF.PNI", F_OPTIONAL, 14, 14,
		"Private Number Information"),
};

struct osim_card_profile *osim_cprof_tsim(void *ctx)
{
	struct osim_card_profile *cprof;
	struct osim_file_desc *mf;
	int rc;

	cprof = talloc_zero(ctx, struct osim_card_profile);
	cprof->name = "TETRA SIM";
	cprof->sws = tsim_card_sws;

	mf = alloc_df(cprof, 0x3f00, "MF");

	cprof->mf = mf;

	add_filedesc(mf, sim_ef_in_mf, ARRAY_SIZE(sim_ef_in_mf));
	add_df_with_ef(mf, 0x7F20, "DF.TETRA", sim_ef_in_tetra,
			ARRAY_SIZE(sim_ef_in_tetra));

	rc = osim_int_cprof_add_telecom(mf);
	if (rc != 0) {
		talloc_free(cprof);
		return NULL;
	}

	return cprof;
}
