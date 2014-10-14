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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <netinet/ip.h>
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

	fprintf(stderr, "MESSAGE to 0x%08x, msg length %d\n%s\n",
		dest_host, len, osmo_hexdump(buf, len));

	return len;
}

/* override */
int gprs_ns_callback(enum gprs_ns_evt event, struct gprs_nsvc *nsvc,
			 struct msgb *msg, uint16_t bvci)
{
	fprintf(stderr, "CALLBACK, event %d, msg length %d, bvci 0x%04x\n%s\n\n",
			event, msgb_bssgp_len(msg), bvci,
			osmo_hexdump(msgb_bssgph(msg), msgb_bssgp_len(msg)));
	return 0;
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
	/* OSMO_ASSERT(last_oph.primitive == PRIM_BSSGP_GMM_SUSPEND); */

	send_bssgp_resume(BSSGP_PDUT_RESUME, tlli);
	/* OSMO_ASSERT(last_oph.primitive == PRIM_BSSGP_GMM_RESUME); */

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
	printf("===== BSSGP test END\n\n");

	exit(EXIT_SUCCESS);
}
