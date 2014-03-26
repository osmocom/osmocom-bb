/*
 * (C) 2011 by Holger Hans Peter Freyther
 * (C) 2011 by On-Waves
 * (C) 2014 by Daniel Willmann <dwillmann@sysmocom.de>
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

#include <osmocom/core/application.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/lapdm.h>
#include <osmocom/gsm/rsl.h>

#include <errno.h>

#include <string.h>

#define CHECK_RC(rc)	\
	if (rc != 0) {	\
		printf("Operation failed rc=%d on %s:%d\n", rc, __FILE__, __LINE__); \
		abort(); \
	}

static struct log_info info = {};
static int dummy_l1_header_len = 0;

struct lapdm_polling_state {
	struct lapdm_channel *bts;
	int bts_read;

	struct lapdm_channel *ms;
	int ms_read;
};

static struct msgb *msgb_from_array(const uint8_t *data, int len)
{
	struct msgb *msg = msgb_alloc_headroom(4096, 128, "data");
	msg->l3h = msgb_put(msg, len);
	memcpy(msg->l3h, data, len);
	return msg;
}

/*
 * Test data is below...
 */
static const uint8_t cm[] = {
	0x05, 0x24, 0x31, 0x03, 0x50, 0x18, 0x93, 0x08,
	0x29, 0x47, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80,
};

static const uint8_t ua[] = {
	0x01, 0x73, 0x41, 0x05, 0x24, 0x31, 0x03, 0x50,
	0x18, 0x93, 0x08, 0x29, 0x47, 0x80, 0x00, 0x00,
	0x00, 0x00, 0x80, 0x2b, 0x2b, 0x2b, 0x2b
};

static const uint8_t mm[] = {
	0x00, 0x0c, 0x00, 0x03, 0x01, 0x01, 0x20, 0x02,
	0x00, 0x0b, 0x00, 0x03, 0x05, 0x04, 0x0d
};

static const uint8_t dummy1[] = {
	0xab, 0x03, 0x30, 0x60, 0x06,
};

static const uint8_t rel_req[] = {
	0x02, 0x07, 0x01, 0x0a, 0x02, 0x40, 0x14, 0x01
};

static const uint8_t est_req_sdcch_sapi3[] = {
	0x02, 0x04, 0x01, 0x20, 0x02, 0x03
};

static const uint8_t est_req_sacch_sapi3[] = {
	0x02, 0x04, 0x01, 0x0b, 0x02, 0x43
};

static struct msgb *create_cm_serv_req(void)
{
	struct msgb *msg;

	msg = msgb_from_array(cm, sizeof(cm));
	rsl_rll_push_l3(msg, RSL_MT_EST_REQ, 0, 0, 1);
	msgb_push(msg, dummy_l1_header_len);
	return msg;
}

static struct msgb *create_mm_id_req(void)
{
	struct msgb *msg;

	msg = msgb_from_array(mm, sizeof(mm));
	msg->l2h = msg->data + 3;
	OSMO_ASSERT(msgb_l2len(msg) == 12);
	msg->l3h = msg->l2h + 6;
	OSMO_ASSERT(msgb_l3len(msg) == 6);
	msgb_push(msg, dummy_l1_header_len);

	return msg;
}

static struct msgb *create_empty_msg(void)
{
	struct msgb *msg;

	msg = msgb_from_array(NULL, 0);
	OSMO_ASSERT(msgb_l3len(msg) == 0);
	rsl_rll_push_l3(msg, RSL_MT_DATA_REQ, 0, 0, 1);
	msgb_push(msg, dummy_l1_header_len);
	return msg;
}

static struct msgb *create_dummy_data_req(void)
{
	struct msgb *msg;

	msg = msgb_from_array(dummy1, sizeof(dummy1));
	rsl_rll_push_l3(msg, RSL_MT_DATA_REQ, 0, 0, 1);
	msgb_push(msg, dummy_l1_header_len);
	return msg;
}

static struct msgb *create_rel_req(void)
{
	struct msgb *msg;

