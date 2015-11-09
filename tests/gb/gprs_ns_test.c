/* test routines for NS connection handling
 * (C) 2013 by sysmocom s.f.m.c. GmbH
 * Author: Jacob Erlbeck <jerlbeck@sysmocom.de>
 */

#undef _GNU_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/application.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/rate_ctr.h>
#include <osmocom/gprs/gprs_msgb.h>
#include <osmocom/gprs/gprs_ns.h>
#include <osmocom/gprs/gprs_bssgp.h>

#define REMOTE_BSS_ADDR 0x01020304
#define REMOTE_SGSN_ADDR 0x05060708

#define SGSN_NSEI 0x0100

static int sent_pdu_type = 0;

static int gprs_process_message(struct gprs_ns_inst *nsi, const char *text,
				struct sockaddr_in *peer, const unsigned char* data,
				size_t data_len);

static void send_ns_reset(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr,
			  enum ns_cause cause, uint16_t nsvci, uint16_t nsei)
{
	/* GPRS Network Service, PDU type: NS_RESET,
	 */
	unsigned char msg[12] = {
		0x02, 0x00, 0x81, 0x01, 0x01, 0x82, 0x11, 0x22,
		0x04, 0x82, 0x11, 0x22
	};

	msg[3] = cause;
	msg[6] = nsvci / 256;
	msg[7] = nsvci % 256;
	msg[10] = nsei / 256;
	msg[11] = nsei % 256;

	gprs_process_message(nsi, "RESET", src_addr, msg, sizeof(msg));
}

static void send_ns_reset_ack(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr,
			      uint16_t nsvci, uint16_t nsei)
{
	/* GPRS Network Service, PDU type: NS_RESET_ACK,
	 */
	unsigned char msg[9] = {
		0x03, 0x01, 0x82, 0x11, 0x22,
		0x04, 0x82, 0x11, 0x22
	};

	msg[3] = nsvci / 256;
	msg[4] = nsvci % 256;
	msg[7] = nsei / 256;
	msg[8] = nsei % 256;

	gprs_process_message(nsi, "RESET_ACK", src_addr, msg, sizeof(msg));
}

static void send_ns_alive(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr)
{
	/* GPRS Network Service, PDU type: NS_ALIVE */
	unsigned char msg[1] = {
		0x0a
	};

	gprs_process_message(nsi, "ALIVE", src_addr, msg, sizeof(msg));
}

static void send_ns_alive_ack(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr)
{
	/* GPRS Network Service, PDU type: NS_ALIVE_ACK */
	unsigned char msg[1] = {
		0x0b
	};

	gprs_process_message(nsi, "ALIVE_ACK", src_addr, msg, sizeof(msg));
}

static void send_ns_unblock(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr)
{
	/* GPRS Network Service, PDU type: NS_UNBLOCK */
	unsigned char msg[1] = {
		0x06
	};

	gprs_process_message(nsi, "UNBLOCK", src_addr, msg, sizeof(msg));
}

static void send_ns_unblock_ack(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr)
{
	/* GPRS Network Service, PDU type: NS_UNBLOCK_ACK */
	unsigned char msg[1] = {
		0x07
	};

	gprs_process_message(nsi, "UNBLOCK_ACK", src_addr, msg, sizeof(msg));
}

static void send_ns_unitdata(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr,
			     uint16_t nsbvci,
			     const unsigned char *bssgp_msg, size_t bssgp_msg_size)
{
	/* GPRS Network Service, PDU type: NS_UNITDATA */
	unsigned char msg[4096] = {
		0x00, 0x00, 0x00, 0x00
	};

	OSMO_ASSERT(bssgp_msg_size <= sizeof(msg) - 4);

	msg[2] = nsbvci / 256;
	msg[3] = nsbvci % 256;
	memcpy(msg + 4, bssgp_msg, bssgp_msg_size);

	gprs_process_message(nsi, "UNITDATA", src_addr, msg, bssgp_msg_size + 4);
}

static void send_bssgp_reset(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr,
			     uint16_t bvci)
{
	/* GPRS Network Service, PDU type: NS_UNITDATA, BVCI 0
	 * BSSGP RESET */
	unsigned char msg[22] = {
		0x22, 0x04, 0x82, 0x4a,
		0x2e, 0x07, 0x81, 0x08, 0x08, 0x88, 0x10, 0x20,
		0x30, 0x40, 0x50, 0x60, 0x10, 0x00
	};

	msg[3] = bvci / 256;
	msg[4] = bvci % 256;

	send_ns_unitdata(nsi, src_addr, 0, msg, sizeof(msg));
}

