#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/bitvec.h>
#include <osmocom/core/bits.h>

static char lol[1024]; // we pollute this with printed vectors
static inline void test_rl(const struct bitvec *bv)
{
	bitvec_to_string_r(bv, lol);
	printf("%s [%d] RL0=%d, RL1=%d\n", lol, bv->cur_bit, bitvec_rl(bv, false), bitvec_rl(bv, true));
}

static inline void test_shift(struct bitvec *bv, unsigned n)
{
	bitvec_to_string_r(bv, lol);
	printf("%s << %d:\n", lol, n);
	bitvec_shiftl(bv, n);
	bitvec_to_string_r(bv, lol);
	printf("%s\n", lol);
}

static inline void test_get(struct bitvec *bv, unsigned n)
{
	bitvec_to_string_r(bv, lol);
	printf("%s [%d]", lol, bv->cur_bit);
	int16_t x = bitvec_get_int16_msb(bv, n);
	uint8_t tmp[2];
	osmo_store16be(x, &tmp);
	printf(" -> %d (%u bit) ["OSMO_BIN_SPEC" "OSMO_BIN_SPEC"]:\n", x, n, OSMO_BIN_PRINT(tmp[0]), OSMO_BIN_PRINT(tmp[1]));
	bitvec_to_string_r(bv, lol);
	printf("%s [%d]\n", lol, bv->cur_bit);
}

static inline void test_fill(struct bitvec *bv, unsigned n, enum bit_value val)
{
	bitvec_to_string_r(bv, lol);
	unsigned bvlen = bv->cur_bit;
	int fi =  bitvec_fill(bv, n, val);
	printf("%c>  FILL %s [%d] -%d-> [%d]:\n", bit_value_to_char(val), lol, bvlen, n, fi);
	bitvec_to_string_r(bv, lol);
	printf("         %s [%d]\n\n", lol, bv->cur_bit);
}

static inline void test_spare(struct bitvec *bv, unsigned n)
{
	bitvec_to_string_r(bv, lol);
	unsigned bvlen = bv->cur_bit;
	int sp = bitvec_spare_padding(bv, n);
	printf("%c> SPARE %s [%d] -%d-> [%d]:\n", bit_value_to_char(L), lol, bvlen, n, sp);
	bitvec_to_string_r(bv, lol);
	printf("         %s [%d]\n\n", lol, bv->cur_bit);
}

static inline void test_set(struct bitvec *bv, enum bit_value bit)
{
	bitvec_to_string_r(bv, lol);
	unsigned bvlen = bv->cur_bit;
	int set = bitvec_set_bit(bv, bit);
	printf("%c>   SET %s [%d] ++> [%d]:\n", bit_value_to_char(bit), lol, bvlen, set);
	bitvec_to_string_r(bv, lol);
	printf("         %s [%d]\n\n", lol, bv->cur_bit);
}

