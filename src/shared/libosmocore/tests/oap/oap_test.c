/* Test Osmocom Authentication Protocol */
/*
 * (C) 2016 by sysmocom s.f.m.c. GmbH
 * All Rights Reserved
 *
 * Author: Neels Hofmeyr
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
#include <osmocom/gsm/oap.h>

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static struct msgb *oap_encoded(const struct osmo_oap_message *oap_msg)
{
	struct msgb *msgb = msgb_alloc_headroom(1000, 64, __func__);
	OSMO_ASSERT(msgb);
	osmo_oap_encode(msgb, oap_msg);
	return msgb;
}

static bool encode_decode_makes_same_msg(struct osmo_oap_message *oap_msg)
{
	struct osmo_oap_message oap_msg_decoded;
	struct msgb *msgb;
	int rc;
	bool result;
	memset(&oap_msg_decoded, 0, sizeof(oap_msg_decoded));

	msgb = oap_encoded(oap_msg);
	printf("encoded message:\n%s\n",
	       osmo_hexdump((void*)msgb_l2(msgb), msgb_l2len(msgb)));
	rc = osmo_oap_decode(&oap_msg_decoded, msgb_l2(msgb), msgb_l2len(msgb));

	if (rc) {
		printf("osmo_oap_decode() returned error: %d\n", rc);
		result = false;
		goto free_msgb;
	}

	rc = memcmp(oap_msg, &oap_msg_decoded, sizeof(oap_msg_decoded));
	if (rc) {
		printf("decoded message mismatches encoded message\n");
		printf("original:\n%s\n",
		       osmo_hexdump((void*)oap_msg, sizeof(*oap_msg)));
		printf("en- and decoded:\n%s\n",
		       osmo_hexdump((void*)&oap_msg_decoded, sizeof(oap_msg_decoded)));
		result = false;
		goto free_msgb;
	}

	printf("ok\n");
	result = true;

free_msgb:
	talloc_free(msgb);
	return result;
}

static void test_oap_messages_dec_enc(void)
{
	printf("Testing OAP messages\n");

	struct osmo_oap_message oap_msg;

#define CLEAR() memset(&oap_msg, 0, sizeof(oap_msg))
#define CHECK() OSMO_ASSERT(encode_decode_makes_same_msg(&oap_msg))

	printf("- Register Request\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_REGISTER_REQUEST;
	oap_msg.client_id = 0x2342;
	CHECK();

	printf("- Register Error\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_REGISTER_ERROR;
	oap_msg.cause = GMM_CAUSE_PROTO_ERR_UNSPEC;
	CHECK();

	printf("- Register Result\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_REGISTER_RESULT;
	CHECK();

	printf("- Challenge Request, no rand, no autn\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_CHALLENGE_REQUEST;
	oap_msg.rand_present = 0;
	oap_msg.autn_present = 0;
	CHECK();

	printf("- Challenge Request, with rand, no autn\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_CHALLENGE_REQUEST;
	osmo_hexparse("0102030405060708090a0b0c0d0e0f10",
		      oap_msg.rand, 16);
	oap_msg.rand_present = 1;
	oap_msg.autn_present = 0;
	CHECK();

	printf("- Challenge Request, no rand, with autn\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_CHALLENGE_REQUEST;
	oap_msg.rand_present = 0;
	osmo_hexparse("cec4e3848a33000086781158ca40f136",
		      oap_msg.autn, 16);
	oap_msg.autn_present = 1;
	CHECK();

	printf("- Challenge Request, with rand, with autn\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_CHALLENGE_REQUEST;
	osmo_hexparse("0102030405060708090a0b0c0d0e0f10",
		      oap_msg.rand, 16);
	oap_msg.rand_present = 1;
	osmo_hexparse("cec4e3848a33000086781158ca40f136",
		      oap_msg.autn, 16);
	oap_msg.autn_present = 1;
	CHECK();

	printf("- Challenge Error\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_CHALLENGE_ERROR;
	oap_msg.cause = GMM_CAUSE_GSM_AUTH_UNACCEPT;
	CHECK();

	printf("- Challenge Result\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_CHALLENGE_RESULT;
	osmo_hexparse("0102030405060708",
		      oap_msg.xres, 8);
	oap_msg.xres_present = 1;
	CHECK();

	printf("- Sync Request\n");
	CLEAR();
	oap_msg.message_type = OAP_MSGT_SYNC_REQUEST;
	osmo_hexparse("102030405060708090a0b0c0d0e0f001",
		      oap_msg.auts, 16);
	oap_msg.auts_present = 1;
	CHECK();

	/* Sync Error and Sync Result are not used in OAP */
}

const struct log_info_cat default_categories[] = {
};

static struct log_info info = {
	.cat = default_categories,
	.num_cat = ARRAY_SIZE(default_categories),
};

int main(int argc, char **argv)
{
	osmo_init_logging(&info);

	test_oap_messages_dec_enc();

	printf("Done.\n");
	return EXIT_SUCCESS;
}
