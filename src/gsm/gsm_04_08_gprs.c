/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010      by On-Waves
 * (C) 2014-2015 by Sysmocom s.f.m.c. GmbH
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <osmocom/gsm/protocol/gsm_04_08_gprs.h>

#include <osmocom/core/utils.h>

/* Protocol related stuff, should go into libosmocore */

/* 10.5.5.14 GPRS MM Cause / Table 10.5.147 */
const struct value_string gsm48_gmm_cause_names_[] = {
	{ GMM_CAUSE_IMSI_UNKNOWN,	"IMSI unknown in HLR" },
	{ GMM_CAUSE_ILLEGAL_MS,		"Illegal MS" },
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
	{ GMM_CAUSE_MSC_TEMP_NOTREACH,	"MSC temporarily not reachable" },
	{ GMM_CAUSE_NET_FAIL,		"Network failure" },
	{ GMM_CAUSE_CONGESTION,		"Congestion" },
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
