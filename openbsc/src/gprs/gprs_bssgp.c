/* GPRS BSSGP protocol implementation as per 3GPP TS 08.18 */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * TODO:
 *  o  properly count incoming BVC-RESET packets in counter group
 *  o  set log context as early as possible for outgoing packets
 */

#include <errno.h>
#include <stdint.h>

#include <netinet/in.h>

#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/talloc.h>
#include <osmocore/rate_ctr.h>

#include <openbsc/debug.h>
#include <openbsc/gsm_data.h>
#include <openbsc/gsm_04_08_gprs.h>
#include <openbsc/gprs_bssgp.h>
#include <openbsc/gprs_llc.h>
#include <openbsc/gprs_ns.h>

void *bssgp_tall_ctx = NULL;

#define BVC_F_BLOCKED	0x0001

enum bssgp_ctr {
	BSSGP_CTR_PKTS_IN,
	BSSGP_CTR_PKTS_OUT,
	BSSGP_CTR_BYTES_IN,
	BSSGP_CTR_BYTES_OUT,
	BSSGP_CTR_BLOCKED,
	BSSGP_CTR_DISCARDED,
};

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
			  uint8_t *cause)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
		(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint32_t _tlli;

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = 0; /* Signalling */
	bgph->pdu_type = BSSGP_PDUT_SUSPEND_NACK;

	_tlli = htonl(tlli);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, 4, (uint8_t *) &_tlli);
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

/* Chapter 8.4 BVC-Reset Procedure */
static int bssgp_rx_bvc_reset(struct msgb *msg, struct tlv_parsed *tp,	
			      uint16_t ns_bvci)
{
	struct bssgp_bvc_ctx *bctx;
	uint16_t nsei = msgb_nsei(msg);
	uint16_t bvci;
	int rc;

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
			LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u RESET "
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

	/* Acknowledge the RESET to the BTS */
	rc = bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_RESET_ACK,
				  nsei, bvci, ns_bvci);
	return 0;
}

static int bssgp_rx_bvc_block(struct msgb *msg, struct tlv_parsed *tp)
{
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

	LOGP(DBSSGP, LOGL_INFO, "BSSGP BVCI=%u BVC-BLOCK\n", bvci);

	ptp_ctx = btsctx_by_bvci_nsei(bvci, msgb_nsei(msg));
	if (!ptp_ctx)
		return bssgp_tx_status(BSSGP_CAUSE_UNKNOWN_BVCI, &bvci, msg);

	ptp_ctx->state |= BVC_S_BLOCKED;
	rate_ctr_inc(&ptp_ctx->ctrg->ctr[BSSGP_CTR_BLOCKED]);

	/* FIXME: Send NM_BVC_BLOCK.ind to NM */

	/* We always acknowledge the BLOCKing */
	return bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_BLOCK_ACK, msgb_nsei(msg),
				    bvci, msgb_bvci(msg));
};

static int bssgp_rx_bvc_unblock(struct msgb *msg, struct tlv_parsed *tp)
{
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

	/* FIXME: Send NM_BVC_UNBLOCK.ind to NM */

	/* We always acknowledge the unBLOCKing */
	return bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_UNBLOCK_ACK, msgb_nsei(msg),
				    bvci, msgb_bvci(msg));
};

/* Uplink unit-data */
static int bssgp_rx_ul_ud(struct msgb *msg, struct tlv_parsed *tp,
			  struct bssgp_bvc_ctx *ctx)
{
	struct bssgp_ud_hdr *budh = (struct bssgp_ud_hdr *) msgb_bssgph(msg);

	/* extract TLLI and parse TLV IEs */
	msgb_tlli(msg) = ntohl(budh->tlli);

	DEBUGP(DBSSGP, "BSSGP TLLI=0x%08x UPLINK-UNITDATA\n", msgb_tlli(msg));

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

	return gprs_llc_rcvmsg(msg, tp);
}

static int bssgp_rx_suspend(struct msgb *msg, struct tlv_parsed *tp,
			    struct bssgp_bvc_ctx *ctx)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	struct gprs_ra_id raid;
	uint32_t tlli;

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

	/* FIXME: pass the SUSPEND request to GMM */
	/* SEND SUSPEND_ACK or SUSPEND_NACK */
	bssgp_tx_suspend_ack(msgb_nsei(msg), tlli, &raid, 0);

	return 0;
}

static int bssgp_rx_resume(struct msgb *msg, struct tlv_parsed *tp,
			   struct bssgp_bvc_ctx *ctx)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	struct gprs_ra_id raid;
	uint32_t tlli;

	if (!TLVP_PRESENT(tp, BSSGP_IE_TLLI) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_ROUTEING_AREA) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_SUSPEND_REF_NR)) {
		LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u Rx RESUME "
			"missing mandatory IE\n", ctx->bvci);
		return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);
	}

	tlli = ntohl(*(uint32_t *)TLVP_VAL(tp, BSSGP_IE_TLLI));

	DEBUGP(DBSSGP, "BSSGP BVCI=%u TLLI=0x%08x RESUME\n", ctx->bvci, tlli);

	gsm48_parse_ra(&raid, TLVP_VAL(tp, BSSGP_IE_ROUTEING_AREA));

	/* FIXME: pass the RESUME request to GMM */
	/* SEND RESUME_ACK or RESUME_NACK */
	bssgp_tx_resume_ack(msgb_nsei(msg), tlli, &raid);
	return 0;
}