	msg = msgb_from_array(rel_req, sizeof(rel_req));
	msg->l2h = msg->data;
	msgb_push(msg, dummy_l1_header_len);
	msg->l3h = msg->l2h + sizeof(struct abis_rsl_rll_hdr);
	return msg;
}

static struct msgb *create_est_req(const uint8_t *est_req, size_t est_req_size)
{
	struct msgb *msg;

	msg = msgb_from_array(est_req, est_req_size);
	msg->l2h = msg->data;
	msgb_push(msg, dummy_l1_header_len);
	msg->l3h = msg->l2h + sizeof(struct abis_rsl_rll_hdr);
	return msg;
}

static int send(struct msgb *in_msg, struct lapdm_channel *chan)
{
	struct osmo_phsap_prim pp;
	struct msgb *msg;
	int rc;

	msg = msgb_alloc_headroom(128, 64, "PH-DATA.ind");
	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_DATA,
			PRIM_OP_INDICATION, msg);
	/* copy over actual MAC block */
	msg->l2h = msgb_put(msg, msgb_l2len(in_msg));
	memcpy(msg->l2h, in_msg->l2h, msgb_l2len(in_msg));

	/* LAPDm requires those... */
	pp.u.data.chan_nr = 0;
	pp.u.data.link_id = 0;
        /* feed into the LAPDm code of libosmogsm */
        rc = lapdm_phsap_up(&pp.oph, &chan->lapdm_dcch);
	OSMO_ASSERT(rc == 0 || rc == -EBUSY);
	return 0;
}

/* Receive from L1 */
static int send_buf(const uint8_t *buf, size_t len, struct lapdm_channel *chan)
{
	struct osmo_phsap_prim pp;
	struct msgb *msg;
	int rc;

	msg = msgb_from_array(buf, len);
	msg->l2h = msg->l3h;
	msg->l3h = NULL;
	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_DATA,
			PRIM_OP_INDICATION, msg);

	/* LAPDm requires those... */
	pp.u.data.chan_nr = 0;
	pp.u.data.link_id = 0;
        /* feed into the LAPDm code of libosmogsm */
        rc = lapdm_phsap_up(&pp.oph, &chan->lapdm_dcch);
	OSMO_ASSERT(rc == 0 || rc == -EBUSY);
	return 0;
}

/* Receive from L3 */
static int enqueue_buf(const uint8_t *buf, size_t len, int sapi, struct lapdm_channel *chan)
{
	struct osmo_dlsap_prim dp;
	struct msgb *msg;
	int rc;
	struct lapdm_datalink *dl;

	dl = lapdm_datalink_for_sapi(&chan->lapdm_dcch, sapi);
	OSMO_ASSERT(dl);

	msg = msgb_from_array(buf, len);
	osmo_prim_init(&dp.oph, 0, PRIM_DL_DATA, PRIM_OP_REQUEST, msg);

	rc = lapd_recv_dlsap(&dp, &dl->dl.lctx);
	OSMO_ASSERT(rc == 0 || rc == -EBUSY);
	return 0;
}

static int send_sabm(struct lapdm_channel *chan, int sapi, const uint8_t *data, size_t len)
{
	struct osmo_phsap_prim pp;
	struct msgb *msg;
	int rc;

	OSMO_ASSERT(sapi == 0 || sapi == 3);

	msg = msgb_alloc_headroom(128, 64, "PH-DATA.ind");
	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_DATA,
			PRIM_OP_INDICATION, msg);
	/* copy over actual MAC block */
	msg->l2h = msgb_put(msg, 3 + len);
	msg->l2h[0] = 0x01 | (sapi << 2);
	msg->l2h[1] = 0x3f;
	msg->l2h[2] = 0x01 | (len << 2);

	if (len > 0)
		memcpy(msg->l2h + 3, data, len);

	/* LAPDm requires those... */
	pp.u.data.chan_nr = 0;
	pp.u.data.link_id = 0;
        /* feed into the LAPDm code of libosmogsm */
        rc = lapdm_phsap_up(&pp.oph, &chan->lapdm_dcch);
	OSMO_ASSERT(rc == 0 || rc == -EBUSY);
	return 0;
}

/*
 * I get called from the LAPDm code when something was sent my way...
 */
