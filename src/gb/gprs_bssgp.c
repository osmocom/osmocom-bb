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
 * TODO:
 *  o  properly count incoming BVC-RESET packets in counter group
 *  o  set log context as early as possible for outgoing packets
 */

#include <errno.h>
#include <stdint.h>

#include <netinet/in.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/rate_ctr.h>

#include <osmocom/gprs/gprs_bssgp.h>
#include <osmocom/gprs/gprs_ns.h>

#include "common_vty.h"

void *bssgp_tall_ctx = NULL;

static const struct rate_ctr_desc bssgp_ctr_description[] = {
	{ "packets.in",	"Packets at BSSGP Level ( In)" },
	{ "packets.out","Packets at BSSGP Level (Out)" },
	{ "bytes.in",	"Bytes at BSSGP Level   ( In)" },
	{ "bytes.out",	"Bytes at BSSGP Level   (Out)" },
	{ "blocked",	"BVC Blocking count" },
	{ "discarded",	"BVC LLC Discarded count" },
};

static const struct rate_ctr_group_desc bssgp_ctrg_desc = {
	.group_name_prefix = "bssgp.bss_ctx",
	.group_description = "BSSGP Peer Statistics",
	.num_ctr = ARRAY_SIZE(bssgp_ctr_description),
	.ctr_desc = bssgp_ctr_description,
};

LLIST_HEAD(bssgp_bvc_ctxts);

static int _bssgp_tx_dl_ud(struct bssgp_flow_control *fc, struct msgb *msg,
			   uint32_t llc_pdu_len, void *priv);

/* Find a BTS Context based on parsed RA ID and Cell ID */
struct bssgp_bvc_ctx *btsctx_by_raid_cid(const struct gprs_ra_id *raid, uint16_t cid)
{
	struct bssgp_bvc_ctx *bctx;

	llist_for_each_entry(bctx, &bssgp_bvc_ctxts, list) {
		if (!memcmp(&bctx->ra_id, raid, sizeof(bctx->ra_id)) &&
		    bctx->cell_id == cid)
			return bctx;
	}
	return NULL;
}

/* Find a BTS context based on BVCI+NSEI tuple */
struct bssgp_bvc_ctx *btsctx_by_bvci_nsei(uint16_t bvci, uint16_t nsei)
{
	struct bssgp_bvc_ctx *bctx;

	llist_for_each_entry(bctx, &bssgp_bvc_ctxts, list) {
		if (bctx->nsei == nsei && bctx->bvci == bvci)
			return bctx;
	}
	return NULL;
}

struct bssgp_bvc_ctx *btsctx_alloc(uint16_t bvci, uint16_t nsei)
{
	struct bssgp_bvc_ctx *ctx;

	ctx = talloc_zero(bssgp_tall_ctx, struct bssgp_bvc_ctx);
	if (!ctx)
		return NULL;
	ctx->bvci = bvci;
	ctx->nsei = nsei;
	/* FIXME: BVCI is not unique, only BVCI+NSEI ?!? */
	ctx->ctrg = rate_ctr_group_alloc(ctx, &bssgp_ctrg_desc, bvci);
	ctx->fc = talloc_zero(ctx, struct bssgp_flow_control);
	/* cofigure for 2Mbit, 30 packets in queue */
	bssgp_fc_init(ctx->fc, 100000, 2*1024*1024/8, 30, &_bssgp_tx_dl_ud);

	llist_add(&ctx->list, &bssgp_bvc_ctxts);

	return ctx;
}

/* Chapter 10.4.5: Flow Control BVC ACK */
static int bssgp_tx_fc_bvc_ack(uint16_t nsei, uint8_t tag, uint16_t ns_bvci)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = ns_bvci;

	bgph->pdu_type = BSSGP_PDUT_FLOW_CONTROL_BVC_ACK;
	msgb_tvlv_put(msg, BSSGP_IE_TAG, 1, &tag);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* 10.3.7 SUSPEND-ACK PDU */
int bssgp_tx_suspend_ack(uint16_t nsei, uint32_t tlli,
			 const struct gprs_ra_id *ra_id, uint8_t suspend_ref)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
		(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint32_t _tlli;
	uint8_t ra[6];

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_SUSPEND_ACK;

	_tlli = htonl(tlli);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, 4, (uint8_t *) &_tlli);
	gsm48_construct_ra(ra, ra_id);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, 6, ra);
	msgb_tvlv_put(msg, BSSGP_IE_SUSPEND_REF_NR, 1, &suspend_ref);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* 10.3.8 SUSPEND-NACK PDU */
int bssgp_tx_suspend_nack(uint16_t nsei, uint32_t tlli,
			  const struct gprs_ra_id *ra_id,
			  uint8_t *cause)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
		(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint32_t _tlli;
	uint8_t ra[6];

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_SUSPEND_NACK;

	_tlli = htonl(tlli);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, 4, (uint8_t *) &_tlli);
	gsm48_construct_ra(ra, ra_id);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, 6, ra);
	if (cause)
		msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, cause);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* 10.3.10 RESUME-ACK PDU */
int bssgp_tx_resume_ack(uint16_t nsei, uint32_t tlli,
			const struct gprs_ra_id *ra_id)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
		(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint32_t _tlli;
	uint8_t ra[6];

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_RESUME_ACK;

	_tlli = htonl(tlli);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, 4, (uint8_t *) &_tlli);
	gsm48_construct_ra(ra, ra_id);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, 6, ra);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* 10.3.11 RESUME-NACK PDU */
int bssgp_tx_resume_nack(uint16_t nsei, uint32_t tlli,
			 const struct gprs_ra_id *ra_id, uint8_t *cause)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
		(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint32_t _tlli;
	uint8_t ra[6];

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_SUSPEND_NACK;

	_tlli = htonl(tlli);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, 4, (uint8_t *) &_tlli);
	gsm48_construct_ra(ra, ra_id);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, 6, ra);
	if (cause)
		msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, cause);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

