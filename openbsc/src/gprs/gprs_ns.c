/* GPRS Networks Service (NS) messages on the Gb interfacebvci = msgb_bvci(msg);
 * 3GPP TS 08.16 version 8.0.1 Release 1999 / ETSI TS 101 299 V8.0.1 (2002-05) */

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

/* Some introduction into NS:  NS is used typically on top of frame relay,
 * but in the ip.access world it is encapsulated in UDP packets.  It serves
 * as an intermediate shim betwen BSSGP and the underlying medium.  It doesn't
 * do much, apart from providing congestion notification and status indication.
 *
 * Terms:
 * 	NS		Network Service
 *	NSVC		NS Virtual Connection
 *	NSEI		NS Entity Identifier
 *	NSVL		NS Virtual Link
 *	NSVLI		NS Virtual Link Identifier
 *	BVC		BSSGP Virtual Connection
 *	BVCI		BSSGP Virtual Connection Identifier
 *	NSVCG		NS Virtual Connection Goup
 *	Blocked		NS-VC cannot be used for user traffic
 *	Alive		Ability of a NS-VC to provide communication
 *
 *  There can be multiple BSSGP virtual connections over one (group of) NSVC's.  BSSGP will
 * therefore identify the BSSGP virtual connection by a BVCI passed down to NS.
 * NS then has to firgure out which NSVC's are responsible for this BVCI.
 * Those mappings are administratively configured.
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#include <arpa/inet.h>

#include <openbsc/gsm_data.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>
#include <openbsc/debug.h>
#include <openbsc/signal.h>
#include <openbsc/gprs_ns.h>
#include <openbsc/gprs_bssgp.h>

#define NS_ALLOC_SIZE	1024

static const struct tlv_definition ns_att_tlvdef = {
	.def = {
		[NS_IE_CAUSE]	= { TLV_TYPE_TvLV, 0 },
		[NS_IE_VCI]	= { TLV_TYPE_TvLV, 0 },
		[NS_IE_PDU]	= { TLV_TYPE_TvLV, 0 },
		[NS_IE_BVCI]	= { TLV_TYPE_TvLV, 0 },
		[NS_IE_NSEI]	= { TLV_TYPE_TvLV, 0 },
	},
};

/* Lookup struct gprs_nsvc based on NSVCI */
static struct gprs_nsvc *nsvc_by_nsvci(struct gprs_ns_inst *nsi,
					uint16_t nsvci)
{
	struct gprs_nsvc *nsvc;
	llist_for_each_entry(nsvc, &nsi->gprs_nsvcs, list) {
		if (nsvc->nsvci == nsvci)
			return nsvc;
	}
	return NULL;
}

/* Lookup struct gprs_nsvc based on NSVCI */
static struct gprs_nsvc *nsvc_by_nsei(struct gprs_ns_inst *nsi,
					uint16_t nsei)
{
	struct gprs_nsvc *nsvc;
	llist_for_each_entry(nsvc, &nsi->gprs_nsvcs, list) {
		if (nsvc->nsei == nsei)
			return nsvc;
	}
	return NULL;
}


/* Lookup struct gprs_nsvc based on remote peer socket addr */
static struct gprs_nsvc *nsvc_by_rem_addr(struct gprs_ns_inst *nsi,
					  struct sockaddr_in *sin)
{
	struct gprs_nsvc *nsvc;
	llist_for_each_entry(nsvc, &nsi->gprs_nsvcs, list) {
		if (!memcmp(&nsvc->ip.bts_addr, sin, sizeof(*sin)))
			return nsvc;
	}
	return NULL;
}

static void gprs_ns_timer_cb(void *data);

static struct gprs_nsvc *nsvc_create(struct gprs_ns_inst *nsi, uint16_t nsvci)
{
	struct gprs_nsvc *nsvc;

