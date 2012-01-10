#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/a5.h>

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

static const char *
binstr(ubit_t *d, int n)
{
	static char str[256];
	int i;

	for (i=0; i<n; i++)
		str[i] = d[i] ? '1' : '0';

	str[i] = '\0';

	return str;
}

int main(int argc, char **argv)
{
	ubit_t exp[114];
	ubit_t out[114];
	int n, i;

	for (n=0; n<3; n++) {
		/* "Randomize" */
		for (i=0; i<114; i++)
			out[i] = i & 1;

		/* DL */
		osmo_pbit2ubit(exp, &dl[15*n], 114);

		osmo_a5(n, key, fn, out, NULL);

		printf("A5/%d - DL: %s", n, binstr(out, 114));

		if (!memcmp(exp, out, 114))
			printf(" => OK\n");
		else {
			printf(" => BAD\n");
			printf(" Expected: %s", binstr(out, 114));
			fprintf(stderr, "[!] A5/%d DL failed", n);
			exit(1);
		}

		/* UL */
		osmo_pbit2ubit(exp, &ul[15*n], 114);

		osmo_a5(n, key, fn, NULL, out);

		printf("A5/%d - UL: %s", n, binstr(out, 114));

		if (!memcmp(exp, out, 114))
			printf(" => OK\n");
		else {
			printf(" => BAD\n");
			printf(" Expected: %s", binstr(out, 114));
			fprintf(stderr, "[!] A5/%d UL failed", n);
			exit(1);
		}
	}

	return 0;
}
