#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/a5.h>

// make compiler happy
void _a5_3(const uint8_t *key, uint32_t fn, ubit_t *dl, ubit_t *ul, bool fn_correct);
void _a5_4(const uint8_t *key, uint32_t fn, ubit_t *dl, ubit_t *ul, bool fn_correct);

static const uint8_t key[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };
static const uint32_t fn = 123456;
static const uint8_t dl[] = {
	/* A5/0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* A5/1 */
	0xcb, 0xa2, 0x55, 0x76, 0x17, 0x5d, 0x3b, 0x1c,
	0x7b, 0x2f, 0x29, 0xa8, 0xc1, 0xb6, 0x00,

	/* A5/2 */
	0x45, 0x9c, 0x88, 0xc3, 0x82, 0xb7, 0xff, 0xb3,
	0x98, 0xd2, 0xf9, 0x6e, 0x0f, 0x14, 0x80,
};
static const uint8_t ul[] = {
	/* A5/0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	/* A5/1 */
	0xd9, 0x03, 0x5e, 0x0f, 0x2a, 0xec, 0x13, 0x9a,
	0x05, 0xd4, 0xa8, 0x7b, 0xb1, 0x64, 0x80,

	/* A5/2 */
	0xf0, 0x3a, 0xac, 0xde, 0xe3, 0x5b, 0x5e, 0x65,
	0x80, 0xba, 0xab, 0xc0, 0x59, 0x26, 0x40,
};

static inline bool print_a5(int n, int k, const char * dir, const ubit_t * out, const char * block)
{
	uint8_t len = 114 / 8 + 1, buf[len], res[len];
	printf("A5/%d - %s: %s => ", n, dir, osmo_ubit_dump(out, 114));
	osmo_hexparse(block, res, len);
	osmo_ubit2pbit(buf, out, 114);
	if (0 != memcmp(buf, res, len)) {
		printf("FAIL\nGOT: [%d] %s\nEXP: [%d] %s\n", k, osmo_hexdump_nospc(buf, len), k, osmo_hexdump_nospc(res, len));
		return false;
	}
	printf("OK\n");
	return true;
}

static inline bool test_a53(const char * kc, uint32_t count, const char * block1, const char * block2)
{
	ubit_t dlout[114], ulout[114];
	uint8_t key[8];

	osmo_hexparse(kc, key, 8);
	_a5_3(key, count, dlout, NULL, false);
	_a5_3(key, count, NULL, ulout, false);

	return print_a5(3, 8, "DL", dlout, block1) & print_a5(3, 8, "UL", ulout, block2);
}

static inline bool test_a54(const char * kc, uint32_t count, const char * block1, const char * block2)
{
	ubit_t dlout[114], ulout[114];
	uint8_t key[16];

	osmo_hexparse(kc, key, 16);
	_a5_4(key, count, dlout, NULL, false);
	_a5_4(key, count, NULL, ulout, false);

	return print_a5(4, 8, "DL", dlout, block1) & print_a5(4, 8, "UL", ulout, block2);
}


int main(int argc, char **argv)
{
	ubit_t exp[114], out[114];
	int n, i;

	for (n=0; n<3; n++) {
		/* "Randomize" */
		for (i=0; i<114; i++)
			out[i] = i & 1;

		/* DL */
		osmo_pbit2ubit(exp, &dl[15*n], 114);

		osmo_a5(n, key, fn, out, NULL);

		printf("A5/%d - DL: %s", n, osmo_ubit_dump(out, 114));

		if (!memcmp(exp, out, 114))
			printf(" => OK\n");
		else {
			printf(" => BAD\n");
			printf(" Expected: %s", osmo_ubit_dump(out, 114));
			fprintf(stderr, "[!] A5/%d DL failed", n);
			exit(1);
		}

		/* UL */
		osmo_pbit2ubit(exp, &ul[15*n], 114);

		osmo_a5(n, key, fn, NULL, out);

		printf("A5/%d - UL: %s", n, osmo_ubit_dump(out, 114));

		if (!memcmp(exp, out, 114))
			printf(" => OK\n");
		else {
			printf(" => BAD\n");
			printf(" Expected: %s", osmo_ubit_dump(out, 114));
			fprintf(stderr, "[!] A5/%d UL failed", n);
			exit(1);
		}
	}

// test vectors from 3GPP TS 55.217 and TS 55.218
	test_a53("2BD6459F82C5BC00", 0x24F20F, "889EEAAF9ED1BA1ABBD8436232E440", "5CA3406AA244CF69CF047AADA2DF40");
	test_a53("952C49104881FF48", 0x061272, "FB4D5FBCEE13A33389285686E9A5C0", "25090378E0540457C57E367662E440");
	test_a53("EFA8B2229E720C2A", 0x33FD3F, "0E4015755A336469C3DD8680E30340", "6F10669E2B4E18B042431A28E47F80");
	test_a53("952C49104881FF48", 0x061527, "AB7DB38A573A325DAA76E4CB800A40", "4C4B594FEA9D00FE8978B7B7BC1080");
	test_a53("3451F23A43BD2C87", 0x0E418C, "75F7C4C51560905DFBA05E46FB54C0", "192C95353CDF979E054186DF15BF00");
	test_a53("CAA2639BE82435CF", 0x2FF229, "301437E4D4D6565D4904C631606EC0", "F0A3B8795E264D3E1A82F684353DC0");
	test_a53("7AE67E87400B9FA6", 0x2F24E5, "F794290FEF643D2EA348A7796A2100", "CB6FA6C6B8A705AF9FEFE975818500");
	test_a53("58AF69935540698B", 0x05446B, "749CA4E6B691E5A598C461D5FE4740", "31C9E444CD04677ADAA8A082ADBC40");
	test_a53("017F81E5F236FE62", 0x156B26, "2A6976761E60CC4E8F9F52160276C0", "A544D8475F2C78C35614128F1179C0");
	test_a53("1ACA8B448B767B39", 0x0BC3B5, "A4F70DC5A2C9707F5FA1C60EB10640", "7780B597B328C1400B5C74823E8500");
	test_a54("3D43C388C9581E337FF1F97EB5C1F85E", 0x35D2CF, "A2FE3034B6B22CC4E33C7090BEC340", "170D7497432FF897B91BE8AECBA880");
	test_a54("A4496A64DF4F399F3B4506814A3E07A1", 0x212777, "89CDEE360DF9110281BCF57755A040", "33822C0C779598C9CBFC49183AF7C0");

	return 0;
}
