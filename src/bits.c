
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

/* generalized bit reversal function, Chapter 7 "Hackers Delight" */
uint32_t osmo_bit_reversal(uint32_t x, enum osmo_br_mode k)
{
	if (k &  1) x = (x & 0x55555555) <<  1 | (x & 0xAAAAAAAA) >>  1;
	if (k &  2) x = (x & 0x33333333) <<  2 | (x & 0xCCCCCCCC) >>  2;
	if (k &  4) x = (x & 0x0F0F0F0F) <<  4 | (x & 0xF0F0F0F0) >>  4;
	if (k &  8) x = (x & 0x00FF00FF) <<  8 | (x & 0xFF00FF00) >>  8;
	if (k & 16) x = (x & 0x0000FFFF) << 16 | (x & 0xFFFF0000) >> 16;

	return x;
}

/* generalized bit reversal function, Chapter 7 "Hackers Delight" */
uint32_t osmo_revbytebits_32(uint32_t x)
{
	x = (x & 0x55555555) <<  1 | (x & 0xAAAAAAAA) >>  1;
	x = (x & 0x33333333) <<  2 | (x & 0xCCCCCCCC) >>  2;
	x = (x & 0x0F0F0F0F) <<  4 | (x & 0xF0F0F0F0) >>  4;

	return x;
}

uint32_t osmo_revbytebits_8(uint8_t x)
{
	x = (x & 0x55) <<  1 | (x & 0xAA) >>  1;
	x = (x & 0x33) <<  2 | (x & 0xCC) >>  2;
	x = (x & 0x0F) <<  4 | (x & 0xF0) >>  4;

	return x;
}

void osmo_revbytebits_buf(uint8_t *buf, int len)
{
	unsigned int i;
	unsigned int unaligned_cnt;
	int len_remain = len;

	unaligned_cnt = ((unsigned long)buf & 3);
	for (i = 0; i < unaligned_cnt; i++) {
		buf[i] = osmo_revbytebits_8(buf[i]);
		len_remain--;
		if (len_remain <= 0)
			return;
	}

	for (i = unaligned_cnt; i < len; i += 4) {
		uint32_t *cur = (uint32_t *) (buf + i);
		*cur = osmo_revbytebits_32(*cur);
		len_remain -= 4;
	}

	for (i = len - len_remain; i < len; i++) {
		buf[i] = osmo_revbytebits_8(buf[i]);
		len_remain--;
	}
}

/*! @} */