	nsvc = talloc_zero(nsi, struct gprs_nsvc);
	nsvc->nsvci = nsvci;
	/* before RESET procedure: BLOCKED and DEAD */
	nsvc->state = NSE_S_BLOCKED;
	nsvc->nsi = nsi;
	nsvc->timer.cb = gprs_ns_timer_cb;
	nsvc->timer.data = nsvc;

	llist_add(&nsvc->list, &nsi->gprs_nsvcs);

	return nsvc;
}

static void ns_dispatch_signal(struct gprs_nsvc *nsvc, unsigned int signal,
			       uint8_t cause)
{
	struct ns_signal_data nssd;

	nssd.nsvc = nsvc;
	nssd.cause = cause;

	dispatch_signal(SS_NS, signal, &nssd);
}

/* Section 10.3.2, Table 13 */
static const struct value_string ns_cause_str[] = {
	{ NS_CAUSE_TRANSIT_FAIL,	"Transit network failure" },
	{ NS_CAUSE_OM_INTERVENTION, 	"O&M intervention" },
	{ NS_CAUSE_EQUIP_FAIL,		"Equipment failure" },
	{ NS_CAUSE_NSVC_BLOCKED,	"NS-VC blocked" },
	{ NS_CAUSE_NSVC_UNKNOWN,	"NS-VC unknown" },
	{ NS_CAUSE_BVCI_UNKNOWN,	"BVCI unknown" },
	{ NS_CAUSE_SEM_INCORR_PDU,	"Semantically incorrect PDU" },
	{ NS_CAUSE_PDU_INCOMP_PSTATE,	"PDU not compatible with protocol state" },
	{ NS_CAUSE_PROTO_ERR_UNSPEC,	"Protocol error, unspecified" },
	{ NS_CAUSE_INVAL_ESSENT_IE,	"Invalid essential IE" },
	{ NS_CAUSE_MISSING_ESSENT_IE,	"Missing essential IE" },
	{ 0, NULL }
};

const char *gprs_ns_cause_str(enum ns_cause cause)
{
	return get_value_string(ns_cause_str, cause);
}

static int nsip_sendmsg(struct gprs_nsvc *nsvc, struct msgb *msg);

static int gprs_ns_tx(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	int ret;

	switch (nsvc->nsi->ll) {
	case GPRS_NS_LL_UDP:
		ret = nsip_sendmsg(nsvc, msg);
		break;
	default:
		LOGP(DNS, LOGL_ERROR, "unsupported NS linklayer %u\n", nsvc->nsi->ll);
		msgb_free(msg);
		ret = -EIO;
		break;
	}
	return ret;
}

static int gprs_ns_tx_simple(struct gprs_nsvc *nsvc, uint8_t pdu_type)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "GPRS/NS");
	struct gprs_ns_hdr *nsh;

	if (!msg)
		return -ENOMEM;

	nsh = (struct gprs_ns_hdr *) msgb_put(msg, sizeof(*nsh));

	nsh->pdu_type = pdu_type;

	return gprs_ns_tx(nsvc, msg);
}

int gprs_ns_tx_reset(struct gprs_nsvc *nsvc, uint8_t cause)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "GPRS/NS");
	struct gprs_ns_hdr *nsh;
	uint16_t nsvci = htons(nsvc->nsvci);
	uint16_t nsei = htons(nsvc->nsei);

	if (!msg)
		return -ENOMEM;

	nsh = (struct gprs_ns_hdr *) msgb_put(msg, sizeof(*nsh));
	nsh->pdu_type = NS_PDUT_RESET;

	msgb_tvlv_put(msg, NS_IE_CAUSE, 1, &cause);
	msgb_tvlv_put(msg, NS_IE_VCI, 2, (uint8_t *) &nsvci);
	msgb_tvlv_put(msg, NS_IE_NSEI, 2, (uint8_t *) &nsei);

	return gprs_ns_tx(nsvc, msg);

}

