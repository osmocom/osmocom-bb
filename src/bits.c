
#include <stdint.h>

#include <osmocore/bits.h>

/* convert unpacked bits to packed bits, return length in bytes */
int osmo_ubit2pbit(pbit_t *out, const ubit_t *in, unsigned int num_bits)
{
	unsigned int i;
	uint8_t curbyte = 0;
	pbit_t *outptr = out;

	for (i = 0; i < num_bits; i++) {
		uint8_t bitnum = 7 - (i % 8);

		curbyte |= (in[i] << bitnum);

		if (i > 0 && i % 8 == 0) {
			*outptr++ = curbyte;
			curbyte = 0;
		}
	}
	/* we have a non-modulo-8 bitcount */
	if (i % 8)
		*outptr++ = curbyte;

	return outptr - out;
}

/* convert packed bits to unpacked bits, return length in bytes */
int osmo_pbit2ubit(ubit_t *out, const pbit_t *in, unsigned int num_bits)
{
	unsigned int i;
	ubit_t *cur = out;
	ubit_t *limit = out + num_bits;

	for (i = 0; i < (num_bits/8)+1; i++) {
		pbit_t byte = in[i];
		*cur++ = (byte >> 7) & 1;
		if (cur >= limit)
			break;
		*cur++ = (byte >> 6) & 1;
		if (cur >= limit)
			break;
		*cur++ = (byte >> 5) & 1;
		if (cur >= limit)
			break;
		*cur++ = (byte >> 4) & 1;
		if (cur >= limit)
			break;
		*cur++ = (byte >> 3) & 1;
		if (cur >= limit)
			break;
		*cur++ = (byte >> 2) & 1;
		if (cur >= limit)
			break;
		*cur++ = (byte >> 1) & 1;
		if (cur >= limit)
			break;
		*cur++ = (byte >> 0) & 1;
		if (cur >= limit)
			break;
	}
	return cur - out;
}
