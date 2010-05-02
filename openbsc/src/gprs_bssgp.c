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
 */

#include <errno.h>
#include <sys/types.h>

#include <netinet/in.h>

#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/talloc.h>

#include <openbsc/debug.h>
#include <openbsc/gsm_data.h>
#include <openbsc/gsm_04_08_gprs.h>
#include <openbsc/gprs_bssgp.h>
#include <openbsc/gprs_llc.h>
#include <openbsc/gprs_ns.h>

/* global pointer to the gsm network data structure */
/* FIXME: this must go! */
extern struct gsm_network *bsc_gsmnet;
struct gprs_ns_inst *bssgp_nsi;

void *bssgp_tall_ctx = NULL;

/* BSSGP Protocol specific, not implementation specific */
/* FIXME: This needs to go into libosmocore after finished */

/* Chapter 11.3.9 / Table 11.10: Cause coding */
static const struct value_string bssgp_cause_strings[] = {
	{ BSSGP_CAUSE_PROC_OVERLOAD,	"Processor overload" },
	{ BSSGP_CAUSE_EQUIP_FAIL,	"Equipment Failure" },
	{ BSSGP_CAUSE_TRASIT_NET_FAIL,	"Transit netowkr service failure" },
	{ BSSGP_CAUSE_CAPA_GREATER_0KPBS,"Transmission capacity modified" },
	{ BSSGP_CAUSE_UNKNOWN_MS,	"Unknown MS" },
	{ BSSGP_CAUSE_UNKNOWN_BVCI,	"Unknown BVCI" },
	{ BSSGP_CAUSE_CELL_TRAF_CONG,	"Cell traffic congestion" },
	{ BSSGP_CAUSE_SGSN_CONG,	"SGSN congestion" },
	{ BSSGP_CAUSE_OML_INTERV,	"O&M intervention" },
	{ BSSGP_CAUSE_BVCI_BLOCKED,	"BVCI blocked" },
	{ BSSGP_CAUSE_PFC_CREATE_FAIL,	"PFC create failure" },
	{ BSSGP_CAUSE_SEM_INCORR_PDU,	"Semantically incorrect PDU" },
	{ BSSGP_CAUSE_INV_MAND_INF,	"Invalid mandatory information" },
	{ BSSGP_CAUSE_MISSING_MAND_IE,	"Missing mandatory IE" },
	{ BSSGP_CAUSE_MISSING_COND_IE,	"Missing conditional IE" },
	{ BSSGP_CAUSE_UNEXP_COND_IE,	"Unexpected conditional IE" },
	{ BSSGP_CAUSE_COND_IE_ERR,	"Conditional IE error" },
	{ BSSGP_CAUSE_PDU_INCOMP_STATE,	"PDU incompatible with protocol state" },
	{ BSSGP_CAUSE_PROTO_ERR_UNSPEC,	"Protocol error - unspecified" },
	{ BSSGP_CAUSE_PDU_INCOMP_FEAT, 	"PDU not compatible with feature set" },
	{ 0, NULL },
};

const char *bssgp_cause_str(enum gprs_bssgp_cause cause)
{
	return get_value_string(bssgp_cause_strings, cause);
}


/* Our actual implementation */

#define BVC_F_BLOCKED	0x0001

/* The per-BTS context that we keep on the SGSN side of the BSSGP link */
struct bssgp_bts_ctx {
	struct llist_head list;

	/* parsed RA ID and Cell ID of the remote BTS */
	struct gprs_ra_id ra_id;
	uint16_t cell_id;

	/* NSEI and BVCI of underlying Gb link.  Together they
	 * uniquely identify a link to a BTS (5.4.4) */
	uint16_t bvci;
	uint16_t nsei;

	uint32_t bvc_state;

	/* we might want to add this as a shortcut later, avoiding the NSVC
	 * lookup for every packet, similar to a routing cache */
	//struct gprs_nsvc *nsvc;
};
LLIST_HEAD(bts_ctxts);

/* Find a BTS Context based on parsed RA ID and Cell ID */
struct bssgp_bts_ctx *btsctx_by_raid_cid(const struct gprs_ra_id *raid, uint16_t cid)
{
	struct bssgp_bts_ctx *bctx;

