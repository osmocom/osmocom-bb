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
	const uint16_t expected_octet_length;
	const uint16_t expected_septet_length;
	const uint8_t ud_hdr_ind;
};

static const char simple_text[] = "test text";
#define simple_septet_length 9
static const uint8_t simple_enc[] = {
	0xf4, 0xf2, 0x9c, 0x0e, 0xa2, 0x97, 0xf1, 0x74
};

static const char escape_text[] = "!$ a more#^- complicated test@@?_%! case";
#define escape_septet_length 41 /* note: the ^ counts as two, because it is a extension character */
static const uint8_t escape_enc[] = {
	0x21, 0x01, 0x28, 0x0c, 0x6a, 0xbf, 0xe5, 0xe5, 0xd1,
	0x86, 0xd2, 0x02, 0x8d, 0xdf, 0x6d, 0x38, 0x3b, 0x3d,
	0x0e, 0xd3, 0xcb, 0x64, 0x10, 0xbd, 0x3c, 0xa7, 0x03,
	0x00, 0xbf, 0x48, 0x29, 0x04, 0x1a, 0x87, 0xe7, 0x65,
};

static const char enhanced_text[] = "enhanced ^ {][} test |+~ ^ test";
#define enhanced_septet_length 39 /* note: the characters { } [ ] ^ | ~ count as two (each of them), because they are extension characters */
static const uint8_t enhanced_enc[] = {
	0x65, 0x37, 0x3A, 0xEC, 0x1E, 0x97, 0xC9, 0xA0, 0x0D,
	0x05, 0xB4, 0x41, 0x6D, 0x7C, 0x1B, 0xDE, 0x26, 0x05,
	0xA2, 0x97, 0xE7, 0x74, 0xD0, 0x06, 0xB8, 0xDA, 0xF4,
	0x40, 0x1B, 0x0A, 0x88, 0x5E, 0x9E, 0xD3, 0x01,
};

static const char enhancedV2_text[] = "enhanced ^ {][} test |+~ ^ tests";
#define enhancedV2_septet_length 40 /* note: number of octets are equal to the enhanced_text! */
static const uint8_t enhancedV2_enc[] = {
	0x65, 0x37, 0x3A, 0xEC, 0x1E, 0x97, 0xC9, 0xA0, 0x0D,
	0x05, 0xB4, 0x41, 0x6D, 0x7C, 0x1B, 0xDE, 0x26, 0x05,
	0xA2, 0x97, 0xE7, 0x74, 0xD0, 0x06, 0xB8, 0xDA, 0xF4,
	0x40, 0x1B, 0x0A, 0x88, 0x5E, 0x9E, 0xD3, 0xE7,
};



static const char concatenated_text[] =
		"this is a testmessage. this is a testmessage. this is a testmessage. this is a testmessage. "
		"this is a testmessage. this is a testmessage. cut here .....: this is a second testmessage. end here.";

static const char splitted_text_part1[] =
		"this is a testmessage. this is a testmessage. this is a testmessage. this is a testmessage. "
		"this is a testmessage. this is a testmessage. cut here .....:";
#define concatenated_part1_septet_length_with_header 160
#define concatenated_part1_septet_length 153
static const uint8_t concatenated_part1_enc[] = {
		0x05, 0x00, 0x03, 0x6f, 0x02, 0x01,
		0xe8, 0xe8, 0xf4, 0x1c, 0x94, 0x9e, 0x83, 0xc2,
		0x20, 0x7a, 0x79, 0x4e, 0x6f, 0x97, 0xe7, 0xf3,
		0xf0, 0xb9, 0xec, 0x02, 0xd1, 0xd1, 0xe9, 0x39,
		0x28, 0x3d, 0x07, 0x85, 0x41, 0xf4, 0xf2, 0x9c,
		0xde, 0x2e, 0xcf, 0xe7, 0xe1, 0x73, 0xd9, 0x05,
		0xa2, 0xa3, 0xd3, 0x73, 0x50, 0x7a, 0x0e, 0x0a,
		0x83, 0xe8, 0xe5, 0x39, 0xbd, 0x5d, 0x9e, 0xcf,
		0xc3, 0xe7, 0xb2, 0x0b, 0x44, 0x47, 0xa7, 0xe7,
		0xa0, 0xf4, 0x1c, 0x14, 0x06, 0xd1, 0xcb, 0x73,
		0x7a, 0xbb, 0x3c, 0x9f, 0x87, 0xcf, 0x65, 0x17,
		0x88, 0x8e, 0x4e, 0xcf, 0x41, 0xe9, 0x39, 0x28,
		0x0c, 0xa2, 0x97, 0xe7, 0xf4, 0x76, 0x79, 0x3e,
		0x0f, 0x9f, 0xcb, 0x2e, 0x10, 0x1d, 0x9d, 0x9e,
		0x83, 0xd2, 0x73, 0x50, 0x18, 0x44, 0x2f, 0xcf,
		0xe9, 0xed, 0xf2, 0x7c, 0x1e, 0x3e, 0x97, 0x5d,
		0xa0, 0x71, 0x9d, 0x0e, 0x42, 0x97, 0xe5, 0x65,
		0x90, 0xcb, 0xe5, 0x72, 0xb9, 0x74,
};

