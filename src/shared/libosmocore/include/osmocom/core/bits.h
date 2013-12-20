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


/* BIT REVERSAL */

/*! \brief bit-reversal mode for osmo_bit_reversal() */
enum osmo_br_mode {
	/*! \brief reverse all bits in a 32bit dword */
	OSMO_BR_BITS_IN_DWORD	= 31,
	/*! \brief reverse byte order in a 32bit dword */
	OSMO_BR_BYTES_IN_DWORD	= 24,
	/*! \brief reverse bits of each byte in a 32bit dword */
	OSMO_BR_BITS_IN_BYTE	= 7,
	/*! \brief swap the two 16bit words in a 32bit dword */
	OSMO_BR_WORD_SWAP	= 16,
};

/*! \brief generic bit reversal function */
uint32_t osmo_bit_reversal(uint32_t x, enum osmo_br_mode k);

/* \brief reverse the bits within each byte of a 32bit word */
uint32_t osmo_revbytebits_32(uint32_t x);

/* \brief reverse the bits within a byte */
uint32_t osmo_revbytebits_8(uint8_t x);

/* \brief reverse the bits of each byte in a given buffer */
void osmo_revbytebits_buf(uint8_t *buf, int len);

/*! @} */

#endif /* _OSMO_BITS_H */