	llist_for_each_entry(bctx, &bts_ctxts, list) {
		if (!memcmp(&bctx->ra_id, raid, sizeof(bctx->ra_id)) &&
		    bctx->cell_id == cid)
			return bctx;
	}
	return NULL;
}

/* Find a BTS context based on BVCI+NSEI tuple */
struct bssgp_bts_ctx *btsctx_by_bvci_nsei(uint16_t bvci, uint16_t nsei)
{
	struct bssgp_bts_ctx *bctx;

	llist_for_each_entry(bctx, &bts_ctxts, list) {
		if (bctx->nsei == nsei && bctx->bvci == bvci)
			return bctx;
	}
	return NULL;
}

struct bssgp_btx_ctx *btsctx_alloc(uint16_t bvci, uint16_t nsei)
{
	struct bssgp_bts_ctx *ctx;

	ctx = talloc_zero(bssgp_tall_ctx, struct bssgp_bts_ctx);
	if (!ctx)
		return NULL;
	ctx->bvci = bvci;
	ctx->nsei = nsei;
	llist_add(&ctx->list, &bts_ctxts);

	return ctx;
}

static inline struct msgb *bssgp_msgb_alloc(void)
{
	return msgb_alloc_headroom(4096, 128, "BSSGP");
}

/* Transmit a simple response such as BLOCK/UNBLOCK/RESET ACK/NACK */
static int bssgp_tx_simple_bvci(u_int8_t pdu_type, u_int16_t nsei,
			        u_int16_t bvci, u_int16_t ns_bvci)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	u_int16_t _bvci;

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = ns_bvci;

	bgph->pdu_type = pdu_type;
	_bvci = htons(bvci);
	msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (u_int8_t *) &_bvci);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* Chapter 10.4.5: Flow Control BVC ACK */
static int bssgp_tx_fc_bvc_ack(u_int16_t nsei, u_int8_t tag, u_int16_t ns_bvci)
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

/* Chapter 10.4.14: Status */
int bssgp_tx_status(u_int8_t cause, u_int16_t *bvci, struct msgb *orig_msg)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));

	DEBUGPC(DGPRS, "BSSGP: TX STATUS, cause=%s\n", bssgp_cause_str(cause));
	msgb_nsei(msg) = msgb_nsei(orig_msg);
	msgb_bvci(msg) = 0;

	bgph->pdu_type = BSSGP_PDUT_STATUS;
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, &cause);
	if (bvci) {
		u_int16_t _bvci = htons(*bvci);
		msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (u_int8_t *) &_bvci);
	}
	if (orig_msg)
		msgb_tvlv_put(msg, BSSGP_IE_PDU_IN_ERROR,
			      msgb_l3len(orig_msg), msgb_bssgph(orig_msg));

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

static void bssgp_parse_cell_id(struct gprs_ra_id *raid, uint16_t *cid,
				const uint8_t *buf)
{
	/* 6 octets RAC */
	gsm48_parse_ra(raid, buf);
	/* 2 octets CID */
	*cid = ntohs(*(uint16_t *) (buf+6));
}

/* Chapter 8.4 BVC-Reset Procedure */
static int bssgp_rx_bvc_reset(struct msgb *msg, struct tlv_parsed *tp,	
			      uint16_t ns_bvci)
{
	struct bssgp_bts_ctx *bctx;
	uint16_t nsei = msgb_nsei(msg);
	uint16_t bvci;
	int rc;

	bvci = ntohs(*(u_int16_t *)TLVP_VAL(tp, BSSGP_IE_BVCI));
	DEBUGPC(DGPRS, "BVCI=%u, cause=%s\n", bvci,
		bssgp_cause_str(*TLVP_VAL(tp, BSSGP_IE_CAUSE)));

	/* look-up or create the BTS context for this BVC */
	bctx = btsctx_by_bvci_nsei(bvci, nsei);
	if (!bctx)
		bctx = btsctx_alloc(bvci, nsei);

	/* When we receive a BVC-RESET PDU (at least of a PTP BVCI), the BSS
	 * informs us about its RAC + Cell ID, so we can create a mapping */
	if (bvci != 0 && bvci != 1) {
		if (!TLVP_PRESENT(tp, BSSGP_IE_CELL_ID)) {
			LOGP(DGPRS, LOGL_ERROR, "BSSGP RESET BVCI=%u "
				"missing mandatory IE\n", bvci);
			return -EINVAL;
		}
		/* actually extract RAC / CID */
		bssgp_parse_cell_id(&bctx->ra_id, &bctx->cell_id,
				    TLVP_VAL(tp, BSSGP_IE_CELL_ID));
		LOGP(DGPRS, LOGL_NOTICE, "Cell %u-%u-%u-%u CI %u on BVCI %u\n",
			bctx->ra_id.mcc, bctx->ra_id.mnc, bctx->ra_id.lac,
			bctx->ra_id.rac, bctx->cell_id, bvci);
	}

	/* Acknowledge the RESET to the BTS */
	rc = bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_RESET_ACK,
				  nsei, bvci, ns_bvci);
	return 0;
}

