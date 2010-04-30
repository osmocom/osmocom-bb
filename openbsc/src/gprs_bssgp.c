/* GPRS BSSGP protocol implementation as per 3GPP TS 08.18 */

/* (C) 2009 by Harald Welte <laforge@gnumonks.org>
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

/* Chapter 11.3.9 / Table 11.10: Cause coding */
static const char *bssgp_cause_strings[] = {
	[BSSGP_CAUSE_PROC_OVERLOAD]	= "Processor overload",
	[BSSGP_CAUSE_EQUIP_FAIL]	= "Equipment Failure",
	[BSSGP_CAUSE_TRASIT_NET_FAIL]	= "Transit netowkr service failure",
	[BSSGP_CAUSE_CAPA_GREATER_0KPBS]= "Transmission capacity modified",
	[BSSGP_CAUSE_UNKNOWN_MS]	= "Unknown MS",
	[BSSGP_CAUSE_UNKNOWN_BVCI]	= "Unknown BVCI",
	[BSSGP_CAUSE_CELL_TRAF_CONG]	= "Cell traffic congestion",
	[BSSGP_CAUSE_SGSN_CONG]		= "SGSN congestion",
	[BSSGP_CAUSE_OML_INTERV]	= "O&M intervention",
	[BSSGP_CAUSE_BVCI_BLOCKED]	= "BVCI blocked",
	[BSSGP_CAUSE_PFC_CREATE_FAIL]	= "PFC create failure",
	[BSSGP_CAUSE_SEM_INCORR_PDU]	= "Semantically incorrect PDU",
	[BSSGP_CAUSE_INV_MAND_INF]	= "Invalid mandatory information",
	[BSSGP_CAUSE_MISSING_MAND_IE]	= "Missing mandatory IE",
	[BSSGP_CAUSE_MISSING_COND_IE]	= "Missing conditional IE",
	[BSSGP_CAUSE_UNEXP_COND_IE]	= "Unexpected conditional IE",
	[BSSGP_CAUSE_COND_IE_ERR]	= "Conditional IE error",
	[BSSGP_CAUSE_PDU_INCOMP_STATE]	= "PDU incompatible with protocol state",
	[BSSGP_CAUSE_PROTO_ERR_UNSPEC]	= "Protocol error - unspecified",
	[BSSGP_CAUSE_PDU_INCOMP_FEAT]	= "PDU not compatible with feature set",
};

static const char *bssgp_cause_str(enum gprs_bssgp_cause cause)
{
	if (cause >= ARRAY_SIZE(bssgp_cause_strings))
		return "undefined";

	if (bssgp_cause_strings[cause])
		return bssgp_cause_strings[cause];

	return "undefined";
}

static inline int bssgp_tlv_parse(struct tlv_parsed *tp, u_int8_t *buf, int len)
{
	return tlv_parse(tp, &tvlv_att_def, buf, len, 0, 0);
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
static int bssgp_tx_status(u_int8_t cause, u_int16_t *bvci, struct msgb *orig_msg)
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
			      msgb_l3len(orig_msg), orig_msg->l3h);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* Uplink unit-data */
static int bssgp_rx_ul_ud(struct msgb *msg, u_int16_t bvci)
{
	struct bssgp_ud_hdr *budh = (struct bssgp_ud_hdr *) msg->l3h;
	struct gsm_bts *bts;
	int data_len = msgb_l3len(msg) - sizeof(*budh);
	struct tlv_parsed tp;
	int rc;

	DEBUGP(DGPRS, "BSSGP UL-UD\n");

	msgb_tlli(msg) = ntohl(budh->tlli);
	rc = bssgp_tlv_parse(&tp, budh->data, data_len);

	/* Cell ID and LLC_PDU are the only mandatory IE */
	if (!TLVP_PRESENT(&tp, BSSGP_IE_CELL_ID) ||
	    !TLVP_PRESENT(&tp, BSSGP_IE_LLC_PDU))
		return -EIO;

	/* Determine the BTS based on the Cell ID */
	bts = gsm48_bts_by_ra_id(bsc_gsmnet,
				 TLVP_VAL(&tp, BSSGP_IE_CELL_ID),
				 TLVP_LEN(&tp, BSSGP_IE_CELL_ID));
	if (bts)
		msg->trx = bts->c0;

	msgb_llch(msg) = TLVP_VAL(&tp, BSSGP_IE_LLC_PDU);

	return gprs_llc_rcvmsg(msg, &tp);
}

static int bssgp_rx_suspend(struct msgb *msg, u_int16_t bvci)
{
	struct bssgp_normal_hdr *bgph = (struct bssgp_normal_hdr *) msg->l3h;
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

	/* SEND SUSPEND_ACK or SUSPEND_NACK */
	/* FIXME */
}

static int bssgp_rx_resume(struct msgb *msg, u_int16_t bvci)
{
	struct bssgp_normal_hdr *bgph = (struct bssgp_normal_hdr *) msg->l3h;
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

	/* SEND RESUME_ACK or RESUME_NACK */
	/* FIXME */
}

