#ifndef _OSMO_BITS_H
#define _OSMO_BITS_H

#include <stdint.h>

typedef uint8_t sbit_t;		/* soft bit (-127...127) */
typedef uint8_t ubit_t;		/* unpacked bit (0 or 1) */
typedef uint8_t pbit_t;		/* packed bis (8 bits in a byte) */

/* determine how many bytes we would need for 'num_bits' packed bits */
static inline unsigned int osmo_pbit_bytesize(unsigned int num_bits)
{
	unsigned int pbit_bytesize = num_bits / 8;

	if (num_bits % 8)
		pbit_bytesize++;

	return pbit_bytesize;
}

/* convert unpacked bits to packed bits, return length in bytes */
int osmo_ubit2pbit(pbit_t *out, const ubit_t *in, unsigned int num_bits);

/* convert packed bits to unpacked bits, return length in bytes */
int osmo_pbit2ubit(ubit_t *out, const pbit_t *in, unsigned int num_bits);

#endif
