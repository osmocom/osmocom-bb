/* Test routines for the BSSGP implementation in libosmogb
 *
 * (C) 2014 by sysmocom s.f.m.c. GmbH
 * Author: Jacob Erlbeck <jerlbeck@sysmocom.de>
 *
 * Skeleton based on bssgp_fc_test.c
 * (C) 2012 by Harald Welte <laforge@gnumonks.org>
 */

#undef _GNU_SOURCE
#define _GNU_SOURCE

#include <osmocom/core/application.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/prim.h>
#include <osmocom/gprs/gprs_bssgp.h>
#include <osmocom/gprs/gprs_ns.h>
#include <osmocom/gprs/gprs_bssgp_bss.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <dlfcn.h>

#define BSS_NSEI 0x0b55

static struct osmo_prim_hdr last_oph = {0};

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

	fprintf(stderr, "MESSAGE to 0x%08x, msg length %zu\n%s\n",
		dest_host, len, osmo_hexdump(buf, len));

	return len;
}

/* override */
int gprs_ns_callback(enum gprs_ns_evt event, struct gprs_nsvc *nsvc,
			 struct msgb *msg, uint16_t bvci)
{
	fprintf(stderr, "CALLBACK, event %d, msg length %td, bvci 0x%04x\n%s\n\n",
			event, msgb_bssgp_len(msg), bvci,
			osmo_hexdump(msgb_bssgph(msg), msgb_bssgp_len(msg)));
	return 0;
}

struct msgb *last_ns_tx_msg = NULL;

/* override */
int gprs_ns_sendmsg(struct gprs_ns_inst *nsi, struct msgb *msg)
{
	msgb_free(last_ns_tx_msg);
	last_ns_tx_msg = msg;

	return msgb_length(msg);
}

int bssgp_prim_cb(struct osmo_prim_hdr *oph, void *ctx)
{
	printf("BSSGP primitive, SAP %d, prim = %d, op = %d, msg = %s\n",
	       oph->sap, oph->primitive, oph->operation, msgb_hexdump(oph->msg));

	last_oph.sap = oph->sap;
	last_oph.primitive = oph->primitive;
	last_oph.operation = oph->operation;
	last_oph.msg = NULL;
	return -1;
}

static void msgb_bssgp_send_and_free(struct msgb *msg)
{
	msgb_nsei(msg) = BSS_NSEI;

	bssgp_rcvmsg(msg);

	msgb_free(msg);
}

static void send_bssgp_supend(enum bssgp_pdu_type pdu_type, uint32_t tlli)
{
	struct msgb *msg = bssgp_msgb_alloc();
	uint32_t tlli_be = htonl(tlli);
	uint8_t rai[] = {0x0f, 0xf1, 0x80, 0x20, 0x37, 0x00};

	msgb_v_put(msg, pdu_type);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, sizeof(tlli_be), (uint8_t *)&tlli_be);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, sizeof(rai), &rai[0]);

	msgb_bssgp_send_and_free(msg);
}

static void send_bssgp_resume(enum bssgp_pdu_type pdu_type, uint32_t tlli)
{
	struct msgb *msg = bssgp_msgb_alloc();
	uint32_t tlli_be = htonl(tlli);
	uint8_t rai[] = {0x0f, 0xf1, 0x80, 0x20, 0x37, 0x00};
	uint8_t suspend_ref = 1;

	msgb_v_put(msg, pdu_type);
	msgb_tvlv_put(msg, BSSGP_IE_TLLI, sizeof(tlli_be), (uint8_t *)&tlli_be);
	msgb_tvlv_put(msg, BSSGP_IE_ROUTEING_AREA, sizeof(rai), &rai[0]);
	msgb_tvlv_put(msg, BSSGP_IE_SUSPEND_REF_NR, 1, &suspend_ref);

	msgb_bssgp_send_and_free(msg);
}