/* Uplink unit-data */
static int bssgp_rx_ul_ud(struct msgb *msg)
{
	struct bssgp_ud_hdr *budh = (struct bssgp_ud_hdr *) msgb_bssgph(msg);
	int data_len = msgb_l3len(msg) - sizeof(*budh);
	struct tlv_parsed tp;
	int rc;

	DEBUGP(DGPRS, "BSSGP UL-UD\n");

	/* extract TLLI and parse TLV IEs */
	msgb_tlli(msg) = ntohl(budh->tlli);
	rc = bssgp_tlv_parse(&tp, budh->data, data_len);

	/* Cell ID and LLC_PDU are the only mandatory IE */
	if (!TLVP_PRESENT(&tp, BSSGP_IE_CELL_ID) ||
	    !TLVP_PRESENT(&tp, BSSGP_IE_LLC_PDU))
		return -EIO;

	/* FIXME: lookup bssgp_bts_ctx based on BVCI + NSEI */

	msgb_llch(msg) = TLVP_VAL(&tp, BSSGP_IE_LLC_PDU);

	return gprs_llc_rcvmsg(msg, &tp);
}

static int bssgp_rx_suspend(struct msgb *msg)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	int data_len = msgb_l3len(msg) - sizeof(*bgph);
	struct tlv_parsed tp;
	int rc;

	DEBUGP(DGPRS, "BSSGP SUSPEND\n");

	rc = bssgp_tlv_parse(&tp, bgph->data, data_len);
	if (rc < 0)
		return rc;

	if (!TLVP_PRESENT(&tp, BSSGP_IE_TLLI) ||
	    !TLVP_PRESENT(&tp, BSSGP_IE_ROUTEING_AREA))
		return -EIO;

	/* FIXME: pass the SUSPEND request to GMM */
	/* SEND SUSPEND_ACK or SUSPEND_NACK */
}

static int bssgp_rx_resume(struct msgb *msg)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	int data_len = msgb_l3len(msg) - sizeof(*bgph);
	struct tlv_parsed tp;
	int rc;

	DEBUGP(DGPRS, "BSSGP RESUME\n");

	rc = bssgp_tlv_parse(&tp, bgph->data, data_len);
	if (rc < 0)
		return rc;

	if (!TLVP_PRESENT(&tp, BSSGP_IE_TLLI) ||
	    !TLVP_PRESENT(&tp, BSSGP_IE_ROUTEING_AREA) ||
	    !TLVP_PRESENT(&tp, BSSGP_IE_SUSPEND_REF_NR))
		return -EIO;

	/* FIXME: pass the RESUME request to GMM */
	/* SEND RESUME_ACK or RESUME_NACK */
}

static int bssgp_rx_fc_bvc(struct msgb *msg, struct tlv_parsed *tp)
{

	DEBUGP(DGPRS, "BSSGP FC BVC\n");

	if (!TLVP_PRESENT(tp, BSSGP_IE_TAG) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BVC_BUCKET_SIZE) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BUCKET_LEAK_RATE) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BMAX_DEFAULT_MS) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_R_DEFAULT_MS))
		return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);

	/* FIXME: actually implement flow control */

	/* Send FLOW_CONTROL_BVC_ACK */
	return bssgp_tx_fc_bvc_ack(msgb_nsei(msg), *TLVP_VAL(tp, BSSGP_IE_TAG),
				   msgb_bvci(msg));
}