uint16_t bssgp_parse_cell_id(struct gprs_ra_id *raid, const uint8_t *buf)
{
	/* 6 octets RAC */
	gsm48_parse_ra(raid, buf);
	/* 2 octets CID */
	return ntohs(*(uint16_t *) (buf+6));
}

int bssgp_create_cell_id(uint8_t *buf, const struct gprs_ra_id *raid,
			 uint16_t cid)
{
	uint16_t *out_cid = (uint16_t *) (buf + 6);
	/* 6 octets RAC */
	gsm48_construct_ra(buf, raid);
	/* 2 octets CID */
	*out_cid = htons(cid);

	return 8;
}

/* Chapter 8.4 BVC-Reset Procedure */
static int bssgp_rx_bvc_reset(struct msgb *msg, struct tlv_parsed *tp,	
			      uint16_t ns_bvci)
{
	struct osmo_bssgp_prim nmp;
	struct bssgp_bvc_ctx *bctx;
	uint16_t nsei = msgb_nsei(msg);
	uint16_t bvci;

	bvci = ntohs(*(uint16_t *)TLVP_VAL(tp, BSSGP_IE_BVCI));
	DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx RESET cause=%s\n", bvci,
		bssgp_cause_str(*TLVP_VAL(tp, BSSGP_IE_CAUSE)));

	/* look-up or create the BTS context for this BVC */
	bctx = btsctx_by_bvci_nsei(bvci, nsei);
	if (!bctx)
		bctx = btsctx_alloc(bvci, nsei);

	/* As opposed to NS-VCs, BVCs are NOT blocked after RESET */
	bctx->state &= ~BVC_S_BLOCKED;

	/* When we receive a BVC-RESET PDU (at least of a PTP BVCI), the BSS
	 * informs us about its RAC + Cell ID, so we can create a mapping */
	if (bvci != 0 && bvci != 1) {
		if (!TLVP_PRESENT(tp, BSSGP_IE_CELL_ID)) {
			LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u Rx RESET "
				"missing mandatory IE\n", bvci);
			return -EINVAL;
		}
		/* actually extract RAC / CID */
		bctx->cell_id = bssgp_parse_cell_id(&bctx->ra_id,
						TLVP_VAL(tp, BSSGP_IE_CELL_ID));
		LOGP(DBSSGP, LOGL_NOTICE, "Cell %u-%u-%u-%u CI %u on BVCI %u\n",
			bctx->ra_id.mcc, bctx->ra_id.mnc, bctx->ra_id.lac,
			bctx->ra_id.rac, bctx->cell_id, bvci);
	}

	/* Send NM_BVC_RESET.ind to NM */
	memset(&nmp, 0, sizeof(nmp));
	nmp.nsei = nsei;
	nmp.bvci = bvci;
	nmp.tp = tp;
	nmp.ra_id = &bctx->ra_id;
	osmo_prim_init(&nmp.oph, SAP_BSSGP_NM, PRIM_NM_BVC_RESET,
			PRIM_OP_INDICATION, msg);
	bssgp_prim_cb(&nmp.oph, NULL);

	/* Acknowledge the RESET to the BTS */
	bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_RESET_ACK,
			     nsei, bvci, ns_bvci);
	return 0;
}

static int bssgp_rx_bvc_block(struct msgb *msg, struct tlv_parsed *tp)
{
	struct osmo_bssgp_prim nmp;
	uint16_t bvci;
	struct bssgp_bvc_ctx *ptp_ctx;

	bvci = ntohs(*(uint16_t *)TLVP_VAL(tp, BSSGP_IE_BVCI));
	if (bvci == BVCI_SIGNALLING) {
		/* 8.3.2: Signalling BVC shall never be blocked */
		LOGP(DBSSGP, LOGL_ERROR, "NSEI=%u/BVCI=%u "
			"received block for signalling BVC!?!\n",
			msgb_nsei(msg), msgb_bvci(msg));
		return 0;
	}

	LOGP(DBSSGP, LOGL_INFO, "BSSGP Rx BVCI=%u BVC-BLOCK\n", bvci);

	ptp_ctx = btsctx_by_bvci_nsei(bvci, msgb_nsei(msg));
	if (!ptp_ctx)
		return bssgp_tx_status(BSSGP_CAUSE_UNKNOWN_BVCI, &bvci, msg);

	ptp_ctx->state |= BVC_S_BLOCKED;
	rate_ctr_inc(&ptp_ctx->ctrg->ctr[BSSGP_CTR_BLOCKED]);

	/* Send NM_BVC_BLOCK.ind to NM */
	memset(&nmp, 0, sizeof(nmp));
	nmp.nsei = msgb_nsei(msg);
	nmp.bvci = bvci;
	nmp.tp = tp;
	osmo_prim_init(&nmp.oph, SAP_BSSGP_NM, PRIM_NM_BVC_BLOCK,
			PRIM_OP_INDICATION, msg);
	bssgp_prim_cb(&nmp.oph, NULL);

	/* We always acknowledge the BLOCKing */
	return bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_BLOCK_ACK, msgb_nsei(msg),
				    bvci, msgb_bvci(msg));
};

