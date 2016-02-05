/* bit compression routines */

/* (C) 2016 sysmocom s.f.m.c. GmbH by Max Suraev <msuraev@sysmocom.de>
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

/*! \defgroup bitcomp Bit compression
 *  @{
 */

/*! \file bitcomp.c
 *  \brief Osmocom bit compression routines
 */

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include <osmocom/core/bitvec.h>
#include <osmocom/core/bitcomp.h>

/*
 * Terminating codes for uninterrupted sequences of 0 and 1 up to 64 bit length
 * according to TS 44.060 9.1.10
 */
static const unsigned t4_term[2][64] = {
	{
		0b0000110111,
		0b10,
		0b11,
		0b010,
		0b011,
		0b0011,
		0b0010,
		0b00011,
		0b000101,
		0b000100,
		0b0000100,
		0b0000101,
		0b0000111,
		0b00000100,
		0b00000111,
		0b000011000,
		0b0000010111,
		0b0000011000,
		0b0000001000,
		0b00001100111,
		0b00001101000,
		0b00001101100,
		0b00000110111,
		0b00000101000,
		0b00000010111,
		0b00000011000,
		0b000011001010,
		0b000011001011,
		0b000011001100,
		0b000011001101,
		0b000001101000,
		0b000001101001,
		0b000001101010,
		0b000001101011,
		0b000011010010,
		0b000011010011,
		0b000011010100,
		0b000011010101,
		0b000011010110,
		0b000011010111,
		0b000001101100,
		0b000001101101,
		0b000011011010,
		0b000011011011,
		0b000001010100,
		0b000001010101,
		0b000001010110,
		0b000001010111,
		0b000001100100,
		0b000001100101,
		0b000001010010,
		0b000001010011,
		0b000000100100,
		0b000000110111,
		0b000000111000,
		0b000000100111,
		0b000000101000,
		0b000001011000,
		0b000001011001,
		0b000000101011,
		0b000000101100,
		0b000001011010,
		0b000001100110,
		0b000001100111
	},
	{
		0b00110101,
		0b000111,
		0b0111,
		0b1000,
		0b1011,
		0b1100,
		0b1110,
		0b1111,
		0b10011,
		0b10100,
		0b00111,
		0b01000,
		0b001000,
		0b000011,
		0b110100,
		0b110101,
		0b101010,
		0b101011,
		0b0100111,
		0b0001100,
		0b0001000,
		0b0010111,
		0b0000011,
		0b0000100,
		0b0101000,
		0b0101011,
		0b0010011,
		0b0100100,
		0b0011000,
		0b00000010,
		0b00000011,
		0b00011010,
		0b00011011,
		0b00010010,
		0b00010011,
		0b00010100,
		0b00010101,
		0b00010110,
		0b00010111,
		0b00101000,
		0b00101001,
		0b00101010,
		0b00101011,
		0b00101100,
		0b00101101,
		0b00000100,
		0b00000101,
		0b00001010,
		0b00001011,
		0b01010010,
		0b01010011,
		0b01010100,
		0b01010101,
		0b00100100,
		0b00100101,
		0b01011000,
		0b01011001,
		0b01011010,
		0b01011011,
		0b01001010,
		0b01001011,
		0b00110010,
		0b00110011,
		0b00110100
	}
};

static const unsigned t4_term_length[2][64] = {
	{10, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 9, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12},
	{8, 6, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8}
};

static const unsigned t4_min_term_length[] = {2, 4};
static const unsigned t4_min_make_up_length[] = {10, 5};

static const unsigned t4_max_term_length[] = {12, 8};
static const unsigned t4_max_make_up_length[] = {13, 9};

static const unsigned t4_make_up_length[2][15] = {
	{10, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13},
	{5, 5, 6, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9}
};

static const unsigned t4_make_up_ind[15] = {64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};

static const unsigned t4_make_up[2][15] = {
	{
		0b0000001111,
		0b000011001000,
		0b000011001001,
		0b000001011011,
		0b000000110011,
		0b000000110100,
		0b000000110101,
		0b0000001101100,
		0b0000001101101,
		0b0000001001010,
		0b0000001001011,
		0b0000001001100,
		0b0000001001101,
		0b0000001110010,
		0b0000001110011
	},
	{
		0b11011,
		0b10010,
		0b010111,
		0b0110111,
		0b00110110,
		0b00110111,
		0b01100100,
		0b01100101,
		0b01101000,
		0b01100111,
		0b011001100,
		0b011001101,
		0b011010010,
		0b011010011,
		0b011010100
	 }
};