/* We expect msgb_bssgph() to point to the BSSGP header */
int gprs_bssgp_rcvmsg(struct msgb *msg)
{
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_bssgph(msg);
	struct tlv_parsed tp;
	uint8_t pdu_type = bgph->pdu_type;
	int data_len = msgb_l3len(msg) - sizeof(*bgph);
	uint16_t bvci;	/* PTP BVCI */
	uint16_t ns_bvci = msgb_bvci(msg);
	int rc = 0;

	/* Identifiers from DOWN: NSEI, BVCI (both in msg->cb) */

	/* UNITDATA BSSGP headers have TLLI in front */
	if (pdu_type != BSSGP_PDUT_UL_UNITDATA &&
	    pdu_type != BSSGP_PDUT_DL_UNITDATA)
		rc = bssgp_tlv_parse(&tp, bgph->data, data_len);

	switch (pdu_type) {
	case BSSGP_PDUT_UL_UNITDATA:
		/* some LLC data from the MS */
		rc = bssgp_rx_ul_ud(msg);
		break;
	case BSSGP_PDUT_RA_CAPABILITY:
		/* BSS requests RA capability or IMSI */
		DEBUGP(DGPRS, "BSSGP RA CAPABILITY UPDATE\n");
		/* FIXME: send RA_CAPA_UPDATE_ACK */
		break;
	case BSSGP_PDUT_RADIO_STATUS:
		DEBUGP(DGPRS, "BSSGP RADIO STATUS\n");
		/* BSS informs us of some exception */
		/* FIXME: notify GMM */
		break;
	case BSSGP_PDUT_SUSPEND:
		/* MS wants to suspend */
		rc = bssgp_rx_suspend(msg);
		break;
	case BSSGP_PDUT_RESUME:
		/* MS wants to resume */
		rc = bssgp_rx_resume(msg);
		break;
	case BSSGP_PDUT_FLUSH_LL:
		/* BSS informs MS has moved to one cell to other cell */
		DEBUGP(DGPRS, "BSSGP FLUSH LL\n");
		/* FIXME: notify GMM */
		/* Send FLUSH_LL_ACK */
		break;
	case BSSGP_PDUT_LLC_DISCARD:
		/* BSS informs that some LLC PDU's have been discarded */
		DEBUGP(DGPRS, "BSSGP LLC DISCARDED\n");
		/* FIXME: notify GMM */
		break;
	case BSSGP_PDUT_FLOW_CONTROL_BVC:
		/* BSS informs us of available bandwidth in Gb interface */
		rc = bssgp_rx_fc_bvc(msg, &tp);
		break;
	case BSSGP_PDUT_FLOW_CONTROL_MS:
		/* BSS informs us of available bandwidth to one MS */
		DEBUGP(DGPRS, "BSSGP FC MS\n");
		/* FIXME: actually implement flow control */
		/* FIXME: Send FLOW_CONTROL_MS_ACK */
		break;
	case BSSGP_PDUT_BVC_BLOCK:
		/* BSS tells us that BVC shall be blocked */
		DEBUGP(DGPRS, "BSSGP BVC BLOCK ");
		if (!TLVP_PRESENT(&tp, BSSGP_IE_BVCI) ||
		    !TLVP_PRESENT(&tp, BSSGP_IE_CAUSE))
			goto err_mand_ie;
		bvci = ntohs(*(u_int16_t *)TLVP_VAL(&tp, BSSGP_IE_BVCI));
		DEBUGPC(DGPRS, "BVCI=%u, cause=%s\n", bvci,
			bssgp_cause_str(*TLVP_VAL(&tp, BSSGP_IE_CAUSE)));
		/* We always acknowledge the BLOCKing */
		rc = bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_BLOCK_ACK,
					  msgb_nsei(msg), bvci, ns_bvci);
		break;
	case BSSGP_PDUT_BVC_UNBLOCK:
		/* BSS tells us that BVC shall be unblocked */
		DEBUGP(DGPRS, "BSSGP BVC UNBLOCK ");
		if (!TLVP_PRESENT(&tp, BSSGP_IE_BVCI))
			goto err_mand_ie;
		bvci = ntohs(*(u_int16_t *)TLVP_VAL(&tp, BSSGP_IE_BVCI));
		DEBUGPC(DGPRS, "BVCI=%u\n", bvci);
		/* We always acknowledge the unBLOCKing */
		rc = bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_UNBLOCK_ACK,
					  msgb_nsei(msg), bvci, ns_bvci);
		break;
	case BSSGP_PDUT_BVC_RESET:
		/* BSS tells us that BVC init is required */
		DEBUGP(DGPRS, "BSSGP BVC RESET ");
		if (!TLVP_PRESENT(&tp, BSSGP_IE_BVCI) ||
		    !TLVP_PRESENT(&tp, BSSGP_IE_CAUSE))
			goto err_mand_ie;
		rc = bssgp_rx_bvc_reset(msg, &tp, ns_bvci);
		break;
	case BSSGP_PDUT_STATUS:
		/* Some exception has occurred */
		/* FIXME: notify GMM */
	case BSSGP_PDUT_DOWNLOAD_BSS_PFC:
	case BSSGP_PDUT_CREATE_BSS_PFC_ACK:
	case BSSGP_PDUT_CREATE_BSS_PFC_NACK:
	case BSSGP_PDUT_MODIFY_BSS_PFC:
	case BSSGP_PDUT_DELETE_BSS_PFC_ACK:
		DEBUGP(DGPRS, "BSSGP PDU type 0x%02x not [yet] implemented\n",
			pdu_type);
		break;
	/* those only exist in the SGSN -> BSS direction */
	case BSSGP_PDUT_DL_UNITDATA:
	case BSSGP_PDUT_PAGING_PS:
	case BSSGP_PDUT_PAGING_CS:
	case BSSGP_PDUT_RA_CAPA_UPDATE_ACK:
	case BSSGP_PDUT_SUSPEND_ACK:
	case BSSGP_PDUT_SUSPEND_NACK:
	case BSSGP_PDUT_RESUME_ACK:
	case BSSGP_PDUT_RESUME_NACK:
	case BSSGP_PDUT_FLUSH_LL_ACK:
	case BSSGP_PDUT_FLOW_CONTROL_BVC_ACK:
	case BSSGP_PDUT_FLOW_CONTROL_MS_ACK:
	case BSSGP_PDUT_BVC_BLOCK_ACK:
	case BSSGP_PDUT_BVC_UNBLOCK_ACK:
	case BSSGP_PDUT_SGSN_INVOKE_TRACE:
		DEBUGP(DGPRS, "BSSGP PDU type 0x%02x only exists in DL\n",
			pdu_type);
		rc = -EINVAL;
		break;
	default:
		DEBUGP(DGPRS, "BSSGP PDU type 0x%02x unknown\n", pdu_type);
		break;
	}

	return rc;