static const char splitted_text_part2[] = " this is a second testmessage. end here.";
#define concatenated_part2_septet_length_with_header 47
#define concatenated_part2_septet_length 40
static const uint8_t concatenated_part2_enc[] = {
		0x05, 0x00, 0x03, 0x6f, 0x02, 0x02,
		0x40, 0x74, 0x74, 0x7a, 0x0e, 0x4a, 0xcf, 0x41,
		0x61, 0xd0, 0xbc, 0x3c, 0x7e, 0xbb, 0xc9, 0x20,
		0x7a, 0x79, 0x4e, 0x6f, 0x97, 0xe7, 0xf3, 0xf0,
		0xb9, 0xec, 0x02, 0x95, 0xdd, 0x64, 0x10, 0xba,
		0x2c, 0x2f, 0xbb, 0x00,
};

static const struct test_case test_multiple_encode[] =
{
	{
		.input = concatenated_text,
		.expected = concatenated_part1_enc,
		.expected_octet_length = sizeof(concatenated_part1_enc),
		.expected_septet_length = concatenated_part1_septet_length,
		.ud_hdr_ind = 1,
	},
	{
		.input = concatenated_text,
		.expected = concatenated_part2_enc,
		.expected_octet_length = sizeof(concatenated_part2_enc),
		.expected_septet_length = concatenated_part2_septet_length,
		.ud_hdr_ind = 1,
	},
};

static const struct test_case test_encode[] =
{
	{
		.input = simple_text,
		.expected = simple_enc,
		.expected_octet_length = sizeof(simple_enc),
		.expected_septet_length = simple_septet_length,
		.ud_hdr_ind = 0,
	},
	{
		.input = escape_text,
		.expected = escape_enc,
		.expected_octet_length = sizeof(escape_enc),
		.expected_septet_length = escape_septet_length,
		.ud_hdr_ind = 0,
	},
	{
		.input = enhanced_text,
		.expected = enhanced_enc,
		.expected_octet_length = sizeof(enhanced_enc),
		.expected_septet_length = enhanced_septet_length,
		.ud_hdr_ind = 0,
	},
	{
		.input = enhancedV2_text,
		.expected = enhancedV2_enc,
		.expected_octet_length = sizeof(enhancedV2_enc),
		.expected_septet_length = enhancedV2_septet_length,
		.ud_hdr_ind = 0,
	},
};

static const struct test_case test_decode[] =
{
	{
		.input = simple_enc,
		.input_length = sizeof(simple_enc),
		.expected = simple_text,
		.expected_septet_length = simple_septet_length,
		.ud_hdr_ind = 0,
	},
	{
		.input = escape_enc,
		.input_length = sizeof(escape_enc),
		.expected = escape_text,
		.expected_septet_length = escape_septet_length,
		.ud_hdr_ind = 0,
	},
	{
		.input = enhanced_enc,
		.input_length = sizeof(enhanced_enc),
		.expected = enhanced_text,
		.expected_septet_length = enhanced_septet_length,
		.ud_hdr_ind = 0,
	},
	{
		.input = enhancedV2_enc,
		.input_length = sizeof(enhancedV2_enc),
		.expected = enhancedV2_text,
		.expected_septet_length = enhancedV2_septet_length,
		.ud_hdr_ind = 0,
	},
	{
		.input = concatenated_part1_enc,
		.input_length = sizeof(concatenated_part1_enc),
		.expected = splitted_text_part1,
		.expected_septet_length = concatenated_part1_septet_length_with_header,
		.ud_hdr_ind = 1,
	},
	{
		.input = concatenated_part2_enc,
		.input_length = sizeof(concatenated_part2_enc),
		.expected = splitted_text_part2,
		.expected_septet_length = concatenated_part2_septet_length_with_header,
		.ud_hdr_ind = 1,
	},
};

