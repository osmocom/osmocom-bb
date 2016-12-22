/* (C) 2009-2016 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010      by On-Waves
 * (C) 2014-2015 by Sysmocom s.f.m.c. GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <osmocom/gsm/protocol/gsm_04_08_gprs.h>
#include <osmocom/crypt/gprs_cipher.h>
#include <osmocom/core/utils.h>

#include <stdbool.h>

/* Protocol related stuff, should go into libosmocore */

/* 10.5.5.14 GPRS MM Cause / Table 10.5.147 */
const struct value_string gsm48_gmm_cause_names_[] = {
	{ GMM_CAUSE_IMSI_UNKNOWN,	"IMSI unknown in HLR" },
	{ GMM_CAUSE_ILLEGAL_MS,		"Illegal MS" },
	{ GMM_CAUSE_IMEI_NOT_ACCEPTED,	"IMEI not accepted" },
	{ GMM_CAUSE_ILLEGAL_ME,		"Illegal ME" },
	{ GMM_CAUSE_GPRS_NOTALLOWED,	"GPRS services not allowed" },
	{ GMM_CAUSE_GPRS_OTHER_NOTALLOWED,
			"GPRS services and non-GPRS services not allowed" },
	{ GMM_CAUSE_MS_ID_NOT_DERIVED,
			"MS identity cannot be derived by the network" },
	{ GMM_CAUSE_IMPL_DETACHED,	"Implicitly detached" },
	{ GMM_CAUSE_PLMN_NOTALLOWED,	"PLMN not allowed" },
	{ GMM_CAUSE_LA_NOTALLOWED,	"Location Area not allowed" },
	{ GMM_CAUSE_ROAMING_NOTALLOWED,
			"Roaming not allowed in this location area" },
	{ GMM_CAUSE_NO_GPRS_PLMN,
				"GPRS services not allowed in this PLMN" },
	{ GMM_CAUSE_NO_SUIT_CELL_IN_LA,	"No suitable cell in LA" },
	{ GMM_CAUSE_MSC_TEMP_NOTREACH,	"MSC temporarily not reachable" },
	{ GMM_CAUSE_NET_FAIL,		"Network failure" },
	{ GMM_CAUSE_MAC_FAIL,		"MAC failure" },
	{ GMM_CAUSE_SYNC_FAIL,		"SYNC failure" },
	{ GMM_CAUSE_CONGESTION,		"Congestion" },
	{ GMM_CAUSE_GSM_AUTH_UNACCEPT,	"GSM authentication unacceptable" },
	{ GMM_CAUSE_NOT_AUTH_FOR_CSG,	"Not authorized for this CSG" },
	{ GMM_CAUSE_SMS_VIA_GPRS_IN_RA,	"SMS provided via GPRS in this RA" },
	{ GMM_CAUSE_NO_PDP_ACTIVATED,	"No PDP context activated" },
	{ GMM_CAUSE_SEM_INCORR_MSG,	"Semantically incorrect message" },
	{ GMM_CAUSE_INV_MAND_INFO, "Invalid mandatory information" },
	{ GMM_CAUSE_MSGT_NOTEXIST_NOTIMPL,
			"Message type non-existant or not implemented" },
	{ GMM_CAUSE_MSGT_INCOMP_P_STATE,
			"Message type not compatible with protocol state" },
	{ GMM_CAUSE_IE_NOTEXIST_NOTIMPL,
			"Information element non-existent or not implemented" },
	{ GMM_CAUSE_COND_IE_ERR,	"Conditional IE error" },
	{ GMM_CAUSE_MSG_INCOMP_P_STATE,
				"Message not compatible with protocol state " },
	{ GMM_CAUSE_PROTO_ERR_UNSPEC,	"Protocol error, unspecified" },
	{ 0, NULL }
};

const struct value_string *gsm48_gmm_cause_names = gsm48_gmm_cause_names_;

