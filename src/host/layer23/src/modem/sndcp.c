/* GPRS SNDCP User/SN/SNSM interfaces as per 3GPP TS 04.65 */
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

#include <osmocom/gprs/llc/llc.h>
#include <osmocom/gprs/llc/llc_prim.h>
#include <osmocom/gprs/sm/sm_prim.h>
#include <osmocom/gprs/sm/sm.h>
#include <osmocom/gprs/sndcp/sndcp_prim.h>
#include <osmocom/gprs/sndcp/sndcp.h>

#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/sndcp.h>

/* Received SN-XID.cnf from SNDCP layer: */
static int modem_sndcp_handle_sn_xid_cnf(struct osmobb_apn *apn, struct osmo_gprs_sndcp_prim *sndcp_prim)
{
	LOGP(DSNDCP, LOGL_ERROR, "%s(): Rx SN-XID.cnf: TODO IMPLEMENT!\n", __func__);
	return 0;
}

/* Received SN-UNITDTA.ind from SNDCP layer: */
static int modem_sndcp_handle_sn_unitdata_ind(struct osmobb_apn *apn, struct osmo_gprs_sndcp_prim *sndcp_prim)
{
	const char *npdu_name = osmo_gprs_sndcp_prim_name(sndcp_prim);
	struct msgb *msg;
	int rc;

	LOGP(DSNDCP, LOGL_DEBUG, "Rx %s TLLI=0x%08x SAPI=%s NSAPI=%u NPDU=[%s]\n",
		npdu_name,
		sndcp_prim->sn.tlli, osmo_gprs_llc_sapi_name(sndcp_prim->sn.sapi),
		sndcp_prim->sn.data_req.nsapi,
		osmo_hexdump(sndcp_prim->sn.data_ind.npdu, sndcp_prim->sn.data_ind.npdu_len));

	msg = msgb_alloc(sndcp_prim->sn.data_ind.npdu_len, "tx_tun");
	memcpy(msgb_put(msg, sndcp_prim->sn.data_ind.npdu_len),
	       sndcp_prim->sn.data_ind.npdu,
	       sndcp_prim->sn.data_ind.npdu_len);
	rc = osmo_tundev_send(apn->tun, msg);
	return rc;
}

static int modem_sndcp_prim_up_cb(struct osmo_gprs_sndcp_prim *sndcp_prim, void *user_data)
{
	struct osmocom_ms *ms = user_data;
	struct osmobb_apn *apn;
	const char *npdu_name = osmo_gprs_sndcp_prim_name(sndcp_prim);
	int rc = 0;

	if (sndcp_prim->oph.sap != OSMO_GPRS_SNDCP_SAP_SN) {
		LOGP(DSNDCP, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, npdu_name);
		OSMO_ASSERT(0);
	}

	/* TODO: properly retrieve APN/PDP based on TLLI/SAPI/NSAPI: */
	apn = llist_first_entry_or_null(&ms->gprs.apn_list, struct osmobb_apn, list);
	if (!apn) {
		LOGP(DSNDCP, LOGL_NOTICE, "Unable to find destination APN: Rx %s\n", npdu_name);
		return -ENODEV;
	}

	switch (OSMO_PRIM_HDR(&sndcp_prim->oph)) {
	case OSMO_PRIM(OSMO_GPRS_SNDCP_SN_UNITDATA, PRIM_OP_INDICATION):
		rc = modem_sndcp_handle_sn_unitdata_ind(apn, sndcp_prim);
		break;
	case OSMO_PRIM(OSMO_GPRS_SNDCP_SN_XID, PRIM_OP_CONFIRM):
		rc = modem_sndcp_handle_sn_xid_cnf(apn, sndcp_prim);
		break;
	default:
		LOGP(DSNDCP, LOGL_ERROR, "%s(): Rx %s UNIMPLEMENTED\n", __func__, npdu_name);
		break;
	};
	return rc;
}

