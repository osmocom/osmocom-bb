#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/bitcomp.h>

static char lol[1024]; // for pretty-printing

int main(int argc, char **argv)
{
	srand(time(NULL));

	struct bitvec bv, out;
	uint8_t i = 20, test[i], data[i];

	bv.data_len = i;
        bv.data = test;
	out.data_len = i;
	out.data = data;
	bitvec_zero(&bv);
	bitvec_zero(&out);

	printf("\nrunning static tests...\n");

	printf("\nTEST1:\n 00110111 01000111 10000001 1111\n");
	bitvec_zero(&bv);
	bitvec_set_uint(&bv, 0x374781F, 28); bitvec_to_string_r(&bv, lol); printf("%s", lol);

	printf("\nEncoded:\n%d", osmo_t4_encode(&bv)); bitvec_to_string_r(&bv, lol); printf("%s", lol);
	printf(" [%d]\nExpected:\n0 11011110 10001000 01110101 01100101 100 [35]\n", bv.cur_bit);

	bitvec_zero(&bv);
	bitvec_set_uint(&bv, 0xDE887565, 32);
	bitvec_set_uint(&bv, 4, 3);
	bitvec_to_string_r(&bv, lol);
	printf(" %s [%d]\n", lol, bv.cur_bit);

	printf("\nTEST2:\n 11111111 11111111 11111111 11111111 11111111 11111111 11111111 11111111 11111111 11111111 00000000 00\n");
	bitvec_zero(&bv);
	bitvec_set_uint(&bv, 0xFFFFFFFF, 32);
	bitvec_set_uint(&bv, 0xFFFFFFFF, 32);
	bitvec_set_uint(&bv, 0xFFFFFC00, 26); bitvec_to_string_r(&bv, lol); printf("%s", lol);
	printf("\nEncoded:\n%d", osmo_t4_encode(&bv)); bitvec_to_string_r(&bv, lol); printf("%s", lol);
	printf(" [%d]\nExpected:\n1 11011101 01000001 00 [18]\n", bv.cur_bit);

	return 0;
}
