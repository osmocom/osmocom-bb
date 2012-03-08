/*
 * (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by On-Waves
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
#include <osmocom/gsm/gsm0480.h>
#include <osmocom/gsm/gsm_utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t ussd_request[] = {
	0x0b, 0x7b, 0x1c, 0x15, 0xa1, 0x13, 0x02, 0x01,
	0x03, 0x02, 0x01, 0x3b, 0x30, 0x0b, 0x04, 0x01,
	0x0f, 0x04, 0x06, 0x2a, 0xd5, 0x4c, 0x16, 0x1b,
	0x01, 0x7f, 0x01, 0x00
};

static int parse_ussd(const uint8_t *_data, int len)
{
	uint8_t *data;
	int rc;
	struct ss_request req;
	struct gsm48_hdr *hdr;

	data = malloc(len);
	memcpy(data, _data, len);
	hdr = (struct gsm48_hdr *) &data[0];
	rc = gsm0480_decode_ss_request(hdr, len, &req);
	free(data);

	return rc;
}

static int parse_mangle_ussd(const uint8_t *_data, int len)
{
	uint8_t *data;
	int rc;
	struct ss_request req;
	struct gsm48_hdr *hdr;

	data = malloc(len);
	memcpy(data, _data, len);
	hdr = (struct gsm48_hdr *) &data[0];
	hdr->data[1] = len - sizeof(*hdr) - 2;
	rc = gsm0480_decode_ss_request(hdr, len, &req);
	free(data);

	return rc;
}

struct log_info info = {};

static void test_7bit_ussd(const char *text, const char *encoded_hex, const char *appended_after_decode)
{
	uint8_t coded[256];
	char decoded[256];
	int octets_written;
	int buffer_size;
	int nchars;

	printf("original = %s\n", osmo_hexdump((uint8_t *)text, strlen(text)));
	gsm_7bit_encode_n_ussd(coded, sizeof(coded), text, &octets_written);
	printf("encoded = %s\n", osmo_hexdump(coded, octets_written));

	OSMO_ASSERT(strcmp(encoded_hex, osmo_hexdump_nospc(coded, octets_written)) == 0);

	gsm_7bit_decode_n_ussd(decoded, sizeof(decoded), coded, octets_written * 8 / 7);
	octets_written = strlen(decoded);
	printf("decoded = %s\n\n", osmo_hexdump((uint8_t *)decoded, octets_written));

	OSMO_ASSERT(strncmp(text, decoded, strlen(text)) == 0);
	OSMO_ASSERT(strcmp(appended_after_decode, decoded + strlen(text)) == 0);

	/* check buffer limiting */
	memset(decoded, 0xaa, sizeof(decoded));

	for (buffer_size = 1; buffer_size < sizeof(decoded) - 1; ++buffer_size)
	{
		nchars = gsm_7bit_decode_n_ussd(decoded, buffer_size, coded, octets_written * 8 / 7);
		OSMO_ASSERT(nchars <= buffer_size);
		OSMO_ASSERT(decoded[buffer_size] == (char)0xaa);
		OSMO_ASSERT(decoded[nchars] == '\0');
	}

	memset(coded, 0xaa, sizeof(coded));

	for (buffer_size = 0; buffer_size < sizeof(coded) - 1; ++buffer_size)
	{
		gsm_7bit_encode_n_ussd(coded, buffer_size, text, &octets_written);
		OSMO_ASSERT(octets_written <= buffer_size);
		OSMO_ASSERT(coded[buffer_size] == 0xaa);
	}
}

int main(int argc, char **argv)
{
	struct ss_request req;
	const int size = sizeof(ussd_request);
	int i;
	struct msgb *msg;

	osmo_init_logging(&info);

	gsm0480_decode_ss_request((struct gsm48_hdr *) ussd_request, size, &req);
	printf("Tested if it still works. Text was: %s\n", req.ussd_text);


	printf("Testing parsing a USSD request and truncated versions\n");

	for (i = size; i > sizeof(struct gsm48_hdr); --i) {
		int rc = parse_ussd(&ussd_request[0], i);
		printf("Result for %d is %d\n", rc, i);
	}

	printf("Mangling the container now\n");
	for (i = size; i > sizeof(struct gsm48_hdr) + 2; --i) {
		int rc = parse_mangle_ussd(&ussd_request[0], i);
		printf("Result for %d is %d\n", rc, i);
	}

	printf("<CR> case test for 7 bit encode\n");
	test_7bit_ussd("01234567",   "b0986c46abd96e",   "");
	test_7bit_ussd("0123456",    "b0986c46abd91a",   "");
	test_7bit_ussd("01234567\r", "b0986c46abd96e0d", "");
        /* The appended \r is compliant to GSM 03.38 section 6.1.2.3.1: */
	test_7bit_ussd("0123456\r",  "b0986c46abd91a0d", "\r");
	test_7bit_ussd("012345\r",   "b0986c46ab351a",   "");

	printf("Checking GSM 04.80 USSD message generation.\n");

	test_7bit_ussd("", "", "");
	msg = gsm0480_create_unstructuredSS_Notify (0x00, "");
	printf ("Created unstructuredSS_Notify (0x00): %s\n",
			osmo_hexdump(msgb_data(msg), msgb_length(msg)));
	msgb_free (msg);

	test_7bit_ussd("forty-two", "e6b79c9e6fd1ef6f", "");
	msg = gsm0480_create_unstructuredSS_Notify (0x42, "forty-two");
	printf ("Created unstructuredSS_Notify (0x42): %s\n",
			osmo_hexdump(msgb_data(msg), msgb_length(msg)));
	msgb_free (msg);
	return 0;
}
