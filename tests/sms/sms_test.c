/*
 * (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2010 by Nico Golde <nico@ngolde.de>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osmocom/core/msgb.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/utils.h>

struct test_case {
	const uint8_t *input;
	const uint16_t input_length;

	const uint8_t *expected;
	const uint16_t expected_length;
};

static const char simple_text[] = "test text";
static const uint8_t simple_enc[] = {
	0xf4, 0xf2, 0x9c, 0x0e, 0xa2, 0x97, 0xf1, 0x74
};

static const char escape_text[] = "!$ a more#^- complicated test@@?_\%! case";
static const uint8_t escape_enc[] = {
	0x21, 0x01, 0x28, 0x0c, 0x6a, 0xbf, 0xe5, 0xe5, 0xd1,
	0x86, 0xd2, 0x02, 0x8d, 0xdf, 0x6d, 0x38, 0x3b, 0x3d,
	0x0e, 0xd3, 0xcb, 0x64, 0x10, 0xbd, 0x3c, 0xa7, 0x03,
	0x00, 0xbf, 0x48, 0x29, 0x04, 0x1a, 0x87, 0xe7, 0x65,
};

static const struct test_case test_encode[] =
{
	{
		.input = simple_text,
		.expected = simple_enc,
		.expected_length = sizeof(simple_enc),
	},
	{
		.input = escape_text,
		.expected = escape_enc,
		.expected_length = sizeof(escape_enc),
	},
};

static const struct test_case test_decode[] =
{
	{
		.input = simple_enc,
		.input_length = sizeof(simple_enc),
		.expected = simple_text,
	},
	{
		.input = escape_enc,
		.input_length = sizeof(escape_enc),
		.expected = escape_text,
	},
};

int main(int argc, char** argv)
{
	printf("SMS testing\n");
	struct msgb *msg;
	uint8_t *sms;
	uint8_t i;

	uint8_t length;
	uint8_t coded[256];
	char result[256];

	/* test 7-bit encoding */
	for (i = 0; i < ARRAY_SIZE(test_encode); ++i) {
		memset(coded, 0x42, sizeof(coded));
		length = gsm_7bit_encode(coded, test_encode[i].input);

		if (length != test_encode[i].expected_length) {
			fprintf(stderr, "Failed to encode case %d. Got %d, expected %d\n",
				i, length, test_encode[i].expected_length);
			return -1;
		}

		if (memcmp(coded, test_encode[i].expected, length) != 0) {
			fprintf(stderr, "Encoded content does not match for %d\n",
				i);
			return -1;
		}
	}

	/* test 7-bit decoding */
	for (i = 0; i < ARRAY_SIZE(test_decode); ++i) {
		memset(result, 0x42, sizeof(coded));
		gsm_7bit_decode(result, test_decode[i].input,
				test_decode[i].input_length);

		if (strcmp(result, test_decode[i].expected) != 0) {
			fprintf(stderr, "Test case %d failed to decode.\n", i);
			return -1;
		}
	}

	printf("OK\n");
	return 0;
}