static int bssgp_rx_bvc_unblock(struct msgb *msg, struct tlv_parsed *tp)
{
	struct osmo_bssgp_prim nmp;
	uint16_t bvci;
	struct bssgp_bvc_ctx *ptp_ctx;

	bvci = ntohs(*(uint16_t *)TLVP_VAL(tp, BSSGP_IE_BVCI));
	if (bvci == BVCI_SIGNALLING) {
		/* 8.3.2: Signalling BVC shall never be blocked */
		LOGP(DBSSGP, LOGL_ERROR, "NSEI=%u/BVCI=%u "
			"received unblock for signalling BVC!?!\n",
			msgb_nsei(msg), msgb_bvci(msg));
		return 0;
	}

	DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx BVC-UNBLOCK\n", bvci);

	ptp_ctx = btsctx_by_bvci_nsei(bvci, msgb_nsei(msg));
	if (!ptp_ctx)
		return bssgp_tx_status(BSSGP_CAUSE_UNKNOWN_BVCI, &bvci, msg);

	ptp_ctx->state &= ~BVC_S_BLOCKED;

	/* Send NM_BVC_UNBLOCK.ind to NM */
	memset(&nmp, 0, sizeof(nmp));
	nmp.nsei = msgb_nsei(msg);
	nmp.bvci = bvci;
	nmp.tp = tp;
	osmo_prim_init(&nmp.oph, SAP_BSSGP_NM, PRIM_NM_BVC_UNBLOCK,
			PRIM_OP_INDICATION, msg);
	bssgp_prim_cb(&nmp.oph, NULL);

	/* We always acknowledge the unBLOCKing */
	return bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_UNBLOCK_ACK, msgb_nsei(msg),
				    bvci, msgb_bvci(msg));
};

/* Uplink unit-data */
static int bssgp_rx_ul_ud(struct msgb *msg, struct tlv_parsed *tp,
			  struct bssgp_bvc_ctx *ctx)
{
	struct osmo_bssgp_prim gbp;
	struct bssgp_ud_hdr *budh = (struct bssgp_ud_hdr *) msgb_bssgph(msg);

	/* extract TLLI and parse TLV IEs */
	msgb_tlli(msg) = ntohl(budh->tlli);

	DEBUGP(DBSSGP, "BSSGP TLLI=0x%08x Rx UPLINK-UNITDATA\n", msgb_tlli(msg));

	/* Cell ID and LLC_PDU are the only mandatory IE */
	if (!TLVP_PRESENT(tp, BSSGP_IE_CELL_ID) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_LLC_PDU)) {
		LOGP(DBSSGP, LOGL_ERROR, "BSSGP TLLI=0x%08x Rx UL-UD "
			"missing mandatory IE\n", msgb_tlli(msg));
		return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);
	}

	/* store pointer to LLC header and CELL ID in msgb->cb */
	msgb_llch(msg) = (uint8_t *) TLVP_VAL(tp, BSSGP_IE_LLC_PDU);
	msgb_bcid(msg) = (uint8_t *) TLVP_VAL(tp, BSSGP_IE_CELL_ID);

	/* Send BSSGP_UL_UD.ind to NM */
	memset(&gbp, 0, sizeof(gbp));
	gbp.nsei = ctx->nsei;
	gbp.bvci = ctx->bvci;
	gbp.tlli = msgb_tlli(msg);
	gbp.tp = tp;
	osmo_prim_init(&gbp.oph, SAP_BSSGP_LL, PRIM_BSSGP_UL_UD,
			PRIM_OP_INDICATION, msg);
	return bssgp_prim_cb(&gbp.oph, NULL);
}

static int bssgp_rx_suspend(struct msgb *msg, struct tlv_parsed *tp,
			    struct bssgp_bvc_ctx *ctx)
{
	struct osmo_bssgp_prim gbp;
	struct gprs_ra_id raid;
	uint32_t tlli;
	int rc;

	if (!TLVP_PRESENT(tp, BSSGP_IE_TLLI) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_ROUTEING_AREA)) {
		LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u Rx SUSPEND "
			"missing mandatory IE\n", ctx->bvci);
		return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);
	}

	tlli = ntohl(*(uint32_t *)TLVP_VAL(tp, BSSGP_IE_TLLI));

	DEBUGP(DBSSGP, "BSSGP BVCI=%u TLLI=0x%08x Rx SUSPEND\n",
		ctx->bvci, tlli);

	gsm48_parse_ra(&raid, TLVP_VAL(tp, BSSGP_IE_ROUTEING_AREA));

	/* Inform GMM about the SUSPEND request */
	memset(&gbp, 0, sizeof(gbp));
	gbp.nsei = msgb_nsei(msg);
	gbp.bvci = ctx->bvci;
	gbp.tlli = tlli;
	gbp.ra_id = &raid;
	osmo_prim_init(&gbp.oph, SAP_BSSGP_GMM, PRIM_BSSGP_GMM_SUSPEND,
			PRIM_OP_REQUEST, msg);

	rc = bssgp_prim_cb(&gbp.oph, NULL);
	if (rc < 0)
		return bssgp_tx_suspend_nack(msgb_nsei(msg), tlli, &raid, NULL);

	bssgp_tx_suspend_ack(msgb_nsei(msg), tlli, &raid, 0);

	return 0;
}

static int bssgp_rx_resume(struct msgb *msg, struct tlv_parsed *tp,
			   struct bssgp_bvc_ctx *ctx)
{
	struct osmo_bssgp_prim gbp;
	struct gprs_ra_id raid;
	uint32_t tlli;
	uint8_t suspend_ref;
	int rc;

	if (!TLVP_PRESENT(tp, BSSGP_IE_TLLI) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_ROUTEING_AREA) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_SUSPEND_REF_NR)) {
		LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u Rx RESUME "
			"missing mandatory IE\n", ctx->bvci);
		return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);
	}

	tlli = ntohl(*(uint32_t *)TLVP_VAL(tp, BSSGP_IE_TLLI));
	suspend_ref = *TLVP_VAL(tp, BSSGP_IE_SUSPEND_REF_NR);

	DEBUGP(DBSSGP, "BSSGP BVCI=%u TLLI=0x%08x Rx RESUME\n", ctx->bvci, tlli);

	gsm48_parse_ra(&raid, TLVP_VAL(tp, BSSGP_IE_ROUTEING_AREA));

	/* Inform GMM about the RESUME request */
	memset(&gbp, 0, sizeof(gbp));
	gbp.nsei = msgb_nsei(msg);
	gbp.bvci = ctx->bvci;
	gbp.tlli = tlli;
	gbp.ra_id = &raid;
	gbp.u.resume.suspend_ref = suspend_ref;
	osmo_prim_init(&gbp.oph, SAP_BSSGP_GMM, PRIM_BSSGP_GMM_RESUME,
			PRIM_OP_REQUEST, msg);

	rc = bssgp_prim_cb(&gbp.oph, NULL);
	if (rc < 0)
		return bssgp_tx_resume_nack(msgb_nsei(msg), tlli, &raid,
					    NULL);

	bssgp_tx_resume_ack(msgb_nsei(msg), tlli, &raid);
	return 0;
}


