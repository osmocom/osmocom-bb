/* GPRS RLC/MAC protocol implementation as per 3GPP TS 44.060 */
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

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/crypt/kdf.h>
#include <osmocom/gprs/gprs_bssgp.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gprs/rlcmac/rlcmac_prim.h>
#include <osmocom/gprs/rlcmac/rlcmac.h>
#include <osmocom/gprs/llc/llc_prim.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/rlcmac.h>

static int modem_rlcmac_handle_grr(struct osmo_gprs_rlcmac_prim *rlcmac_prim)
{
	int rc;

	osmo_static_assert(sizeof(struct osmo_gprs_rlcmac_grr_prim) == sizeof(struct osmo_gprs_llc_grr_prim),
			   _grr_prim_size);

	switch (rlcmac_prim->oph.primitive) {
	case OSMO_GPRS_RLCMAC_GRR_UNITDATA:
		/* Forward it to upper layers, pass ownership over to LLC: */
		/* Optimization: RLCMAC-GRR-UNITDATA-IND is 1-to-1 ABI compatible with
				 LLC-GRR-UNITDATA-IND, we just need to adapt the header.
				 See osmo_static_assert(_grr_prim_size) above.
		*/
		rlcmac_prim->oph.sap = OSMO_GPRS_LLC_SAP_GRR;
		rlcmac_prim->oph.primitive = OSMO_GPRS_LLC_GRR_UNITDATA;
		osmo_gprs_llc_prim_lower_up((struct osmo_gprs_llc_prim *)rlcmac_prim);
		rc = 1; /* Tell RLCMAC that we take ownership of the prim. */
		break;
	case OSMO_GPRS_RLCMAC_GRR_DATA:
	default:
		LOGP(DRLCMAC, LOGL_NOTICE, "%s(): Unexpected Rx RLCMAC GRR prim %u\n",
			__func__, rlcmac_prim->oph.primitive);
		rc = -EINVAL;
		break;
	}
	return rc;
}

static int modem_rlcmac_handle_gmmrr(struct osmo_gprs_rlcmac_prim *rlcmac_prim)
{
	int rc;
	switch (rlcmac_prim->oph.primitive) {
	case OSMO_GPRS_RLCMAC_GMMRR_PAGE:
		LOGP(DRLCMAC, LOGL_ERROR, "%s(): TODO: implement answering to paging indication TLLI=0x%08x\n",
		     __func__, rlcmac_prim->gmmrr.page_ind.tlli);
		rc = 0;
		break;
	default:
		LOGP(DRLCMAC, LOGL_NOTICE, "%s(): Unexpected Rx RLCMAC GMMRR prim %u\n",
			__func__, rlcmac_prim->oph.primitive);
		rc = -EINVAL;
	}
	return rc;
}

static int modem_rlcmac_prim_up_cb(struct osmo_gprs_rlcmac_prim *rlcmac_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_rlcmac_prim_name(rlcmac_prim);
	int rc = 0;

	switch (rlcmac_prim->oph.sap) {
	case OSMO_GPRS_RLCMAC_SAP_GRR:
		LOGP(DRLCMAC, LOGL_DEBUG, "%s(): Rx %s TLLI=0x%08x ll=[%s]\n",
		     __func__, pdu_name, rlcmac_prim->grr.tlli,
		     osmo_hexdump(rlcmac_prim->grr.ll_pdu, rlcmac_prim->grr.ll_pdu_len));
		rc = modem_rlcmac_handle_grr(rlcmac_prim);
		break;
	case OSMO_GPRS_RLCMAC_SAP_GMMRR:
		LOGP(DRLCMAC, LOGL_DEBUG, "%s(): Rx %s\n",
		     __func__, pdu_name);
		rc = modem_rlcmac_handle_gmmrr(rlcmac_prim);
		break;
	default:
		LOGP(DRLCMAC, LOGL_NOTICE, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}
	return rc;
}

static int modem_rlcmac_prim_down_cb(struct osmo_gprs_rlcmac_prim *rlcmac_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_rlcmac_prim_name(rlcmac_prim);
	int rc = 0;

	switch (rlcmac_prim->oph.sap) {
	default:
		LOGP(DRLCMAC, LOGL_DEBUG, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}
	return rc;
}

int modem_rlcmac_init(struct osmocom_ms *ms)
{
	int rc;
	rc = osmo_gprs_rlcmac_init(OSMO_GPRS_RLCMAC_LOCATION_MS);
	if (rc != 0)
		return rc;

	osmo_gprs_rlcmac_set_log_cat(OSMO_GPRS_RLCMAC_LOGC_RLCMAC, DRLCMAC);

	osmo_gprs_rlcmac_prim_set_up_cb(modem_rlcmac_prim_up_cb, ms);
	osmo_gprs_rlcmac_prim_set_down_cb(modem_rlcmac_prim_down_cb, ms);
	return rc;
}
