/* GPRS BSSGP protocol implementation as per 3GPP TS 08.18 */

/* (C) 2009-2012 by Harald Welte <laforge@gnumonks.org>
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

#include <errno.h>
#include <stdint.h>

#include <netinet/in.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gprs/gprs_bssgp.h>
#include <osmocom/gprs/gprs_bssgp_bss.h>
#include <osmocom/gprs/gprs_ns.h>

#include "common_vty.h"

uint8_t *bssgp_msgb_tlli_put(struct msgb *msg, uint32_t tlli)
{
	uint32_t _tlli = htonl(tlli);
	return msgb_tvlv_put(msg, BSSGP_IE_TLLI, 4, (uint8_t *) &_tlli);
}

/*! \brief GMM-SUSPEND.req (Chapter 10.3.6) */
int bssgp_tx_suspend(uint16_t nsei, uint32_t tlli,
		     const struct gprs_ra_id *ra_id)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint8_t ra[6];

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=0) Tx SUSPEND (TLLI=0x%04x)\n",
		tlli);
	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_SUSPEND;

	bssgp_msgb_tlli_put(msg, tlli);

	gsm48_construct_ra(ra, ra_id);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, 6, ra);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief GMM-RESUME.req (Chapter 10.3.9) */
int bssgp_tx_resume(uint16_t nsei, uint32_t tlli,
		    const struct gprs_ra_id *ra_id, uint8_t suspend_ref)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint8_t ra[6];

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=0) Tx RESUME (TLLI=0x%04x)\n",
		tlli);
	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_RESUME;

	bssgp_msgb_tlli_put(msg, tlli);

	gsm48_construct_ra(ra, ra_id);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, 6, ra);

	msgb_tvlv_put(msg, BSSGP_IE_SUSPEND_REF_NR, 1, &suspend_ref);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit RA-CAPABILITY-UPDATE (10.3.3) */
int bssgp_tx_ra_capa_upd(struct bssgp_bvc_ctx *bctx, uint32_t tlli, uint8_t tag)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
		(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=%u) Tx RA-CAPA-UPD (TLLI=0x%04x)\n",
		bctx->bvci, tlli);

	/* set NSEI and BVCI in msgb cb */
	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = bctx->bvci;

	bgph->pdu_type = BSSGP_PDUT_RA_CAPA_UDPATE;
	bssgp_msgb_tlli_put(msg, tlli);

	msgb_tvlv_put(msg, BSSGP_IE_TAG, 1, &tag);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* first common part of RADIO-STATUS */
static struct msgb *common_tx_radio_status(struct bssgp_bvc_ctx *bctx)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
		(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=%u) Tx RADIO-STATUS ",
		bctx->bvci);

	/* set NSEI and BVCI in msgb cb */
	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = bctx->bvci;

	bgph->pdu_type = BSSGP_PDUT_RADIO_STATUS;

	return msg;
}

/* second common part of RADIO-STATUS */
static int common_tx_radio_status2(struct msgb *msg, uint8_t cause)
{
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, &cause);
	LOGPC(DBSSGP, LOGL_NOTICE, "CAUSE=%u\n", cause);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit RADIO-STATUS for TLLI (10.3.5) */
int bssgp_tx_radio_status_tlli(struct bssgp_bvc_ctx *bctx, uint8_t cause,
				uint32_t tlli)
{
	struct msgb *msg = common_tx_radio_status(bctx);

	if (!msg)
		return -ENOMEM;
	bssgp_msgb_tlli_put(msg, tlli);
	LOGPC(DBSSGP, LOGL_NOTICE, "TLLI=0x%08x ", tlli);

	return common_tx_radio_status2(msg, cause);
}

/*! \brief Transmit RADIO-STATUS for TMSI (10.3.5) */
int bssgp_tx_radio_status_tmsi(struct bssgp_bvc_ctx *bctx, uint8_t cause,
				uint32_t tmsi)
{
	struct msgb *msg = common_tx_radio_status(bctx);
	uint32_t _tmsi = htonl(tmsi);

	if (!msg)
		return -ENOMEM;
	msgb_tvlv_put(msg, BSSGP_IE_TMSI, 4, (uint8_t *)&_tmsi);
	LOGPC(DBSSGP, LOGL_NOTICE, "TMSI=0x%08x ", tmsi);

	return common_tx_radio_status2(msg, cause);
}