int gprs_ns_tx_status(struct gprs_nsvc *nsvc, uint8_t cause,
		      uint16_t bvci, struct msgb *orig_msg)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "GPRS/NS");
	struct gprs_ns_hdr *nsh;
	uint16_t nsvci = htons(nsvc->nsvci);

	bvci = htons(bvci);

	if (!msg)
		return -ENOMEM;

	nsh = (struct gprs_ns_hdr *) msgb_put(msg, sizeof(*nsh));
	nsh->pdu_type = NS_PDUT_STATUS;

	msgb_tvlv_put(msg, NS_IE_CAUSE, 1, &cause);

	/* Section 9.2.7.1: Static conditions for NS-VCI */
	if (cause == NS_CAUSE_NSVC_BLOCKED ||
	    cause == NS_CAUSE_NSVC_UNKNOWN)
		msgb_tvlv_put(msg, NS_IE_VCI, 2, (uint8_t *)&nsvci);

	/* Section 9.2.7.2: Static conditions for NS PDU */
	switch (cause) {
	case NS_CAUSE_SEM_INCORR_PDU:
	case NS_CAUSE_PDU_INCOMP_PSTATE:
	case NS_CAUSE_PROTO_ERR_UNSPEC:
	case NS_CAUSE_INVAL_ESSENT_IE:
	case NS_CAUSE_MISSING_ESSENT_IE:
		msgb_tvlv_put(msg, NS_IE_PDU, msgb_l2len(orig_msg),
			      orig_msg->l2h);
		break;
	default:
		break;
	}

	/* Section 9.2.7.3: Static conditions for BVCI */
	if (cause == NS_CAUSE_BVCI_UNKNOWN)
		msgb_tvlv_put(msg, NS_IE_VCI, 2, (uint8_t *)&bvci);

	return gprs_ns_tx(nsvc, msg);
}

int gprs_ns_tx_block(struct gprs_nsvc *nsvc, uint8_t cause)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "GPRS/NS");
	struct gprs_ns_hdr *nsh;
	uint16_t nsvci = htons(nsvc->nsvci);

	if (!msg)
		return -ENOMEM;

	/* be conservative and mark it as blocked even now! */
	nsvc->state |= NSE_S_BLOCKED;

	nsh = (struct gprs_ns_hdr *) msgb_put(msg, sizeof(*nsh));
	nsh->pdu_type = NS_PDUT_BLOCK;

	msgb_tvlv_put(msg, NS_IE_CAUSE, 1, &cause);
	msgb_tvlv_put(msg, NS_IE_VCI, 2, (uint8_t *) &nsvci);

	return gprs_ns_tx(nsvc, msg);
}

int gprs_ns_tx_unblock(struct gprs_nsvc *nsvc)
{
	return gprs_ns_tx_simple(nsvc, NS_PDUT_UNBLOCK);
}

#define NS_ALIVE_RETRIES  10	/* after 3 failed retransmit we declare BTS as dead */

static const uint8_t timer_mode_tout[_NSVC_TIMER_NR] = {
	[NSVC_TIMER_TNS_RESET]	= 60,
	[NSVC_TIMER_TNS_ALIVE]	= 3,
	[NSVC_TIMER_TNS_TEST]	= 30,
};

static void nsvc_start_timer(struct gprs_nsvc *nsvc, enum nsvc_timer_mode mode)
{
	nsvc->alive_retries = 0;

	if (bsc_timer_pending(&nsvc->timer))
		bsc_del_timer(&nsvc->timer);

	nsvc->timer_mode = mode;
	bsc_schedule_timer(&nsvc->timer, timer_mode_tout[mode], 0);
}

