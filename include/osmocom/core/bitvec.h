#ifndef _BITVEC_H
#define _BITVEC_H

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

/*! \defgroup bitvec Bit vectors
 *  @{
 */

/*! \file bitvec.h
 *  \brief Osmocom bit vector abstraction
 */

#include <stdint.h>

/*! \brief A single GSM bit
 *
 * In GSM mac blocks, every bit can be 0 or 1, or L or H.  L/H are
 * defined relative to the 0x2b padding pattern */
enum bit_value {
	ZERO	= 0, 	/*!< \brief A zero (0) bit */
	ONE	= 1,	/*!< \brief A one (1) bit */
	L	= 2,	/*!< \brief A CSN.1 "L" bit */
	H	= 3,	/*!< \brief A CSN.1 "H" bit */
};

/*! \brief structure describing a bit vector */
struct bitvec {
	unsigned int cur_bit;	/*!< \brief curser to the next unused bit */
	unsigned int data_len;	/*!< \brief length of data array in bytes */
	uint8_t *data;		/*!< \brief pointer to data array */
};

enum bit_value bitvec_get_bit_pos(const struct bitvec *bv, unsigned int bitnr);
enum bit_value bitvec_get_bit_pos_high(const struct bitvec *bv,
					unsigned int bitnr);
unsigned int bitvec_get_nth_set_bit(const struct bitvec *bv, unsigned int n);
int bitvec_set_bit_pos(struct bitvec *bv, unsigned int bitnum,
			enum bit_value bit);
int bitvec_set_bit(struct bitvec *bv, enum bit_value bit);
int bitvec_get_bit_high(struct bitvec *bv);
int bitvec_set_bits(struct bitvec *bv, enum bit_value *bits, int count);
int bitvec_set_uint(struct bitvec *bv, unsigned int in, int count);
int bitvec_get_uint(struct bitvec *bv, int num_bits);
int bitvec_find_bit_pos(const struct bitvec *bv, unsigned int n, enum bit_value val);
int bitvec_spare_padding(struct bitvec *bv, unsigned int up_to_bit);

/*! @} */

#endif /* _BITVEC_H */