/*! \brief Transmit RADIO-STATUS for IMSI (10.3.5) */
int bssgp_tx_radio_status_imsi(struct bssgp_bvc_ctx *bctx, uint8_t cause,
				const char *imsi)
{
	struct msgb *msg = common_tx_radio_status(bctx);
	uint8_t mi[10];
	int imsi_len = gsm48_generate_mid_from_imsi(mi, imsi);

	if (!msg)
		return -ENOMEM;

	/* strip the MI type and length values (2 bytes) */
	if (imsi_len > 2)
		msgb_tvlv_put(msg, BSSGP_IE_IMSI, imsi_len-2, mi+2);
	LOGPC(DBSSGP, LOGL_NOTICE, "IMSI=%s ", imsi);

	return common_tx_radio_status2(msg, cause);
}

/*! \brief Transmit FLUSH-LL-ACK (Chapter 10.4.2) */
int bssgp_tx_flush_ll_ack(struct bssgp_bvc_ctx *bctx, uint32_t tlli,
			   uint8_t action, uint16_t bvci_new,
			   uint32_t num_octets)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint16_t _bvci_new = htons(bvci_new);
	uint32_t _oct_aff = htonl(num_octets & 0xFFFFFF);

	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_FLUSH_LL_ACK;

	bssgp_msgb_tlli_put(msg, tlli);
	msgb_tvlv_put(msg, BSSGP_IE_FLUSH_ACTION, 1, &action);
	if (action == 1) /* transferred */
		msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &_bvci_new);
	msgb_tvlv_put(msg, BSSGP_IE_NUM_OCT_AFF, 3, (uint8_t *) &_oct_aff);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit LLC-DISCARDED (Chapter 10.4.3) */
int bssgp_tx_llc_discarded(struct bssgp_bvc_ctx *bctx, uint32_t tlli,
			   uint8_t num_frames, uint32_t num_octets)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint16_t _bvci = htons(bctx->bvci);
	uint32_t _oct_aff = htonl(num_octets & 0xFFFFFF);

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=%u) Tx LLC-DISCARDED "
	     "TLLI=0x%04x, FRAMES=%u, OCTETS=%u\n", bctx->bvci, tlli,
	     num_frames, num_octets);
	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_LLC_DISCARD;

	bssgp_msgb_tlli_put(msg, tlli);

	msgb_tvlv_put(msg, BSSGP_IE_LLC_FRAMES_DISCARDED, 1, &num_frames);
	msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &_bvci);
	msgb_tvlv_put(msg, BSSGP_IE_NUM_OCT_AFF, 3, ((uint8_t *) &_oct_aff) + 1);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit a BVC-BLOCK message (Chapter 10.4.8) */
int bssgp_tx_bvc_block(struct bssgp_bvc_ctx *bctx, uint8_t cause)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint16_t _bvci = htons(bctx->bvci);

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=%u) Tx BVC-BLOCK "
	     "CAUSE=%u\n", bctx->bvci, cause);

	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_BVC_BLOCK;

	msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &_bvci);
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, &cause);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit a BVC-UNBLOCK message (Chapter 10.4.10) */
int bssgp_tx_bvc_unblock(struct bssgp_bvc_ctx *bctx)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint16_t _bvci = htons(bctx->bvci);

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=%u) Tx BVC-BLOCK\n", bctx->bvci);

	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_BVC_UNBLOCK;

	msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &_bvci);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit a BVC-RESET message (Chapter 10.4.12) */