static int bssgp_rx_llc_disc(struct msgb *msg, struct tlv_parsed *tp,
			     struct bssgp_bvc_ctx *ctx)
{
	uint32_t tlli;

	if (!TLVP_PRESENT(tp, BSSGP_IE_TLLI) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_LLC_FRAMES_DISCARDED) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BVCI) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_NUM_OCT_AFF)) {
		LOGP(DBSSGP, LOGL_ERROR, "BSSGP BVCI=%u Rx LLC DISCARDED "
			"missing mandatory IE\n", ctx->bvci);
	}

	tlli = ntohl(*(uint32_t *)TLVP_VAL(tp, BSSGP_IE_TLLI));

	DEBUGP(DBSSGP, "BSSGP BVCI=%u TLLI=%u LLC DISCARDED\n",
		ctx->bvci, tlli);

	rate_ctr_inc(&ctx->ctrg->ctr[BSSGP_CTR_DISCARDED]);

	/* FIXME: send NM_LLC_DISCARDED to NM */
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

	/* FIXME: actually implement flow control */

	/* Send FLOW_CONTROL_BVC_ACK */
	return bssgp_tx_fc_bvc_ack(msgb_nsei(msg), *TLVP_VAL(tp, BSSGP_IE_TAG),
				   msgb_bvci(msg));
}

/* Receive a BSSGP PDU from a BSS on a PTP BVCI */
static int gprs_bssgp_rx_ptp(struct msgb *msg, struct tlv_parsed *tp,
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
static int gprs_bssgp_rx_sign(struct msgb *msg, struct tlv_parsed *tp,
				struct bssgp_bvc_ctx *bctx)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	uint8_t pdu_type = bgph->pdu_type;
	int rc = 0;
	uint16_t ns_bvci = msgb_bvci(msg);
	uint16_t bvci;

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
		DEBUGP(DBSSGP, "BSSGP BVCI=%u Rx FLUSH LL ACK\n", bctx->bvci);
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
int gprs_bssgp_rcvmsg(struct msgb *msg)
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
		log_set_context(BSC_CTX_BVC, bctx);
		rate_ctr_inc(&bctx->ctrg->ctr[BSSGP_CTR_PKTS_IN]);
		rate_ctr_add(&bctx->ctrg->ctr[BSSGP_CTR_BYTES_IN],
			     msgb_bssgp_len(msg));
	}

	if (ns_bvci == BVCI_SIGNALLING)
		rc = gprs_bssgp_rx_sign(msg, &tp, bctx);
	else if (ns_bvci == BVCI_PTM)
		rc = bssgp_tx_status(BSSGP_CAUSE_PDU_INCOMP_FEAT, NULL, msg);
	else
		rc = gprs_bssgp_rx_ptp(msg, &tp, bctx);

	return rc;
}

/* Entry function from upper level (LLC), asking us to transmit a BSSGP PDU
 * to a remote MS (identified by TLLI) at a BTS identified by its BVCI and NSEI */
int gprs_bssgp_tx_dl_ud(struct msgb *msg)
{
	struct bssgp_bvc_ctx *bctx;
	struct bssgp_ud_hdr *budh;
	uint8_t llc_pdu_tlv_hdr_len = 2;
	uint8_t *llc_pdu_tlv, *qos_profile;
	uint16_t pdu_lifetime = 1000; /* centi-seconds */
	uint8_t qos_profile_default[3] = { 0x00, 0x00, 0x21 };
	uint16_t msg_len = msg->len;
	uint16_t bvci = msgb_bvci(msg);
	uint16_t nsei = msgb_nsei(msg);

	/* Identifiers from UP: TLLI, BVCI, NSEI (all in msgb->cb) */
	if (bvci <= BVCI_PTM ) {
		LOGP(DBSSGP, LOGL_ERROR, "Cannot send DL-UD to BVCI %u\n",
			bvci);
		return -EINVAL;
	}

	bctx = btsctx_by_bvci_nsei(bvci, nsei);
	if (!bctx) {
		/* FIXME: don't simply create missing context, but reject message */
		bctx = btsctx_alloc(bvci, nsei);
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
		llc_pdu_tlv[1] = msg_len & 0x3f;
		llc_pdu_tlv[1] |= 0x80;
	}

	/* FIXME: optional elements */

	/* prepend the pdu lifetime */
	pdu_lifetime = htons(pdu_lifetime);
	msgb_tvlv_push(msg, BSSGP_IE_PDU_LIFETIME, 2, (uint8_t *)&pdu_lifetime);

	/* prepend the QoS profile, TLLI and pdu type */
	budh = (struct bssgp_ud_hdr *) msgb_push(msg, sizeof(*budh));
	memcpy(budh->qos_profile, qos_profile_default, sizeof(qos_profile_default));
	budh->tlli = htonl(msgb_tlli(msg));
	budh->pdu_type = BSSGP_PDUT_DL_UNITDATA;

	rate_ctr_inc(&bctx->ctrg->ctr[BSSGP_CTR_PKTS_OUT]);
	rate_ctr_add(&bctx->ctrg->ctr[BSSGP_CTR_BYTES_OUT], msg->len);

	/* Identifiers down: BVCI, NSEI (in msgb->cb) */

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}