static int bts_to_ms_tx_cb(struct msgb *in_msg, struct lapdm_entity *le, void *_ctx)
{
	struct lapdm_polling_state *state = _ctx;


	printf("%s: MS->BTS(us) message %d\n", __func__, msgb_length(in_msg));


	if (state->bts_read == 0) {
		printf("BTS: Verifying CM request.\n");
		OSMO_ASSERT(msgb_l3len(in_msg) == ARRAY_SIZE(cm));
		OSMO_ASSERT(memcmp(in_msg->l3h, cm,
			ARRAY_SIZE(cm)) == 0);
	} else if (state->bts_read == 1) {
		printf("BTS: Verifying dummy message.\n");
		OSMO_ASSERT(msgb_l3len(in_msg) == ARRAY_SIZE(dummy1));
		OSMO_ASSERT(memcmp(in_msg->l3h, dummy1,
			ARRAY_SIZE(dummy1)) == 0);
	} else {
		printf("BTS: Do not know to verify: %d\n", state->bts_read);
	}

	state->bts_read += 1;
	msgb_free(in_msg);

	return 0;
}

static int ms_to_bts_l1_cb(struct osmo_prim_hdr *oph, void *_ctx)
{
	int rc;
	struct lapdm_polling_state *state = _ctx;
	printf("%s: MS(us) -> BTS prim message\n", __func__);

	/* i stuff it into the LAPDm channel of the BTS */
	rc = send(oph->msg, state->bts);
	msgb_free(oph->msg);
	return rc;
}

static int dequeue_prim(struct lapdm_entity *le, struct osmo_phsap_prim *pp,
			const char *queue_name)
{
	int rc;
	int l2_header_len;
	int l3_len = 0;

	/* Take message from queue */
	rc = lapdm_phsap_dequeue_prim(le, pp);

	fprintf(stderr, "dequeue: got rc %d: %s\n", rc,
		rc <= 0 ? strerror(-rc) : "-");

	if (rc < 0)
		return rc;

	l2_header_len = msgb_l2len(pp->oph.msg);
	if (msgb_l3(pp->oph.msg)) {
		l3_len = msgb_l3len(pp->oph.msg);
		l2_header_len -= l3_len;
	} else
		fprintf(stderr, "MSGB: L3 is undefined\n");

	if (l2_header_len < 0 || l2_header_len > pp->oph.msg->data_len) {
		fprintf(stderr,
			"MSGB inconsistent: data = %p, l2 = %p, l3 = %p, tail = %p\n",
			pp->oph.msg->data,
			pp->oph.msg->l2h,
			pp->oph.msg->l3h,
			pp->oph.msg->tail);
		l2_header_len = -1;
	}

	printf("Took message from %s queue: "
	       "L2 header size %d, L3 size %d, "
	       "SAP %#x, %d/%d, Link 0x%02x\n",
	       queue_name,
	       l2_header_len, l3_len,
	       pp->oph.sap, pp->oph.primitive, pp->oph.operation,
	       pp->u.data.link_id);
	printf("Message: %s\n", msgb_hexdump(pp->oph.msg));

	return rc;
}

static int ms_to_bts_tx_cb(struct msgb *msg, struct lapdm_entity *le, void *_ctx)
{
	struct lapdm_polling_state *state = _ctx;

	printf("%s: BTS->MS(us) message %d\n", __func__, msgb_length(msg));

	if (state->ms_read == 0) {
		struct abis_rsl_rll_hdr hdr;

		printf("MS: Verifying incoming primitive.\n");
		OSMO_ASSERT(msg->len == sizeof(struct abis_rsl_rll_hdr) + 3);

		/* verify the header */
		memset(&hdr, 0, sizeof(hdr));
		rsl_init_rll_hdr(&hdr, RSL_MT_EST_CONF);
		hdr.c.msg_discr |= ABIS_RSL_MDISC_TRANSP;
		OSMO_ASSERT(memcmp(msg->data, &hdr, sizeof(hdr)) == 0);

		/* Verify the added RSL_IE_L3_INFO but we have a bug here */
		OSMO_ASSERT(msg->data[6] == RSL_IE_L3_INFO);
		#warning "RSL_IE_L3_INFO 16 bit length is wrong"
		/* This should be okay but it is actually 0x0, 0x9c on ia-32 */
		/* OSMO_ASSERT(msg->data[7] == 0x0 && msg->data[8] == 0x0); */
	} else if (state->ms_read == 1) {
		printf("MS: Verifying incoming MM message: %d\n", msgb_l3len(msg));
		OSMO_ASSERT(msgb_l3len(msg) == 3);
		OSMO_ASSERT(memcmp(msg->l3h, &mm[12], msgb_l3len(msg)) == 0);
	} else {
		printf("MS: Do not know to verify: %d\n", state->ms_read);
	}

	state->ms_read += 1;
	msgb_free(msg);
	return 0;
}