static void setup_ns(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr,
		     uint16_t nsvci, uint16_t nsei)
{
	printf("Setup NS-VC: remote 0x%08x:%d, "
	       "NSVCI 0x%04x(%d), NSEI 0x%04x(%d)\n\n",
	       ntohl(src_addr->sin_addr.s_addr), ntohs(src_addr->sin_port),
	       nsvci, nsvci, nsei, nsei);

	send_ns_reset(nsi, src_addr, NS_CAUSE_OM_INTERVENTION, nsvci, nsei);
	send_ns_alive(nsi, src_addr);
	send_ns_unblock(nsi, src_addr);
	send_ns_alive_ack(nsi, src_addr);
}

static void setup_bssgp(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr,
			uint16_t bvci) __attribute__((__unused__));

static void setup_bssgp(struct gprs_ns_inst *nsi, struct sockaddr_in *src_addr,
			uint16_t bvci)
{
	printf("Setup BSSGP: remote 0x%08x:%d, "
	       "BVCI 0x%04x(%d)\n\n",
	       ntohl(src_addr->sin_addr.s_addr), ntohs(src_addr->sin_port),
	       bvci, bvci);

	send_bssgp_reset(nsi, src_addr, bvci);
}

/* GPRS Network Service, PDU type: NS_RESET,
 * Cause: O&M intervention, NS VCI: 0x1122, NSEI 0x1122
 */
static const unsigned char gprs_ns_reset[12] = {
	0x02, 0x00, 0x81, 0x01, 0x01, 0x82, 0x11, 0x22,
	0x04, 0x82, 0x11, 0x22
};

/* GPRS Network Service, PDU type: NS_RESET,
 * Cause: O&M intervention, NS VCI: 0x3344, NSEI 0x1122
 */
static const unsigned char gprs_ns_reset_vci2[12] = {
	0x02, 0x00, 0x81, 0x01, 0x01, 0x82, 0x33, 0x44,
	0x04, 0x82, 0x11, 0x22
};

/* GPRS Network Service, PDU type: NS_RESET,
 * Cause: O&M intervention, NS VCI: 0x1122, NSEI 0x3344
 */
static const unsigned char gprs_ns_reset_nsei2[12] = {
	0x02, 0x00, 0x81, 0x01, 0x01, 0x82, 0x11, 0x22,
	0x04, 0x82, 0x33, 0x44
};

/* GPRS Network Service, PDU type: NS_ALIVE */
static const unsigned char gprs_ns_alive[1] = {
	0x0a
};

/* GPRS Network Service, PDU type: NS_STATUS,
 * Cause: PDU not compatible with the protocol state
 * PDU: NS_ALIVE
 */
static const unsigned char gprs_ns_status_invalid_alive[7] = {
	0x08, 0x00, 0x81, 0x0a, 0x02, 0x81, 0x0a
};

/* GPRS Network Service, PDU type: NS_ALIVE_ACK */
static const unsigned char gprs_ns_alive_ack[1] = {
	0x0b
};

/* GPRS Network Service, PDU type: NS_UNBLOCK */
static const unsigned char gprs_ns_unblock[1] = {
	0x06
};


/* GPRS Network Service, PDU type: NS_STATUS,
 * Cause: PDU not compatible with the protocol state
 * PDU: NS_RESET_ACK, NS VCI: 0x1122, NSEI 0x1122
 */
static const unsigned char gprs_ns_status_invalid_reset_ack[15] = {
	0x08, 0x00, 0x81, 0x0a, 0x02, 0x89, 0x03, 0x01,
	0x82, 0x11, 0x22, 0x04, 0x82, 0x11, 0x22
};

/* GPRS Network Service, PDU type: NS_UNITDATA, BVCI 0 */
static const unsigned char gprs_bssgp_reset[22] = {
	0x00, 0x00, 0x00, 0x00, 0x22, 0x04, 0x82, 0x4a,
	0x2e, 0x07, 0x81, 0x08, 0x08, 0x88, 0x10, 0x20,
	0x30, 0x40, 0x50, 0x60, 0x10, 0x00
};

int gprs_ns_rcvmsg(struct gprs_ns_inst *nsi, struct msgb *msg,
		   struct sockaddr_in *saddr, enum gprs_ns_ll ll);

/* override */
int gprs_ns_callback(enum gprs_ns_evt event, struct gprs_nsvc *nsvc,
			 struct msgb *msg, uint16_t bvci)
{
	printf("CALLBACK, event %d, msg length %td, bvci 0x%04x\n%s\n\n",
			event, msgb_bssgp_len(msg), bvci,
			osmo_hexdump(msgb_bssgph(msg), msgb_bssgp_len(msg)));
	return 0;
}