static void gprs_ns_timer_cb(void *data)
{
	struct gprs_nsvc *nsvc = data;

	switch (nsvc->timer_mode) {
	case NSVC_TIMER_TNS_ALIVE:
		/* Tns-alive case: we expired without response ! */
		nsvc->alive_retries++;
		if (nsvc->alive_retries > NS_ALIVE_RETRIES) {
			/* mark as dead and blocked */
			nsvc->state = NSE_S_BLOCKED;
			LOGP(DNS, LOGL_NOTICE,
				"NSEI=%u Tns-alive expired more then "
				"%u times, blocking NS-VC\n", nsvc->nsei,
				NS_ALIVE_RETRIES);
			ns_dispatch_signal(nsvc, S_NS_BLOCK, NS_CAUSE_NSVC_BLOCKED);
			return;
		}
		nsvc_start_timer(nsvc, NSVC_TIMER_TNS_ALIVE);
		break;
	case NSVC_TIMER_TNS_TEST:
		/* Tns-test case: send NS-ALIVE PDU */
		gprs_ns_tx_simple(nsvc, NS_PDUT_ALIVE);
		/* start Tns-alive timer */
		nsvc_start_timer(nsvc, NSVC_TIMER_TNS_ALIVE);
		break;
	case NSVC_TIMER_TNS_RESET:
		/* Chapter 7.3: Re-send the RESET */
		gprs_ns_tx_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
		/* Re-start Tns-reset timer */
		nsvc_start_timer(nsvc, NSVC_TIMER_TNS_RESET);
		break;
	case _NSVC_TIMER_NR:
		break;
	}
}

/* Section 9.2.6 */
static int gprs_ns_tx_reset_ack(struct gprs_nsvc *nsvc)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "GPRS/NS");
	struct gprs_ns_hdr *nsh;
	uint16_t nsvci, nsei;

	if (!msg)
		return -ENOMEM;

	nsvci = htons(nsvc->nsvci);
	nsei = htons(nsvc->nsei);

	nsh = (struct gprs_ns_hdr *) msgb_put(msg, sizeof(*nsh));

	nsh->pdu_type = NS_PDUT_RESET_ACK;

	LOGP(DNS, LOGL_INFO, "NSEI=%u Tx NS RESET ACK (NSVCI=%u)\n",
		nsvc->nsei, nsvc->nsvci);

	msgb_tvlv_put(msg, NS_IE_VCI, 2, (uint8_t *)&nsvci);
	msgb_tvlv_put(msg, NS_IE_NSEI, 2, (uint8_t *)&nsei);

	return gprs_ns_tx(nsvc, msg);
}

/* Section 9.2.10: transmit side / NS-UNITDATA-REQUEST primitive */
int gprs_ns_sendmsg(struct gprs_ns_inst *nsi, struct msgb *msg)
{
	struct gprs_nsvc *nsvc;
	struct gprs_ns_hdr *nsh;
	uint16_t bvci = msgb_bvci(msg);

	nsvc = nsvc_by_nsei(nsi, msgb_nsei(msg));
	if (!nsvc) {
		LOGP(DNS, LOGL_ERROR, "Unable to resolve NSEI %u "
			"to NS-VC!\n", msgb_nsei(msg));
		return -EINVAL;
	}

	if (!(nsvc->state & NSE_S_ALIVE)) {
		LOGP(DNS, LOGL_ERROR, "NSEI=%u is not alive, cannot send\n",
			nsvc->nsei);
		return -EBUSY;
	}
	if (nsvc->state & NSE_S_BLOCKED) {
		LOGP(DNS, LOGL_ERROR, "NSEI=%u is blocked, cannot send\n",
			nsvc->nsei);
		return -EBUSY;
	}

	nsh = (struct gprs_ns_hdr *) msgb_push(msg, sizeof(*nsh) + 3);
	if (!nsh) {
		LOGP(DNS, LOGL_ERROR, "Not enough headroom for NS header\n");
		return -EIO;
	}

	nsh->pdu_type = NS_PDUT_UNITDATA;
	/* spare octet in data[0] */
	nsh->data[1] = bvci >> 8;
	nsh->data[2] = bvci & 0xff;

	return gprs_ns_tx(nsvc, msg);
}