static void test_lapdm_polling()
{
	printf("I do some very simple LAPDm test.\n");

	int rc;
	struct lapdm_polling_state test_state;
	struct osmo_phsap_prim pp;

	/* Configure LAPDm on both sides */
	struct lapdm_channel bts_to_ms_channel;
	struct lapdm_channel ms_to_bts_channel;
	memset(&bts_to_ms_channel, 0, sizeof(bts_to_ms_channel));
	memset(&ms_to_bts_channel, 0, sizeof(ms_to_bts_channel));

	memset(&test_state, 0, sizeof(test_state));
	test_state.bts = &bts_to_ms_channel;
	test_state.ms = &ms_to_bts_channel;

	/* BTS to MS in polling mode */
	lapdm_channel_init(&bts_to_ms_channel, LAPDM_MODE_BTS);
        lapdm_channel_set_flags(&bts_to_ms_channel, LAPDM_ENT_F_POLLING_ONLY);
        lapdm_channel_set_l1(&bts_to_ms_channel, NULL, &test_state);
        lapdm_channel_set_l3(&bts_to_ms_channel, bts_to_ms_tx_cb, &test_state);

	/* MS to BTS in direct mode */
	lapdm_channel_init(&ms_to_bts_channel, LAPDM_MODE_MS);
	lapdm_channel_set_l1(&ms_to_bts_channel, ms_to_bts_l1_cb, &test_state);
	lapdm_channel_set_l3(&ms_to_bts_channel, ms_to_bts_tx_cb, &test_state);

	/*
	 * We try to send messages from the MS to the BTS to the MS..
	 */
	/* 1. Start with MS -> BTS, BTS should have a pending message */
	printf("Establishing link.\n");
	lapdm_rslms_recvmsg(create_cm_serv_req(), &ms_to_bts_channel);

	/* 2. Poll on the BTS for sending out a confirmation */
	printf("\nConfirming\n");
	OSMO_ASSERT(test_state.bts_read == 1);
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(pp.oph.msg->data == pp.oph.msg->l2h);
	send(pp.oph.msg, &ms_to_bts_channel);
	msgb_free(pp.oph.msg);
	OSMO_ASSERT(test_state.ms_read == 1);

	/* 3. Send some data to the MS */
	printf("\nSending back to MS\n");
	lapdm_rslms_recvmsg(create_mm_id_req(), &bts_to_ms_channel);
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	send(pp.oph.msg, &ms_to_bts_channel);
	msgb_free(pp.oph.msg);
	OSMO_ASSERT(test_state.ms_read == 2);

	/* verify that there is nothing more to poll */
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	OSMO_ASSERT(rc < 0);

	/* 3. And back to the BTS */
	printf("\nSending back to BTS\n");
	OSMO_ASSERT(test_state.ms_read == 2);
	lapdm_rslms_recvmsg(create_dummy_data_req(), &ms_to_bts_channel);


	/* 4. And back to the MS, but let's move data/l2h apart */
	OSMO_ASSERT(test_state.bts_read == 2);
	OSMO_ASSERT(test_state.ms_read == 2);
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	send(pp.oph.msg, &ms_to_bts_channel);
	OSMO_ASSERT(test_state.ms_read == 2);
	msgb_free(pp.oph.msg);

	/* verify that there is nothing more to poll */
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	OSMO_ASSERT(rc < 0);
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_acch, &pp, "ACCH");
	OSMO_ASSERT(rc < 0);

	/* check sending an empty L3 message fails */
	rc = lapdm_rslms_recvmsg(create_empty_msg(), &bts_to_ms_channel);
	OSMO_ASSERT(rc == -1);
	OSMO_ASSERT(test_state.ms_read == 2);

	/* clean up */
	lapdm_channel_exit(&bts_to_ms_channel);
	lapdm_channel_exit(&ms_to_bts_channel);

	/* Check if exit is idempotent */
	lapdm_channel_exit(&bts_to_ms_channel);
	lapdm_channel_exit(&ms_to_bts_channel);
}

