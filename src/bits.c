
#include <stdint.h>

#include <osmocom/core/bits.h>

/*! \addtogroup bits
 *  @{
 */

/*! \file bits.c
 *  \brief Osmocom bit level support code
 */


/*! \brief convert unpacked bits to packed bits, return length in bytes
 *  \param[out] out output buffer of packed bits
 *  \param[in] in input buffer of unpacked bits
 *  \param[in] num_bits number of bits
 */
int osmo_ubit2pbit(pbit_t *out, const ubit_t *in, unsigned int num_bits)
{
	unsigned int i;
	uint8_t curbyte = 0;
	pbit_t *outptr = out;

	for (i = 0; i < num_bits; i++) {
		uint8_t bitnum = 7 - (i % 8);

		curbyte |= (in[i] << bitnum);

		if(i % 8 == 7){
			*outptr++ = curbyte;
			curbyte = 0;
		}
	}
	/* we have a non-modulo-8 bitcount */
	if (i % 8)
		*outptr++ = curbyte;

	return outptr - out;
}

/*! \brief convert packed bits to unpacked bits, return length in bytes
 *  \param[out] out output buffer of unpacked bits
 *  \param[in] in input buffer of packed bits
 *  \param[in] num_bits number of bits
 */
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
                       unsigned int num_bits, int lsb_mode)
{
	int i, op, bn;
	for (i=0; i<num_bits; i++) {
		op = out_ofs + i;
		bn = lsb_mode ? (op&7) : (7-(op&7));
		if (in[in_ofs+i])
			out[op>>3] |= 1 << bn;
		else
			out[op>>3] &= ~(1 << bn);
	}
	return ((out_ofs + num_bits - 1) >> 3) + 1;
}

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
                       unsigned int num_bits, int lsb_mode)
{
	int i, ip, bn;
	for (i=0; i<num_bits; i++) {
		ip = in_ofs + i;
		bn = lsb_mode ? (ip&7) : (7-(ip&7));
		out[out_ofs+i] = !!(in[ip>>3] & (1<<bn));
	}
	return out_ofs + num_bits;
}

/*! }@ */