/* 10.5.6.6 SM Cause / Table 10.5.157 */
const struct value_string gsm48_gsm_cause_names_[] = {
	{ GSM_CAUSE_INSUFF_RSRC, "Insufficient resources" },
	{ GSM_CAUSE_MISSING_APN, "Missing or unknown APN" },
	{ GSM_CAUSE_UNKNOWN_PDP, "Unknown PDP address or PDP type" },
	{ GSM_CAUSE_AUTH_FAILED, "User Authentication failed" },
	{ GSM_CAUSE_ACT_REJ_GGSN, "Activation rejected by GGSN" },
	{ GSM_CAUSE_ACT_REJ_UNSPEC, "Activation rejected, unspecified" },
	{ GSM_CAUSE_SERV_OPT_NOTSUPP, "Service option not supported" },
	{ GSM_CAUSE_REQ_SERV_OPT_NOTSUB,
				"Requested service option not subscribed" },
	{ GSM_CAUSE_SERV_OPT_TEMP_OOO,
				"Service option temporarily out of order" },
	{ GSM_CAUSE_NSAPI_IN_USE, "NSAPI already used" },
	{ GSM_CAUSE_DEACT_REGULAR, "Regular deactivation" },
	{ GSM_CAUSE_QOS_NOT_ACCEPTED, "QoS not accepted" },
	{ GSM_CAUSE_NET_FAIL, "Network Failure" },
	{ GSM_CAUSE_REACT_RQD, "Reactivation required" },
	{ GSM_CAUSE_FEATURE_NOTSUPP, "Feature not supported " },
	{ GSM_CAUSE_INVALID_TRANS_ID, "Invalid transaction identifier" },
	{ GSM_CAUSE_SEM_INCORR_MSG, "Semantically incorrect message" },
	{ GSM_CAUSE_INV_MAND_INFO, "Invalid mandatory information" },
	{ GSM_CAUSE_MSGT_NOTEXIST_NOTIMPL,
			"Message type non-existant or not implemented" },
	{ GSM_CAUSE_MSGT_INCOMP_P_STATE,
			"Message type not compatible with protocol state" },
	{ GSM_CAUSE_IE_NOTEXIST_NOTIMPL,
			"Information element non-existent or not implemented" },
	{ GSM_CAUSE_COND_IE_ERR, "Conditional IE error" },
	{ GSM_CAUSE_MSG_INCOMP_P_STATE,
				"Message not compatible with protocol state " },
	{ GSM_CAUSE_PROTO_ERR_UNSPEC, "Protocol error, unspecified" },
	{ 0, NULL }
};

const struct value_string *gsm48_gsm_cause_names = gsm48_gsm_cause_names_;

/*! \brief Check if MS supports particular version of GEA by inspecting
 *         MS network capability IE specified in 3GPP TS 24.008
 *  \param[in] ms_net_cap Buffer with raw MS network capability IE value,
 *                        3 - 10 bytes
 *  \param[in] cap_len Length of ms_net_cap, in bytes
 *  \param[in] gea Version of GEA to check
 *  \returns true if given version is supported by MS, false otherwise
 */
bool gprs_ms_net_cap_gea_supported(const uint8_t *ms_net_cap, uint8_t cap_len,
				   enum gprs_ciph_algo gea)
{
	switch (gea) {
	case GPRS_ALGO_GEA0:
		return true;
	case GPRS_ALGO_GEA1: /* 1st bit is GEA1: */
		return 0x80 & ms_net_cap[0];
	case GPRS_ALGO_GEA2: /* extended GEA bits start from 2nd bit */
		return 0x40 & ms_net_cap[1]; /* of the next byte */
	case GPRS_ALGO_GEA3:
		return 0x20 & ms_net_cap[1];
	case GPRS_ALGO_GEA4:
		return 0x10 & ms_net_cap[1];
	default:
		return false;
	}
}