static int modem_sndcp_prim_down_cb(struct osmo_gprs_llc_prim *llc_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_llc_prim_name(llc_prim);
	int rc = 0;

	if (llc_prim->oph.sap != OSMO_GPRS_LLC_SAP_LL) {
		LOGP(DSNDCP, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}

	switch (OSMO_PRIM_HDR(&llc_prim->oph)) {
	case OSMO_PRIM(OSMO_GPRS_LLC_LL_UNITDATA, PRIM_OP_REQUEST):
	case OSMO_PRIM(OSMO_GPRS_LLC_LL_XID, PRIM_OP_REQUEST):
		LOGP(DSNDCP, LOGL_DEBUG, "%s(): Rx %s TLLI=0x%08x SAPI=%s L3=[%s]\n",
		     __func__, pdu_name,
		     llc_prim->ll.tlli, osmo_gprs_llc_sapi_name(llc_prim->ll.sapi),
		     osmo_hexdump(llc_prim->ll.l3_pdu, llc_prim->ll.l3_pdu_len));
		rc = osmo_gprs_llc_prim_upper_down(llc_prim);
		rc = 1; /* Tell SNDCP layer we took msgb ownsership and transfer it to LLC */
		break;
	default:
		LOGP(DSNDCP, LOGL_ERROR, "%s(): Rx %s UNIMPLEMENTED\n", __func__, pdu_name);
		break;
	};
	return rc;
}

static int modem_sndcp_prim_snsm_cb(struct osmo_gprs_sndcp_prim *sndcp_prim, void *user_data)
{
	const char *npdu_name = osmo_gprs_sndcp_prim_name(sndcp_prim);
	int rc = 0;

	if (sndcp_prim->oph.sap != OSMO_GPRS_SNDCP_SAP_SNSM) {
		LOGP(DSNDCP, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, npdu_name);
		OSMO_ASSERT(0);
	}

	switch (OSMO_PRIM_HDR(&sndcp_prim->oph)) {
	case OSMO_PRIM(OSMO_GPRS_SM_SMREG_PDP_ACTIVATE, PRIM_OP_RESPONSE):
		LOGP(DSNDCP, LOGL_INFO, "%s(): Rx %s\n", __func__, npdu_name);
		rc = osmo_gprs_sm_prim_sndcp_upper_down(sndcp_prim);
		rc = 1; /* Tell SNDCP layer we took msgb ownership and transfer it to SM */
		break;
	default:
		LOGP(DSNDCP, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, npdu_name);
		OSMO_ASSERT(0);
	}
	return rc;
}


int modem_sndcp_init(struct osmocom_ms *ms)
{
	int rc;
	rc = osmo_gprs_sndcp_init(OSMO_GPRS_SNDCP_LOCATION_MS);
	if (rc != 0)
		return rc;

	osmo_gprs_sndcp_set_log_cat(OSMO_GPRS_SNDCP_LOGC_SNDCP, DSNDCP);
	osmo_gprs_sndcp_set_log_cat(OSMO_GPRS_SNDCP_LOGC_SLHC, DSNDCP);

	osmo_gprs_sndcp_prim_set_up_cb(modem_sndcp_prim_up_cb, ms);
	osmo_gprs_sndcp_prim_set_down_cb(modem_sndcp_prim_down_cb, ms);
	osmo_gprs_sndcp_prim_set_snsm_cb(modem_sndcp_prim_snsm_cb, ms);
	return rc;
}

int modem_sndcp_sn_xid_req(struct osmobb_apn *apn)
{
	struct osmo_gprs_sndcp_prim *sndcp_prim;
	int rc;
	struct osmocom_ms *ms = apn->ms;
	struct gprs_settings *set = &ms->gprs;

	/* TODO: look up PDP context IDs from ms once we have GMM layer. */
	uint32_t tlli = 0xe1c5d364;
	uint8_t sapi = OSMO_GPRS_LLC_SAPI_SNDCP3;
	uint8_t nsapi = 1;

	sndcp_prim = osmo_gprs_sndcp_prim_alloc_sn_xid_req(tlli, sapi, nsapi);
	OSMO_ASSERT(sndcp_prim);
	sndcp_prim->sn.xid_req.pcomp_rfc1144.active = set->pcomp_rfc1144.active;
	sndcp_prim->sn.xid_req.pcomp_rfc1144.s01 = set->pcomp_rfc1144.s01;
	sndcp_prim->sn.xid_req.dcomp_v42bis.active = set->dcomp_v42bis.active;
	sndcp_prim->sn.xid_req.dcomp_v42bis.p0 = set->dcomp_v42bis.p0;
	sndcp_prim->sn.xid_req.dcomp_v42bis.p1 = set->dcomp_v42bis.p1;
	sndcp_prim->sn.xid_req.dcomp_v42bis.p2 = set->dcomp_v42bis.p2;
	rc = osmo_gprs_sndcp_prim_upper_down(sndcp_prim);
	return rc;
}

int modem_sndcp_sn_unitdata_req(struct osmobb_apn *apn, uint8_t *npdu, size_t npdu_len)
{
	struct osmo_gprs_sndcp_prim *sndcp_prim;
	int rc;

	/* TODO: look up PDP context IDs from apn->ms once we have GMM layer. */
	uint32_t tlli = 0xe1c5d364;
	uint8_t sapi = OSMO_GPRS_LLC_SAPI_SNDCP3;
	uint8_t nsapi = 1;

	sndcp_prim = osmo_gprs_sndcp_prim_alloc_sn_unitdata_req(tlli, sapi, nsapi, npdu, npdu_len);
	OSMO_ASSERT(sndcp_prim);
	rc = osmo_gprs_sndcp_prim_upper_down(sndcp_prim);
	return rc;
}
