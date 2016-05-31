/*
 * (C) 2016 by Sysmocom s.f.m.c. GmbH
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <osmocom/core/utils.h>
#include <osmocom/codec/codec.h>

const uint8_t sid_update[] = {0x20, 0x44, 0x29, 0xc2, 0x92, 0x91, 0xf4};
const uint8_t sid_first[] = {0x20, 0x44, 0x00, 0x00, 0x00, 0x00, 0x04};

#define PAYLOAD_LEN 34
#define SID_LEN 7

static const char * cmpr(int a, int b)
{
	return (a == b) ? "OK" : "BAD";
}

static void test_sid_dec(const uint8_t *t, size_t len)
{
	uint8_t cmr, tmp[SID_LEN];
	enum osmo_amr_type ft;
	enum osmo_amr_quality bfi;
	int8_t sti, cmi;
	memcpy(tmp, t, SID_LEN);
	int rc = osmo_amr_rtp_dec(tmp, len, &cmr, &cmi, &ft, &bfi, &sti);
	printf("[%d] decode RTP %s%s: FT %s, CMR %s, CMI is %d, SID type %s\t",
	       rc, osmo_hexdump(tmp, len), cmpr(bfi, AMR_GOOD),
	       get_value_string(osmo_amr_type_names, ft),
	       get_value_string(osmo_amr_type_names, cmr),
	       cmi, sti ? "UPDATE" : "FIRST");
	if (sti == -1)
		printf("FAIL: incompatible STI for SID\n");
	rc = osmo_amr_rtp_enc(tmp, cmr, ft, bfi);
	printf("[%d] encode [%d]\n", rc, memcmp(tmp, t, SID_LEN));
}

static void test_amr_rt(uint8_t _cmr, enum osmo_amr_type _ft,
			enum osmo_amr_quality _bfi)
{
	uint8_t cmr, payload[PAYLOAD_LEN];
	enum osmo_amr_type ft;
	enum osmo_amr_quality bfi;
	int8_t sti, cmi;
	int rc, re = osmo_amr_rtp_enc(payload, _cmr, _ft, _bfi);
	rc = osmo_amr_rtp_dec(payload, PAYLOAD_LEN, &cmr, &cmi, &ft, &bfi, &sti);
	printf("[%d/%d] %s, CMR: %s, FT: %s, BFI: %s, CMI: %d, STI: %d\n", re,
	       rc, get_value_string(osmo_amr_type_names, ft),
	       cmpr(_cmr, cmr), cmpr(_ft, ft), cmpr(_bfi, bfi), cmi, sti);
}

uint8_t fr[] = {0xd8, 0xa9, 0xb5, 0x1d, 0xda, 0xa8, 0x82, 0xcc, 0xec, 0x52,
		      0x29, 0x05, 0xa8, 0xc3, 0xe3, 0x0e, 0xb0, 0x89, 0x7a, 0xee,
		      0x42, 0xca, 0xc4, 0x97, 0x22, 0xe6, 0x9e, 0xa8, 0xb8, 0xec,
		      0x52, 0x26, 0xbd};
uint8_t sid_fr[] = {0xd7, 0x27, 0x93, 0xe5, 0xe3, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			  0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

uint8_t hr[] = {0x06, 0x46, 0x76, 0xb1, 0x8e, 0x48, 0x9a, 0x2f, 0x5e, 0x4c,
		      0x22, 0x2b, 0x62, 0x25};
uint8_t sid_hr[] = {0x03, 0x8e, 0xb6, 0xcb, 0xff, 0xff, 0xff, 0xff, 0xff,
			  0xff, 0xff, 0xff, 0xff, 0xff};

static void test_sid_xr(uint8_t *t, size_t len, bool hr)
{
	printf("%s SID ? %s:: %d\n", hr ? "HR" : "FR", osmo_hexdump(t, len),
	       hr ? osmo_hr_check_sid(t, len) : osmo_fr_check_sid(t, len));
}

int main(int argc, char **argv)
{
	printf("AMR RTP payload decoder test:\n");
	test_sid_dec(sid_first, 7);
	test_sid_dec(sid_update, 7);
	test_amr_rt(0, AMR_NO_DATA, AMR_BAD);
	test_amr_rt(0, AMR_NO_DATA, AMR_GOOD);
	test_amr_rt(AMR_12_2, AMR_12_2, AMR_BAD);
	test_amr_rt(AMR_12_2, AMR_12_2, AMR_GOOD);
	test_amr_rt(AMR_7_40, AMR_7_40, AMR_BAD);
	test_amr_rt(AMR_7_40, AMR_7_40, AMR_GOOD);
	printf("FR RTP payload SID test:\n");
	test_sid_xr(sid_fr, 33, false);
	test_sid_xr(fr, 33, false);

	printf("HR RTP payload SID test:\n");
	test_sid_xr(sid_hr, 14, true);
	test_sid_xr(hr, 14, true);

	return 0;
}