/* override */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen)
{
	typedef ssize_t (*sendto_t)(int, const void *, size_t, int,
			const struct sockaddr *, socklen_t);
	static sendto_t real_sendto = NULL;
	uint32_t dest_host = htonl(((struct sockaddr_in *)dest_addr)->sin_addr.s_addr);

	if (!real_sendto)
		real_sendto = dlsym(RTLD_NEXT, "sendto");

	sent_pdu_type = len > 0 ? ((uint8_t *)buf)[0] : -1;

	if (dest_host == REMOTE_BSS_ADDR)
		printf("MESSAGE to BSS, msg length %zu\n%s\n\n", len, osmo_hexdump(buf, len));
	else if (dest_host == REMOTE_SGSN_ADDR)
		printf("MESSAGE to SGSN, msg length %zu\n%s\n\n", len, osmo_hexdump(buf, len));
	else
		return real_sendto(sockfd, buf, len, flags, dest_addr, addrlen);

	return len;
}

/* override */
int gprs_ns_sendmsg(struct gprs_ns_inst *nsi, struct msgb *msg)
{
	typedef int (*gprs_ns_sendmsg_t)(struct gprs_ns_inst *nsi, struct msgb *msg);
	static gprs_ns_sendmsg_t real_gprs_ns_sendmsg = NULL;
	uint16_t bvci = msgb_bvci(msg);
	uint16_t nsei = msgb_nsei(msg);

	unsigned char *buf = msg->data;
	size_t len = msg->len;

	if (!real_gprs_ns_sendmsg)
		real_gprs_ns_sendmsg = dlsym(RTLD_NEXT, "gprs_ns_sendmsg");

	if (nsei == SGSN_NSEI)
		printf("NS UNITDATA MESSAGE to SGSN, BVCI 0x%04x, msg length %zu\n%s\n\n",
		       bvci, len, osmo_hexdump(buf, len));
	else
		printf("NS UNITDATA MESSAGE to BSS, BVCI 0x%04x, msg length %zu\n%s\n\n",
		       bvci, len, osmo_hexdump(buf, len));

	return real_gprs_ns_sendmsg(nsi, msg);
}

static void dump_rate_ctr_group(FILE *stream, const char *prefix,
			    struct rate_ctr_group *ctrg)
{
	unsigned int i;

	for (i = 0; i < ctrg->desc->num_ctr; i++) {
		struct rate_ctr *ctr = &ctrg->ctr[i];
		if (ctr->current && !strchr(ctrg->desc->ctr_desc[i].name, '.'))
			fprintf(stream, " %s%s: %llu%s",
				prefix, ctrg->desc->ctr_desc[i].description,
				(long long)ctr->current,
				"\n");
	};
}

/* Signal handler for signals from NS layer */
static int test_signal(unsigned int subsys, unsigned int signal,
		  void *handler_data, void *signal_data)
{
	struct ns_signal_data *nssd = signal_data;

	if (subsys != SS_L_NS)
		return 0;

	switch (signal) {
	case S_NS_RESET:
		printf("==> got signal NS_RESET, NS-VC 0x%04x/%s\n",
		       nssd->nsvc->nsvci,
		       gprs_ns_ll_str(nssd->nsvc));
		break;

	case S_NS_ALIVE_EXP:
		printf("==> got signal NS_ALIVE_EXP, NS-VC 0x%04x/%s\n",
		       nssd->nsvc->nsvci,
		       gprs_ns_ll_str(nssd->nsvc));
		break;

	case S_NS_BLOCK:
		printf("==> got signal NS_BLOCK, NS-VC 0x%04x/%s\n",
		       nssd->nsvc->nsvci,
		       gprs_ns_ll_str(nssd->nsvc));
		break;

	case S_NS_UNBLOCK:
		printf("==> got signal NS_UNBLOCK, NS-VC 0x%04x/%s\n",
		       nssd->nsvc->nsvci,
		       gprs_ns_ll_str(nssd->nsvc));
		break;

	case S_NS_REPLACED:
		printf("==> got signal NS_REPLACED: 0x%04x/%s",
		       nssd->nsvc->nsvci,
		       gprs_ns_ll_str(nssd->nsvc));
		printf(" -> 0x%04x/%s\n",
		       nssd->old_nsvc->nsvci,
		       gprs_ns_ll_str(nssd->old_nsvc));
		break;

	case S_NS_MISMATCH:
		printf("==> got signal NS_MISMATCH: 0x%04x/%s pdu=%d, ie=%d\n",
		       nssd->nsvc->nsvci, gprs_ns_ll_str(nssd->nsvc),
		       nssd->pdu_type, nssd->ie_type);
		break;

	default:
		printf("==> got signal %d, NS-VC 0x%04x/%s\n", signal,
		       nssd->nsvc->nsvci,
		       gprs_ns_ll_str(nssd->nsvc));
		break;
	}

	return 0;
}