static int bssgp_rx_fc_bvc(struct msgb *msg, struct tlv_parsed *tp,
			   u_int16_t ns_bvci)
{

	DEBUGP(DGPRS, "BSSGP FC BVC\n");

	if (!TLVP_PRESENT(tp, BSSGP_IE_TAG) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BVC_BUCKET_SIZE) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BUCKET_LEAK_RATE) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_BMAX_DEFAULT_MS) ||
	    !TLVP_PRESENT(tp, BSSGP_IE_R_DEFAULT_MS))
		return bssgp_tx_status(BSSGP_CAUSE_MISSING_MAND_IE, NULL, msg);

	/* Send FLOW_CONTROL_BVC_ACK */
	return bssgp_tx_fc_bvc_ack(msgb_nsei(msg), *TLVP_VAL(tp, BSSGP_IE_TAG),
				   ns_bvci);
}
/* We expect msg->l3h to point to the BSSGP header */
int gprs_bssgp_rcvmsg(struct msgb *msg, u_int16_t ns_bvci)
{
	struct bssgp_normal_hdr *bgph = (struct bssgp_normal_hdr *) msg->l3h;
	struct tlv_parsed tp;
	u_int8_t pdu_type = bgph->pdu_type;
	int data_len = msgb_l3len(msg) - sizeof(*bgph);
	u_int16_t bvci;
	int rc = 0;

	if (pdu_type != BSSGP_PDUT_UL_UNITDATA &&
	    pdu_type != BSSGP_PDUT_DL_UNITDATA)
		rc = bssgp_tlv_parse(&tp, bgph->data, data_len);

	switch (pdu_type) {
	case BSSGP_PDUT_UL_UNITDATA:
		/* some LLC data from the MS */
		rc = bssgp_rx_ul_ud(msg, ns_bvci);
		break;
	case BSSGP_PDUT_RA_CAPABILITY:
		/* BSS requests RA capability or IMSI */
		DEBUGP(DGPRS, "BSSGP RA CAPABILITY UPDATE\n");
		/* FIXME: send RA_CAPA_UPDATE_ACK */
		break;
	case BSSGP_PDUT_RADIO_STATUS:
		DEBUGP(DGPRS, "BSSGP RADIO STATUS\n");
		/* BSS informs us of some exception */
		break;
	case BSSGP_PDUT_SUSPEND:
		/* MS wants to suspend */
		rc = bssgp_rx_suspend(msg, ns_bvci);
		break;
	case BSSGP_PDUT_RESUME:
		/* MS wants to resume */
		rc = bssgp_rx_resume(msg, ns_bvci);
		break;
	case BSSGP_PDUT_FLUSH_LL:
		/* BSS informs MS has moved to one cell to other cell */
		DEBUGP(DGPRS, "BSSGP FLUSH LL\n");
		/* Send FLUSH_LL_ACK */
		break;
	case BSSGP_PDUT_LLC_DISCARD:
		/* BSS informs that some LLC PDU's have been discarded */
		DEBUGP(DGPRS, "BSSGP LLC DISCARDED\n");
		break;
	case BSSGP_PDUT_FLOW_CONTROL_BVC:
		/* BSS informs us of available bandwidth in Gb interface */
		rc = bssgp_rx_fc_bvc(msg, &tp, ns_bvci);
		break;
	case BSSGP_PDUT_FLOW_CONTROL_MS:
		/* BSS informs us of available bandwidth to one MS */
		DEBUGP(DGPRS, "BSSGP FC MS\n");
		/* Send FLOW_CONTROL_MS_ACK */
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
		rc = bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_UNBLOCK_ACK,
					  msgb_nsei(msg), bvci, ns_bvci);
		break;
	case BSSGP_PDUT_BVC_RESET:
		/* BSS tells us that BVC init is required */
		DEBUGP(DGPRS, "BSSGP BVC RESET ");
		if (!TLVP_PRESENT(&tp, BSSGP_IE_BVCI) ||
		    !TLVP_PRESENT(&tp, BSSGP_IE_CAUSE))
			goto err_mand_ie;
		bvci = ntohs(*(u_int16_t *)TLVP_VAL(&tp, BSSGP_IE_BVCI));
		DEBUGPC(DGPRS, "BVCI=%u, cause=%s\n", bvci,
			bssgp_cause_str(*TLVP_VAL(&tp, BSSGP_IE_CAUSE)));
		rc = bssgp_tx_simple_bvci(BSSGP_PDUT_BVC_RESET_ACK,
					  msgb_nsei(msg), bvci, ns_bvci);
		break;
	case BSSGP_PDUT_STATUS:
		/* Some exception has occurred */
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

int gprs_bssgp_tx_dl_ud(struct msgb *msg)
{
	struct gsm_bts *bts;
	struct bssgp_ud_hdr *budh;
	u_int8_t llc_pdu_tlv_hdr_len = 2;
	u_int8_t *llc_pdu_tlv, *qos_profile;
	u_int16_t pdu_lifetime = 1000; /* centi-seconds */
	u_int8_t qos_profile_default[3] = { 0x00, 0x00, 0x21 };
	u_int16_t msg_len = msg->len;

	if (!msg->trx) {
		DEBUGP(DGPRS, "Cannot transmit DL-UD without TRX assigned\n");
		return -EINVAL;
	}

	bts = msg->trx->bts;

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

	msgb_nsei(msg) = bts->gprs.nse.nsei;
	msgb_bvci(msg) = bts->gprs.cell.bvci;

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}
