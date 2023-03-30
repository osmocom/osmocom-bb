/* GPRS LLC protocol implementation as per 3GPP TS 04.64 */
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
#include <string.h>
#include <stdbool.h>

#include <osmocom/core/prim.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>

#include <osmocom/gprs/gprs_msgb.h>
#include <osmocom/gprs/llc/llc_prim.h>
#include <osmocom/gprs/llc/llc.h>
#include <osmocom/gprs/gmm/gmm_prim.h>
#include <osmocom/gprs/rlcmac/rlcmac_prim.h>
#include <osmocom/gprs/sndcp/sndcp_prim.h>

#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/modem/llc.h>

static int modem_llc_handle_ll_gmm(struct osmo_gprs_llc_prim *llc_prim)
{
	int rc;

	switch (llc_prim->oph.primitive) {
	case OSMO_GPRS_LLC_LL_UNITDATA:
		break;
	case OSMO_GPRS_LLC_LL_RESET:
	case OSMO_GPRS_LLC_LL_ESTABLISH:
	case OSMO_GPRS_LLC_LL_XID:
	case OSMO_GPRS_LLC_LL_DATA:
	case OSMO_GPRS_LLC_LL_STATUS:
	default:
		LOGP(DLLC, LOGL_NOTICE, "%s(): Unexpected Rx LL prim %u\n",
			__func__, llc_prim->oph.primitive);
		return -EINVAL;
	}

	/* GMM took ownership of the message, tell LLC layer to not free it: */
	rc = osmo_gprs_gmm_prim_llc_lower_up(llc_prim);
	rc = 1;
	return rc;
}

static int modem_llc_handle_ll_sndcp(struct osmo_gprs_llc_prim *llc_prim)
{
	int rc;
	switch (llc_prim->oph.primitive) {
	case OSMO_GPRS_LLC_LL_RESET:
	case OSMO_GPRS_LLC_LL_ESTABLISH:
	case OSMO_GPRS_LLC_LL_XID:
	case OSMO_GPRS_LLC_LL_DATA:
	case OSMO_GPRS_LLC_LL_UNITDATA:
	case OSMO_GPRS_LLC_LL_STATUS:
		/* Forward it to upper layers, pass owneserip over to SNDCP: */
		osmo_gprs_sndcp_prim_lower_up(llc_prim);
		rc = 1; /* Tell LLC that we take ownership of the prim. */
		break;
	default:
		LOGP(DLLC, LOGL_NOTICE, "%s(): Unexpected Rx LL prim %u\n",
			__func__, llc_prim->oph.primitive);
		rc = -EINVAL;
	}
	return rc;
}

int modem_llc_prim_up_cb(struct osmo_gprs_llc_prim *llc_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_llc_prim_name(llc_prim);
	int rc = 0;

	switch (llc_prim->oph.sap) {
	case OSMO_GPRS_LLC_SAP_LLGMM:
		LOGP(DLLC, LOGL_DEBUG, "%s(): Rx %s TLLI=0x%08x\n",
		     __func__, pdu_name, llc_prim->llgmm.tlli);
		break;
	case OSMO_GPRS_LLC_SAP_LL:
		LOGP(DLLC, LOGL_DEBUG, "%s(): Rx %s TLLI=0x%08x SAPI=%s l3=[%s]\n",
		     __func__, pdu_name, llc_prim->ll.tlli,
		     osmo_gprs_llc_sapi_name(llc_prim->ll.sapi),
		     osmo_hexdump(llc_prim->ll.l3_pdu, llc_prim->ll.l3_pdu_len));

		switch (llc_prim->ll.sapi) {
		case OSMO_GPRS_LLC_SAPI_GMM:
			rc = modem_llc_handle_ll_gmm(llc_prim);
			break;
		case OSMO_GPRS_LLC_SAPI_SNDCP3:
		case OSMO_GPRS_LLC_SAPI_SNDCP5:
		case OSMO_GPRS_LLC_SAPI_SNDCP9:
		case OSMO_GPRS_LLC_SAPI_SNDCP11:
			rc = modem_llc_handle_ll_sndcp(llc_prim);
			break;
		case OSMO_GPRS_LLC_SAPI_TOM2:
		case OSMO_GPRS_LLC_SAPI_SMS:
		case OSMO_GPRS_LLC_SAPI_TOM8:
			LOGP(DLLC, LOGL_NOTICE, "%s(): Unimplemented Rx llc_sapi %s\n", __func__, pdu_name);
			rc = -EINVAL;
			break;
		default:
			LOGP(DLLC, LOGL_NOTICE, "%s(): Unexpected Rx llc_sapi %s\n", __func__, pdu_name);
			rc = -EINVAL;
			break;
		}
		break;
	default:
		LOGP(DLLC, LOGL_NOTICE, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}
	return rc;
}

int modem_llc_prim_down_cb(struct osmo_gprs_llc_prim *llc_prim, void *user_data)
{
	const char *pdu_name = osmo_gprs_llc_prim_name(llc_prim);
	int rc = 0;

	osmo_static_assert(sizeof(struct osmo_gprs_llc_grr_prim) == sizeof(struct osmo_gprs_rlcmac_grr_prim),
			   _grr_prim_size);

	switch (llc_prim->oph.sap) {
	case OSMO_GPRS_LLC_SAP_GRR:
		LOGP(DLLC, LOGL_DEBUG, "%s(): Rx %s ll=[%s]\n",  __func__, pdu_name,
		     osmo_hexdump(llc_prim->grr.ll_pdu, llc_prim->grr.ll_pdu_len));
		/* Forward it to lower layers, pass ownership over to RLCMAC: */
		/* Optimization: LLC-GRR-UNITDATA-IND is 1-to-1 ABI compatible with
				 RLCMAC-GRR-UNITDATA-IND, we just need to adapt the header.
				 See osmo_static_assert(_grr_prim_size) above.
		*/
		llc_prim->oph.sap = OSMO_GPRS_RLCMAC_SAP_GRR;
		llc_prim->oph.primitive = OSMO_GPRS_RLCMAC_GRR_UNITDATA;
		osmo_gprs_rlcmac_prim_upper_down((struct osmo_gprs_rlcmac_prim *)llc_prim);
		rc = 1; /* Tell RLCMAC that we take ownership of the prim. */
		break;
	default:
		LOGP(DLLC, LOGL_DEBUG, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}
	return rc;
}

int modem_llc_init(struct osmocom_ms *ms, const char *cipher_plugin_path)
{
	int rc;
	rc = osmo_gprs_llc_init(OSMO_GPRS_LLC_LOCATION_MS, cipher_plugin_path);
	if (rc != 0)
		return rc;

	osmo_gprs_llc_set_log_cat(OSMO_GPRS_LLC_LOGC_LLC, DLLC);

	osmo_gprs_llc_prim_set_up_cb(modem_llc_prim_up_cb, ms);
	osmo_gprs_llc_prim_set_down_cb(modem_llc_prim_down_cb, ms);
	return rc;
}
