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

#include <osmocom/core/msgb.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/prim.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/gprs/rlcmac/rlcmac_prim.h>
#include <osmocom/gprs/rlcmac/rlcmac.h>
#include <osmocom/gprs/llc/llc_prim.h>
#include <osmocom/gprs/gmm/gmm_prim.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/rlcmac.h>
#include <osmocom/bb/modem/grr.h>

#include <l1ctl_proto.h>

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
	struct osmo_gprs_gmm_prim *gmm_prim;
	int rc;

	osmo_static_assert(sizeof(struct osmo_gprs_rlcmac_gmmrr_prim) == sizeof(struct osmo_gprs_gmm_gmmrr_prim),
			   _gmmrr_prim_size);

	switch (rlcmac_prim->oph.primitive) {
	case OSMO_GPRS_RLCMAC_GMMRR_PAGE:
		/* Forward it to upper layers, pass ownership over to GMM: */
		/* Optimization: RLCMAC-GMMRR-ASSIGN-REQ is 1-to-1 ABI compatible with
				 GMM-GMMRR-ASSIGN-REQ, we just need to adapt the header.
				 See osmo_static_assert(_gmmrr_prim_size) above.
		*/
		gmm_prim = (struct osmo_gprs_gmm_prim *)rlcmac_prim;
		gmm_prim->oph.sap = OSMO_GPRS_GMM_SAP_GMMRR;
		gmm_prim->oph.primitive = OSMO_GPRS_RLCMAC_GMMRR_PAGE;
		osmo_gprs_gmm_prim_lower_up(gmm_prim);
		rc = 1; /* Tell RLCMAC that we take ownership of the prim. */
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

static int modem_rlcmac_prim_down_cb(struct osmo_gprs_rlcmac_prim *prim, void *user_data)
{
	const struct osmo_gprs_rlcmac_l1ctl_prim *lp = &prim->l1ctl;
	const char *pdu_name = osmo_gprs_rlcmac_prim_name(prim);
	struct osmocom_ms *ms = user_data;

	switch (OSMO_PRIM_HDR(&prim->oph)) {
	case OSMO_PRIM(OSMO_GPRS_RLCMAC_L1CTL_RACH, PRIM_OP_REQUEST):
		if (lp->rach_req.is_11bit) {
			LOGP(DRLCMAC, LOGL_NOTICE,
			     "%s(): 11-bit RACH is not supported\n", __func__);
			return -ENOTSUP;
		}
		return modem_grr_tx_chan_req(ms, lp->rach_req.ra);
	case OSMO_PRIM(OSMO_GPRS_RLCMAC_L1CTL_PDCH_DATA, PRIM_OP_REQUEST):
		return l1ctl_tx_gprs_ul_block_req(ms,
						  lp->pdch_data_req.fn,
						  lp->pdch_data_req.ts_nr,
						  lp->pdch_data_req.data,
						  lp->pdch_data_req.data_len);
	case OSMO_PRIM(OSMO_GPRS_RLCMAC_L1CTL_CFG_UL_TBF, PRIM_OP_REQUEST):
		return l1ctl_tx_gprs_ul_tbf_cfg_req(ms,
						    lp->cfg_ul_tbf_req.ul_tbf_nr,
						    lp->cfg_ul_tbf_req.ul_slotmask);
	case OSMO_PRIM(OSMO_GPRS_RLCMAC_L1CTL_CFG_DL_TBF, PRIM_OP_REQUEST):
		return l1ctl_tx_gprs_dl_tbf_cfg_req(ms,
						    lp->cfg_dl_tbf_req.dl_tbf_nr,
						    lp->cfg_dl_tbf_req.dl_slotmask,
						    lp->cfg_dl_tbf_req.dl_tfi);
	default:
		LOGP(DRLCMAC, LOGL_DEBUG, "%s(): Unexpected Rx %s\n", __func__, pdu_name);
		OSMO_ASSERT(0);
	}
}

static int l1ctl_dl_block_cb(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct l1ctl_gprs_dl_block_ind *ind = (void *)msg->l1h;
	const uint32_t fn = osmo_load32be(&ind->hdr.fn);
	struct osmo_gprs_rlcmac_prim *prim;

	/* FIXME: sadly, rlcmac_prim_l1ctl_alloc() is not exposed */
	prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_pdch_data_ind(0, 0, 0, 0, 0, NULL, 0);
	prim->l1ctl = (struct osmo_gprs_rlcmac_l1ctl_prim) {
		.pdch_data_ind = {
			.fn = fn,
			.ts_nr = ind->hdr.tn,
			.rx_lev = ind->meas.rx_lev,
			.ber10k = osmo_load16be(&ind->meas.ber10k),
			.ci_cb = osmo_load16be(&ind->meas.ci_cb),
			.data_len = msgb_l2len(msg),
			.data = msgb_l2(msg),
		}
	};
	osmo_gprs_rlcmac_prim_lower_up(prim);

	/* Every fn % 13 == 12 we have either a PTCCH or an IDLE slot, thus
	 * every fn % 13 ==  8 we add 5 frames, or 4 frames othrwise.  The
	 * resulting value is first fn of the next block. */
	const uint32_t rts_fn = GSM_TDMA_FN_SUM(fn, (fn % 13 == 8) ? 5 : 4);
	prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_pdch_rts_ind(ind->hdr.tn, rts_fn, ind->usf);
	osmo_gprs_rlcmac_prim_lower_up(prim);

	return 0;
}

int modem_rlcmac_init(struct osmocom_ms *ms)
{
	int rc;
	rc = osmo_gprs_rlcmac_init(OSMO_GPRS_RLCMAC_LOCATION_MS);
	if (rc != 0)
		return rc;

	osmo_gprs_rlcmac_set_log_cat(OSMO_GPRS_RLCMAC_LOGC_RLCMAC, DRLCMAC);
	osmo_gprs_rlcmac_set_log_cat(OSMO_GPRS_RLCMAC_LOGC_TBFUL, DRLCMAC);
	osmo_gprs_rlcmac_set_log_cat(OSMO_GPRS_RLCMAC_LOGC_TBFDL, DRLCMAC);

	osmo_gprs_rlcmac_prim_set_up_cb(modem_rlcmac_prim_up_cb, ms);
	osmo_gprs_rlcmac_prim_set_down_cb(modem_rlcmac_prim_down_cb, ms);

	ms->l1_entity.l1_gprs_dl_block_ind = &l1ctl_dl_block_cb;

	return rc;
}