int main(int argc, char** argv)
{
	printf("SMS testing\n");
	struct msgb *msg;
	uint8_t i;

	uint8_t octet_length;
	uint8_t septet_length;
	uint8_t gsm_septet_length;
	uint8_t coded[256];
	uint8_t tmp[160];
	uint8_t septet_data[256];
	uint8_t ud_header[6];
	char result[256];

	/* test 7-bit encoding */
	for (i = 0; i < ARRAY_SIZE(test_encode); ++i) {
		memset(coded, 0x42, sizeof(coded));
		septet_length = gsm_7bit_encode(coded, test_encode[i].input);
		octet_length = gsm_get_octet_len(septet_length);
		if (octet_length != test_encode[i].expected_octet_length) {
			fprintf(stderr, "Encode case %d: Octet length failure. Got %d, expected %d\n",
				i, octet_length, test_encode[i].expected_octet_length);
			return -1;
		}

		if (septet_length != test_encode[i].expected_septet_length){
			fprintf(stderr, "Encode case %d: Septet length failure. Got %d, expected %d\n",
				i, septet_length, test_encode[i].expected_septet_length);
			return -1;
		}

		if (memcmp(coded, test_encode[i].expected, octet_length) != 0) {
			fprintf(stderr, "Encoded content does not match for case %d\n",
				i);
			return -1;
		}
	}


	/* Test: encode multiple SMS */
	int number_of_septets = gsm_septet_encode(septet_data, test_multiple_encode[0].input);

	/* SMS part 1 */
	memset(tmp, 0x42, sizeof(tmp));
	memset(coded, 0x42, sizeof(coded));
	memcpy(tmp, septet_data, concatenated_part1_septet_length);

	/* In our case: test_multiple_decode[0].ud_hdr_ind equals number of padding bits*/
	octet_length = gsm_septets2octets(coded, tmp, concatenated_part1_septet_length, test_multiple_encode[0].ud_hdr_ind);

	/* copy header */
	memset(tmp, 0x42, sizeof(tmp));
	int udh_length = test_multiple_encode[0].expected[0] + 1;
	memcpy(tmp, test_multiple_encode[0].expected, udh_length);
	memcpy(tmp + udh_length, coded, octet_length);
	memset(coded, 0x42, sizeof(coded));
	memcpy(coded, tmp, octet_length + 6);

	if (memcmp(coded, test_multiple_encode[0].expected, octet_length) != 0) {
		fprintf(stderr, "Multiple-SMS encoded content does not match for part 1\n");
		return -1;
	}


	/* SMS part 2 */
	memset(tmp, 0x42, sizeof(tmp));
	memset(coded, 0x42, sizeof(coded));
	memcpy(tmp, septet_data + concatenated_part1_septet_length, concatenated_part2_septet_length);

	/* In our case: test_multiple_decode[1].ud_hdr_ind equals number of padding bits*/
	octet_length = gsm_septets2octets(coded, tmp, concatenated_part2_septet_length, test_multiple_encode[1].ud_hdr_ind);

	/* copy header */
	memset(tmp, 0x42, sizeof(tmp));
	udh_length = test_multiple_encode[1].expected[0] + 1;
	memcpy(tmp, test_multiple_encode[1].expected, udh_length);
	memcpy(tmp + udh_length, coded, octet_length);
	memset(coded, 0x42, sizeof(coded));
	memcpy(coded, tmp, octet_length + 6);

	if (memcmp(coded, test_multiple_encode[1].expected, octet_length) != 0) {
		fprintf(stderr, "Multiple-SMS encoded content does not match for part 2\n");
		return -1;
	}



	/* test 7-bit decoding */
	for (i = 0; i < ARRAY_SIZE(test_decode); ++i) {
		memset(result, 0x42, sizeof(coded));
		septet_length = gsm_7bit_decode_hdr(result, test_decode[i].input,
				test_decode[i].expected_septet_length, test_decode[i].ud_hdr_ind);

		if (strcmp(result, test_decode[i].expected) != 0) {
			fprintf(stderr, "Test case %d failed to decode.\n", i);
			return -1;
		}
		if (septet_length != test_decode[i].expected_septet_length) {
			fprintf(stderr, "Decode case %d: Septet length failure. Got %d, expected %d\n",
				i, septet_length, test_decode[i].expected_septet_length);
			return -1;
		}
	}

	printf("OK\n");
	return 0;
}