static int bssgp_rx_llc_disc(struct msgb *msg, struct tlv_parsed *tp,
			     struct bssgp_bvc_ctx *ctx)
{
	struct osmo_bssgp_prim nmp;
	uint32_t tlli = 0;

	if (!TLVP_PRESENT(tp, BSSGP_IE_TLLI) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_LLC_FRAMES_DISCARDED) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BVCI) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_NUM_OCT_AFF)) {
		LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u Rx LLC DISCARDED "
			"missing mandatory IE\n", ctx->bvci);
	}

	if (TLVP_PRESENT(tp, BSSGP_IE_TLLI))
		tlli = ntohl(*(uint32_t *)TLVP_VAL(tp, BSSGP_IE_TLLI));

	DEBUGP(DBSSGP, "BSSGP BVCI=%u TLLI=%08x Rx LLC DISCARDED\n",
		ctx->bvci, tlli);

	rate_ctr_inc(&ctx->ctrg->ctr[BSSGP_CTR_DISCARDED]);

	/* send NM_LLC_DISCARDED to NM */
	memset(&nmp, 0, sizeof(nmp));
	nmp.nsei = msgb_nsei(msg);
	nmp.bvci = ctx->bvci;
	nmp.tlli = tlli;
	nmp.tp = tp;
	osmo_prim_init(&nmp.oph, SAP_BSSGP_NM, PRIM_NM_LLC_DISCARDED,
			PRIM_OP_INDICATION, msg);

	return bssgp_prim_cb(&nmp.oph, NULL);
}

/* One element (msgb) in a BSSGP Flow Control queue */
struct bssgp_fc_queue_element {
	/* linked list of queue elements */
	struct llist_head list;
	/* The message that we have enqueued */
	struct msgb *msg;
	/* Length of the LLC PDU part of the contained message */
	uint32_t llc_pdu_len;
	/* private pointer passed to the flow control out_cb function */
	void *priv;
};

static int fc_queue_timer_cfg(struct bssgp_flow_control *fc);
static int bssgp_fc_needs_queueing(struct bssgp_flow_control *fc, uint32_t pdu_len);

static void fc_timer_cb(void *data)
{
	struct bssgp_flow_control *fc = data;
	struct bssgp_fc_queue_element *fcqe;
	struct timeval time_now;

	/* if the queue is empty, we return without sending something
	 * and without re-starting the timer */
	if (llist_empty(&fc->queue))
		return;

	/* get the first entry from the queue */
	fcqe = llist_entry(fc->queue.next, struct bssgp_fc_queue_element,
			   list);

	if (bssgp_fc_needs_queueing(fc, fcqe->llc_pdu_len)) {
		LOGP(DBSSGP, LOGL_NOTICE, "BSSGP-FC: fc_timer_cb() but still "
			"not able to send PDU of %u bytes\n", fcqe->llc_pdu_len);
		/* make sure we re-start the timer */
		fc_queue_timer_cfg(fc);
		return;
	}

	/* remove from the queue */
	llist_del(&fcqe->list);

	fc->queue_depth--;

	/* record the time we transmitted this PDU */
	gettimeofday(&time_now, NULL);
	fc->time_last_pdu = time_now;

	/* call the output callback for this FC instance */
	fc->out_cb(fcqe->priv, fcqe->msg, fcqe->llc_pdu_len, NULL);

	/* we expect that out_cb will in the end free the msgb once
	 * it is no longer needed */

	/* but we have to free the queue element ourselves */
	talloc_free(fcqe);

	/* re-configure the timer for the next PDU */
	fc_queue_timer_cfg(fc);
}

/* configure/schedule the flow control timer to expire once the bucket
 * will have leaked a sufficient number of bytes to transmit the next
 * PDU in the queue */
static int fc_queue_timer_cfg(struct bssgp_flow_control *fc)
{
	struct bssgp_fc_queue_element *fcqe;
	uint32_t msecs;

	if (llist_empty(&fc->queue))
		return 0;

	fcqe = llist_entry(&fc->queue.next, struct bssgp_fc_queue_element,
			   list);

	/* Calculate the point in time at which we will have leaked
	 * a sufficient number of bytes from the bucket to transmit
	 * the first PDU in the queue */
	msecs = (fcqe->llc_pdu_len * 1000) / fc->bucket_leak_rate;
	/* FIXME: add that time to fc->time_last_pdu and subtract it from
	 * current time */

	fc->timer.data = fc;
	fc->timer.cb = &fc_timer_cb;
	osmo_timer_schedule(&fc->timer, msecs / 1000, (msecs % 1000) * 1000);

	return 0;
}

/* Enqueue a PDU in the flow control queue for delayed transmission */
static int fc_enqueue(struct bssgp_flow_control *fc, struct msgb *msg,
		      uint32_t llc_pdu_len, void *priv)
{
	struct bssgp_fc_queue_element *fcqe;

	if (fc->queue_depth >= fc->max_queue_depth)
		return -ENOSPC;

	fcqe = talloc_zero(fc, struct bssgp_fc_queue_element);
	if (!fcqe)
		return -ENOMEM;
	fcqe->msg = msg;
	fcqe->llc_pdu_len = llc_pdu_len;
	fcqe->priv = priv;

	llist_add_tail(&fcqe->list, &fc->queue);

	fc->queue_depth++;

	/* re-configure the timer for dequeueing the pdu */
	fc_queue_timer_cfg(fc);

	return 0;
}

