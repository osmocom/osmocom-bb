/* bit vector utility routines */

/* (C) 2009 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! \addtogroup bitvec
 *  @{
 */

/*! \file bitvec.c
 *  \brief Osmocom bit vector abstraction
 */

#include <errno.h>
#include <stdint.h>

#include <osmocom/core/bitvec.h>

#define BITNUM_FROM_COMP(byte, bit)	((byte*8)+bit)

static inline unsigned int bytenum_from_bitnum(unsigned int bitnum)
{
	unsigned int bytenum = bitnum / 8;

	return bytenum;
}

/* convert ZERO/ONE/L/H to a bitmask at given pos in a byte */
static uint8_t bitval2mask(enum bit_value bit, uint8_t bitnum)
{
	int bitval;

	switch (bit) {
	case ZERO:
		bitval = (0 << bitnum);
		break;
	case ONE:
		bitval = (1 << bitnum);
		break;
	case L:
		bitval = ((0x2b ^ (0 << bitnum)) & (1 << bitnum));
		break;
	case H:
		bitval = ((0x2b ^ (1 << bitnum)) & (1 << bitnum));
		break;
	default:
		return 0;
	}
	return bitval;
}

/*! \brief check if the bit is 0 or 1 for a given position inside a bitvec
 *  \param[in] bv the bit vector on which to check
 *  \param[in] bitnr the bit number inside the bit vector to check
 *  \returns 
 */
enum bit_value bitvec_get_bit_pos(const struct bitvec *bv, unsigned int bitnr)
{
	unsigned int bytenum = bytenum_from_bitnum(bitnr);
	unsigned int bitnum = 7 - (bitnr % 8);
	uint8_t bitval;

	if (bytenum >= bv->data_len)
		return -EINVAL;

	bitval = bitval2mask(ONE, bitnum);

	if (bv->data[bytenum] & bitval)
		return ONE;

	return ZERO;
}

/*! \brief check if the bit is L or H for a given position inside a bitvec
 *  \param[in] bv the bit vector on which to check
 *  \param[in] bitnr the bit number inside the bit vector to check
 */
enum bit_value bitvec_get_bit_pos_high(const struct bitvec *bv,
					unsigned int bitnr)
{
	unsigned int bytenum = bytenum_from_bitnum(bitnr);
	unsigned int bitnum = 7 - (bitnr % 8);
	uint8_t bitval;

	if (bytenum >= bv->data_len)
		return -EINVAL;

	bitval = bitval2mask(H, bitnum);

	if ((bv->data[bytenum] & (1 << bitnum)) == bitval)
		return H;

	return L;
}

/*! \brief get the Nth set bit inside the bit vector
 *  \param[in] bv the bit vector to use
 *  \param[in] n the bit number to get
 *  \returns the bit number (offset) of the Nth set bit in \a bv
 */
unsigned int bitvec_get_nth_set_bit(const struct bitvec *bv, unsigned int n)
{
	unsigned int i, k = 0;

	for (i = 0; i < bv->data_len*8; i++) {
		if (bitvec_get_bit_pos(bv, i) == ONE) {
			k++;
			if (k == n)
				return i;
		}
	}

	return 0;
}

/*! \brief set a bit at given position in a bit vector
 *  \param[in] bv bit vector on which to operate
 *  \param[in] bitnum number of bit to be set
 *  \param[in] bit value to which the bit is to be set
 */
int bitvec_set_bit_pos(struct bitvec *bv, unsigned int bitnr,
			enum bit_value bit)
{
	unsigned int bytenum = bytenum_from_bitnum(bitnr);
	unsigned int bitnum = 7 - (bitnr % 8);
	uint8_t bitval;

	if (bytenum >= bv->data_len)
		return -EINVAL;

	/* first clear the bit */
	bitval = bitval2mask(ONE, bitnum);
	bv->data[bytenum] &= ~bitval;

	/* then set it to desired value */
	bitval = bitval2mask(bit, bitnum);
	bv->data[bytenum] |= bitval;

	return 0;
}

/*! \brief set the next bit inside a bitvec
 *  \param[in] bv bit vector to be used
 *  \param[in] bit value of the bit to be set
 */
int bitvec_set_bit(struct bitvec *bv, enum bit_value bit)
{
	int rc;

	rc = bitvec_set_bit_pos(bv, bv->cur_bit, bit);
	if (!rc)
		bv->cur_bit++;

	return rc;
}

/*! \brief get the next bit (low/high) inside a bitvec */
int bitvec_get_bit_high(struct bitvec *bv)
{
	int rc;

	rc = bitvec_get_bit_pos_high(bv, bv->cur_bit);
	if (rc >= 0)
		bv->cur_bit++;

	return rc;
}

/*! \brief set multiple bits (based on array of bitvals) at current pos
 *  \param[in] bv bit vector
 *  \param[in] bits array of \ref bit_value
 *  \param[in] count number of bits to set
 */
int bitvec_set_bits(struct bitvec *bv, enum bit_value *bits, int count)
{
	int i, rc;

	for (i = 0; i < count; i++) {
		rc = bitvec_set_bit(bv, bits[i]);
		if (rc)
			return rc;
	}

	return 0;
}

/*! \brief set multiple bits (based on numeric value) at current pos */
int bitvec_set_uint(struct bitvec *bv, unsigned int ui, int num_bits)
{
	int i, rc;

	for (i = 0; i < num_bits; i++) {
		int bit = 0;
		if (ui & (1 << (num_bits - i - 1)))
			bit = 1;
		rc = bitvec_set_bit(bv, bit);
		if (rc)
			return rc;
	}

	return 0;
}

/*! \brief get multiple bits (based on numeric value) from current pos */
int bitvec_get_uint(struct bitvec *bv, int num_bits)
{
	int i;
	unsigned int ui = 0;

	for (i = 0; i < num_bits; i++) {
		int bit = bitvec_get_bit_pos(bv, bv->cur_bit);
		if (bit < 0)
			return bit;
		if (bit)
			ui |= (1 << (num_bits - i - 1));
		bv->cur_bit++;
	}

	return ui;
}

/*! \brief pad all remaining bits up to num_bits */
int bitvec_spare_padding(struct bitvec *bv, unsigned int up_to_bit)
{
	unsigned int i;

	for (i = bv->cur_bit; i <= up_to_bit; i++)
		bitvec_set_bit(bv, L);

	return 0;
}

/*! \brief find first bit set in bit vector */
int bitvec_find_bit_pos(const struct bitvec *bv, unsigned int n,
			enum bit_value val)
{
	unsigned int i;

	for (i = n; i < bv->data_len*8; i++) {
		if (bitvec_get_bit_pos(bv, i) == val)
			return i;
	}

	return -1;
}

/*! @} */