static int gprs_process_message(struct gprs_ns_inst *nsi, const char *text, struct sockaddr_in *peer, const unsigned char* data, size_t data_len)
{
	struct msgb *msg;
	int ret;
	if (data_len > NS_ALLOC_SIZE - NS_ALLOC_HEADROOM) {
		fprintf(stderr, "message too long: %zu\n", data_len);
		return -1;
	}

	msg = gprs_ns_msgb_alloc();
	memmove(msg->data, data, data_len);
	msg->l2h = msg->data;
	msgb_put(msg, data_len);

	printf("PROCESSING %s from 0x%08x:%d\n%s\n\n",
	       text, ntohl(peer->sin_addr.s_addr), ntohs(peer->sin_port),
	       osmo_hexdump(data, data_len));

	ret = gprs_ns_rcvmsg(nsi, msg, peer, GPRS_NS_LL_UDP);

	printf("result (%s) = %d\n\n", text, ret);

	msgb_free(msg);

	return ret;
}

static int gprs_send_message(struct gprs_ns_inst *nsi, const char *text,
			     uint16_t nsei, uint16_t bvci,
			     const unsigned char* data, size_t data_len)
{
	struct msgb *msg;
	int ret;
	if (data_len > NS_ALLOC_SIZE - NS_ALLOC_HEADROOM) {
		fprintf(stderr, "message too long: %zu\n", data_len);
		return -1;
	}

	msg = gprs_ns_msgb_alloc();
	memmove(msg->data, data, data_len);
	msg->l2h = msg->data;
	msgb_put(msg, data_len);

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = bvci;

	printf("SENDING %s to NSEI 0x%04x, BVCI 0x%04x\n", text, nsei, bvci);

	ret = gprs_ns_sendmsg(nsi, msg);

	printf("result (%s) = %d\n\n", text, ret);

	return ret;
}

static void gprs_dump_nsi(struct gprs_ns_inst *nsi)
{
	struct gprs_nsvc *nsvc;

	printf("Current NS-VCIs:\n");
	llist_for_each_entry(nsvc, &nsi->gprs_nsvcs, list) {
		struct sockaddr_in *peer = &(nsvc->ip.bts_addr);
		printf("    VCI 0x%04x, NSEI 0x%04x, peer 0x%08x:%d%s%s%s\n",
		       nsvc->nsvci, nsvc->nsei,
		       ntohl(peer->sin_addr.s_addr), ntohs(peer->sin_port),
		       nsvc->state & NSE_S_BLOCKED ? ", blocked" : "",
		       nsvc->state & NSE_S_ALIVE   ? "" : ", dead",
		       nsvc->nsvci_is_valid   ? "" : ", invalid VCI"
		      );
		dump_rate_ctr_group(stdout, "        ", nsvc->ctrg);
	}
	printf("\n");
}

static int expire_nsvc_timer(struct gprs_nsvc *nsvc)
{
	int rc;

	if (!osmo_timer_pending(&nsvc->timer))
		return -1;

	rc = nsvc->timer_mode;
	osmo_timer_del(&nsvc->timer);

	nsvc->timer.cb(nsvc->timer.data);

	return rc;
}

static void test_nsvc()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in peer[1] = {{0},};
	struct gprs_nsvc *nsvc;
	int i;

	peer[0].sin_family = AF_INET;
	peer[0].sin_port = htons(1111);
	peer[0].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);

	for (i=0; i<4; ++i) {
		printf("--- Create via RESET (round %d) ---\n\n", i);

		send_ns_reset(nsi, &peer[0], NS_CAUSE_OM_INTERVENTION,
			      0x1001, 0x1000);
		gprs_dump_nsi(nsi);

		printf("--- Delete nsvc object (round %d)---\n\n", i);

		nsvc = gprs_nsvc_by_nsvci(nsi, 0x1001);
		OSMO_ASSERT(nsvc != NULL);
		gprs_nsvc_delete(nsvc);

		gprs_dump_nsi(nsi);
	}

	gprs_ns_destroy(nsi);
	nsi = NULL;

	printf("--- Process timers ---\n\n");
	/* wait for rate_ctr_timer expiry */
	usleep(1100000);
	/* ensure termination */
	alarm(2);
	osmo_timers_update();
	alarm(0);
}