/* According to Section 8.2 */
static int bssgp_fc_needs_queueing(struct bssgp_flow_control *fc, uint32_t pdu_len)
{
	struct timeval time_now, time_diff;
	int64_t bucket_predicted;
	uint32_t csecs_elapsed, leaked;

	/* B' = B + L(p) - (Tc - Tp)*R */

	/* compute number of centi-seconds that have elapsed since transmitting
	 * the last PDU (Tc - Tp) */
	gettimeofday(&time_now, NULL);
	timersub(&time_now, &fc->time_last_pdu, &time_diff);
	csecs_elapsed = time_diff.tv_sec*100 + time_diff.tv_usec/10000;

	/* compute number of bytes that have leaked in the elapsed number
	 * of centi-seconds */
	leaked = csecs_elapsed * (fc->bucket_leak_rate / 100);
	/* add the current PDU length to the last bucket level */
	bucket_predicted = fc->bucket_counter + pdu_len;
	/* ... and subtract the number of leaked bytes */
	bucket_predicted -= leaked;

	if (bucket_predicted < pdu_len) {
		/* this is just to make sure the bucket doesn't underflow */
		bucket_predicted = pdu_len;
		goto pass;
	}

	if (bucket_predicted <= fc->bucket_size_max) {
		/* the bucket is not full yet, we can pass the packet */
		fc->bucket_counter = bucket_predicted;
		goto pass;
	}

	/* bucket is full, PDU needs to be delayed */
	return 1;

pass:
	/* if we reach here, the PDU can pass */
	return 0;
}

/* output callback for BVC flow control */
static int _bssgp_tx_dl_ud(struct bssgp_flow_control *fc, struct msgb *msg,
			   uint32_t llc_pdu_len, void *priv)
{
	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* input function of the flow control implementation, called first
 * for the MM flow control, and then as the MM flow control output
 * callback in order to perform BVC flow control */
int bssgp_fc_in(struct bssgp_flow_control *fc, struct msgb *msg,
		uint32_t llc_pdu_len, void *priv)
{
	struct timeval time_now;

	if (llc_pdu_len > fc->bucket_size_max) {
		LOGP(DBSSGP, LOGL_NOTICE, "Single PDU (size=%u) is larger "
		     "than maximum bucket size (%u)!\n", llc_pdu_len,
		     fc->bucket_size_max);
		return -EIO;
	}

	if (bssgp_fc_needs_queueing(fc, llc_pdu_len)) {
		return fc_enqueue(fc, msg, llc_pdu_len, priv);
	} else {
		/* record the time we transmitted this PDU */
		gettimeofday(&time_now, NULL);
		fc->time_last_pdu = time_now;
		return fc->out_cb(priv, msg, llc_pdu_len, NULL);
	}
}


/* Initialize the Flow Control structure */
void bssgp_fc_init(struct bssgp_flow_control *fc,
		   uint32_t bucket_size_max, uint32_t bucket_leak_rate,
		   uint32_t max_queue_depth,
		   int (*out_cb)(struct bssgp_flow_control *fc, struct msgb *msg,
				 uint32_t llc_pdu_len, void *priv))
{
	fc->out_cb = out_cb;
	fc->bucket_size_max = bucket_size_max;
	fc->bucket_leak_rate = bucket_leak_rate;
	fc->max_queue_depth = max_queue_depth;
	INIT_LLIST_HEAD(&fc->queue);
	gettimeofday(&fc->time_last_pdu, NULL);
}

/* Initialize the Flow Control parameters for a new MS according to
 * default values for the BVC specified by BVCI and NSEI */
int bssgp_fc_ms_init(struct bssgp_flow_control *fc_ms, uint16_t bvci,
		     uint16_t nsei, uint32_t max_queue_depth)
{
	struct bssgp_bvc_ctx *ctx;

	ctx = btsctx_by_bvci_nsei(bvci, nsei);
	if (!ctx)
		return -ENODEV;

	/* output call-back of per-MS FC is per-CTX FC */
	bssgp_fc_init(fc_ms, ctx->bmax_default_ms, ctx->r_default_ms,
			max_queue_depth, bssgp_fc_in);

	return 0;
}

static int bssgp_rx_fc_bvc(struct msgb *msg, struct tlv_parsed *tp,
			   struct bssgp_bvc_ctx *bctx)
{

	DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx Flow Control BVC\n",
		bctx->bvci);

	if (!TLVP_PRESENT(tp, BSSGP_IE_TAG) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BVC_BUCKET_SIZE) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BUCKET_LEAK_RATE) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BMAX_DEFAULT_MS) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_R_DEFAULT_MS)) {
		LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u Rx FC BVC "
			"missing mandatory IE\n", bctx->bvci);
		return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);
	}

	/* 11.3.5 Bucket Size in 100 octets unit */
	bctx->fc->bucket_size_max = 100 *
		ntohs(*(uint16_t *)TLVP_VAL(tp, BSSGP_IE_BVC_BUCKET_SIZE));
	/* 11.3.4 Bucket Leak Rate in 100 bits/sec unit */
	bctx->fc->bucket_leak_rate = 100 *
		ntohs(*(uint16_t *)TLVP_VAL(tp, BSSGP_IE_BUCKET_LEAK_RATE)) / 8;
	/* 11.3.2 in octets */
	bctx->bmax_default_ms =
		ntohs(*(uint16_t *)TLVP_VAL(tp, BSSGP_IE_BMAX_DEFAULT_MS));
	/* 11.3.32 Bucket Leak rate in 100bits/sec unit */
	bctx->r_default_ms = 100 *
		ntohs(*(uint16_t *)TLVP_VAL(tp, BSSGP_IE_R_DEFAULT_MS)) / 8;

	/* Send FLOW_CONTROL_BVC_ACK */
	return bssgp_tx_fc_bvc_ack(msgb_nsei(msg), *TLVP_VAL(tp, BSSGP_IE_TAG),
				   msgb_bvci(msg));
}

