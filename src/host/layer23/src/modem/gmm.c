/* GPRS GMM interfaces as per 3GPP TS 24.008, TS 24.007 */
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

#include <osmocom/gprs/llc/llc.h>
#include <osmocom/gprs/llc/llc_prim.h>
#include <osmocom/gprs/gmm/gmm_prim.h>
#include <osmocom/gprs/gmm/gmm.h>
#include <osmocom/gprs/rlcmac/rlcmac_prim.h>
#include <osmocom/gprs/sm/sm_prim.h>

#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/gmm.h>

static int modem_gmm_prim_up_cb(struct osmo_gprs_gmm_prim *gmm_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_gmm_prim_name(gmm_prim);
	struct osmocom_ms *ms = user_data;
	int rc = 0;

	switch (gmm_prim->oph.sap) {
	case OSMO_GPRS_GMM_SAP_GMMREG:
		switch (OSMO_PRIM_HDR(&gmm_prim->oph)) {
		case OSMO_PRIM(OSMO_GPRS_GMM_GMMREG_ATTACH, PRIM_OP_CONFIRM):
			if (gmm_prim->gmmreg.attach_cnf.accepted) {
				LOGP(DGMM, LOGL_NOTICE, "%s(): Rx %s: Attach success P-TMSI=0x%08x\n",
				     __func__, pdu_name, gmm_prim->gmmreg.attach_cnf.acc.allocated_ptmsi);
				ms->subscr.ptmsi = gmm_prim->gmmreg.attach_cnf.acc.allocated_ptmsi;
			} else {
				uint8_t cause = gmm_prim->gmmreg.attach_cnf.rej.cause;
				LOGP(DGMM, LOGL_ERROR, "%s(): Rx %s: Attach rejected, cause=%u (%s)\n",
				     __func__, pdu_name, cause, get_value_string(gsm48_gmm_cause_names, cause));
			}
			break;
		case OSMO_PRIM(OSMO_GPRS_GMM_GMMREG_DETACH, PRIM_OP_CONFIRM):
		case OSMO_PRIM(OSMO_GPRS_GMM_GMMREG_DETACH, PRIM_OP_INDICATION):
		default:
			LOGP(DGMM, LOGL_ERROR, "%s(): Rx %s UNIMPLEMENTED\n", __func__, pdu_name);
			break;
		};
		break;
	case OSMO_GPRS_GMM_SAP_GMMSM:
		switch (OSMO_PRIM_HDR(&gmm_prim->oph)) {
		case OSMO_PRIM(OSMO_GPRS_GMM_GMMSM_ESTABLISH, PRIM_OP_CONFIRM):
		case OSMO_PRIM(OSMO_GPRS_GMM_GMMSM_RELEASE,   PRIM_OP_INDICATION):
		case OSMO_PRIM(OSMO_GPRS_GMM_GMMSM_UNITDATA,  PRIM_OP_INDICATION):
			osmo_gprs_sm_prim_gmm_lower_up(gmm_prim);
			rc = 1; /* Tell RLCMAC that we take ownership of the prim. */
			break;
		default:
			LOGP(DGMM, LOGL_ERROR, "%s(): Rx %s UNIMPLEMENTED\n", __func__, pdu_name);
			break;
		};
		break;
	default:
		LOGP(DGMM, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}

	return rc;
}

static int modem_gmm_prim_down_cb(struct osmo_gprs_gmm_prim *gmm_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_gmm_prim_name(gmm_prim);
	int rc = 0;

	osmo_static_assert(sizeof(struct osmo_gprs_gmm_gmmrr_prim) == sizeof(struct osmo_gprs_rlcmac_gmmrr_prim),
			   _gmmrr_prim_size);

	switch (gmm_prim->oph.sap) {
	case OSMO_GPRS_GMM_SAP_GMMRR:
		/* Forward it to lower layers, pass ownership over to RLCMAC: */
		/* Optimization: GMM-GMMRR-ASSIGN-REQ is 1-to-1 ABI compatible with
				 RLCMAC-GMMRR-ASSIGN-REQ, we just need to adapt the header.
				 See osmo_static_assert(_gmmrr_prim_size) above.
		*/
		OSMO_ASSERT(gmm_prim->oph.primitive == OSMO_GPRS_GMM_GMMRR_ASSIGN);
		gmm_prim->oph.sap = OSMO_GPRS_RLCMAC_SAP_GMMRR;
		gmm_prim->oph.primitive = OSMO_GPRS_RLCMAC_GMMRR_ASSIGN;
		osmo_gprs_rlcmac_prim_upper_down((struct osmo_gprs_rlcmac_prim *)gmm_prim);
		rc = 1; /* Tell GMM that we take ownership of the prim. */
		break;
	case OSMO_GPRS_GMM_SAP_GMMREG:
	default:
		LOGP(DGMM, LOGL_ERROR, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}

	return rc;
}

static int modem_gmm_prim_llc_down_cb(struct osmo_gprs_llc_prim *llc_prim, void *user_data)
{
	int rc;

	rc = osmo_gprs_llc_prim_upper_down(llc_prim);

	/* LLC took ownership of the message, tell GMM layer to not free it: */
	rc = 1;
	return rc;
}

int modem_gmm_init(struct osmocom_ms *ms)
{
	int rc;
	rc = osmo_gprs_gmm_init(OSMO_GPRS_GMM_LOCATION_MS);
	if (rc != 0)
		return rc;

	osmo_gprs_gmm_set_log_cat(OSMO_GPRS_GMM_LOGC_GMM, DGMM);

	osmo_gprs_gmm_prim_set_up_cb(modem_gmm_prim_up_cb, ms);
	osmo_gprs_gmm_prim_set_down_cb(modem_gmm_prim_down_cb, ms);
	osmo_gprs_gmm_prim_set_llc_down_cb(modem_gmm_prim_llc_down_cb, ms);

	osmo_gprs_gmm_enable_gprs(true);
	return rc;
}

int modem_gmm_gmmreg_attach_req(const struct osmocom_ms *ms)
{
	struct osmo_gprs_gmm_prim *gmm_prim;
	const struct gsm_subscriber *subscr = &ms->subscr;
	int rc;

	gmm_prim = osmo_gprs_gmm_prim_alloc_gmmreg_attach_req();
	gmm_prim->gmmreg.attach_req.attach_type = OSMO_GPRS_GMM_ATTACH_TYPE_GPRS;
	gmm_prim->gmmreg.attach_req.ptmsi = subscr->ptmsi;
	gmm_prim->gmmreg.attach_req.attach_with_imsi = (subscr->ptmsi == GSM_RESERVED_TMSI);
	memcpy(gmm_prim->gmmreg.attach_req.imsi, subscr->imsi, ARRAY_SIZE(subscr->imsi));
	memcpy(gmm_prim->gmmreg.attach_req.imei, ms->settings.imei, ARRAY_SIZE(ms->settings.imei));
	memcpy(gmm_prim->gmmreg.attach_req.imeisv, ms->settings.imeisv, ARRAY_SIZE(ms->settings.imeisv));
	rc = osmo_gprs_gmm_prim_upper_down(gmm_prim);
	if (rc < 0)
		LOGP(DMM, LOGL_ERROR, "Failed submitting GMMREG-ATTACH.req\n");
	return rc;
}

int modem_gmm_gmmreg_detach_req(const struct osmocom_ms *ms)
{
	struct osmo_gprs_gmm_prim *gmm_prim;
	const struct gsm_subscriber *subscr = &ms->subscr;
	int rc;

	gmm_prim = osmo_gprs_gmm_prim_alloc_gmmreg_detach_req();
	gmm_prim->gmmreg.detach_req.ptmsi = subscr->ptmsi;
	gmm_prim->gmmreg.detach_req.detach_type = OSMO_GPRS_GMM_DETACH_MS_TYPE_GPRS;
	gmm_prim->gmmreg.detach_req.poweroff_type = OSMO_GPRS_GMM_DETACH_POWEROFF_TYPE_NORMAL;
	rc = osmo_gprs_gmm_prim_upper_down(gmm_prim);
	if (rc < 0)
		LOGP(DMM, LOGL_ERROR, "Failed submitting GMMREG-DETACH.req\n");
	return rc;
}