int bssgp_tx_bvc_reset(struct bssgp_bvc_ctx *bctx, uint16_t bvci, uint8_t cause)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint16_t _bvci = htons(bvci);

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP (BVCI=%u) Tx BVC-RESET "
	     "CAUSE=%u\n", bvci, cause);

	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_BVC_RESET;

	msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &_bvci);
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, &cause);
	if (bvci != BVCI_PTM) {
		uint8_t bssgp_cid[8];
		bssgp_create_cell_id(bssgp_cid, &bctx->ra_id, bctx->cell_id);
		msgb_tvlv_put(msg, BSSGP_IE_CELL_ID, sizeof(bssgp_cid), bssgp_cid);
	}
	/* Optional: Feature Bitmap */

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit a FLOW_CONTROL-BVC (Chapter 10.4.4)
 *  \param[in] bctx BVC Context
 *  \param[in] tag Additional tag to identify acknowledge
 *  \param[in] bucket_size Maximum bucket size in octets
 *  \param[in] bucket_leak_rate Bucket leak rate in octets/sec
 *  \param[in] bmax_default_ms Maximum bucket size default for MS
 *  \param[in] r_default_ms Bucket leak rate default for MS in octets/sec
 *  \param[in] bucket_full_ratio Ratio (in percent) of queue filling
 *  \param[in] queue_delay_ms Average queuing delay in milliseconds
 */
