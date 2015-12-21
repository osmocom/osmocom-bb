#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/bitvec.h>

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

		fprintf(stderr, "bitvec: %s\n", osmo_hexdump(bv.data, bv.data_len));

		/* Read from bitvec */
		memset(out, 0xff, sizeof(out));
		bv.cur_bit = i;
		rc = bitvec_get_uint(&bv, 8);
		OSMO_ASSERT(rc == 0x7e);
		rc = bitvec_get_bytes(&bv, out + 1, in_size);
		OSMO_ASSERT(rc >= 0);
		rc = bitvec_get_uint(&bv, 8);
		OSMO_ASSERT(rc == 0x7e);

		fprintf(stderr, "out: %s\n", osmo_hexdump(out, sizeof(out)));

		OSMO_ASSERT(out[0] == 0xff);
		OSMO_ASSERT(out[in_size+1] == 0xff);
		OSMO_ASSERT(memcmp(in, out + 1, in_size) == 0);
	}

	printf("=== end %s ===\n", __func__);
}

int main(int argc, char **argv)
{
	test_byte_ops();
	return 0;
}