static void test_ignored_messages()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in peer[1] = {{0},};

	peer[0].sin_family = AF_INET;
	peer[0].sin_port = htons(1111);
	peer[0].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);

	printf("--- Send unexpected NS STATUS (should not be answered)---\n\n");
	/* Do not respond, see 3GPP TS 08.16, 7.5.1 */
	gprs_process_message(nsi, "STATUS", &peer[0],
			     gprs_ns_status_invalid_alive,
			     sizeof(gprs_ns_status_invalid_alive));

	printf("--- Send unexpected NS ALIVE ACK (should not be answered)---\n\n");
	/* Ignore this, see 3GPP TS 08.16, 7.4.1 */
	send_ns_alive_ack(nsi, &peer[0]);

	printf("--- Send unexpected NS RESET ACK (should not be answered)---\n\n");
	/* Ignore this, see 3GPP TS 08.16, 7.3.1 */
	send_ns_reset_ack(nsi, &peer[0], 0xe001, 0xe000);

	gprs_ns_destroy(nsi);
	nsi = NULL;
}

static void test_bss_port_changes()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in peer[4] = {{0},};

	peer[0].sin_family = AF_INET;
	peer[0].sin_port = htons(1111);
	peer[0].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);
	peer[1].sin_family = AF_INET;
	peer[1].sin_port = htons(2222);
	peer[1].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);
	peer[2].sin_family = AF_INET;
	peer[2].sin_port = htons(3333);
	peer[2].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);
	peer[3].sin_family = AF_INET;
	peer[3].sin_port = htons(4444);
	peer[3].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);

	printf("--- Setup, send BSSGP RESET ---\n\n");

	setup_ns(nsi, &peer[0], 0x1122, 0x1122);
	gprs_dump_nsi(nsi);
	gprs_process_message(nsi, "BSSGP RESET", &peer[0],
			     gprs_bssgp_reset, sizeof(gprs_bssgp_reset));

	printf("--- Peer port changes, RESET, message remains unchanged ---\n\n");

	send_ns_reset(nsi, &peer[1], NS_CAUSE_OM_INTERVENTION, 0x1122, 0x1122);
	gprs_dump_nsi(nsi);

	printf("--- Peer port changes, RESET, VCI changes ---\n\n");

	send_ns_reset(nsi, &peer[2], NS_CAUSE_OM_INTERVENTION, 0x3344, 0x1122);
	gprs_dump_nsi(nsi);

	printf("--- Peer port changes, RESET, NSEI changes ---\n\n");

	send_ns_reset(nsi, &peer[3], NS_CAUSE_OM_INTERVENTION, 0x1122, 0x3344);
	gprs_dump_nsi(nsi);

	printf("--- Peer port 3333, RESET, VCI is changed back ---\n\n");

	send_ns_reset(nsi, &peer[2], NS_CAUSE_OM_INTERVENTION, 0x1122, 0x1122);
	gprs_dump_nsi(nsi);

	printf("--- Peer port 4444, RESET, NSEI is changed back ---\n\n");

	send_ns_reset(nsi, &peer[3], NS_CAUSE_OM_INTERVENTION, 0x1122, 0x1122);
	gprs_dump_nsi(nsi);

	gprs_ns_destroy(nsi);
	nsi = NULL;
}