/* Section 9.2.10: receive side */
static int gprs_ns_rx_unitdata(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	struct gprs_ns_hdr *nsh = (struct gprs_ns_hdr *)msg->l2h;
	uint16_t bvci;

	if (nsvc->state & NSE_S_BLOCKED)
		return gprs_ns_tx_status(nsvc, NS_CAUSE_NSVC_BLOCKED,
					 0, msg);

	/* spare octet in data[0] */
	bvci = nsh->data[1] << 8 | nsh->data[2];
	msgb_bssgph(msg) = &nsh->data[3];
	msgb_bvci(msg) = bvci;

	/* call upper layer (BSSGP) */
	return nsvc->nsi->cb(GPRS_NS_EVT_UNIT_DATA, nsvc, msg, bvci);
}

/* Section 9.2.7 */
static int gprs_ns_rx_status(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	struct gprs_ns_hdr *nsh = (struct gprs_ns_hdr *) msg->l2h;
	struct tlv_parsed tp;
	uint8_t cause;
	int rc;

	LOGP(DNS, LOGL_INFO, "NSEI=%u NS STATUS ", nsvc->nsei);

	rc = tlv_parse(&tp, &ns_att_tlvdef, nsh->data, msgb_l2len(msg), 0, 0);

	if (!TLVP_PRESENT(&tp, NS_IE_CAUSE)) {
		LOGPC(DNS, LOGL_INFO, "missing cause IE\n");
		return -EINVAL;
	}

	cause = *TLVP_VAL(&tp, NS_IE_CAUSE);
	LOGPC(DNS, LOGL_INFO, "cause=%s\n", gprs_ns_cause_str(cause));

	return 0;
}

/* Section 7.3 */
static int gprs_ns_rx_reset(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	struct gprs_ns_hdr *nsh = (struct gprs_ns_hdr *) msg->l2h;
	struct tlv_parsed tp;
	uint8_t *cause;
	uint16_t *nsvci, *nsei;
	int rc;

	rc = tlv_parse(&tp, &ns_att_tlvdef, nsh->data, msgb_l2len(msg), 0, 0);

	if (!TLVP_PRESENT(&tp, NS_IE_CAUSE) ||
	    !TLVP_PRESENT(&tp, NS_IE_VCI) ||
	    !TLVP_PRESENT(&tp, NS_IE_NSEI)) {
		LOGP(DNS, LOGL_ERROR, "NS RESET Missing mandatory IE\n");
		gprs_ns_tx_status(nsvc, NS_CAUSE_MISSING_ESSENT_IE, 0, msg);
		return -EINVAL;
	}

	cause = (uint8_t *) TLVP_VAL(&tp, NS_IE_CAUSE);
	nsvci = (uint16_t *) TLVP_VAL(&tp, NS_IE_VCI);
	nsei = (uint16_t *) TLVP_VAL(&tp, NS_IE_NSEI);

	LOGP(DNS, LOGL_INFO, "NSEI=%u Rx NS RESET (NSVCI=%u, cause=%s)\n",
		nsvc->nsvci, nsvc->nsei, gprs_ns_cause_str(*cause));

	/* Mark NS-VC as blocked and alive */
	nsvc->state = NSE_S_BLOCKED | NSE_S_ALIVE;

	nsvc->nsei = ntohs(*nsei);
	nsvc->nsvci = ntohs(*nsvci);

	/* start the test procedure */
	nsvc_start_timer(nsvc, NSVC_TIMER_TNS_ALIVE);

	/* inform interested parties about the fact that this NSVC
	 * has received RESET */
	ns_dispatch_signal(nsvc, S_NS_RESET, *cause);

	return gprs_ns_tx_reset_ack(nsvc);
}