/*! \brief Attempt to decode compressed bit vector
 *
 * Return length of RLE according to modified ITU-T T.4 from TS 44.060 Table 9.1.10.2
 * or -1 if no applicable RLE found
 * N. B: we need explicit bit length to make decoding unambiguous
*/
static inline int t4_rle_term(unsigned w, bool b, unsigned bits)
{
	unsigned i;
	for (i = 0; i < 64; i++)
		if (w == t4_term[b][i] && bits == t4_term_length[b][i])
			return i;
	return -1;
}

static inline int t4_rle_makeup(unsigned w, bool b, unsigned bits)
{
	unsigned i;
	for (i = 0; i < 15; i++)
		if (w == t4_make_up[b][i] && bits == t4_make_up_length[b][i])
			return t4_make_up_ind[i];
	return -1;
}

/*! \brief Make-up codes for a given length
 *
 * Return proper make-up code word for an uninterrupted sequence of b bits
 * of length len according to modified ITU-T T.4 from TS 44.060 Table 9.1.10.2 */
static inline int t4_rle(struct bitvec *bv, unsigned len, bool b)
{
	if (len >= 960) {
		bitvec_set_uint(bv, t4_make_up[b][14], t4_make_up_length[b][14]);
		return bitvec_set_uint(bv, t4_term[b][len - 960], t4_term_length[b][len - 960]);
	}

	if (len >= 896) {
		bitvec_set_uint(bv, t4_make_up[b][13], t4_make_up_length[b][13]);
		return bitvec_set_uint(bv, t4_term[b][len - 896], t4_term_length[b][len - 896]);
	}

	if (len >= 832) {
		bitvec_set_uint(bv, t4_make_up[b][12], t4_make_up_length[b][12]);
		return bitvec_set_uint(bv, t4_term[b][len - 832], t4_term_length[b][len - 832]);
	}

	if (len >= 768) {
		bitvec_set_uint(bv, t4_make_up[b][11], t4_make_up_length[b][11]);
		return bitvec_set_uint(bv, t4_term[b][len - 768], t4_term_length[b][len - 768]);
	}

	if (len >= 704) {
		bitvec_set_uint(bv, t4_make_up[b][10], t4_make_up_length[b][10]);
		return bitvec_set_uint(bv, t4_term[b][len - 704], t4_term_length[b][len - 704]);
	}

	if (len >= 640) {
		bitvec_set_uint(bv, t4_make_up[b][9], t4_make_up_length[b][9]);
		return bitvec_set_uint(bv, t4_term[b][len - 640], t4_term_length[b][len - 640]);
	}

	if (len >= 576) {
		bitvec_set_uint(bv, t4_make_up[b][8], t4_make_up_length[b][8]);
		return bitvec_set_uint(bv, t4_term[b][len - 576], t4_term_length[b][len - 576]);
	}

	if (len >= 512) {
		bitvec_set_uint(bv, t4_make_up[b][7], t4_make_up_length[b][7]);
		return bitvec_set_uint(bv, t4_term[b][len - 512], t4_term_length[b][len - 512]);
	}

	if (len >= 448) {
		bitvec_set_uint(bv, t4_make_up[b][6], t4_make_up_length[b][6]);
		return bitvec_set_uint(bv, t4_term[b][len - 448], t4_term_length[b][len - 448]);
	}

	if (len >= 384) {
		bitvec_set_uint(bv, t4_make_up[b][5], t4_make_up_length[b][5]);
		return bitvec_set_uint(bv, t4_term[b][len - 384], t4_term_length[b][len - 384]);
	}

	if (len >= 320) {
		bitvec_set_uint(bv, t4_make_up[b][4], t4_make_up_length[b][4]);
		return bitvec_set_uint(bv, t4_term[b][len - 320], t4_term_length[b][len - 320]);
	}

	if (len >= 256) {
		bitvec_set_uint(bv, t4_make_up[b][3], t4_make_up_length[b][3]);
		return bitvec_set_uint(bv, t4_term[b][len - 256], t4_term_length[b][len - 256]);
	}

	if (len >= 192) {
		bitvec_set_uint(bv, t4_make_up[b][2], t4_make_up_length[b][2]);
		return bitvec_set_uint(bv, t4_term[b][len - 192], t4_term_length[b][len - 192]);
	}

	if (len >= 128) {
		bitvec_set_uint(bv, t4_make_up[b][1], t4_make_up_length[b][1]);
		return bitvec_set_uint(bv, t4_term[b][len - 128], t4_term_length[b][len - 128]);
	}

	if (len >= 64) {
		bitvec_set_uint(bv, t4_make_up[b][0], t4_make_up_length[b][0]);
		return bitvec_set_uint(bv, t4_term[b][len - 64], t4_term_length[b][len - 64]);
	}

	return bitvec_set_uint(bv, t4_term[b][len], t4_term_length[b][len]);
}

