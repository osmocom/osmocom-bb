/*
 * a5.c
 *
 * Full reimplementation of A5/1,2 (split and threadsafe)
 *
 * The logic behind the algorithm is taken from "A pedagogical implementation
 * of the GSM A5/1 and A5/2 "voice privacy" encryption algorithms." by
 * Marc Briceno, Ian Goldberg, and David Wagner.
 *
 * Copyright (C) 2011  Sylvain Munaut <tnt@246tNt.com>
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
 */

#include <string.h>

#include <osmocom/gsm/a5.h>

void
osmo_a5(int n, const uint8_t *key, uint32_t fn, ubit_t *dl, ubit_t *ul)
{
	switch (n)
	{
	case 0:
		if (dl)
			memset(dl, 0x00, 114);
		if (ul)
			memset(ul, 0x00, 114);
		break;

	case 1:
		osmo_a5_1(key, fn, dl, ul);
		break;

	case 2:
		osmo_a5_2(key, fn, dl, ul);
		break;

	default:
		/* a5/[3..7] not supported here/yet */
		break;
	}
}


/* ------------------------------------------------------------------------ */
/* A5/1&2 common stuff                                                                     */
/* ------------------------------------------------------------------------ */

#define A5_R1_LEN	19
#define A5_R2_LEN	22
#define A5_R3_LEN	23
#define A5_R4_LEN	17	/* A5/2 only */

#define A5_R1_MASK	((1<<A5_R1_LEN)-1)
#define A5_R2_MASK	((1<<A5_R2_LEN)-1)
#define A5_R3_MASK	((1<<A5_R3_LEN)-1)
#define A5_R4_MASK	((1<<A5_R4_LEN)-1)

#define A5_R1_TAPS	0x072000 /* x^19 + x^5 + x^2 + x + 1 */
#define A5_R2_TAPS	0x300000 /* x^22 + x + 1 */
#define A5_R3_TAPS	0x700080 /* x^23 + x^15 + x^2 + x + 1 */
#define A5_R4_TAPS	0x010800 /* x^17 + x^5 + 1 */

static inline uint32_t
_a5_12_parity(uint32_t x)
{
	x ^= x >> 16;
	x ^= x >> 8;
	x ^= x >> 4;
	x ^= x >> 2;
	x ^= x >> 1;
	return x & 1;
}

static inline uint32_t
_a5_12_majority(uint32_t v1, uint32_t v2, uint32_t v3)
{
	return (!!v1 + !!v2 + !!v3) >= 2;
}

static inline uint32_t
_a5_12_clock(uint32_t r, uint32_t mask, uint32_t taps)
{
	return ((r << 1) & mask) | _a5_12_parity(r & taps);
}


/* ------------------------------------------------------------------------ */
/* A5/1                                                                     */
/* ------------------------------------------------------------------------ */

#define A51_R1_CLKBIT	0x000100
#define A51_R2_CLKBIT	0x000400
#define A51_R3_CLKBIT	0x000400

static inline void
_a5_1_clock(uint32_t r[], int force)
{
	int cb[3], maj;

	cb[0] = !!(r[0] & A51_R1_CLKBIT);
	cb[1] = !!(r[1] & A51_R2_CLKBIT);
	cb[2] = !!(r[2] & A51_R3_CLKBIT);

	maj = _a5_12_majority(cb[0], cb[1], cb[2]);

	if (force || (maj == cb[0]))
		r[0] = _a5_12_clock(r[0], A5_R1_MASK, A5_R1_TAPS);

	if (force || (maj == cb[1]))
		r[1] = _a5_12_clock(r[1], A5_R2_MASK, A5_R2_TAPS);

	if (force || (maj == cb[2]))
		r[2] = _a5_12_clock(r[2], A5_R3_MASK, A5_R3_TAPS);
}

static inline uint8_t
_a5_1_get_output(uint32_t r[])
{
	return	(r[0] >> (A5_R1_LEN-1)) ^
		(r[1] >> (A5_R2_LEN-1)) ^
		(r[2] >> (A5_R3_LEN-1));
}