static void test_byte_ops()
{
	struct bitvec bv;
	const uint8_t *in = (const uint8_t *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	uint8_t out[26 + 2];
	uint8_t data[64];
	int i;
	int rc;
	int in_size = strlen((const char *)in);

	printf("=== start %s ===\n", __func__);

	bv.data = data;
	bv.data_len = sizeof(data);

	for (i = 0; i < 32; i++) {
		/* Write to bitvec */
		memset(data, 0x00, sizeof(data));
		bv.cur_bit = i;
		rc = bitvec_set_uint(&bv, 0x7e, 8);
		OSMO_ASSERT(rc >= 0);
		rc = bitvec_set_bytes(&bv, in, in_size);
		OSMO_ASSERT(rc >= 0);
		rc = bitvec_set_uint(&bv, 0x7e, 8);
		OSMO_ASSERT(rc >= 0);

		printf("bitvec: %s\n", osmo_hexdump(bv.data, bv.data_len));

		/* Read from bitvec */
		memset(out, 0xff, sizeof(out));
		bv.cur_bit = i;
		rc = bitvec_get_uint(&bv, 8);
		OSMO_ASSERT(rc == 0x7e);
		rc = bitvec_get_bytes(&bv, out + 1, in_size);
		OSMO_ASSERT(rc >= 0);
		rc = bitvec_get_uint(&bv, 8);
		OSMO_ASSERT(rc == 0x7e);

		printf("out: %s\n", osmo_hexdump(out, sizeof(out)));

		OSMO_ASSERT(out[0] == 0xff);
		OSMO_ASSERT(out[in_size+1] == 0xff);
		OSMO_ASSERT(memcmp(in, out + 1, in_size) == 0);
	}

	printf("=== end %s ===\n", __func__);
}

static void test_unhex(const char *hex)
{
	int rc;
	struct bitvec b;
	uint8_t d[64] = {0};
	b.data = d;
	b.data_len = sizeof(d);
	b.cur_bit = 0;

	rc = bitvec_unhex(&b, hex);
	printf("%d -=> cur_bit=%u\n", rc, b.cur_bit);
	printf("%s\n", osmo_hexdump_nospc(d, 64));
	printf("%s\n", hex);
}

static inline void test_array_item(unsigned t, struct bitvec *b, unsigned int n,
				   uint32_t *array, unsigned int p)
{
	unsigned int i, x, y;
	bitvec_zero(b);
	x = b->cur_bit;
	i = bitvec_add_array(b, array, n, true, t);
	y = b->cur_bit;
	bitvec_add_array(b, array, n, false, t);
	printf("\nbits: %u, est: %u, real: %u, x: %u, y: %u\n",
	       t, i, b->cur_bit, x, y);
	for (i = 0; i < p; i++) {
		printf(OSMO_BIT_SPEC " ", OSMO_BIT_PRINT(b->data[i]));
		if (0 == (i + 1) % 15)
			printf("\n");
	}
}

static void test_array()
{
	struct bitvec b;
	uint8_t d[4096];
	b.data = d;
	b.data_len = sizeof(d);

	unsigned int i, n = 64;
	uint32_t array[n];
	for (i = 0; i < n; i++) {
		array[i] = i * i * i + i;
		printf("0x%x ", array[i]);
	}

	test_array_item(3, &b, n, array, n);
	test_array_item(9, &b, n, array, n * 2);
	test_array_item(17, &b, n, array, n * 3);
}

int main(int argc, char **argv)
{
	struct bitvec bv;
	uint8_t i = 8, test[i];

	memset(test, 0, i);
	bv.data_len = i;
	bv.data = test;
	bv.cur_bit = 0;

	printf("test shifting...\n");

	bitvec_set_uint(&bv, 0x0E, 7);
	test_shift(&bv, 3);
	test_shift(&bv, 17);
	bitvec_set_uint(&bv, 0, 32);
	bitvec_set_uint(&bv, 0x0A, 7);
	test_shift(&bv, 24);

	printf("checking RL functions...\n");

	bitvec_zero(&bv);
	test_rl(&bv);
	bitvec_set_uint(&bv, 0x000F, 32);
	test_rl(&bv);
	bitvec_shiftl(&bv, 18);
	test_rl(&bv);
	bitvec_set_uint(&bv, 0x0F, 8);
	test_rl(&bv);
	bitvec_zero(&bv);
	bitvec_set_uint(&bv, 0xFF, 8);
	test_rl(&bv);
	bitvec_set_uint(&bv, 0xFE, 7);
	test_rl(&bv);
	bitvec_set_uint(&bv, 0, 17);
	test_rl(&bv);
	bitvec_shiftl(&bv, 18);
	test_rl(&bv);

	printf("probing bit access...\n");

	bitvec_zero(&bv);
	bitvec_set_uint(&bv, 0x3747817, 32);
	bitvec_shiftl(&bv, 10);

	test_get(&bv, 2);
	test_get(&bv, 7);
	test_get(&bv, 9);
	test_get(&bv, 13);
	test_get(&bv, 16);
	test_get(&bv, 42);

	printf("feeling bit fills...\n");

	test_set(&bv, ONE);
	test_fill(&bv, 3, ZERO);
	test_spare(&bv, 38);
	test_spare(&bv, 43);
	test_spare(&bv, 1);
	test_spare(&bv, 7);
	test_fill(&bv, 5, ONE);
	test_fill(&bv, 3, L);

	printf("byte me...\n");

	test_byte_ops();
	test_unhex("48282407a6a074227201000b2b2b2b2b2b2b2b2b2b2b2b");
	test_unhex("47240c00400000000000000079eb2ac9402b2b2b2b2b2b");
	test_unhex("47283c367513ba333004242b2b2b2b2b2b2b2b2b2b2b2b");
	test_unhex("DEADFACE000000000000000000000000000000BEEFFEED");
	test_unhex("FFFFFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");

	printf("arrr...\n");

	test_array();

	printf("\nbitvec ok.\n");

	return 0;
}
