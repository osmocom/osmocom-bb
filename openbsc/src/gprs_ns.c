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
#include <sys/types.h>

#include <arpa/inet.h>

#include <openbsc/gsm_data.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>
#include <openbsc/debug.h>
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
					u_int16_t nsvci)
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
					u_int16_t nsei)
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

static struct gprs_nsvc *nsvc_create(struct gprs_ns_inst *nsi, u_int16_t nsvci)
{
	struct gprs_nsvc *nsvc;

	nsvc = talloc_zero(nsi, struct gprs_nsvc);
	nsvc->nsvci = nsvci;
	/* before RESET procedure: BLOCKED and DEAD */
	nsvc->state = NSE_S_BLOCKED;
	nsvc->nsi = nsi;
	llist_add(&nsvc->list, &nsi->gprs_nsvcs);

	return nsvc;
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
	{ NS_CAUSE_PROTO_ERR_UNSPEC,	"Protocol error }, unspecified" },
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
		LOGP(DGPRS, LOGL_ERROR, "unsupported NS linklayer %u\n", nsvc->nsi->ll);
		msgb_free(msg);
		ret = -EIO;
		break;
	}
	return ret;
}

static int gprs_ns_tx_simple(struct gprs_nsvc *nsvc, u_int8_t pdu_type)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "GPRS/NS");
	struct gprs_ns_hdr *nsh;

	if (!msg)
		return -ENOMEM;

	nsh = (struct gprs_ns_hdr *) msgb_put(msg, sizeof(*nsh));

	nsh->pdu_type = pdu_type;

	return gprs_ns_tx(nsvc, msg);
}

#define NS_TIMER_ALIVE	3, 0 	/* after 3 seconds without response, we retry */
#define NS_TIMER_TEST	30, 0	/* every 10 seconds we check if the BTS is still alive */
#define NS_ALIVE_RETRIES  10	/* after 3 failed retransmit we declare BTS as dead */

static void gprs_ns_alive_cb(void *data)
{
	struct gprs_nsvc *nsvc = data;

	if (nsvc->timer_is_tns_alive) {
		/* Tns-alive case: we expired without response ! */
		nsvc->alive_retries++;
		if (nsvc->alive_retries > NS_ALIVE_RETRIES) {
			/* mark as dead and blocked */
			nsvc->state = NSE_S_BLOCKED;
			DEBUGP(DGPRS, "Tns-alive more then %u retries, "
				" blocking NS-VC\n", NS_ALIVE_RETRIES);
			/* FIXME: inform higher layers */
			return;
		}
	} else {
		/* Tns-test case: send NS-ALIVE PDU */
		gprs_ns_tx_simple(nsvc, NS_PDUT_ALIVE);
		/* start Tns-alive timer */
		nsvc->timer_is_tns_alive = 1;
	}
	bsc_schedule_timer(&nsvc->alive_timer, NS_TIMER_ALIVE);
}

/* Section 9.2.6 */
static int gprs_ns_tx_reset_ack(struct gprs_nsvc *nsvc)
{
	struct msgb *msg = msgb_alloc(NS_ALLOC_SIZE, "GPRS/NS");
	struct gprs_ns_hdr *nsh;
	u_int16_t nsvci, nsei;

	if (!msg)
		return -ENOMEM;

	nsvci = htons(nsvc->nsvci);
	nsei = htons(nsvc->nsei);

	nsh = (struct gprs_ns_hdr *) msgb_put(msg, sizeof(*nsh));

	nsh->pdu_type = NS_PDUT_RESET_ACK;

	DEBUGP(DGPRS, "nsvci=%u, nsei=%u\n", nsvc->nsvci, nsvc->nsei);

	msgb_tvlv_put(msg, NS_IE_VCI, 2, (u_int8_t *)&nsvci);
	msgb_tvlv_put(msg, NS_IE_NSEI, 2, (u_int8_t *)&nsei);

	return gprs_ns_tx(nsvc, msg);
}