static void test_lapdm_contention_resolution()
{
	printf("I test contention resultion by having two mobiles collide and "
		"first mobile repeating SABM.\n");

	int rc;
	struct lapdm_polling_state test_state;
	struct osmo_phsap_prim pp;
	uint8_t *cm2;

	/* Configure LAPDm on both sides */
	struct lapdm_channel bts_to_ms_channel;
	memset(&bts_to_ms_channel, 0, sizeof(bts_to_ms_channel));

	memset(&test_state, 0, sizeof(test_state));
	test_state.bts = &bts_to_ms_channel;

	/* BTS to MS in polling mode */
	lapdm_channel_init(&bts_to_ms_channel, LAPDM_MODE_BTS);
	lapdm_channel_set_flags(&bts_to_ms_channel, LAPDM_ENT_F_POLLING_ONLY);
	lapdm_channel_set_l1(&bts_to_ms_channel, NULL, &test_state);
	lapdm_channel_set_l3(&bts_to_ms_channel, bts_to_ms_tx_cb, &test_state);

	/* Send SABM MS 1, we must get UA */
	send_sabm(&bts_to_ms_channel, 0, cm, sizeof(cm));
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, ua, ARRAY_SIZE(ua)) == 0);

	/* Send SABM MS 2, we must get nothing, due to collision */
	cm2 = malloc(sizeof(cm));
	memcpy(cm2, cm, sizeof(cm));
	cm2[0] += 1;
	send_sabm(&bts_to_ms_channel, 0, cm2, sizeof(cm2));
	free(cm2);
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	OSMO_ASSERT(rc == -ENODEV);

	/* Send SABM MS 1 again, we must get UA gain */
	send_sabm(&bts_to_ms_channel, 0, cm, sizeof(cm));
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, ua, ARRAY_SIZE(ua)) == 0);

	/* clean up */
	lapdm_channel_exit(&bts_to_ms_channel);

	/* idempotent */
	lapdm_channel_exit(&bts_to_ms_channel);
}

static void test_lapdm_early_release()
{
	printf("I test RF channel release of an unestablished channel.\n");

	int rc;
	struct lapdm_polling_state test_state;

	/* Configure LAPDm on both sides */
	struct lapdm_channel bts_to_ms_channel;
	memset(&bts_to_ms_channel, 0, sizeof(bts_to_ms_channel));

	memset(&test_state, 0, sizeof(test_state));
	test_state.bts = &bts_to_ms_channel;

	/* BTS to MS in polling mode */
	lapdm_channel_init(&bts_to_ms_channel, LAPDM_MODE_BTS);
	lapdm_channel_set_flags(&bts_to_ms_channel, LAPDM_ENT_F_POLLING_ONLY);
	lapdm_channel_set_l1(&bts_to_ms_channel, NULL, &test_state);
	lapdm_channel_set_l3(&bts_to_ms_channel, bts_to_ms_tx_cb, &test_state);

	/* Send the release request */
	rc = lapdm_rslms_recvmsg(create_rel_req(), &bts_to_ms_channel);
	OSMO_ASSERT(rc == -EINVAL);

	/* clean up */
	lapdm_channel_exit(&bts_to_ms_channel);

	/* Check if exit is idempotent */
	lapdm_channel_exit(&bts_to_ms_channel);
}

