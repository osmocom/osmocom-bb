/* test routines for NS connection handling
 * (C) 2013 by sysmocom s.f.m.c. GmbH
 * Author: Jacob Erlbeck <jerlbeck@sysmocom.de>
 */

#undef _GNU_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
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
	printf("CALLBACK, event %d, msg length %d, bvci 0x%04x\n%s\n\n",
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

	if (!real_sendto)
		real_sendto = dlsym(RTLD_NEXT, "sendto");

	if (((struct sockaddr_in *)dest_addr)->sin_addr.s_addr != htonl(0x01020304))
		return real_sendto(sockfd, buf, len, flags, dest_addr, addrlen);

	printf("RESPONSE, msg length %d\n%s\n\n", len, osmo_hexdump(buf, len));

	return len;
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
		fprintf(stderr, "message too long: %d\n", data_len);
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

static void gprs_dump_nsi(struct gprs_ns_inst *nsi)
{
	struct gprs_nsvc *nsvc;

	printf("Current NS-VCIs:\n");
	llist_for_each_entry(nsvc, &nsi->gprs_nsvcs, list) {
		struct sockaddr_in *peer = &(nsvc->ip.bts_addr);
		printf("    VCI 0x%04x, NSEI 0x%04x, peer 0x%08x:%d\n",
		       nsvc->nsvci, nsvc->nsei,
		       ntohl(peer->sin_addr.s_addr), ntohs(peer->sin_port)
		      );
		dump_rate_ctr_group(stdout, "        ", nsvc->ctrg);
	}
	printf("\n");
}

static void test_ns()
{
	struct gprs_ns_inst *nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);
	struct sockaddr_in peer[4] = {{0},};

	peer[0].sin_family = AF_INET;
	peer[0].sin_port = htons(1111);
	peer[0].sin_addr.s_addr = htonl(0x01020304);
	peer[1].sin_family = AF_INET;
	peer[1].sin_port = htons(2222);
	peer[1].sin_addr.s_addr = htonl(0x01020304);
	peer[2].sin_family = AF_INET;
	peer[2].sin_port = htons(3333);
	peer[2].sin_addr.s_addr = htonl(0x01020304);
	peer[3].sin_family = AF_INET;
	peer[3].sin_port = htons(4444);
	peer[3].sin_addr.s_addr = htonl(0x01020304);

	gprs_process_message(nsi, "RESET", &peer[0],
			     gprs_ns_reset, sizeof(gprs_ns_reset));
	gprs_dump_nsi(nsi);
	gprs_process_message(nsi, "ALIVE", &peer[0],
			     gprs_ns_alive, sizeof(gprs_ns_alive));
	gprs_process_message(nsi, "UNBLOCK", &peer[0],
			     gprs_ns_unblock, sizeof(gprs_ns_unblock));
	gprs_process_message(nsi, "BSSGP RESET", &peer[0],
			     gprs_bssgp_reset, sizeof(gprs_bssgp_reset));

	printf("--- Peer port changes, RESET, message remains unchanged ---\n\n");

	gprs_process_message(nsi, "RESET", &peer[1],
			     gprs_ns_reset, sizeof(gprs_ns_reset));
	gprs_dump_nsi(nsi);

	printf("--- Peer port changes, RESET, VCI changes ---\n\n");

	gprs_process_message(nsi, "RESET", &peer[2],
			     gprs_ns_reset_vci2, sizeof(gprs_ns_reset_vci2));
	gprs_dump_nsi(nsi);

	printf("--- Peer port changes, RESET, NSEI changes ---\n\n");

	gprs_process_message(nsi, "RESET", &peer[3],
			     gprs_ns_reset_nsei2, sizeof(gprs_ns_reset_nsei2));
	gprs_dump_nsi(nsi);

	printf("--- Peer port 3333, RESET, VCI is changed back ---\n\n");

	gprs_process_message(nsi, "RESET", &peer[2],
			     gprs_ns_reset, sizeof(gprs_ns_reset));
	gprs_dump_nsi(nsi);

	printf("--- Peer port 4444, RESET, NSEI is changed back ---\n\n");

	gprs_process_message(nsi, "RESET", &peer[3],
			     gprs_ns_reset, sizeof(gprs_ns_reset));
	gprs_dump_nsi(nsi);

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

	printf("===== NS protocol test START\n");
	test_ns();
	printf("===== NS protocol test END\n\n");

	exit(EXIT_SUCCESS);
}
