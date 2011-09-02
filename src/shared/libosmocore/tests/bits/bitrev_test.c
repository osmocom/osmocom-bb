
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/bits.h>

static const uint8_t input[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
static const uint8_t exp_out[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

int main(int argc, char **argv)
{
	uint8_t out[ARRAY_SIZE(input)];
	unsigned int offs;

	for (offs = 0; offs < sizeof(out); offs++) {
		uint8_t *start = out + offs;
		uint8_t len = sizeof(out) - offs;

		memcpy(out, input, sizeof(out));

		printf("INORDER:  %s\n", osmo_hexdump(start, len));
		osmo_revbytebits_buf(start, len);
		printf("REVERSED: %s\n", osmo_hexdump(start, len));
		if (memcmp(start, exp_out + offs, len)) {
			printf("EXPECTED: %s\n", osmo_hexdump(exp_out+offs, len));
			fprintf(stderr, "REVERSED != EXPECTED!\n");
			exit(1);
		}
		printf("\n");
	}

	return 0;
}