static void lapdm_establish(const uint8_t *est_req, size_t est_req_size)
{
	int rc;
	struct lapdm_polling_state test_state;
	struct osmo_phsap_prim pp;
	struct msgb *msg;

	/* Configure LAPDm on both sides */
	struct lapdm_channel bts_to_ms_channel;
	memset(&bts_to_ms_channel, 0, sizeof(bts_to_ms_channel));

	memset(&test_state, 0, sizeof(test_state));
	test_state.bts = &bts_to_ms_channel;

	/* BTS to MS in polling mode */
	lapdm_channel_init(&bts_to_ms_channel, LAPDM_MODE_BTS);
	lapdm_channel_set_flags(&bts_to_ms_channel, LAPDM_ENT_F_POLLING_ONLY);
	lapdm_channel_set_l1(&bts_to_ms_channel, NULL, &test_state);
	lapdm_channel_set_l3(&bts_to_ms_channel, bts_to_ms_tx_cb, &test_state);

	/* Send the release request */
	msg = create_est_req(est_req, est_req_size);
	rc = lapdm_rslms_recvmsg(msg, &bts_to_ms_channel);
	fprintf(stderr, "recvmsg: got rc %d: %s\n", rc, rc <= 0 ? strerror(-rc) : "???");
	OSMO_ASSERT(rc == 0);

	/* Take message from queue */
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	if (rc < 0)
		rc = dequeue_prim(&bts_to_ms_channel.lapdm_acch, &pp, "ACCH");

	CHECK_RC(rc);

	OSMO_ASSERT(pp.oph.msg->data == msgb_l2(pp.oph.msg));

	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	OSMO_ASSERT(rc < 0);
	rc = dequeue_prim(&bts_to_ms_channel.lapdm_acch, &pp, "ACCH");
	OSMO_ASSERT(rc < 0);

	/* clean up */
	lapdm_channel_exit(&bts_to_ms_channel);

	/* idempotent */
	lapdm_channel_exit(&bts_to_ms_channel);
}

static void test_lapdm_establishment()
{
	printf("I test RF channel establishment.\n");
	printf("Testing SAPI3/SDCCH\n");
	lapdm_establish(est_req_sdcch_sapi3, sizeof(est_req_sdcch_sapi3));
	printf("Testing SAPI3/SACCH\n");
	lapdm_establish(est_req_sacch_sapi3, sizeof(est_req_sacch_sapi3));
}


const uint8_t cm_chg[] = {
	0x01, 0x00, 0x49, 0x06, 0x16, 0x03, 0x33,
	0x59, 0xa6, 0x20, 0x0a, 0x20, 0x04, 0x04,
	0x2f, 0x65, 0x23, 0x02, 0x00, 0x24, 0x04
};

const uint8_t cm_chg_ack[] = {
	0x01, 0x21, 0x01
};

const uint8_t gprs_susp[] = {
	0x01, 0x02, 0x35, 0x06, 0x34, 0xe3, 0xd4,
	0xd2, 0x6f, 0x09, 0xf1, 0x07, 0x00, 0x01, 0x00, 0x02
};

const uint8_t cipher_cmd[] = {
	0x06, 0x35, 0x01
};

/* The cipher command we send to the MS after updating our N(R) */
const uint8_t cipher_cmd_out[] = {
	0x03, 0x40, 0x0d, 0x06, 0x35, 0x01
};

uint8_t cipher_compl[] = {
	0x01, 0x24, 0x09, 0x06, 0x32
};

uint8_t cipher_compl_ack[] = {
	0x01, 0x61, 0x01
};

static const uint8_t ua_sms[] = {
	0x0d, 0x73, 0x01
};

const uint8_t cp_data_1[] = {
	0x0d, 0x00, 0x53, 0x59, 0x01, 0x5c, 0x00,
	0x4c, 0x00, 0x06, 0x91, 0x86, 0x77, 0x07,
	0x00, 0xf9, 0x51, 0x11, 0x76, 0x05, 0x81, 0x29, 0x32
};

const uint8_t cp_data_1_ack[] = {
	0x0d, 0x21, 0x01
};

static int bts_to_ms_dummy_tx_cb(struct msgb *in_msg, struct lapdm_entity *le, void *_ctx)
{
	printf("%s: MS->BTS(us) message %d\n", __func__, msgb_length(in_msg));
	msgb_free(in_msg);

	return 0;
}