static void test_bss_reset_ack()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in peer[4] = {{0},};
	struct gprs_nsvc *nsvc;
	struct sockaddr_in *nse[4];
	int rc;

	peer[0].sin_family = AF_INET;
	peer[0].sin_port = htons(1111);
	peer[0].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);
	peer[1].sin_family = AF_INET;
	peer[1].sin_port = htons(2222);
	peer[1].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);
	peer[2].sin_family = AF_INET;
	peer[2].sin_port = htons(3333);
	peer[2].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);
	peer[3].sin_family = AF_INET;
	peer[3].sin_port = htons(4444);
	peer[3].sin_addr.s_addr = htonl(REMOTE_BSS_ADDR);

	nse[0] = &peer[0];
	nse[1] = &peer[1];

	printf("--- Setup VC 1 BSS -> SGSN ---\n\n");

	setup_ns(nsi, nse[0], 0x1001, 0x1000);
	gprs_dump_nsi(nsi);

	printf("--- Setup VC 2 BSS -> SGSN ---\n\n");

	setup_ns(nsi, nse[1], 0x2001, 0x2000);
	gprs_dump_nsi(nsi);

	printf("--- Setup VC 1 SGSN -> BSS ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, 0x1001);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	send_ns_reset_ack(nsi, nse[0], 0x1001, 0x1000);
	gprs_dump_nsi(nsi);

	printf("--- Exchange NSEI 1 + 2 links ---\n\n");

	nse[1] = &peer[0];
	nse[0] = &peer[1];

	printf("--- Setup VC 2 SGSN -> BSS (hits NSEI 1) ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, 0x2001);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	send_ns_reset_ack(nsi, nse[0], 0x1001, 0x1000);
	gprs_dump_nsi(nsi);

	printf("--- Setup VC 2 SGSN -> BSS (hits NSEI 2) ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, 0x2001);
	rc = gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	OSMO_ASSERT(rc < 0);

	printf("--- Setup VC 1 SGSN -> BSS (hits NSEI 1) ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, 0x1001);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	send_ns_reset_ack(nsi, nse[0], 0x1001, 0x1000);
	gprs_dump_nsi(nsi);

	printf("--- Setup VC 2 BSS -> SGSN ---\n\n");

	setup_ns(nsi, nse[1], 0x2001, 0x2000);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 3rd paragraph. */
	/* This is not rejected because the NSEI has been
	 * assigned dynamically and not by configuration.
	 * This is not strictly spec conformant. */

	printf("--- RESET with invalid NSEI, BSS -> SGSN ---\n\n");

	send_ns_reset(nsi, nse[0], NS_CAUSE_OM_INTERVENTION,
		      0x1001, 0xf000);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 2nd paragraph. */
	/* This is not rejected because the NSEI has been
	 * assigned dynamically and not by configuration.
	 * This is not strictly spec conformant. */

	printf("--- RESET with invalid NSVCI, BSS -> SGSN ---\n\n");

	send_ns_reset(nsi, nse[0], NS_CAUSE_OM_INTERVENTION,
		      0xf001, 0x1000);
	gprs_dump_nsi(nsi);

	printf("--- RESET with old NSEI, NSVCI, BSS -> SGSN ---\n\n");

	send_ns_reset(nsi, nse[0], NS_CAUSE_OM_INTERVENTION,
		      0x1001, 0x1000);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 5th paragraph. */

	printf("--- Unexpected RESET_ACK VC 1, BSS -> SGSN ---\n\n");

	send_ns_reset_ack(nsi, nse[0], 0x1001, 0x1000);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 4th paragraph. */

	printf("---  RESET_ACK with invalid NSEI, BSS -> SGSN ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, 0x1001);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	send_ns_reset_ack(nsi, nse[0], 0x1001, 0xf000);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 4th paragraph. */

	printf("---  RESET_ACK with invalid NSVCI, BSS -> SGSN ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, 0x1001);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	send_ns_reset_ack(nsi, nse[0], 0xf001, 0x1000);
	gprs_dump_nsi(nsi);

	/* Test crossing RESET and UNBLOCK_ACK */

	printf("---  RESET (BSS -> SGSN) crossing an UNBLOCK_ACK (SGSN -> BSS) ---\n\n");

	setup_ns(nsi, nse[0], 0x1001, 0x1000);
	nsvc = gprs_nsvc_by_nsvci(nsi, 0x1001);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	OSMO_ASSERT(nsvc->state & NSE_S_BLOCKED);
	OSMO_ASSERT(nsvc->state & NSE_S_RESET);
	send_ns_unblock_ack(nsi, nse[0]);
	gprs_dump_nsi(nsi);
	send_ns_reset_ack(nsi, nse[0], 0x1001, 0x1000);
	expire_nsvc_timer(nsvc);
	OSMO_ASSERT(nsvc->state & NSE_S_BLOCKED);
	OSMO_ASSERT(nsvc->state & NSE_S_RESET);
	send_ns_reset_ack(nsi, nse[0], 0x1001, 0x1000);
	gprs_dump_nsi(nsi);

	gprs_ns_destroy(nsi);
	nsi = NULL;
}


static void test_sgsn_reset()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in sgsn_peer= {0};
	struct gprs_nsvc *nsvc;

	sgsn_peer.sin_family = AF_INET;
	sgsn_peer.sin_port = htons(32000);
	sgsn_peer.sin_addr.s_addr = htonl(REMOTE_SGSN_ADDR);

	gprs_dump_nsi(nsi);

	printf("--- Setup SGSN connection, BSS -> SGSN ---\n\n");

	gprs_ns_nsip_connect(nsi, &sgsn_peer, SGSN_NSEI, SGSN_NSEI+1);
	send_ns_reset_ack(nsi, &sgsn_peer, SGSN_NSEI+1, SGSN_NSEI);
	send_ns_alive_ack(nsi, &sgsn_peer);
	send_ns_unblock_ack(nsi, &sgsn_peer);
	gprs_dump_nsi(nsi);

	printf("--- RESET, SGSN -> BSS ---\n\n");

	send_ns_reset(nsi, &sgsn_peer, NS_CAUSE_OM_INTERVENTION,
		      SGSN_NSEI+1, SGSN_NSEI);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 3rd paragraph. */

	printf("--- RESET with invalid NSEI, SGSN -> BSS ---\n\n");

	send_ns_reset(nsi, &sgsn_peer, NS_CAUSE_OM_INTERVENTION,
		      SGSN_NSEI+1, 0xf000);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 2nd paragraph. */

	printf("--- RESET with invalid NSVCI, SGSN -> BSS ---\n\n");

	send_ns_reset(nsi, &sgsn_peer, NS_CAUSE_OM_INTERVENTION,
		      0xf001, SGSN_NSEI);
	gprs_dump_nsi(nsi);

	printf("--- RESET, SGSN -> BSS ---\n\n");

	send_ns_reset(nsi, &sgsn_peer, NS_CAUSE_OM_INTERVENTION,
		      SGSN_NSEI+1, SGSN_NSEI);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 5th paragraph. */

	printf("--- Unexpected RESET_ACK VC 1, BSS -> SGSN ---\n\n");

	send_ns_reset_ack(nsi, &sgsn_peer, SGSN_NSEI+1, SGSN_NSEI);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 4th paragraph. */

	printf("---  RESET_ACK with invalid NSEI, BSS -> SGSN ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, SGSN_NSEI+1);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	send_ns_reset_ack(nsi, &sgsn_peer, SGSN_NSEI+1, 0xe000);
	gprs_dump_nsi(nsi);

	/* Test 3GPP TS 08.16, 7.3.1, 4th paragraph. */

	printf("---  RESET_ACK with invalid NSVCI, BSS -> SGSN ---\n\n");

	nsvc = gprs_nsvc_by_nsvci(nsi, SGSN_NSEI+1);
	gprs_nsvc_reset(nsvc, NS_CAUSE_OM_INTERVENTION);
	send_ns_reset_ack(nsi, &sgsn_peer, 0xe001, SGSN_NSEI);
	gprs_dump_nsi(nsi);


	gprs_ns_destroy(nsi);
	nsi = NULL;
}

static void test_sgsn_reset_invalid_state()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in sgsn_peer= {0};
	struct gprs_nsvc *nsvc;
	int retry;
	uint8_t dummy_sdu[] = {0x01, 0x02, 0x03, 0x04};

	sgsn_peer.sin_family = AF_INET;
	sgsn_peer.sin_port = htons(32000);
	sgsn_peer.sin_addr.s_addr = htonl(REMOTE_SGSN_ADDR);

	gprs_dump_nsi(nsi);

	printf("=== %s ===\n", __func__);
	printf("--- Setup SGSN connection, BSS -> SGSN ---\n\n");

	gprs_ns_nsip_connect(nsi, &sgsn_peer, SGSN_NSEI, SGSN_NSEI+1);
	OSMO_ASSERT(sent_pdu_type == NS_PDUT_RESET);
	send_ns_reset_ack(nsi, &sgsn_peer, SGSN_NSEI+1, SGSN_NSEI);
	OSMO_ASSERT(sent_pdu_type == NS_PDUT_ALIVE);
	send_ns_alive_ack(nsi, &sgsn_peer);
	OSMO_ASSERT(sent_pdu_type == NS_PDUT_UNBLOCK);
	send_ns_unblock_ack(nsi, &sgsn_peer);
	gprs_dump_nsi(nsi);
	nsvc = gprs_nsvc_by_nsvci(nsi, SGSN_NSEI+1);
	OSMO_ASSERT(nsvc->state == NSE_S_ALIVE);
	OSMO_ASSERT(nsvc->remote_state == NSE_S_ALIVE);

	printf("--- Time out local test procedure ---\n\n");

	OSMO_ASSERT(expire_nsvc_timer(nsvc) == NSVC_TIMER_TNS_TEST);
	OSMO_ASSERT(expire_nsvc_timer(nsvc) == NSVC_TIMER_TNS_ALIVE);

	for (retry = 1; retry <= nsi->timeout[NS_TOUT_TNS_ALIVE_RETRIES]; ++retry)
		OSMO_ASSERT(expire_nsvc_timer(nsvc) == NSVC_TIMER_TNS_ALIVE);

	OSMO_ASSERT(nsvc->state == NSE_S_BLOCKED);

	printf("--- Remote test procedure continues ---\n\n");

	send_ns_alive(nsi, &sgsn_peer);
	OSMO_ASSERT(sent_pdu_type == NS_PDUT_RESET);

	printf("--- Don't send a NS_RESET_ACK message (pretend it is lost) ---\n\n");

	sent_pdu_type = -1;
	send_ns_alive(nsi, &sgsn_peer);
	OSMO_ASSERT(sent_pdu_type == -1);

	send_ns_reset_ack(nsi, &sgsn_peer, SGSN_NSEI+1, SGSN_NSEI);
	OSMO_ASSERT(sent_pdu_type == NS_PDUT_ALIVE);

	send_ns_alive_ack(nsi, &sgsn_peer);
	OSMO_ASSERT(nsvc->state == (NSE_S_ALIVE | NSE_S_BLOCKED));
	OSMO_ASSERT(nsvc->remote_state == (NSE_S_ALIVE | NSE_S_BLOCKED));
	OSMO_ASSERT(sent_pdu_type == NS_PDUT_UNBLOCK);

	send_ns_unblock_ack(nsi, &sgsn_peer);
	OSMO_ASSERT(nsvc->state == NSE_S_ALIVE);
	OSMO_ASSERT(nsvc->remote_state == NSE_S_ALIVE);

	send_ns_unitdata(nsi, &sgsn_peer, 0x1234, dummy_sdu, sizeof(dummy_sdu));

	gprs_ns_destroy(nsi);
	nsi = NULL;
}

static void test_sgsn_output()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in sgsn_peer= {0};

	sgsn_peer.sin_family = AF_INET;
	sgsn_peer.sin_port = htons(32000);
	sgsn_peer.sin_addr.s_addr = htonl(REMOTE_SGSN_ADDR);

	gprs_dump_nsi(nsi);

	printf("--- Send message to SGSN ---\n\n");

	gprs_send_message(nsi, "BSSGP RESET", SGSN_NSEI, 0,
			     gprs_bssgp_reset+4, sizeof(gprs_bssgp_reset)-4);

	printf("--- Setup dead connection to SGSN ---\n\n");

	gprs_ns_nsip_connect(nsi, &sgsn_peer, SGSN_NSEI, SGSN_NSEI+1);
	gprs_dump_nsi(nsi);

	printf("--- Send message to SGSN ---\n\n");

	gprs_send_message(nsi, "BSSGP RESET", SGSN_NSEI, 0,
			     gprs_bssgp_reset+4, sizeof(gprs_bssgp_reset)-4);

	printf("--- Make connection to SGSN alive ---\n\n");

	send_ns_reset_ack(nsi, &sgsn_peer, SGSN_NSEI+1, SGSN_NSEI);
	send_ns_alive_ack(nsi, &sgsn_peer);
	gprs_dump_nsi(nsi);

	printf("--- Send message to SGSN ---\n\n");

	gprs_send_message(nsi, "BSSGP RESET", SGSN_NSEI, 0,
			     gprs_bssgp_reset+4, sizeof(gprs_bssgp_reset)-4);

	printf("--- Unblock connection to SGSN ---\n\n");

	send_ns_unblock_ack(nsi, &sgsn_peer);
	send_ns_alive(nsi, &sgsn_peer);
	gprs_dump_nsi(nsi);

	printf("--- Send message to SGSN ---\n\n");

	gprs_send_message(nsi, "BSSGP RESET", SGSN_NSEI, 0,
			     gprs_bssgp_reset+4, sizeof(gprs_bssgp_reset)-4);

	printf("--- Send empty message with BVCI to SGSN ---\n\n");

	gprs_send_message(nsi, "[empty]", SGSN_NSEI, 0x0102,
			     gprs_bssgp_reset, 0);


	gprs_ns_destroy(nsi);
	nsi = NULL;
}


int bssgp_prim_cb(struct osmo_prim_hdr *oph, void *ctx)
{
	return -1;
}

static struct log_info info = {};

int main(int argc, char **argv)
{
	osmo_init_logging(&info);
	log_set_use_color(osmo_stderr_target, 0);
	log_set_print_filename(osmo_stderr_target, 0);
	osmo_signal_register_handler(SS_L_NS, &test_signal, NULL);

	log_set_print_filename(osmo_stderr_target, 0);
	log_set_log_level(osmo_stderr_target, LOGL_INFO);

	setlinebuf(stdout);

	printf("===== NS protocol test START\n");
	test_nsvc();
	test_ignored_messages();
	test_bss_port_changes();
	test_bss_reset_ack();
	test_sgsn_reset();
	test_sgsn_reset_invalid_state();
	test_sgsn_output();
	printf("===== NS protocol test END\n\n");

	exit(EXIT_SUCCESS);
}