const struct value_string gprs_msgt_gmm_names[] = {
	{ GSM48_MT_GMM_ATTACH_REQ,		"ATTACH REQUEST" },
	{ GSM48_MT_GMM_ATTACH_ACK,		"ATTACH ACK" },
	{ GSM48_MT_GMM_ATTACH_COMPL,		"ATTACH COMPLETE" },
	{ GSM48_MT_GMM_ATTACH_REJ,		"ATTACH REJECT" },
	{ GSM48_MT_GMM_DETACH_REQ,		"DETACH REQUEST" },
	{ GSM48_MT_GMM_DETACH_ACK,		"DETACH ACK" },
	{ GSM48_MT_GMM_RA_UPD_REQ,		"RA UPDATE REQUEST" },
	{ GSM48_MT_GMM_RA_UPD_ACK,		"RA UPDATE ACK" },
	{ GSM48_MT_GMM_RA_UPD_COMPL,		"RA UPDATE COMPLETE" },
	{ GSM48_MT_GMM_RA_UPD_REJ,		"RA UPDATE REJECT" },
	{ GSM48_MT_GMM_PTMSI_REALL_CMD,		"PTMSI REALLOC CMD" },
	{ GSM48_MT_GMM_PTMSI_REALL_COMPL,	"PTMSI REALLOC COMPLETE" },
	{ GSM48_MT_GMM_AUTH_CIPH_REQ,		"AUTH & CIPHER REQUEST" },
	{ GSM48_MT_GMM_AUTH_CIPH_RESP,		"AUTH & CIPHER RESPONSE" },
	{ GSM48_MT_GMM_AUTH_CIPH_REJ,		"AUTH & CIPHER REJECT" },
	{ GSM48_MT_GMM_AUTH_CIPH_FAIL,		"AUTH & CIPHER FAILURE" },
	{ GSM48_MT_GMM_ID_REQ,			"IDENTITY REQUEST" },
	{ GSM48_MT_GMM_ID_RESP,			"IDENTITY RESPONSE" },
	{ GSM48_MT_GMM_STATUS,			"STATUS" },
	{ GSM48_MT_GMM_INFO,			"INFO" },
	{ 0, NULL }
};

/* 10.5.5.2 */
const struct value_string gprs_att_t_strs_[] = {
	{ GPRS_ATT_T_ATTACH,		"GPRS attach" },
	{ GPRS_ATT_T_ATT_WHILE_IMSI,	"GPRS attach while IMSI attached" },
	{ GPRS_ATT_T_COMBINED,		"Combined GPRS/IMSI attach" },
	{ 0, NULL }
};

const struct value_string *gprs_att_t_strs = gprs_att_t_strs_;

const struct value_string gprs_upd_t_strs_[] = {
	{ GPRS_UPD_T_RA,		"RA updating" },
	{ GPRS_UPD_T_RA_LA,		"combined RA/LA updating" },
	{ GPRS_UPD_T_RA_LA_IMSI_ATT,	"combined RA/LA updating + IMSI attach" },
	{ GPRS_UPD_T_PERIODIC,		"periodic updating" },
	{ 0, NULL }
};

const struct value_string *gprs_upd_t_strs = gprs_upd_t_strs_;

/* 10.5.5.5 */
const struct value_string gprs_det_t_mo_strs_[] = {
	{ GPRS_DET_T_MO_GPRS,		"GPRS detach" },
	{ GPRS_DET_T_MO_IMSI,		"IMSI detach" },
	{ GPRS_DET_T_MO_COMBINED,	"Combined GPRS/IMSI detach" },
	{ 0, NULL }
};

const struct value_string *gprs_det_t_mo_strs = gprs_det_t_mo_strs_;

const struct value_string gprs_det_t_mt_strs_[] = {
	{ GPRS_DET_T_MT_REATT_REQ,	"re-attach required" },
	{ GPRS_DET_T_MT_REATT_NOTREQ,	"re-attach not required" },
	{ GPRS_DET_T_MT_IMSI,		"IMSI detach (after VLR failure)" },
	{ 0, NULL }
};

const struct value_string *gprs_det_t_mt_strs = gprs_det_t_mt_strs_;

const struct value_string gprs_service_t_strs_[] = {
	{ GPRS_SERVICE_T_SIGNALLING,	"signalling" },
	{ GPRS_SERVICE_T_DATA,		"data" },
	{ GPRS_SERVICE_T_PAGING_RESP,	"paging response" },
	{ GPRS_SERVICE_T_MBMS_MC_SERV,	"MBMS multicast service" },
	{ GPRS_SERVICE_T_MBMS_BC_SERV,	"MBMS broadcast service" },
	{ 0, NULL }
};

const struct value_string *gprs_service_t_strs = gprs_service_t_strs_;