static void test_bssgp_suspend_resume(void)
{
	const uint32_t tlli = 0xf0123456;

	printf("----- %s START\n", __func__);
	memset(&last_oph, 0, sizeof(last_oph));

	send_bssgp_supend(BSSGP_PDUT_SUSPEND, tlli);
	OSMO_ASSERT(last_oph.primitive == PRIM_BSSGP_GMM_SUSPEND);

	send_bssgp_resume(BSSGP_PDUT_RESUME, tlli);
	OSMO_ASSERT(last_oph.primitive == PRIM_BSSGP_GMM_RESUME);

	printf("----- %s END\n", __func__);
}

static void send_bssgp_status(enum gprs_bssgp_cause cause, uint16_t *bvci)
{
	struct msgb *msg = bssgp_msgb_alloc();
	uint8_t cause_ = cause;

	msgb_v_put(msg, BSSGP_PDUT_STATUS);
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, &cause_);
	if (bvci) {
		uint16_t bvci_ = htons(*bvci);
		msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &bvci_);
	}

	msgb_bssgp_send_and_free(msg);
}

static void test_bssgp_status(void)
{
	uint16_t bvci;

	printf("----- %s START\n", __func__);

	send_bssgp_status(BSSGP_CAUSE_PROTO_ERR_UNSPEC, NULL);
	OSMO_ASSERT(last_oph.primitive == PRIM_NM_STATUS);

	/* Enforce prim != PRIM_NM_STATUS */
	last_oph.primitive = PRIM_NM_LLC_DISCARDED;

	bvci = 1234;
	send_bssgp_status(BSSGP_CAUSE_UNKNOWN_BVCI, &bvci);
	OSMO_ASSERT(last_oph.primitive == PRIM_NM_STATUS);

	printf("----- %s END\n", __func__);
}

static void test_bssgp_bad_reset()
{
	struct msgb *msg;
	uint16_t bvci_be = htons(2);
	uint8_t cause = BSSGP_CAUSE_OML_INTERV;

	printf("----- %s START\n", __func__);
	msg = bssgp_msgb_alloc();

	msgb_v_put(msg, BSSGP_PDUT_BVC_RESET);
	msgb_tvlv_put(msg, BSSGP_IE_BVCI, sizeof(bvci_be), (uint8_t *)&bvci_be);
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, sizeof(cause), &cause);

	msgb_bvci(msg) = 0xbad;

	msgb_bssgp_send_and_free(msg);

	printf("----- %s END\n", __func__);
}

static void test_bssgp_flow_control_bvc(void)
{
	struct bssgp_bvc_ctx bctx = {
		.nsei = 0x1234,
		.bvci = 0x5678,
	};
	const uint8_t  tag = 42;
	const uint32_t bmax = 0x1022 * 100;
	const uint32_t rate = 0xc040 / 8 * 100;
	const uint32_t bmax_ms = bmax / 2;
	const uint32_t rate_ms = rate / 2;
	uint8_t  ratio = 0x78;
	uint32_t qdelay = 0x1144 * 10;
	int rc;

	static uint8_t expected_simple_msg[] = {
		0x26,
		0x1e, 0x81, 0x2a,		/* tag */
		0x05, 0x82, 0x10, 0x22,		/* Bmax */
		0x03, 0x82, 0xc0, 0x40,		/* R */
		0x01, 0x82, 0x08, 0x11,		/* Bmax_MS */
		0x1c, 0x82, 0x60, 0x20,		/* R_MS */
	};

	static uint8_t expected_ext_msg[] = {
		0x26,
		0x1e, 0x81, 0x2a,		/* tag */
		0x05, 0x82, 0x10, 0x22,		/* Bmax */
		0x03, 0x82, 0xc0, 0x40,		/* R */
		0x01, 0x82, 0x08, 0x11,		/* Bmax_MS */
		0x1c, 0x82, 0x60, 0x20,		/* R_MS */
		0x3c, 0x81, 0x78,		/* ratio */
		0x06, 0x82, 0x11, 0x44,		/* Qdelay */
	};

	printf("----- %s START\n", __func__);

	rc = bssgp_tx_fc_bvc(&bctx, tag, bmax, rate, bmax_ms, rate_ms,
		NULL, NULL);

	OSMO_ASSERT(rc >= 0);
	OSMO_ASSERT(last_ns_tx_msg != NULL);
	printf("Got message: %s\n", msgb_hexdump(last_ns_tx_msg));
	OSMO_ASSERT(msgb_length(last_ns_tx_msg) == sizeof(expected_simple_msg));
	OSMO_ASSERT(0 == memcmp(msgb_data(last_ns_tx_msg),
			expected_simple_msg, sizeof(expected_simple_msg)));

	rc = bssgp_tx_fc_bvc(&bctx, tag, bmax, rate, bmax_ms, rate_ms,
		&ratio, &qdelay);

	OSMO_ASSERT(rc >= 0);
	OSMO_ASSERT(last_ns_tx_msg != NULL);
	printf("Got message: %s\n", msgb_hexdump(last_ns_tx_msg));
	OSMO_ASSERT(msgb_length(last_ns_tx_msg) == sizeof(expected_ext_msg));
	OSMO_ASSERT(0 == memcmp(msgb_data(last_ns_tx_msg),
			expected_ext_msg, sizeof(expected_ext_msg)));

	msgb_free(last_ns_tx_msg);
	last_ns_tx_msg = NULL;

	printf("----- %s END\n", __func__);
}