static int gprs_ns_rx_block(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	struct gprs_ns_hdr *nsh = (struct gprs_ns_hdr *) msg->l2h;
	struct tlv_parsed tp;
	uint8_t *cause;
	int rc;

	LOGP(DNS, LOGL_INFO, "NSEI=%u Rx NS BLOCK\n", nsvc->nsei);

	nsvc->state |= NSE_S_BLOCKED;

	rc = tlv_parse(&tp, &ns_att_tlvdef, nsh->data, msgb_l2len(msg), 0, 0);

	if (!TLVP_PRESENT(&tp, NS_IE_CAUSE) ||
	    !TLVP_PRESENT(&tp, NS_IE_VCI)) {
		LOGP(DNS, LOGL_ERROR, "NS RESET Missing mandatory IE\n");
		gprs_ns_tx_status(nsvc, NS_CAUSE_MISSING_ESSENT_IE, 0, msg);
		return -EINVAL;
	}

	cause = (uint8_t *) TLVP_VAL(&tp, NS_IE_CAUSE);
	//nsvci = (uint16_t *) TLVP_VAL(&tp, NS_IE_VCI);

	ns_dispatch_signal(nsvc, S_NS_BLOCK, *cause);

	return gprs_ns_tx_simple(nsvc, NS_PDUT_BLOCK_ACK);
}

/* main entry point, here incoming NS frames enter */
int gprs_ns_rcvmsg(struct gprs_ns_inst *nsi, struct msgb *msg,
		   struct sockaddr_in *saddr)
{
	struct gprs_ns_hdr *nsh = (struct gprs_ns_hdr *) msg->l2h;
	struct gprs_nsvc *nsvc;
	int rc = 0;

	/* look up the NSVC based on source address */
	nsvc = nsvc_by_rem_addr(nsi, saddr);
	if (!nsvc) {
		struct tlv_parsed tp;
		uint16_t nsei;
		/* Only the RESET procedure creates a new NSVC */
		if (nsh->pdu_type != NS_PDUT_RESET) {
			LOGP(DNS, LOGL_INFO, "Ignoring NS PDU type 0x%0x "
				"from %s:%u for non-existing NS-VC\n",
				nsh->pdu_type, inet_ntoa(saddr->sin_addr),
				ntohs(saddr->sin_port));
			/* FIXME: send STATUS (but we have no NSVC!) */
			//gprs_ns_tx_reset(nsvc, NS_CAUSE_NSVC_UNKNOWN);
			return -EIO;
		}
		rc = tlv_parse(&tp, &ns_att_tlvdef, nsh->data,
						msgb_l2len(msg), 0, 0);
		if (!TLVP_PRESENT(&tp, NS_IE_CAUSE) ||
		    !TLVP_PRESENT(&tp, NS_IE_VCI) ||
		    !TLVP_PRESENT(&tp, NS_IE_NSEI)) {
			LOGP(DNS, LOGL_ERROR, "NS RESET Missing mandatory IE\n");
			gprs_ns_tx_status(nsvc, NS_CAUSE_MISSING_ESSENT_IE, 0,
					  msg);
			return -EINVAL;
		}
		nsei = ntohs(*(uint16_t *)TLVP_VAL(&tp, NS_IE_NSEI));
		/* Check if we already know this NSEI, the remote end might
		 * simply have changed addresses, or it is a SGSN */
		nsvc = nsvc_by_nsei(nsi, nsei);
		if (!nsvc) {
			LOGP(DNS, LOGL_INFO, "Creating NS-VC for BSS at %s:%u\n",
				inet_ntoa(saddr->sin_addr), ntohs(saddr->sin_port));
			nsvc = nsvc_create(nsi, 0xffff);
		}
		/* Update the remote peer IP address/port */
		nsvc->ip.bts_addr = *saddr;
	} else
		msgb_nsei(msg) = nsvc->nsei;