/* Receive a BSSGP PDU from a BSS on a PTP BVCI */
static int bssgp_rx_ptp(struct msgb *msg, struct tlv_parsed *tp,
			struct bssgp_bvc_ctx *bctx)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	uint8_t pdu_type = bgph->pdu_type;
	int rc = 0;

	/* If traffic is received on a BVC that is marked as blocked, the
	 * received PDU shall not be accepted and a STATUS PDU (Cause value:
	 * BVC Blocked) shall be sent to the peer entity on the signalling BVC */
	if (bctx->state & BVC_S_BLOCKED && pdu_type != BSSGP_PDUT_STATUS) {
		uint16_t bvci = msgb_bvci(msg);
		return bssgp_tx_status(BSSGP_CAUSE_BVCI_BLOCKED, &bvci, msg);
	}

	switch (pdu_type) {
	case BSSGP_PDUT_UL_UNITDATA:
		/* some LLC data from the MS */
		rc = bssgp_rx_ul_ud(msg, tp, bctx);
		break;
	case BSSGP_PDUT_RA_CAPABILITY:
		/* BSS requests RA capability or IMSI */
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx RA CAPABILITY UPDATE\n",
			bctx->bvci);
		/* FIXME: send GMM_RA_CAPABILITY_UPDATE.ind to GMM */
		/* FIXME: send RA_CAPA_UPDATE_ACK */
		break;
	case BSSGP_PDUT_RADIO_STATUS:
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx RADIO STATUS\n", bctx->bvci);
		/* BSS informs us of some exception */
		/* FIXME: send GMM_RADIO_STATUS.ind to GMM */
		break;
	case BSSGP_PDUT_FLOW_CONTROL_BVC:
		/* BSS informs us of available bandwidth in Gb interface */
		rc = bssgp_rx_fc_bvc(msg, tp, bctx);
		break;
	case BSSGP_PDUT_FLOW_CONTROL_MS:
		/* BSS informs us of available bandwidth to one MS */
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx Flow Control MS\n",
			bctx->bvci);
		/* FIXME: actually implement flow control */
		/* FIXME: Send FLOW_CONTROL_MS_ACK */
		break;
	case BSSGP_PDUT_STATUS:
		/* Some exception has occurred */
		/* FIXME: send NM_STATUS.ind to NM */
	case BSSGP_PDUT_DOWNLOAD_BSS_PFC:
	case BSSGP_PDUT_CREATE_BSS_PFC_ACK:
	case BSSGP_PDUT_CREATE_BSS_PFC_NACK:
	case BSSGP_PDUT_MODIFY_BSS_PFC:
	case BSSGP_PDUT_DELETE_BSS_PFC_ACK:
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx PDU type 0x%02x not [yet] "
			"implemented\n", bctx->bvci, pdu_type);
		rc = bssgp_tx_status(BSSGP_CAUSE_PDU_INCOMP_FEAT, NULL, msg);
		break;
	/* those only exist in the SGSN -> BSS direction */
	case BSSGP_PDUT_DL_UNITDATA:
	case BSSGP_PDUT_PAGING_PS:
	case BSSGP_PDUT_PAGING_CS:
	case BSSGP_PDUT_RA_CAPA_UPDATE_ACK:
	case BSSGP_PDUT_FLOW_CONTROL_BVC_ACK:
	case BSSGP_PDUT_FLOW_CONTROL_MS_ACK:
		DEBUGP(DBSSGP, "BSSGP BVCI=%u PDU type 0x%02x only exists "
			"in DL\n", bctx->bvci, pdu_type);
		bssgp_tx_status(BSSGP_CAUSE_PROTO_ERR_UNSPEC, NULL, msg);
		rc = -EINVAL;
		break;
	default:
		DEBUGP(DBSSGP, "BSSGP BVCI=%u PDU type 0x%02x unknown\n",
			bctx->bvci, pdu_type);
		rc = bssgp_tx_status(BSSGP_CAUSE_PROTO_ERR_UNSPEC, NULL, msg);
		break;
	}

	return rc;
}

