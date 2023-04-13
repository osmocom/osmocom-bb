/* GPRS SM interfaces as per 3GPP TS 24.008, TS 24.007 */
/* (C) 2023 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 * Author: Pau Espin Pedrol <pespin@sysmocom.de>
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
 * along with this program.  If not, see <http://www.gnu.org/lienses/>.
 *
 */

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/prim.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/tun.h>
#include <osmocom/gsm/protocol/gsm_04_08_gprs.h>

#include <osmocom/gprs/gmm/gmm.h>
#include <osmocom/gprs/gmm/gmm_prim.h>
#include <osmocom/gprs/sndcp/sndcp.h>
#include <osmocom/gprs/sndcp/sndcp_prim.h>
#include <osmocom/gprs/sm/sm_prim.h>
#include <osmocom/gprs/sm/sm.h>
#include <osmocom/gprs/rlcmac/rlcmac_prim.h>

#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/sm.h>

static int modem_sm_prim_up_cb(struct osmo_gprs_sm_prim *sm_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_sm_prim_name(sm_prim);
	int rc = 0;

	switch (sm_prim->oph.sap) {
	case OSMO_GPRS_SM_SAP_SMREG:
		switch (OSMO_PRIM_HDR(&sm_prim->oph)) {
		case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_ACTIVATE, PRIM_OP_CONFIRM):
			if (sm_prim->smreg.pdp_act_cnf.accepted)
				LOGP(DSM, LOGL_INFO, "%s(): Rx %s (TODO: set IP address in tun device)\n", __func__, pdu_name);
			else
				LOGP(DSM, LOGL_INFO, "%s(): Rx %s: Activate PDP failed! cause '%s'\n", __func__, pdu_name,
				     get_value_string(gsm48_gsm_cause_names, sm_prim->smreg.pdp_act_cnf.rej.cause));
			break;
		case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_ACTIVATE, PRIM_OP_INDICATION):
		case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_DEACTIVATE, PRIM_OP_CONFIRM):
		case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_DEACTIVATE, PRIM_OP_INDICATION):
		case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_MODIFY, PRIM_OP_CONFIRM):
		case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_MODIFY, PRIM_OP_INDICATION):
		case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_ACTIVATE_SEC, PRIM_OP_CONFIRM):
		default:
			LOGP(DSM, LOGL_ERROR, "%s(): Rx %s UNIMPLEMENTED\n", __func__, pdu_name);
			break;
		};
		break;
	default:
		LOGP(DSM, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}

	return rc;
}

int modem_sm_prim_sndcp_up_cb(struct osmo_gprs_sndcp_prim *sndcp_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_sndcp_prim_name(sndcp_prim);
	int rc;

	switch (sndcp_prim->oph.sap) {
	case OSMO_GPRS_SNDCP_SAP_SNSM:
		switch (OSMO_PRIM_HDR(&sndcp_prim->oph)) {
		case OSMO_PRIM(OSMO_GPRS_SNDCP_SNSM_ACTIVATE, PRIM_OP_INDICATION):
			LOGP(DSM, LOGL_INFO, "%s(): Rx %s\n", __func__, pdu_name);
			rc = osmo_gprs_sndcp_prim_dispatch_snsm(sndcp_prim);
			break;
		default:
			LOGP(DSM, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
			OSMO_ASSERT(0);
		}
		break;
	default:
		LOGP(DSM, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}
	return rc;
}

static int modem_sm_prim_down_cb(struct osmo_gprs_sm_prim *sm_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_sm_prim_name(sm_prim);
	int rc = 0;

	switch (sm_prim->oph.sap) {
	default:
		LOGP(DSM, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}

	return rc;
}

static int modem_sm_prim_gmm_down_cb(struct osmo_gprs_gmm_prim *gmm_prim, void *user_data)
{
	int rc;

	rc = osmo_gprs_gmm_prim_upper_down(gmm_prim);

	/* GMM took ownership of the message, tell SM layer to not free it: */
	rc = 1;
	return rc;
}

int modem_sm_init(struct osmocom_ms *ms)
{
	int rc;
	rc = osmo_gprs_sm_init(OSMO_GPRS_SM_LOCATION_MS);
	if (rc != 0)
		return rc;

	osmo_gprs_sm_set_log_cat(OSMO_GPRS_SM_LOGC_SM, DSM);

	osmo_gprs_sm_prim_set_up_cb(modem_sm_prim_up_cb, ms);
	osmo_gprs_sm_prim_set_sndcp_up_cb(modem_sm_prim_sndcp_up_cb, ms);
	osmo_gprs_sm_prim_set_down_cb(modem_sm_prim_down_cb, ms);
	osmo_gprs_sm_prim_set_gmm_down_cb(modem_sm_prim_gmm_down_cb, ms);

	return rc;
}