int bssgp_tx_fc_bvc(struct bssgp_bvc_ctx *bctx, uint8_t tag,
		    uint32_t bucket_size, uint32_t bucket_leak_rate,
		    uint16_t bmax_default_ms, uint32_t r_default_ms,
		    uint8_t *bucket_full_ratio, uint32_t *queue_delay_ms)
{
	struct msgb *msg;
	struct bssgp_normal_hdr *bgph;
	uint16_t e_bucket_size, e_leak_rate, e_bmax_default_ms, e_r_default_ms;
	uint16_t e_queue_delay = 0; /* to make gcc happy */

	if ((bucket_size / 100) > 0xffff)
		return -EINVAL;
	e_bucket_size = bucket_size / 100;

	if ((bucket_leak_rate * 8 / 100) > 0xffff)
		return -EINVAL;
	e_leak_rate = (bucket_leak_rate * 8) / 100;

	if ((bmax_default_ms / 100) > 0xffff)
		return -EINVAL;
	e_bmax_default_ms = bmax_default_ms / 100;

	if ((r_default_ms * 8 / 100) > 0xffff)
		return -EINVAL;
	e_r_default_ms = (r_default_ms * 8) / 100;

	if (queue_delay_ms) {
		if ((*queue_delay_ms / 10) > 60000)
			return -EINVAL;
		else if (*queue_delay_ms == 0xFFFFFFFF)
			e_queue_delay = 0xFFFF;
		else
			e_queue_delay = *queue_delay_ms / 10;
	}

	msg = bssgp_msgb_alloc();
	bgph = (struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = bctx->bvci;
	bgph->pdu_type = BSSGP_PDUT_FLOW_CONTROL_BVC;

	msgb_tvlv_put(msg, BSSGP_IE_TAG, sizeof(tag), (uint8_t *)&tag);
	msgb_tvlv_put(msg, BSSGP_IE_BVC_BUCKET_SIZE,
		     sizeof(e_bucket_size), (uint8_t *) &e_bucket_size);
	msgb_tvlv_put(msg, BSSGP_IE_BUCKET_LEAK_RATE,
		      sizeof(e_leak_rate), (uint8_t *) &e_leak_rate);
	msgb_tvlv_put(msg, BSSGP_IE_BMAX_DEFAULT_MS,
		      sizeof(e_bmax_default_ms),
					(uint8_t *) &e_bmax_default_ms);
	msgb_tvlv_put(msg, BSSGP_IE_R_DEFAULT_MS,
		      sizeof(e_r_default_ms), (uint8_t *) &e_r_default_ms);
	if (bucket_full_ratio)
		msgb_tvlv_put(msg, BSSGP_IE_BUCKET_FULL_RATIO,
			      1, bucket_full_ratio);
	if (queue_delay_ms)
		msgb_tvlv_put(msg, BSSGP_IE_BVC_MEASUREMENT,
			      sizeof(e_queue_delay),
			      (uint8_t *) &e_queue_delay);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief Transmit a FLOW_CONTROL-MS (Chapter 10.4.6)
 *  \param[in] bctx BVC Context
 *  \param[in] tlli TLLI to identify MS
 *  \param[in] tag Additional tag to identify acknowledge
 *  \param[in] ms_bucket_size Maximum bucket size in octets
 *  \param[in] bucket_leak_rate Bucket leak rate in octets/sec
 *  \param[in] bucket_full_ratio Ratio (in percent) of queue filling
 */
int bssgp_tx_fc_ms(struct bssgp_bvc_ctx *bctx, uint32_t tlli, uint8_t tag,
		   uint32_t ms_bucket_size, uint32_t bucket_leak_rate,
		   uint8_t *bucket_full_ratio)
{
	struct msgb *msg;
	struct bssgp_normal_hdr *bgph;
	uint16_t e_bucket_size, e_leak_rate;
	uint32_t e_tlli;

	if ((ms_bucket_size / 100) > 0xffff)
		return -EINVAL;
	e_bucket_size = ms_bucket_size / 100;

	if ((bucket_leak_rate * 8 / 100) > 0xffff)
		return -EINVAL;
	e_leak_rate = (bucket_leak_rate * 8) / 100;

	msg = bssgp_msgb_alloc();
	bgph = (struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = bctx->bvci;
	bgph->pdu_type = BSSGP_PDUT_FLOW_CONTROL_MS;

	e_tlli = htonl(tlli);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, sizeof(e_tlli), (uint8_t *)&e_tlli);
	msgb_tvlv_put(msg, BSSGP_IE_TAG, sizeof(tag), (uint8_t *)&tag);
	msgb_tvlv_put(msg, BSSGP_IE_MS_BUCKET_SIZE,
		      sizeof(e_bucket_size), (uint8_t *) &e_bucket_size);
	msgb_tvlv_put(msg, BSSGP_IE_BUCKET_LEAK_RATE,
		      sizeof(e_leak_rate), (uint8_t *) &e_leak_rate);
	if (bucket_full_ratio)
		msgb_tvlv_put(msg, BSSGP_IE_BUCKET_FULL_RATIO,
			      1, bucket_full_ratio);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/*! \brief RL-UL-UNITDATA.req (Chapter 10.2.2)
 *  \param[in] bctx BVC Context
 *  \param[in] tlli TLLI to identify MS
 *  \param[in] qos_profile Pointer to three octests of QoS profile
 *  \param[in] llc_pdu msgb pointer containing UL Unitdata IE payload
 */
int bssgp_tx_ul_ud(struct bssgp_bvc_ctx *bctx, uint32_t tlli,
		   const uint8_t *qos_profile, struct msgb *llc_pdu)
{
	struct msgb *msg = llc_pdu;
	uint8_t bssgp_cid[8];
	uint8_t bssgp_align[3] = {0, 0, 0};
	struct bssgp_ud_hdr *budh;
	int align = sizeof(*budh);

	/* FIXME: Optional LSA Identifier List, PFI */

	/* Cell Identifier */
	bssgp_create_cell_id(bssgp_cid, &bctx->ra_id, bctx->cell_id);
	align += 2; /* add T+L */
	align += sizeof(bssgp_cid);

	/* First push alignment IE */
	align += 2; /* add T+L */
	align = (4 - align) & 3; /* how many octest are required to align? */
	msgb_tvlv_push(msg, BSSGP_IE_ALIGNMENT, align, bssgp_align);

	/* Push other IEs */
	msgb_tvlv_push(msg, BSSGP_IE_CELL_ID, sizeof(bssgp_cid), bssgp_cid);

	/* User Data Header */
	budh = (struct bssgp_ud_hdr *) msgb_push(msg, sizeof(*budh));
	budh->tlli = htonl(tlli);
	memcpy(budh->qos_profile, qos_profile, 3);
	budh->pdu_type = BSSGP_PDUT_UL_UNITDATA;

	/* set NSEI and BVCI in msgb cb */
	msgb_nsei(msg) = bctx->nsei;
	msgb_bvci(msg) = bctx->bvci;

	rate_ctr_inc(&bctx->ctrg->ctr[BSSGP_CTR_PKTS_OUT]);
	rate_ctr_add(&bctx->ctrg->ctr[BSSGP_CTR_BYTES_OUT], msg->len);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* Parse a single GMM-PAGING.req to a given NSEI/NS-BVCI */
int bssgp_rx_paging(struct bssgp_paging_info *pinfo,
		    struct msgb *msg)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	struct tlv_parsed tp;
	uint8_t ra[6];
	int rc, data_len;

	memset(ra, 0, sizeof(ra));

	data_len = msgb_bssgp_len(msg) - sizeof(*bgph);
	rc = bssgp_tlv_parse(&tp, bgph->data, data_len);
	if (rc < 0)
		goto err_mand_ie;

	switch (bgph->pdu_type) {
	case BSSGP_PDUT_PAGING_PS:
		pinfo->mode = BSSGP_PAGING_PS;
		break;
	case BSSGP_PDUT_PAGING_CS:
		pinfo->mode = BSSGP_PAGING_CS;
		break;
	default:
		return -EINVAL;
	}

	/* IMSI */
	if (!TLVP_PRESENT(&tp, BSSGP_IE_IMSI))
		goto err_mand_ie;
	if (!pinfo->imsi)
		pinfo->imsi = talloc_zero_size(pinfo, 16);
	gsm48_mi_to_string(pinfo->imsi, sizeof(pinfo->imsi),
			   TLVP_VAL(&tp, BSSGP_IE_IMSI),
			   TLVP_LEN(&tp, BSSGP_IE_IMSI));

	/* DRX Parameters */
	if (!TLVP_PRESENT(&tp, BSSGP_IE_DRX_PARAMS))
		goto err_mand_ie;
	pinfo->drx_params = ntohs(*(uint16_t *)TLVP_VAL(&tp, BSSGP_IE_DRX_PARAMS));

	/* Scope */
	if (TLVP_PRESENT(&tp, BSSGP_IE_BSS_AREA_ID)) {
		pinfo->scope = BSSGP_PAGING_BSS_AREA;
	} else if (TLVP_PRESENT(&tp, BSSGP_IE_LOCATION_AREA)) {
		pinfo->scope = BSSGP_PAGING_LOCATION_AREA;
		memcpy(ra, TLVP_VAL(&tp, BSSGP_IE_LOCATION_AREA),
			TLVP_LEN(&tp, BSSGP_IE_LOCATION_AREA));
		gsm48_parse_ra(&pinfo->raid, ra);
	} else if (TLVP_PRESENT(&tp, BSSGP_IE_ROUTEING_AREA)) {
		pinfo->scope = BSSGP_PAGING_ROUTEING_AREA;
		memcpy(ra, TLVP_VAL(&tp, BSSGP_IE_ROUTEING_AREA),
			TLVP_LEN(&tp, BSSGP_IE_ROUTEING_AREA));
		gsm48_parse_ra(&pinfo->raid, ra);
	} else if (TLVP_PRESENT(&tp, BSSGP_IE_BVCI)) {
		pinfo->scope = BSSGP_PAGING_BVCI;
		pinfo->bvci = ntohs(*(uint16_t *)TLVP_VAL(&tp, BSSGP_IE_BVCI));
	} else
		return -EINVAL;

	/* QoS profile mandatory for PS */
	if (pinfo->mode == BSSGP_PAGING_PS) {
		if (!TLVP_PRESENT(&tp, BSSGP_IE_QOS_PROFILE))
			goto err_cond_ie;
		if (TLVP_LEN(&tp, BSSGP_IE_QOS_PROFILE) < 3)
			goto err;

		memcpy(&pinfo->qos, TLVP_VAL(&tp, BSSGP_IE_QOS_PROFILE),
			3);
	}

	/* Optional (P-)TMSI */
	if (TLVP_PRESENT(&tp, BSSGP_IE_TMSI) &&
	    TLVP_LEN(&tp, BSSGP_IE_TMSI) >= 4)
		if (!pinfo->ptmsi)
			pinfo->ptmsi = talloc_zero_size(pinfo, sizeof(uint32_t));
		*(pinfo->ptmsi) = ntohl(*(uint32_t *)
					TLVP_VAL(&tp, BSSGP_IE_TMSI));

	return 0;

err_mand_ie:
err_cond_ie:
err:
	/* FIXME */
	return 0;
}