	switch (nsh->pdu_type) {
	case NS_PDUT_ALIVE:
		/* remote end inquires whether we're still alive,
		 * we need to respond with ALIVE_ACK */
		rc = gprs_ns_tx_simple(nsvc, NS_PDUT_ALIVE_ACK);
		break;
	case NS_PDUT_ALIVE_ACK:
		/* stop Tns-alive */
		bsc_del_timer(&nsvc->timer);
		/* start Tns-test */
		nsvc_start_timer(nsvc, NSVC_TIMER_TNS_TEST);
		if (nsvc->remote_end_is_sgsn) {
			/* FIXME: this should be one level higher */
			if (nsvc->state & NSE_S_BLOCKED)
				rc = gprs_ns_tx_unblock(nsvc);
		}
		break;
	case NS_PDUT_UNITDATA:
		/* actual user data */
		rc = gprs_ns_rx_unitdata(nsvc, msg);
		break;
	case NS_PDUT_STATUS:
		rc = gprs_ns_rx_status(nsvc, msg);
		break;
	case NS_PDUT_RESET:
		rc = gprs_ns_rx_reset(nsvc, msg);
		break;
	case NS_PDUT_RESET_ACK:
		LOGP(DNS, LOGL_INFO, "NSEI=%u Rx NS RESET ACK\n", nsvc->nsei);
		/* mark NS-VC as blocked + active */
		nsvc->state = NSE_S_BLOCKED | NSE_S_ALIVE;
		nsvc->remote_state = NSE_S_BLOCKED | NSE_S_ALIVE;
		if (nsvc->remote_end_is_sgsn) {
			/* stop RESET timer */
			bsc_del_timer(&nsvc->timer);
			/* Initiate TEST proc.: Send ALIVE and start timer */
			rc = gprs_ns_tx_simple(nsvc, NS_PDUT_ALIVE);
			nsvc_start_timer(nsvc, NSVC_TIMER_TNS_ALIVE);
		}
		break;
	case NS_PDUT_UNBLOCK:
		/* Section 7.2: unblocking procedure */
		LOGP(DNS, LOGL_INFO, "NSEI=%u Rx NS UNBLOCK\n", nsvc->nsei);
		nsvc->state &= ~NSE_S_BLOCKED;
		ns_dispatch_signal(nsvc, S_NS_UNBLOCK, 0);
		rc = gprs_ns_tx_simple(nsvc, NS_PDUT_UNBLOCK_ACK);
		break;
	case NS_PDUT_UNBLOCK_ACK:
		LOGP(DNS, LOGL_INFO, "NSEI=%u Rx NS UNBLOCK ACK\n", nsvc->nsei);
		/* mark NS-VC as unblocked + active */
		nsvc->state = NSE_S_ALIVE;
		nsvc->remote_state = NSE_S_ALIVE;
		break;
	case NS_PDUT_BLOCK:
		rc = gprs_ns_rx_block(nsvc, msg);
		break;
	case NS_PDUT_BLOCK_ACK:
		LOGP(DNS, LOGL_INFO, "NSEI=%u Rx NS BLOCK ACK\n", nsvc->nsei);
		/* mark remote NS-VC as blocked + active */
		nsvc->remote_state = NSE_S_BLOCKED | NSE_S_ALIVE;
		break;
	default:
		LOGP(DNS, LOGL_NOTICE, "NSEI=%u Rx Unknown NS PDU type 0x%02x\n",
			nsvc->nsei, nsh->pdu_type);
		rc = -EINVAL;
		break;
	}
	return rc;
}

struct gprs_ns_inst *gprs_ns_instantiate(gprs_ns_cb_t *cb)
{
	struct gprs_ns_inst *nsi = talloc_zero(tall_bsc_ctx, struct gprs_ns_inst);

	nsi->cb = cb;
	INIT_LLIST_HEAD(&nsi->gprs_nsvcs);

	return nsi;
}

void gprs_ns_destroy(struct gprs_ns_inst *nsi)
{
	/* FIXME: clear all timers */

	/* recursively free the NSI and all its NSVCs */
	talloc_free(nsi);
}


/* NS-over-IP code, according to 3GPP TS 48.016 Chapter 6.2
 * We don't support Size Procedure, Configuration Procedure, ChangeWeight Procedure */

