/* RF Channel utilities */

/* (C) 2010 by Sylvain Munaut <tnt@246tNt.com>
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

#include <stdint.h>

#include <osmocom/gsm/gsm_utils.h>

#include <layer1/sync.h>


/*
 * Hopping sequence generation
 *
 * The algorithm is explained in GSM 05.02 Section 6.2.3
 *
 *	if HSN = 0 (cyclic hopping) then:
 *		MAI, integer (0 .. N-1) :
 *			MAI = (FN + MAIO) modulo N
 *
 *	else:
 *		M, integer (0 .. 152) :
 *			M = T2 + RNTABLE((HSN xor T1R) + T3)
 *
 *		S, integer (0 .. N-1) :
 *			M' = M modulo (2 ^ NBIN)
 *			T' = T3 modulo (2 ^ NBIN)
 *
 *			if M' < N then:
 *				S = M'
 *			else:
 *				S = (M'+T') modulo N
 *
 *		MAI, integer (0 .. N-1) :
 *			MAI = (S + MAIO) modulo N
 */

static uint8_t rn_table[114] = {
	 48,  98,  63,   1,  36,  95,  78, 102,  94,  73,
	  0,  64,  25,  81,  76,  59, 124,  23, 104, 100,
	101,  47, 118,  85,  18,  56,  96,  86,  54,   2,
	 80,  34, 127,  13,   6,  89,  57, 103,  12,  74,
	 55, 111,  75,  38, 109,  71, 112,  29,  11,  88,
	 87,  19,   3,  68, 110,  26,  33,  31,   8,  45,
	 82,  58,  40, 107,  32,   5, 106,  92,  62,  67,
	 77, 108, 122,  37,  60,  66, 121,  42,  51, 126,
	117, 114,   4,  90,  43,  52,  53, 113, 120,  72,
	 16,  49,   7,  79, 119,  61,  22,  84,   9,  97,
	 91,  15,  21,  24,  46,  39,  93, 105,  65,  70,
	125,  99,  17, 123,
};


static int pow_nbin_mask(int n)
{
	int x;
	x =	(n     ) |
		(n >> 1) |
		(n >> 2) |
		(n >> 3) |
		(n >> 4) |
		(n >> 5) |
		(n >> 6);
	return x;
}

static int16_t rfch_hop_seq_gen(struct gsm_time *t,
                                uint8_t hsn, uint8_t maio,
                                uint8_t n, uint16_t *arfcn_tbl)
{
	int mai;

	if (!hsn) {
		/* cyclic hopping */
		mai = (t->fn + maio) % n;
	} else {
		/* pseudo random hopping */
		int m, mp, tp, s, pnm;

		pnm = pow_nbin_mask(n);

		m = t->t2 + rn_table[(hsn ^ (t->t1 & 63)) + t->t3];
		mp = m & pnm;

		if (mp < n)
			s = mp;
		else {
			tp = t->t3 & pnm;
			s = (mp + tp) % n;
		}

		mai = (s + maio) % n;
	}

	return arfcn_tbl ? arfcn_tbl[mai] : mai;
}


/* RF Channel parameters */
void rfch_get_params(struct gsm_time *t,
                     uint16_t *arfcn_p, uint8_t *tsc_p, uint8_t *tn_p)
{
	if (l1s.dedicated.type == GSM_DCHAN_NONE) {
		/* Serving cell only */
		if (arfcn_p)
			*arfcn_p = l1s.serving_cell.arfcn;

		if (tsc_p)
			*tsc_p = l1s.serving_cell.bsic & 0x7;

		if (tn_p)
			*tn_p = 0;
	} else {
		/* Dedicated channel */
		if (arfcn_p) {
			if (l1s.dedicated.h) {
				*arfcn_p = rfch_hop_seq_gen(t,
						l1s.dedicated.h1.hsn,
						l1s.dedicated.h1.maio,
						l1s.dedicated.h1.n,
						l1s.dedicated.h1.ma);
			} else {
				*arfcn_p = l1s.dedicated.h0.arfcn;
			}
		}

		if (tsc_p)
			*tsc_p = l1s.dedicated.tsc;

		if (tn_p)
			*tn_p = l1s.dedicated.tn;
	}
}