void
osmo_a5_1(const uint8_t *key, uint32_t fn, ubit_t *dl, ubit_t *ul)
{
	uint32_t r[3] = {0, 0, 0};
	uint32_t fn_count;
	uint32_t b;
	int i;

	/* Key load */
	for (i=0; i<64; i++)
	{
		b = ( key[7 - (i>>3)] >> (i&7) ) & 1;

		_a5_1_clock(r, 1);

		r[0] ^= b;
		r[1] ^= b;
		r[2] ^= b;
	}

	/* Frame count load */
	fn_count = osmo_a5_fn_count(fn);

	for (i=0; i<22; i++)
	{
		b = (fn_count >> i) & 1;

		_a5_1_clock(r, 1);

		r[0] ^= b;
		r[1] ^= b;
		r[2] ^= b;
	}

	/* Mix */
	for (i=0; i<100; i++)
	{
		_a5_1_clock(r, 0);
	}

	/* Output */
	for (i=0; i<114; i++) {
		_a5_1_clock(r, 0);
		if (dl)
			dl[i] = _a5_1_get_output(r);
	}

	for (i=0; i<114; i++) {
		_a5_1_clock(r, 0);
		if (ul)
			ul[i] = _a5_1_get_output(r);
	}
}


/* ------------------------------------------------------------------------ */
/* A5/2                                                                     */
/* ------------------------------------------------------------------------ */

#define A52_R4_CLKBIT0	0x000400
#define A52_R4_CLKBIT1	0x000008
#define A52_R4_CLKBIT2	0x000080

static inline void
_a5_2_clock(uint32_t r[], int force)
{
	int cb[3], maj;

	cb[0] = !!(r[3] & A52_R4_CLKBIT0);
	cb[1] = !!(r[3] & A52_R4_CLKBIT1);
	cb[2] = !!(r[3] & A52_R4_CLKBIT2);

	maj = (cb[0] + cb[1] + cb[2]) >= 2;

	if (force || (maj == cb[0]))
		r[0] = _a5_12_clock(r[0], A5_R1_MASK, A5_R1_TAPS);

	if (force || (maj == cb[1]))
		r[1] = _a5_12_clock(r[1], A5_R2_MASK, A5_R2_TAPS);

	if (force || (maj == cb[2]))
		r[2] = _a5_12_clock(r[2], A5_R3_MASK, A5_R3_TAPS);

	r[3] = _a5_12_clock(r[3], A5_R4_MASK, A5_R4_TAPS);
}

static inline uint8_t
_a5_2_get_output(uint32_t r[], uint8_t *db)
{
	uint8_t cb, tb;

	tb =	(r[0] >> (A5_R1_LEN-1)) ^
		(r[1] >> (A5_R2_LEN-1)) ^
		(r[2] >> (A5_R3_LEN-1));

	cb = *db;

	*db = (	tb ^
		_a5_12_majority( r[0] & 0x08000, ~r[0] & 0x04000,  r[0] & 0x1000) ^
		_a5_12_majority(~r[1] & 0x10000,  r[1] & 0x02000,  r[1] & 0x0200) ^
		_a5_12_majority( r[2] & 0x40000,  r[2] & 0x10000, ~r[2] & 0x2000)
	);

	return cb;
}

void
osmo_a5_2(const uint8_t *key, uint32_t fn, ubit_t *dl, ubit_t *ul)
{
	uint32_t r[4] = {0, 0, 0, 0};
	uint32_t fn_count;
	uint32_t b;
	uint8_t db = 0, o;
	int i;

	/* Key load */
	for (i=0; i<64; i++)
	{
		b = ( key[7 - (i>>3)] >> (i&7) ) & 1;

		_a5_2_clock(r, 1);

		r[0] ^= b;
		r[1] ^= b;
		r[2] ^= b;
		r[3] ^= b;
	}

	/* Frame count load */
	fn_count = osmo_a5_fn_count(fn);

	for (i=0; i<22; i++)
	{
		b = (fn_count >> i) & 1;

		_a5_2_clock(r, 1);

		r[0] ^= b;
		r[1] ^= b;
		r[2] ^= b;
		r[3] ^= b;
	}

	r[0] |= 1 << 15;
	r[1] |= 1 << 16;
	r[2] |= 1 << 18;
	r[3] |= 1 << 10;

	/* Mix */
	for (i=0; i<100; i++)
	{
		_a5_2_clock(r, 0);
	}

	_a5_2_get_output(r, &db);


	/* Output */
	for (i=0; i<114; i++) {
		_a5_2_clock(r, 0);
		o = _a5_2_get_output(r, &db);
		if (dl)
			dl[i] = o;
	}

	for (i=0; i<114; i++) {
		_a5_2_clock(r, 0);
		o = _a5_2_get_output(r, &db);
		if (ul)
			ul[i] = o;
	}
}
