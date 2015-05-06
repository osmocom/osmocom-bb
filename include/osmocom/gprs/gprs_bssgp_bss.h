#pragma once

#include <osmocom/core/msgb.h>
#include <osmocom/gprs/gprs_bssgp.h>

/* GPRS BSSGP protocol implementation as per 3GPP TS 08.18 */

/* (C) 2009-2012 by Harald Welte <laforge@gnumonks.org>
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


uint8_t *bssgp_msgb_tlli_put(struct msgb *msg, uint32_t tlli);

int bssgp_tx_suspend(uint16_t nsei, uint32_t tlli,
		     const struct gprs_ra_id *ra_id);

int bssgp_tx_resume(uint16_t nsei, uint32_t tlli,
		    const struct gprs_ra_id *ra_id, uint8_t suspend_ref);

int bssgp_tx_ra_capa_upd(struct bssgp_bvc_ctx *bctx, uint32_t tlli, uint8_t tag);

int bssgp_tx_radio_status_tlli(struct bssgp_bvc_ctx *bctx, uint8_t cause,
				uint32_t tlli);

int bssgp_tx_radio_status_tmsi(struct bssgp_bvc_ctx *bctx, uint8_t cause,
				uint32_t tmsi);

int bssgp_tx_radio_status_imsi(struct bssgp_bvc_ctx *bctx, uint8_t cause,
				const char *imsi);

int bssgp_tx_flush_ll_ack(struct bssgp_bvc_ctx *bctx, uint32_t tlli,
			   uint8_t action, uint16_t bvci_new,
			   uint32_t num_octets);

int bssgp_tx_llc_discarded(struct bssgp_bvc_ctx *bctx, uint32_t tlli,
			   uint8_t num_frames, uint32_t num_octets);

int bssgp_tx_bvc_block(struct bssgp_bvc_ctx *bctx, uint8_t cause);

int bssgp_tx_bvc_unblock(struct bssgp_bvc_ctx *bctx);

int bssgp_tx_bvc_reset(struct bssgp_bvc_ctx *bctx, uint16_t bvci, uint8_t cause);

int bssgp_tx_ul_ud(struct bssgp_bvc_ctx *bctx, uint32_t tlli,
		   const uint8_t *qos_profile, struct msgb *llc_pdu);

int bssgp_rx_paging(struct bssgp_paging_info *pinfo,
		    struct msgb *msg);

int bssgp_tx_fc_bvc(struct bssgp_bvc_ctx *bctx, uint8_t tag,
		    uint32_t bucket_size, uint32_t bucket_leak_rate,
		    uint32_t bmax_default_ms, uint32_t r_default_ms,
		    uint8_t *bucket_full_ratio, uint32_t *queue_delay_ms);

int bssgp_tx_fc_ms(struct bssgp_bvc_ctx *bctx, uint32_t tlli, uint8_t tag,
		   uint32_t ms_bucket_size, uint32_t bucket_leak_rate,
		   uint8_t *bucket_full_ratio);