err_mand_ie:
	return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);
}

/* Entry function from upper level (LLC), asking us to transmit a BSSGP PDU
 * to a remote MS (identified by TLLI) at a BTS identified by its BVCI and NSEI */
int gprs_bssgp_tx_dl_ud(struct msgb *msg)
{
	struct bssgp_bts_ctx *bctx;
	struct bssgp_ud_hdr *budh;
	u_int8_t llc_pdu_tlv_hdr_len = 2;
	u_int8_t *llc_pdu_tlv, *qos_profile;
	u_int16_t pdu_lifetime = 1000; /* centi-seconds */
	u_int8_t qos_profile_default[3] = { 0x00, 0x00, 0x21 };
	u_int16_t msg_len = msg->len;
	uint16_t bvci = msgb_bvci(msg);
	uint16_t nsei = msgb_nsei(msg);

	/* Identifiers from UP: TLLI, BVCI, NSEI (all in msgb->cb) */
	if (bvci < 2) {
		LOGP(DGPRS, LOGL_ERROR, "Cannot send DL-UD to BVCI %u\n",
			bvci);
		return -EINVAL;
	}

	bctx = btsctx_by_bvci_nsei(bvci, nsei);
	if (!bctx)
		bctx = btsctx_alloc(bvci, nsei);

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
	msgb_tvlv_push(msg, BSSGP_IE_PDU_LIFETIME, 2, (u_int8_t *)&pdu_lifetime);

	/* prepend the QoS profile, TLLI and pdu type */
	budh = (struct bssgp_ud_hdr *) msgb_push(msg, sizeof(*budh));
	memcpy(budh->qos_profile, qos_profile_default, sizeof(qos_profile_default));
	budh->tlli = htonl(msgb_tlli(msg));
	budh->pdu_type = BSSGP_PDUT_DL_UNITDATA;

	/* Identifiers down: BVCI, NSEI (in msgb->cb) */

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}