/* Section 9.2.10: transmit side / NS-UNITDATA-REQUEST primitive */
int gprs_ns_sendmsg(struct gprs_ns_inst *nsi, struct msgb *msg)
{
	struct gprs_nsvc *nsvc;
	struct gprs_ns_hdr *nsh;
	u_int16_t bvci = msgb_bvci(msg);

	nsvc = nsvc_by_nsei(nsi, msgb_nsei(msg));
	if (!nsvc) {
		DEBUGP(DGPRS, "Unable to resolve NSEI %u to NS-VC!\n", msgb_nsei(msg));
		return -EINVAL;
	}

	nsh = (struct gprs_ns_hdr *) msgb_push(msg, sizeof(*nsh) + 3);
	if (!nsh) {
		DEBUGP(DGPRS, "Not enough headroom for NS header\n");
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
	u_int16_t bvci;

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
	u_int8_t cause;
	int rc;

	DEBUGP(DGPRS, "NS STATUS ");

	rc = tlv_parse(&tp, &ns_att_tlvdef, nsh->data, msgb_l2len(msg), 0, 0);

	if (!TLVP_PRESENT(&tp, NS_IE_CAUSE)) {
		DEBUGPC(DGPRS, "missing cause IE\n");
		return -EINVAL;
	}

	cause = *TLVP_VAL(&tp, NS_IE_CAUSE);
	DEBUGPC(DGPRS, "cause=%s\n", gprs_ns_cause_str(cause));

	return 0;
}

/* Section 7.3 */
static int gprs_ns_rx_reset(struct gprs_nsvc *nsvc, struct msgb *msg)
{
	struct gprs_ns_hdr *nsh = (struct gprs_ns_hdr *) msg->l2h;
	struct tlv_parsed tp;
	u_int8_t *cause;
	u_int16_t *nsvci, *nsei;
	int rc;

	DEBUGP(DGPRS, "NS RESET ");

	rc = tlv_parse(&tp, &ns_att_tlvdef, nsh->data, msgb_l2len(msg), 0, 0);

	if (!TLVP_PRESENT(&tp, NS_IE_CAUSE) ||
	    !TLVP_PRESENT(&tp, NS_IE_VCI) ||
	    !TLVP_PRESENT(&tp, NS_IE_NSEI)) {
		/* FIXME: respond with NS_CAUSE_MISSING_ESSENT_IE */
		DEBUGPC(DGPRS, "Missing mandatory IE\n");
		return -EINVAL;
	}

	cause = (u_int8_t *) TLVP_VAL(&tp, NS_IE_CAUSE);
	nsvci = (u_int16_t *) TLVP_VAL(&tp, NS_IE_VCI);
	nsei = (u_int16_t *) TLVP_VAL(&tp, NS_IE_NSEI);

	nsvc->state = NSE_S_BLOCKED | NSE_S_ALIVE;
	nsvc->nsei = ntohs(*nsei);
	nsvc->nsvci = ntohs(*nsvci);

	DEBUGPC(DGPRS, "cause=%s, NSVCI=%u, NSEI=%u\n",
		gprs_ns_cause_str(*cause), nsvc->nsvci, nsvc->nsei);

	/* mark the NS-VC as blocked and alive */
	/* start the test procedure */
	nsvc->alive_timer.cb = gprs_ns_alive_cb;
	nsvc->alive_timer.data = nsvc;
	bsc_schedule_timer(&nsvc->alive_timer, NS_TIMER_ALIVE);

	return gprs_ns_tx_reset_ack(nsvc);
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
		/* Only the RESET procedure creates a new NSVC */
		if (nsh->pdu_type != NS_PDUT_RESET)
			return -EIO;
		nsvc = nsvc_create(nsi, 0xffff);
		nsvc->ip.bts_addr = *saddr;
		rc = gprs_ns_rx_reset(nsvc, msg);
		return rc;
	}
	msgb_nsei(msg) = nsvc->nsei;

	switch (nsh->pdu_type) {
	case NS_PDUT_ALIVE:
		/* remote end inquires whether we're still alive,
		 * we need to respond with ALIVE_ACK */
		rc = gprs_ns_tx_simple(nsvc, NS_PDUT_ALIVE_ACK);
		break;
	case NS_PDUT_ALIVE_ACK:
		/* stop Tns-alive */
		bsc_del_timer(&nsvc->alive_timer);
		/* start Tns-test */
		nsvc->timer_is_tns_alive = 0;
		bsc_schedule_timer(&nsvc->alive_timer, NS_TIMER_TEST);
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
		DEBUGP(DGPRS, "NS RESET ACK\n");
		/* mark remote NS-VC as blocked + active */
		nsvc->remote_state = NSE_S_BLOCKED | NSE_S_ALIVE;
		break;
	case NS_PDUT_UNBLOCK:
		/* Section 7.2: unblocking procedure */
		DEBUGP(DGPRS, "NS UNBLOCK\n");
		nsvc->state &= ~NSE_S_BLOCKED;
		rc = gprs_ns_tx_simple(nsvc, NS_PDUT_UNBLOCK_ACK);
		break;
	case NS_PDUT_UNBLOCK_ACK:
		DEBUGP(DGPRS, "NS UNBLOCK ACK\n");
		/* mark remote NS-VC as unblocked + active */
		nsvc->remote_state = NSE_S_ALIVE;
		break;
	case NS_PDUT_BLOCK:
		DEBUGP(DGPRS, "NS BLOCK\n");
		nsvc->state |= NSE_S_BLOCKED;
		rc = gprs_ns_tx_simple(nsvc, NS_PDUT_UNBLOCK_ACK);
		break;
	case NS_PDUT_BLOCK_ACK:
		DEBUGP(DGPRS, "NS BLOCK ACK\n");
		/* mark remote NS-VC as blocked + active */
		nsvc->remote_state = NSE_S_BLOCKED | NSE_S_ALIVE;
		break;
	default:
		DEBUGP(DGPRS, "Unknown NS PDU type 0x%02x\n", nsh->pdu_type);
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
		fprintf(stderr, "recv error  %s\n", strerror(errno));
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
extern int make_sock(struct bsc_fd *bfd, int proto, u_int16_t port,
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
	if (gprs_ns_tx_simple(nsvc, NS_PDUT_RESET) < 0)
		return NULL;
	/* FIXME: should we run a timer and re-transmit the reset request? */

	return nsvc;
}