/* Read a single NS-over-IP message */
static struct msgb *read_nsip_msg(struct bsc_fd *bfd, int *error,
				  struct sockaddr_in *saddr)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "Abis/IP/GPRS-NS");
	int ret = 0;
	socklen_t saddr_len = sizeof(*saddr);

	if (!msg) {
		*error = -ENOMEM;
		return NULL;
	}

	ret = recvfrom(bfd->fd, msg->data, NS_ALLOC_SIZE, 0,
			(struct sockaddr *)saddr, &saddr_len);
	if (ret < 0) {
		LOGP(DNS, LOGL_ERROR, "recv error %s during NSIP recv\n",
			strerror(errno));
		msgb_free(msg);
		*error = ret;
		return NULL;
	} else if (ret == 0) {
		msgb_free(msg);
		*error = ret;
		return NULL;
	}

	msg->l2h = msg->data;
	msgb_put(msg, ret);

	return msg;
}

static int handle_nsip_read(struct bsc_fd *bfd)
{
	int error;
	struct sockaddr_in saddr;
	struct gprs_ns_inst *nsi = bfd->data;
	struct msgb *msg = read_nsip_msg(bfd, &error, &saddr);

	if (!msg)
		return error;

	return gprs_ns_rcvmsg(nsi, msg, &saddr);
}

static int handle_nsip_write(struct bsc_fd *bfd)
{
	/* FIXME: actually send the data here instead of nsip_sendmsg() */
	return -EIO;
}

int nsip_sendmsg(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	int rc;
	struct gprs_ns_inst *nsi = nsvc->nsi;
	struct sockaddr_in *daddr = &nsvc->ip.bts_addr;

	rc = sendto(nsi->nsip.fd.fd, msg->data, msg->len, 0,
		  (struct sockaddr *)daddr, sizeof(*daddr));

	talloc_free(msg);

	return rc;
}

/* UDP Port 23000 carries the LLC-in-BSSGP-in-NS protocol stack */
static int nsip_fd_cb(struct bsc_fd *bfd, unsigned int what)
{
	int rc = 0;

	if (what & BSC_FD_READ)
		rc = handle_nsip_read(bfd);
	if (what & BSC_FD_WRITE)
		rc = handle_nsip_write(bfd);

	return rc;
}


/* FIXME: this is currently in input/ipaccess.c */
extern int make_sock(struct bsc_fd *bfd, int proto, uint16_t port,
		     int (*cb)(struct bsc_fd *fd, unsigned int what));

/* Listen for incoming GPRS packets */
int nsip_listen(struct gprs_ns_inst *nsi, uint16_t udp_port)
{
	int ret;

	ret = make_sock(&nsi->nsip.fd, IPPROTO_UDP, udp_port, nsip_fd_cb);
	if (ret < 0)
		return ret;

	nsi->ll = GPRS_NS_LL_UDP;
	nsi->nsip.fd.data = nsi;

	return ret;
}

/* Establish a connection (from the BSS) to the SGSN */
struct gprs_nsvc *nsip_connect(struct gprs_ns_inst *nsi,
				struct sockaddr_in *dest, uint16_t nsei,
				uint16_t nsvci)
{
	struct gprs_nsvc *nsvc;

	nsvc = nsvc_by_rem_addr(nsi, dest);
	if (!nsvc) {
		nsvc = nsvc_create(nsi, nsvci);
		nsvc->ip.bts_addr = *dest;
	}
	nsvc->nsei = nsei;
	nsvc->nsvci = nsvci;
	nsvc->remote_end_is_sgsn = 1;

	/* Initiate a RESET procedure */
	/* Mark NS-VC locally as blocked and dead */
	nsvc->state = NSE_S_BLOCKED;
	/* Send NS-RESET PDU */
	if (gprs_ns_tx_reset(nsvc, NS_CAUSE_OM_INTERVENTION) < 0) {
		LOGP(DNS, LOGL_ERROR, "NSEI=%u, error resetting NS-VC\n",
			nsei);
	}
	/* Start Tns-reset */
	nsvc_start_timer(nsvc, NSVC_TIMER_TNS_RESET);

	return nsvc;
}