static void test_bssgp_msgb_copy()
{
	struct msgb *msg, *msg2;
	uint16_t bvci_be = htons(2);
	uint8_t cause = BSSGP_CAUSE_OML_INTERV;

	printf("----- %s START\n", __func__);
	msg = bssgp_msgb_alloc();

	msg->l3h = msgb_data(msg);
	msgb_v_put(msg, BSSGP_PDUT_BVC_RESET);
	msgb_tvlv_put(msg, BSSGP_IE_BVCI, sizeof(bvci_be), (uint8_t *)&bvci_be);
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, sizeof(cause), &cause);

	msgb_bvci(msg) = 0xbad;
	msgb_nsei(msg) = 0xbee;

	printf("Old msgb: %s\n", msgb_hexdump(msg));
	msg2 = bssgp_msgb_copy(msg, "test");
	printf("New msgb: %s\n", msgb_hexdump(msg2));

	OSMO_ASSERT(msgb_bvci(msg2) == 0xbad);
	OSMO_ASSERT(msgb_nsei(msg2) == 0xbee);
	OSMO_ASSERT(msgb_l3(msg2) == msgb_data(msg2));
	OSMO_ASSERT(msgb_bssgph(msg2) == msgb_data(msg2));
	OSMO_ASSERT(msgb_bssgp_len(msg2) == msgb_length(msg2));

	msgb_free(msg);
	msgb_free(msg2);

	printf("----- %s END\n", __func__);
}

static struct log_info info = {};

int main(int argc, char **argv)
{
	struct sockaddr_in bss_peer= {0};

	osmo_init_logging(&info);
	log_set_use_color(osmo_stderr_target, 0);
	log_set_print_filename(osmo_stderr_target, 0);

	bssgp_nsi = gprs_ns_instantiate(gprs_ns_callback, NULL);

	bss_peer.sin_family = AF_INET;
	bss_peer.sin_port = htons(32000);
	bss_peer.sin_addr.s_addr = htonl(0x7f0000ff);

	gprs_ns_nsip_connect(bssgp_nsi, &bss_peer, BSS_NSEI, BSS_NSEI+1);


	printf("===== BSSGP test START\n");
	test_bssgp_suspend_resume();
	test_bssgp_status();
	test_bssgp_bad_reset();
	test_bssgp_flow_control_bvc();
	test_bssgp_msgb_copy();
	printf("===== BSSGP test END\n\n");

	exit(EXIT_SUCCESS);
}