/* Receive a BSSGP PDU from a BSS on a SIGNALLING BVCI */
static int bssgp_rx_sign(struct msgb *msg, struct tlv_parsed *tp,
			 struct bssgp_bvc_ctx *bctx)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	uint8_t pdu_type = bgph->pdu_type;
	int rc = 0;
	uint16_t ns_bvci = msgb_bvci(msg);

	switch (bgph->pdu_type) {
	case BSSGP_PDUT_SUSPEND:
		/* MS wants to suspend */
		rc = bssgp_rx_suspend(msg, tp, bctx);
		break;
	case BSSGP_PDUT_RESUME:
		/* MS wants to resume */
		rc = bssgp_rx_resume(msg, tp, bctx);
		break;
	case BSSGP_PDUT_FLUSH_LL_ACK:
		/* BSS informs us it has performed LL FLUSH */
		DEBUGP(DBSSGP, "BSSGP Rx BVCI=%u FLUSH LL ACK\n", bctx->bvci);
		/* FIXME: send NM_FLUSH_LL.res to NM */
		break;
	case BSSGP_PDUT_LLC_DISCARD:
		/* BSS informs that some LLC PDU's have been discarded */
		rc = bssgp_rx_llc_disc(msg, tp, bctx);
		break;
	case BSSGP_PDUT_BVC_BLOCK:
		/* BSS tells us that BVC shall be blocked */
		if (!TLVP_PRESENT(tp, BSSGP_IE_BVCI) ||
		    !TLVP_PRESENT(tp, BSSGP_IE_CAUSE)) {
			LOGP(DBSSGP, LOGL_ERROR, "BSSGP Rx BVC-BLOCK "
				"missing mandatory IE\n");
			goto err_mand_ie;
		}
		rc = bssgp_rx_bvc_block(msg, tp);
		break;
	case BSSGP_PDUT_BVC_UNBLOCK:
		/* BSS tells us that BVC shall be unblocked */
		if (!TLVP_PRESENT(tp, BSSGP_IE_BVCI)) {
			LOGP(DBSSGP, LOGL_ERROR, "BSSGP Rx BVC-UNBLOCK "
				"missing mandatory IE\n");
			goto err_mand_ie;
		}
		rc = bssgp_rx_bvc_unblock(msg, tp);
		break;
	case BSSGP_PDUT_BVC_RESET:
		/* BSS tells us that BVC init is required */
		if (!TLVP_PRESENT(tp, BSSGP_IE_BVCI) ||
		    !TLVP_PRESENT(tp, BSSGP_IE_CAUSE)) {
			LOGP(DBSSGP, LOGL_ERROR, "BSSGP Rx BVC-RESET "
				"missing mandatory IE\n");
			goto err_mand_ie;
		}
		rc = bssgp_rx_bvc_reset(msg, tp, ns_bvci);
		break;
	case BSSGP_PDUT_STATUS:
		/* Some exception has occurred */
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx BVC STATUS\n", bctx->bvci);
		/* FIXME: send NM_STATUS.ind to NM */
		break;
	/* those only exist in the SGSN -> BSS direction */
	case BSSGP_PDUT_PAGING_PS:
	case BSSGP_PDUT_PAGING_CS:
	case BSSGP_PDUT_SUSPEND_ACK:
	case BSSGP_PDUT_SUSPEND_NACK:
	case BSSGP_PDUT_RESUME_ACK:
	case BSSGP_PDUT_RESUME_NACK:
	case BSSGP_PDUT_FLUSH_LL:
	case BSSGP_PDUT_BVC_BLOCK_ACK:
	case BSSGP_PDUT_BVC_UNBLOCK_ACK:
	case BSSGP_PDUT_SGSN_INVOKE_TRACE:
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx PDU type 0x%02x only exists "
			"in DL\n", bctx->bvci, pdu_type);
		bssgp_tx_status(BSSGP_CAUSE_PROTO_ERR_UNSPEC, NULL, msg);
		rc = -EINVAL;
		break;
	default:
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx PDU type 0x%02x unknown\n",
			bctx->bvci, pdu_type);
		rc = bssgp_tx_status(BSSGP_CAUSE_PROTO_ERR_UNSPEC, NULL, msg);
		break;
	}

	return rc;
err_mand_ie:
	return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);
}

/* We expect msgb_bssgph() to point to the BSSGP header */
int bssgp_rcvmsg(struct msgb *msg)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	struct bssgp_ud_hdr *budh = (struct bssgp_ud_hdr *) msgb_bssgph(msg);
	struct tlv_parsed tp;
	struct bssgp_bvc_ctx *bctx;
	uint8_t pdu_type = bgph->pdu_type;
	uint16_t ns_bvci = msgb_bvci(msg);
	int data_len;
	int rc = 0;

	/* Identifiers from DOWN: NSEI, BVCI (both in msg->cb) */

	/* UNITDATA BSSGP headers have TLLI in front */
	if (pdu_type != BSSGP_PDUT_UL_UNITDATA &&
	    pdu_type != BSSGP_PDUT_DL_UNITDATA) {
		data_len = msgb_bssgp_len(msg) - sizeof(*bgph);
		rc = bssgp_tlv_parse(&tp, bgph->data, data_len);
	} else {
		data_len = msgb_bssgp_len(msg) - sizeof(*budh);
		rc = bssgp_tlv_parse(&tp, budh->data, data_len);
	}

	/* look-up or create the BTS context for this BVC */
	bctx = btsctx_by_bvci_nsei(ns_bvci, msgb_nsei(msg));
	/* Only a RESET PDU can create a new BVC context */
	if (!bctx && pdu_type != BSSGP_PDUT_BVC_RESET) {
		LOGP(DBSSGP, LOGL_NOTICE, "NSEI=%u/BVCI=%u Rejecting PDU "
			"type %u for unknown BVCI\n", msgb_nsei(msg), ns_bvci,
			pdu_type);
		return bssgp_tx_status(BSSGP_CAUSE_UNKNOWN_BVCI, NULL, msg);
	}

	if (bctx) {
		log_set_context(GPRS_CTX_BVC, bctx);
		rate_ctr_inc(&bctx->ctrg->ctr[BSSGP_CTR_PKTS_IN]);
		rate_ctr_add(&bctx->ctrg->ctr[BSSGP_CTR_BYTES_IN],
			     msgb_bssgp_len(msg));
	}

	if (ns_bvci == BVCI_SIGNALLING)
		rc = bssgp_rx_sign(msg, &tp, bctx);
	else if (ns_bvci == BVCI_PTM)
		rc = bssgp_tx_status(BSSGP_CAUSE_PDU_INCOMP_FEAT, NULL, msg);
	else
		rc = bssgp_rx_ptp(msg, &tp, bctx);

	return rc;
}

int bssgp_tx_dl_ud(struct msgb *msg, uint16_t pdu_lifetime,
		   struct bssgp_dl_ud_par *dup)
{
	struct bssgp_bvc_ctx *bctx;
	struct bssgp_ud_hdr *budh;
	uint8_t llc_pdu_tlv_hdr_len = 2;
	uint8_t *llc_pdu_tlv;
	uint16_t msg_len = msg->len;
	uint16_t bvci = msgb_bvci(msg);
	uint16_t nsei = msgb_nsei(msg);
	uint16_t _pdu_lifetime = htons(pdu_lifetime); /* centi-seconds */
	uint16_t drx_params;

	/* Identifiers from UP: TLLI, BVCI, NSEI (all in msgb->cb) */
	if (bvci <= BVCI_PTM ) {
		LOGP(DBSSGP, LOGL_ERROR, "Cannot send DL-UD to BVCI %u\n",
			bvci);
		return -EINVAL;
	}