enum dec_state {
	EXPECT_TERM,
	TOO_LONG,
	NEED_MORE_BITS,
	CORRUPT,
	OK
};

static inline enum dec_state _t4_step(struct bitvec *v, uint16_t w, bool b, unsigned bits, bool term_only)
{
	if (bits > t4_max_make_up_length[b])
		return TOO_LONG;
	if (bits < t4_min_term_length[b])
		return NEED_MORE_BITS;

	if (term_only) {
		if (bits > t4_max_term_length[b])
			return CORRUPT;
		int t = t4_rle_term(w, b, bits);
		if (-1 != t) {
			bitvec_fill(v, t, b ? ONE : ZERO);
			return OK;
		}
		return NEED_MORE_BITS;
	}

	int m = t4_rle_makeup(w, b, bits);
	if (-1 != m) {
		bitvec_fill(v, m, b ? ONE : ZERO);
		return EXPECT_TERM;
	}

	m = t4_rle_term(w, b, bits);
	if (-1 != m) {
		bitvec_fill(v, m, b ? ONE : ZERO);
		return OK;
	}

	return NEED_MORE_BITS;
}

/*! \brief decode T4-encoded bit vector
 *  Assumes MSB first encoding.
 *  \param[in] in bit vector with encoded data
 *  \param[in] cc color code (whether decoding should start with 1 or 0)
 *  \param[out] out the bit vector to store result into
 * returns 0 on success, negative value otherwise
 */
int osmo_t4_decode(const struct bitvec *in, bool cc, struct bitvec *out)
{
	uint8_t orig[in->data_len];
	struct bitvec vec;
	vec.data = orig;
	vec.data_len = in->data_len;
	bitvec_zero(&vec);
	memcpy(vec.data, in->data, in->data_len);
	vec.cur_bit = in->cur_bit;

	/* init decoder using known color code: */
	unsigned bits = t4_min_term_length[cc];
	enum dec_state d;
	int16_t w = bitvec_get_int16_msb(&vec, bits);
	bool b = cc;
	bool term_only = false;

	while (vec.cur_bit > 0) {
		d = _t4_step(out, w, b, bits, term_only);

		switch (d) {
		case EXPECT_TERM:
			bitvec_shiftl(&vec, bits);
			bits = t4_min_term_length[b];
			w = bitvec_get_int16_msb(&vec, bits);
			term_only = true;
			break;
		case OK:
			bitvec_shiftl(&vec, bits);
			bits = t4_min_term_length[!b];
			w = bitvec_get_int16_msb(&vec, bits);
			b = !b;
			term_only = false;
			break;
		case NEED_MORE_BITS:
			bits++;
			w = bitvec_get_int16_msb(&vec, bits);
			break;
		case TOO_LONG:
			return -E2BIG;
		case CORRUPT:
			return -EINVAL;
		}
	}

	return 0;
}

/*! \brief encode bit vector in-place using T4 encoding
 *  Assumes MSB first encoding.
 *  \param[in] bv bit vector to be encoded
 * returns color code (if the encoding started with 0 or 1) or -1 on failure (encoded is bigger than original)
 */
int osmo_t4_encode(struct bitvec *bv)
{
	unsigned rl0 = bitvec_rl(bv, false), rl1 = bitvec_rl(bv, true);
	int r = (rl0 > rl1) ? 0 : 1;
	uint8_t orig[bv->data_len], tmp[bv->data_len * 2]; /* FIXME: better estimate max possible encoding overhead */
	struct bitvec comp, vec;
	comp.data = tmp;
	comp.data_len = bv->data_len * 2;
	bitvec_zero(&comp);
	vec.data = orig;
	vec.data_len = bv->data_len;
	bitvec_zero(&vec);
	memcpy(vec.data, bv->data, bv->data_len);
	vec.cur_bit = bv->cur_bit;

	while (vec.cur_bit > 0) {
		if (rl0 > rl1) {
			bitvec_shiftl(&vec, rl0);
			t4_rle(&comp, rl0, false);
		} else {
			bitvec_shiftl(&vec, rl1);
			t4_rle(&comp, rl1, true);
		}
		/*
		  TODO: implement backtracking for optimal encoding
		  printf(" -> [%d/%d]", comp.cur_bit + vec.cur_bit, bv->cur_bit);
		*/
		rl0 = bitvec_rl(&vec, false);
		rl1 = bitvec_rl(&vec, true);
	}
	if (comp.cur_bit < bv->cur_bit) {
		memcpy(bv->data, tmp, bv->data_len);
		bv->cur_bit = comp.cur_bit;
		return r;
	}
	return -1;
}


