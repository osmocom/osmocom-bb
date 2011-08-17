#ifndef _OSMO_BITS_H
#define _OSMO_BITS_H

#include <stdint.h>

/*! \defgroup bits soft, unpacked and packed bits
 *  @{
 */

/*! \file bits.h
 *  \brief Osmocom bit level support code
 */

typedef int8_t  sbit_t;		/*!< \brief soft bit (-127...127) */
typedef uint8_t ubit_t;		/*!< \brief unpacked bit (0 or 1) */
typedef uint8_t pbit_t;		/*!< \brief packed bis (8 bits in a byte) */

/*
   NOTE on the endianess of pbit_t:
   Bits in a pbit_t are ordered MSB first, i.e. 0x80 is the first bit.
   Bit i in a pbit_t array is array[i/8] & (1<<(7-i%8))
*/

/*! \brief determine how many bytes we would need for \a num_bits packed bits
 *  \param[in] num_bits Number of packed bits
 */
static inline unsigned int osmo_pbit_bytesize(unsigned int num_bits)
{
	unsigned int pbit_bytesize = num_bits / 8;

	if (num_bits % 8)
		pbit_bytesize++;

	return pbit_bytesize;
}

int osmo_ubit2pbit(pbit_t *out, const ubit_t *in, unsigned int num_bits);

int osmo_pbit2ubit(ubit_t *out, const pbit_t *in, unsigned int num_bits);

int osmo_ubit2pbit_ext(pbit_t *out, unsigned int out_ofs,
                       const ubit_t *in, unsigned int in_ofs,
                       unsigned int num_bits, int lsb_mode);

int osmo_pbit2ubit_ext(ubit_t *out, unsigned int out_ofs,
                       const pbit_t *in, unsigned int in_ofs,
                       unsigned int num_bits, int lsb_mode);

/*! }@ */

#endif /* _OSMO_BITS_H */