	bctx = btsctx_by_bvci_nsei(bvci, nsei);
	if (!bctx) {
		LOGP(DBSSGP, LOGL_ERROR, "Cannot send DL-UD to unknown BVCI %u\n",
			bvci);
		return -ENODEV;
	}

	if (msg->len > TVLV_MAX_ONEBYTE)
		llc_pdu_tlv_hdr_len += 1;

	/* prepend the tag and length of the LLC-PDU TLV */
	llc_pdu_tlv = msgb_push(msg, llc_pdu_tlv_hdr_len);
	llc_pdu_tlv[0] = BSSGP_IE_LLC_PDU;
	if (llc_pdu_tlv_hdr_len > 2) {
		llc_pdu_tlv[1] = msg_len >> 8;
		llc_pdu_tlv[2] = msg_len & 0xff;
	} else {
		llc_pdu_tlv[1] = msg_len & 0x7f;
		llc_pdu_tlv[1] |= 0x80;
	}

	/* FIXME: optional elements: Alignment, UTRAN CCO, LSA, PFI */

	if (dup) {
		/* Old TLLI to help BSS map from old->new */
		if (dup->tlli) {
			uint32_t tlli = htonl(*dup->tlli);
			msgb_tvlv_push(msg, BSSGP_IE_TLLI, 4, (uint8_t *) &tlli);
		}

		/* IMSI */
		if (dup->imsi && strlen(dup->imsi)) {
			uint8_t mi[10];
			int imsi_len = gsm48_generate_mid_from_imsi(mi, dup->imsi);
			if (imsi_len > 2)
				msgb_tvlv_push(msg, BSSGP_IE_IMSI,
						imsi_len-2, mi+2);
		}

		/* DRX parameters */
		drx_params = htons(dup->drx_parms);
		msgb_tvlv_push(msg, BSSGP_IE_DRX_PARAMS, 2,
				(uint8_t *) &drx_params);

		/* FIXME: Priority */

		/* MS Radio Access Capability */
		if (dup->ms_ra_cap.len)
			msgb_tvlv_push(msg, BSSGP_IE_MS_RADIO_ACCESS_CAP,
					dup->ms_ra_cap.len, dup->ms_ra_cap.v);

	}

	/* prepend the pdu lifetime */
	msgb_tvlv_push(msg, BSSGP_IE_PDU_LIFETIME, 2, (uint8_t *)&_pdu_lifetime);

	/* prepend the QoS profile, TLLI and pdu type */
	budh = (struct bssgp_ud_hdr *) msgb_push(msg, sizeof(*budh));
	memcpy(budh->qos_profile, dup->qos_profile, sizeof(budh->qos_profile));
	budh->tlli = htonl(msgb_tlli(msg));
	budh->pdu_type = BSSGP_PDUT_DL_UNITDATA;

	rate_ctr_inc(&bctx->ctrg->ctr[BSSGP_CTR_PKTS_OUT]);
	rate_ctr_add(&bctx->ctrg->ctr[BSSGP_CTR_BYTES_OUT], msg->len);

	/* Identifiers down: BVCI, NSEI (in msgb->cb) */

	/* check if we have to go through per-ms flow control or can go
	 * directly to the per-BSS flow control */
	if (dup->fc)
		return bssgp_fc_in(dup->fc, msg, msg_len, bctx->fc);
	else
		return bssgp_fc_in(bctx->fc, msg, msg_len, NULL);
}

/* Send a single GMM-PAGING.req to a given NSEI/NS-BVCI */
int bssgp_tx_paging(uint16_t nsei, uint16_t ns_bvci,
		     struct bssgp_paging_info *pinfo)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint16_t drx_params = htons(pinfo->drx_params);
	uint8_t mi[10];
	int imsi_len = gsm48_generate_mid_from_imsi(mi, pinfo->imsi);
	uint8_t ra[6];

	if (imsi_len < 2)
		return -EINVAL;

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = ns_bvci;

	if (pinfo->mode == BSSGP_PAGING_PS)
		bgph->pdu_type = BSSGP_PDUT_PAGING_PS;
	else
		bgph->pdu_type = BSSGP_PDUT_PAGING_CS;
	/* IMSI */
	msgb_tvlv_put(msg, BSSGP_IE_IMSI, imsi_len-2, mi+2);
	/* DRX Parameters */
	msgb_tvlv_put(msg, BSSGP_IE_DRX_PARAMS, 2,
			(uint8_t *) &drx_params);
	/* Scope */
	switch (pinfo->scope) {
	case BSSGP_PAGING_BSS_AREA:
		{
			uint8_t null = 0;
			msgb_tvlv_put(msg, BSSGP_IE_BSS_AREA_ID, 1, &null);
		}
		break;
	case BSSGP_PAGING_LOCATION_AREA:
		gsm48_construct_ra(ra, &pinfo->raid);
		msgb_tvlv_put(msg, BSSGP_IE_LOCATION_AREA, 4, ra);
		break;
	case BSSGP_PAGING_ROUTEING_AREA:
		gsm48_construct_ra(ra, &pinfo->raid);
		msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, 6, ra);
		break;
	case BSSGP_PAGING_BVCI:
		{
			uint16_t bvci = htons(pinfo->bvci);
			msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *)&bvci);
		}
		break;
	}
	/* QoS profile mandatory for PS */
	if (pinfo->mode == BSSGP_PAGING_PS)
		msgb_tvlv_put(msg, BSSGP_IE_QOS_PROFILE, 3, pinfo->qos);

	/* Optional (P-)TMSI */
	if (pinfo->ptmsi) {
		uint32_t ptmsi = htonl(*pinfo->ptmsi);
		msgb_tvlv_put(msg, BSSGP_IE_TMSI, 4, (uint8_t *) &ptmsi);
	}

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

void bssgp_set_log_ss(int ss)
{
	DBSSGP = ss;
}
