
#include <errno.h>
#include <string.h>

#include <osmocom/sim/sim.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gsm/gsm48.h>

#include "sim_int.h"

/* TS 11.11 / Chapter 9.4 */
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

static int default_decode(struct osim_decoded_data *dd,
			  const struct osim_file_desc *desc,
			  int len, uint8_t *data)
{
	struct osim_decoded_element *elem;

	elem = element_alloc(dd, "Unknown Payload", ELEM_T_BYTES, ELEM_REPR_HEX);
	elem->u.buf = talloc_memdup(elem, data, len);

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

/* Chapter 10.2.x */
static const struct osim_file_desc sim_ef_in_mf[] = {
	EF_TRANSP(0x2FE2, "EF.ICCID", 0,
		  "ICC Identification", &iccid_decode, NULL),
	EF_TRANSP(0x2F05, "EF.ELP", F_OPTIONAL,
		  "Extended language preference", &elp_decode, NULL),
};

/* Chapter 10.3.x */
static const struct osim_file_desc sim_ef_in_gsm[] = {
	EF_TRANSP(0x6F05, "EF.LP", 0,
		  "Language preference", &gsm_lp_decode, NULL),
	EF_TRANSP(0x6F07, "EF.IMSI", 0,
		  "IMSI", &gsm_imsi_decode, NULL),
	EF_TRANSP(0x6F20, "EF.Kc", 0,
		  "Ciphering key Kc", &gsm_kc_decode, NULL),
	EF_TRANSP(0x6F30, "EF.PLMNsel", F_OPTIONAL,
		  "PLMN selector", &gsm_plmnsel_decode, NULL),
	EF_TRANSP(0x6F31, "EF.HPPLMN", 0,
		  "Higher Priority PLMN search period", &gsm_hpplmn_decode, NULL),
	EF_TRANSP_N(0x6F37, "EF.ACMmax", F_OPTIONAL,
		  "ACM maximum value"),
	EF_TRANSP_N(0x6F38, "EF.SST", 0,
		  "SIM service table"),
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
	EF_TRANSP_N(0x6F74, "EF.BCCH", 0,
		  "Broadcast control channels"),
	EF_TRANSP_N(0x6F78, "EF.ACC", 0,
		  "Access control class"),
	EF_TRANSP_N(0x6F7B, "EF.FPLMN", 0,
		  "Forbidden PLMNs"),
	EF_TRANSP_N(0x6F7E, "EF.LOCI", 0,
		  "Location information"),
	EF_TRANSP_N(0x6FAD, "EF.AD", 0,
		  "Administrative data"),
	EF_TRANSP_N(0x6FAE, "EF.Phase", 0,
		  "Phase identification"),
	EF_TRANSP_N(0x6FB1, "EF.VGCS", F_OPTIONAL,
		  "Voice Group Call Service"),
	EF_TRANSP_N(0x6FB2, "EF.VGCSS", F_OPTIONAL,
		  "Voice Group Call Service Status"),
	EF_TRANSP_N(0x6FB3, "EF.VBS", F_OPTIONAL,
		  "Voice Broadcast Service"),
	EF_TRANSP_N(0x6FB4, "EF.VBSS", F_OPTIONAL,
		  "Voice Broadcast Service Status"),
	EF_TRANSP_N(0x6FB5, "EF.eMLPP", F_OPTIONAL,
		  "enhanced Mult Level Pre-emption and Priority"),
	EF_TRANSP_N(0x6FB6, "EF.AAeM", F_OPTIONAL,
		  "Automatic Answer for eMLPP Service"),
	EF_TRANSP_N(0x6F48, "EF.CBMID", F_OPTIONAL,
		  "Cell Broadcast Message Identifier for Data Download"),
	EF_TRANSP_N(0x6FB7, "EF.ECC", F_OPTIONAL,
		  "Emergency Call Code"),
	EF_TRANSP_N(0x6F50, "EF.CBMIR", F_OPTIONAL,
		  "Cell broadcast message identifier range selection"),
	EF_TRANSP_N(0x6F2C, "EF.DCK", F_OPTIONAL,
		  "De-personalization Control Keys"),
	EF_TRANSP_N(0x6F32, "EF.CNL", F_OPTIONAL,
		  "Co-operative Network List"),
	EF_LIN_FIX_N(0x6F51, "EF.NIA", F_OPTIONAL,
		   "Network's Indication of Alerting"),
	EF_TRANSP_N(0x6F52, "EF.KcGPRS", F_OPTIONAL,
		  "GPRS Ciphering key KcGPRS"),
	EF_TRANSP_N(0x6F53, "EF.LOCIGPRS", F_OPTIONAL,
		  "GPRS location information"),
	EF_TRANSP_N(0x6F54, "EF.SUME", F_OPTIONAL,
		  "SetUpMenu Elements"),
	EF_TRANSP_N(0x6F60, "EF.PLMNwAcT", F_OPTIONAL,
		  "User controlled PLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F61, "EF.OPLMNwAcT", F_OPTIONAL,
		  "Operator controlled PLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F62, "EF.HPLMNwAcT", F_OPTIONAL,
		  "HPLMN Selector with Access Technology"),
	EF_TRANSP_N(0x6F63, "EF.CPBCCH", F_OPTIONAL,
		  "CPBCCH Information"),
	EF_TRANSP_N(0x6F64, "EF.InvScan", F_OPTIONAL,
		  "Investigation Scan"),
};

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
