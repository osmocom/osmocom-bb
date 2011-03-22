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


/* In GSM mac blocks, every bit can be 0 or 1, or L or H.  L/H are
 * defined relative to the 0x2b padding pattern */
enum bit_value {
	ZERO	= 0,
	ONE	= 1,
	L	= 2,
	H	= 3,
};

struct bitvec {
	unsigned int cur_bit;	/* curser to the next unused bit */
	unsigned int data_len;	/* length of data array in bytes */
	uint8_t *data;		/* pointer to data array */
};

/* check if the bit is 0 or 1 for a given position inside a bitvec */
enum bit_value bitvec_get_bit_pos(const struct bitvec *bv, unsigned int bitnr);

/* check if the bit is L or H for a given position inside a bitvec */
enum bit_value bitvec_get_bit_pos_high(const struct bitvec *bv,
					unsigned int bitnr);

/* get the Nth set bit inside the bit vector */
unsigned int bitvec_get_nth_set_bit(const struct bitvec *bv, unsigned int n);

/* Set a bit at given position */
int bitvec_set_bit_pos(struct bitvec *bv, unsigned int bitnum,
			enum bit_value bit);

/* Set the next bit in the vector */
int bitvec_set_bit(struct bitvec *bv, enum bit_value bit);

/* get the next bit (low/high) inside a bitvec */
int bitvec_get_bit_high(struct bitvec *bv);

/* Set multiple bits at the current position */
int bitvec_set_bits(struct bitvec *bv, enum bit_value *bits, int count);

/* Add an unsigned integer (of length count bits) to current position */
int bitvec_set_uint(struct bitvec *bv, unsigned int in, int count);

/* get multiple bits (based on numeric value) from current pos */
int bitvec_get_uint(struct bitvec *bv, int num_bits);


/* Pad the bit vector up to a certain bit position */
int bitvec_spare_padding(struct bitvec *bv, unsigned int up_to_bit);

#endif /* _BITVEC_H */
