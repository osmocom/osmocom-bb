/* tests for utilities of libmsomcore */
/*
 * (C) 2014 Holger Hans Peter Freyther
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

#include <osmocom/gsm/ipa.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>

#include <stdio.h>

static void hexdump_test(void)
{
	uint8_t data[4098];
	int i;

	for (i = 0; i < ARRAY_SIZE(data); ++i)
		data[i] = i & 0xff;

	printf("Plain dump\n");
	printf("%s\n", osmo_hexdump(data, 4));

	printf("Corner case\n");
	printf("%s\n", osmo_hexdump(data, ARRAY_SIZE(data)));
	printf("%s\n", osmo_hexdump_nospc(data, ARRAY_SIZE(data)));
}

static void test_idtag_parsing(void)
{
	struct tlv_parsed tvp;
	int rc;

        static uint8_t data[] = {
		0x01, 0x08,
		0x01, 0x07,
		0x01, 0x02,
		0x01, 0x03,
		0x01, 0x04,
		0x01, 0x05,
		0x01, 0x01,
		0x01, 0x00,
		0x11, 0x23, 0x4e, 0x6a, 0x28, 0xd2, 0xa2, 0x53, 0x3a, 0x2a, 0x82, 0xa7, 0x7a, 0xef, 0x29, 0xd4, 0x44, 0x30,
		0x11, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
        };

	rc = ipa_ccm_idtag_parse_off(&tvp, data, sizeof(data), 1);
	OSMO_ASSERT(rc == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 8));
	OSMO_ASSERT(TLVP_LEN(&tvp, 8) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 7));
	OSMO_ASSERT(TLVP_LEN(&tvp, 7) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 2));
	OSMO_ASSERT(TLVP_LEN(&tvp, 2) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 3));
	OSMO_ASSERT(TLVP_LEN(&tvp, 3) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 4));
	OSMO_ASSERT(TLVP_LEN(&tvp, 4) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 5));
	OSMO_ASSERT(TLVP_LEN(&tvp, 5) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 1));
	OSMO_ASSERT(TLVP_LEN(&tvp, 1) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 0));
	OSMO_ASSERT(TLVP_LEN(&tvp, 0) == 0);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 0x23));
	OSMO_ASSERT(TLVP_LEN(&tvp, 0x23) == 16);

	OSMO_ASSERT(TLVP_PRESENT(&tvp, 0x24));
	OSMO_ASSERT(TLVP_LEN(&tvp, 0x24) == 16);

	OSMO_ASSERT(!TLVP_PRESENT(&tvp, 0x25));
}

int main(int argc, char **argv)
{
	static const struct log_info log_info = {};
	log_init(&log_info, NULL);

	hexdump_test();
	test_idtag_parsing();
	return 0;
}
