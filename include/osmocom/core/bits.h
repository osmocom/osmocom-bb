#ifndef _OSMO_BITS_H
#define _OSMO_BITS_H

#include <stdint.h>

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

/*! \brief convert unpacked bits to packed bits, return length in bytes
 *  \param[out] out output buffer of packed bits
 *  \param[in] in input buffer of unpacked bits
 *  \param[in] num_bits number of bits
 */
int osmo_ubit2pbit(pbit_t *out, const ubit_t *in, unsigned int num_bits);

/*! \brief convert packed bits to unpacked bits, return length in bytes
 *  \param[out] out output buffer of unpacked bits
 *  \param[in] in input buffer of packed bits
 *  \param[in] num_bits number of bits
 */
int osmo_pbit2ubit(ubit_t *out, const pbit_t *in, unsigned int num_bits);

/*! \brief convert unpacked bits to packed bits (extended options)
 *  \param[out] out output buffer of packed bits
 *  \param[in] out_ofs offset into output buffer
 *  \param[in] in input buffer of unpacked bits
 *  \param[in] in_ofs offset into input buffer
 *  \param[in] num_bits number of bits
 *  \param[in] lsb_mode Encode bits in LSB orde instead of MSB
 *  \returns length in bytes (max written offset of output buffer + 1)
 */
int osmo_ubit2pbit_ext(pbit_t *out, unsigned int out_ofs,
                       const ubit_t *in, unsigned int in_ofs,
                       unsigned int num_bits, int lsb_mode);

/*! \brief convert packed bits to unpacked bits (extended options)
 *  \param[out] out output buffer of unpacked bits
 *  \param[in] out_ofs offset into output buffer
 *  \param[in] in input buffer of packed bits
 *  \param[in] in_ofs offset into input buffer
 *  \param[in] num_bits number of bits
 *  \param[in] lsb_mode Encode bits in LSB orde instead of MSB
 *  \returns length in bytes (max written offset of output buffer + 1)
 */
int osmo_pbit2ubit_ext(ubit_t *out, unsigned int out_ofs,
                       const pbit_t *in, unsigned int in_ofs,
                       unsigned int num_bits, int lsb_mode);

#endif