static void dump_queue(struct llist_head *head)
{
	struct msgb* msg;

	printf("\nDumping queue:\n");
	llist_for_each_entry(msg, head, list)
		printf("%s\n", msgb_hexdump(msg));
	printf("\n");
}

static void test_lapdm_desync()
{
	printf("I test if desync problems exist in LAPDm\n");

	int rc;
	struct osmo_phsap_prim pp;

	/* Configure LAPDm on both sides */
	struct lapdm_channel bts_to_ms_channel;
	memset(&bts_to_ms_channel, 0, sizeof(bts_to_ms_channel));

	/* BTS to MS in polling mode */
	lapdm_channel_init(&bts_to_ms_channel, LAPDM_MODE_BTS);
	lapdm_channel_set_flags(&bts_to_ms_channel, LAPDM_ENT_F_POLLING_ONLY);
	lapdm_channel_set_l1(&bts_to_ms_channel, NULL, NULL);
	lapdm_channel_set_l3(&bts_to_ms_channel, bts_to_ms_dummy_tx_cb, NULL);
	struct lapdm_datalink *dl = lapdm_datalink_for_sapi(&bts_to_ms_channel.lapdm_dcch, 0);
	dl->mctx.dl = dl;
	dl->dl.lctx.dl = &dl->dl;

	/* Send SABM MS 1, we must get UA */
	printf("\nEstablishing SAPI=0\n");
	send_sabm(&bts_to_ms_channel, 0, cm, sizeof(cm));

	dump_queue(&dl->dl.tx_queue);

	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, ua, ARRAY_SIZE(ua)) == 0);

	printf("\nSending Classmark Change\n");
	send_buf(cm_chg, sizeof(cm_chg), &bts_to_ms_channel);
	dump_queue(&dl->dl.tx_queue);

	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, cm_chg_ack, ARRAY_SIZE(cm_chg_ack)) == 0);

	printf("\nEnqueueing Ciphering Mode Command\n");
	enqueue_buf(cipher_cmd, sizeof(cipher_cmd), 0, &bts_to_ms_channel);
	dump_queue(&dl->dl.tx_queue);

	printf("\nSending GPRS Suspend Request\n");
	send_buf(gprs_susp, sizeof(gprs_susp), &bts_to_ms_channel);
	dump_queue(&dl->dl.tx_queue);

	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, cipher_cmd_out, ARRAY_SIZE(cipher_cmd_out)) == 0);

	printf("\nSending Cipher Mode Complete\n");
	send_buf(cipher_compl, sizeof(cipher_compl), &bts_to_ms_channel);
	dump_queue(&dl->dl.tx_queue);

	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, cipher_compl_ack, ARRAY_SIZE(cipher_compl_ack)) == 0);

	printf("\nEstablishing SAPI=3\n");
	send_sabm(&bts_to_ms_channel, 3, NULL, 0);
	dump_queue(&dl->dl.tx_queue);

	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, ua_sms, ARRAY_SIZE(ua_sms)) == 0);

	printf("\nSending CP-DATA\n");
	send_buf(cp_data_1, sizeof(cp_data_1), &bts_to_ms_channel);
	dump_queue(&dl->dl.tx_queue);

	rc = dequeue_prim(&bts_to_ms_channel.lapdm_dcch, &pp, "DCCH");
	CHECK_RC(rc);
	OSMO_ASSERT(memcmp(pp.oph.msg->l2h, cp_data_1_ack, ARRAY_SIZE(cp_data_1_ack)) == 0);

	/* clean up */
	lapdm_channel_exit(&bts_to_ms_channel);

	/* Check if exit is idempotent */
	lapdm_channel_exit(&bts_to_ms_channel);
}

int main(int argc, char **argv)
{
	osmo_init_logging(&info);

	/* Prevent the test from segfaulting */
	dummy_l1_header_len = 0;
	test_lapdm_polling();

	dummy_l1_header_len = 3;
	test_lapdm_early_release();
	test_lapdm_contention_resolution();
	test_lapdm_establishment();
	test_lapdm_desync();

	printf("Success.\n");

	return 0;
}
