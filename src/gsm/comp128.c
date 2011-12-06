/*
 * COMP128 implementation
 *
 *
 * This code is inspired by original code from :
 *  Marc Briceno <marc@scard.org>, Ian Goldberg <iang@cs.berkeley.edu>,
 *  and David Wagner <daw@cs.berkeley.edu>
 *
 * But it has been fully rewritten from various PDFs found online describing
 * the algorithm because the licence of the code referenced above was unclear.
 * A comment snippet from the original code is included below, it describes
 * where the doc came from and how the algorithm was reverse engineered.
 *
 *
 * (C) 2009 by Sylvain Munaut <tnt@246tNt.com>
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

/*
 * --- SNIP ---
 *
 * This code derived from a leaked document from the GSM standards.
 * Some missing pieces were filled in by reverse-engineering a working SIM.
 * We have verified that this is the correct COMP128 algorithm.
 *
 * The first page of the document identifies it as
 * 	_Technical Information: GSM System Security Study_.
 * 	10-1617-01, 10th June 1988.
 * The bottom of the title page is marked
 * 	Racal Research Ltd.
 * 	Worton Drive, Worton Grange Industrial Estate,
 * 	Reading, Berks. RG2 0SB, England.
 * 	Telephone: Reading (0734) 868601   Telex: 847152
 * The relevant bits are in Part I, Section 20 (pages 66--67).  Enjoy!
 *
 * Note: There are three typos in the spec (discovered by
 * reverse-engineering).
 * First, "z = (2 * x[n] + x[n]) mod 2^(9-j)" should clearly read
 * "z = (2 * x[m] + x[n]) mod 2^(9-j)".
 * Second, the "k" loop in the "Form bits from bytes" section is severely
 * botched: the k index should run only from 0 to 3, and clearly the range
 * on "the (8-k)th bit of byte j" is also off (should be 0..7, not 1..8,
 * to be consistent with the subsequent section).
 * Third, SRES is taken from the first 8 nibbles of x[], not the last 8 as
 * claimed in the document.  (And the document doesn't specify how Kc is
 * derived, but that was also easily discovered with reverse engineering.)
 * All of these typos have been corrected in the following code.
 *
 * --- /SNIP ---
 */

#include <string.h>
#include <stdint.h>

/* The compression tables (just copied ...) */
static const uint8_t table_0[512] = {
 102, 177, 186, 162,   2, 156, 112,  75,  55,  25,   8,  12, 251, 193, 246, 188,
 109, 213, 151,  53,  42,  79, 191, 115, 233, 242, 164, 223, 209, 148, 108, 161,
 252,  37, 244,  47,  64, 211,   6, 237, 185, 160, 139, 113,  76, 138,  59,  70,
  67,  26,  13, 157,  63, 179, 221,  30, 214,  36, 166,  69, 152, 124, 207, 116,
 247, 194,  41,  84,  71,   1,  49,  14,  95,  35, 169,  21,  96,  78, 215, 225,
 182, 243,  28,  92, 201, 118,   4,  74, 248, 128,  17,  11, 146, 132, 245,  48,
 149,  90, 120,  39,  87, 230, 106, 232, 175,  19, 126, 190, 202, 141, 137, 176,
 250,  27, 101,  40, 219, 227,  58,  20,  51, 178,  98, 216, 140,  22,  32, 121,
  61, 103, 203,  72,  29, 110,  85, 212, 180, 204, 150, 183,  15,  66, 172, 196,
  56, 197, 158,   0, 100,  45, 153,   7, 144, 222, 163, 167,  60, 135, 210, 231,
 174, 165,  38, 249, 224,  34, 220, 229, 217, 208, 241,  68, 206, 189, 125, 255,
 239,  54, 168,  89, 123, 122,  73, 145, 117, 234, 143,  99, 129, 200, 192,  82,
 104, 170, 136, 235,  93,  81, 205, 173, 236,  94, 105,  52,  46, 228, 198,   5,
  57, 254,  97, 155, 142, 133, 199, 171, 187,  50,  65, 181, 127, 107, 147, 226,
 184, 218, 131,  33,  77,  86,  31,  44,  88,  62, 238,  18,  24,  43, 154,  23,
  80, 159, 134, 111,   9, 114,   3,  91,  16, 130,  83,  10, 195, 240, 253, 119,
 177, 102, 162, 186, 156,   2,  75, 112,  25,  55,  12,   8, 193, 251, 188, 246,
 213, 109,  53, 151,  79,  42, 115, 191, 242, 233, 223, 164, 148, 209, 161, 108,
  37, 252,  47, 244, 211,  64, 237,   6, 160, 185, 113, 139, 138,  76,  70,  59,
  26,  67, 157,  13, 179,  63,  30, 221,  36, 214,  69, 166, 124, 152, 116, 207,
 194, 247,  84,  41,   1,  71,  14,  49,  35,  95,  21, 169,  78,  96, 225, 215,
 243, 182,  92,  28, 118, 201,  74,   4, 128, 248,  11,  17, 132, 146,  48, 245,
  90, 149,  39, 120, 230,  87, 232, 106,  19, 175, 190, 126, 141, 202, 176, 137,
  27, 250,  40, 101, 227, 219,  20,  58, 178,  51, 216,  98,  22, 140, 121,  32,
 103,  61,  72, 203, 110,  29, 212,  85, 204, 180, 183, 150,  66,  15, 196, 172,
 197,  56,   0, 158,  45, 100,   7, 153, 222, 144, 167, 163, 135,  60, 231, 210,
 165, 174, 249,  38,  34, 224, 229, 220, 208, 217,  68, 241, 189, 206, 255, 125,
  54, 239,  89, 168, 122, 123, 145,  73, 234, 117,  99, 143, 200, 129,  82, 192,
 170, 104, 235, 136,  81,  93, 173, 205,  94, 236,  52, 105, 228,  46,   5, 198,
 254,  57, 155,  97, 133, 142, 171, 199,  50, 187, 181,  65, 107, 127, 226, 147,
 218, 184,  33, 131,  86,  77,  44,  31,  62,  88,  18, 238,  43,  24,  23, 154,
 159,  80, 111, 134, 114,   9,  91,   3, 130,  16,  10,  83, 240, 195, 119, 253,
}, table_1[256] = {
  19,  11,  80, 114,  43,   1,  69,  94,  39,  18, 127, 117,  97,   3,  85,  43,
  27, 124,  70,  83,  47,  71,  63,  10,  47,  89,  79,   4,  14,  59,  11,   5,
  35, 107, 103,  68,  21,  86,  36,  91,  85, 126,  32,  50, 109,  94, 120,   6,
  53,  79,  28,  45,  99,  95,  41,  34,  88,  68,  93,  55, 110, 125, 105,  20,
  90,  80,  76,  96,  23,  60,  89,  64, 121,  56,  14,  74, 101,   8,  19,  78,
  76,  66, 104,  46, 111,  50,  32,   3,  39,   0,  58,  25,  92,  22,  18,  51,
  57,  65, 119, 116,  22, 109,   7,  86,  59,  93,  62, 110,  78,  99,  77,  67,
  12, 113,  87,  98, 102,   5,  88,  33,  38,  56,  23,   8,  75,  45,  13,  75,
  95,  63,  28,  49, 123, 120,  20, 112,  44,  30,  15,  98, 106,   2, 103,  29,
  82, 107,  42, 124,  24,  30,  41,  16, 108, 100, 117,  40,  73,  40,   7, 114,
  82, 115,  36, 112,  12, 102, 100,  84,  92,  48,  72,  97,   9,  54,  55,  74,
 113, 123,  17,  26,  53,  58,   4,   9,  69, 122,  21, 118,  42,  60,  27,  73,
 118, 125,  34,  15,  65, 115,  84,  64,  62,  81,  70,   1,  24, 111, 121,  83,
 104,  81,  49, 127,  48, 105,  31,  10,   6,  91,  87,  37,  16,  54, 116, 126,
  31,  38,  13,   0,  72, 106,  77,  61,  26,  67,  46,  29,  96,  37,  61,  52,
 101,  17,  44, 108,  71,  52,  66,  57,  33,  51,  25,  90,   2, 119, 122,  35,
}, table_2[128] = {
 52,  50,  44,   6,  21,  49,  41,  59,  39,  51,  25,  32,  51,  47,  52,  43,
 37,   4,  40,  34,  61,  12,  28,   4,  58,  23,   8,  15,  12,  22,   9,  18,
 55,  10,  33,  35,  50,   1,  43,   3,  57,  13,  62,  14,   7,  42,  44,  59,
 62,  57,  27,   6,   8,  31,  26,  54,  41,  22,  45,  20,  39,   3,  16,  56,
 48,   2,  21,  28,  36,  42,  60,  33,  34,  18,   0,  11,  24,  10,  17,  61,
 29,  14,  45,  26,  55,  46,  11,  17,  54,  46,   9,  24,  30,  60,  32,   0,
 20,  38,   2,  30,  58,  35,   1,  16,  56,  40,  23,  48,  13,  19,  19,  27,
 31,  53,  47,  38,  63,  15,  49,   5,  37,  53,  25,  36,  63,  29,   5,   7,
}, table_3[64] = {
  1,   5,  29,   6,  25,   1,  18,  23,  17,  19,   0,   9,  24,  25,   6,  31,
 28,  20,  24,  30,   4,  27,   3,  13,  15,  16,  14,  18,   4,   3,   8,   9,
 20,   0,  12,  26,  21,   8,  28,   2,  29,   2,  15,   7,  11,  22,  14,  10,
 17,  21,  12,  30,  26,  27,  16,  31,  11,   7,  13,  23,  10,   5,  22,  19,
}, table_4[32] = {
 15,  12,  10,   4,   1,  14,  11,   7,   5,   0,  14,   7,   1,   2,  13,   8,
 10,   3,   4,   9,   6,   0,   3,   2,   5,   6,   8,   9,  11,  13,  15,  12,
};

static const uint8_t *_comp128_table[5] = { table_0, table_1, table_2, table_3, table_4 };


static inline void
_comp128_compression_round(uint8_t *x, int n, const uint8_t *tbl)
{
	int i, j, m, a, b, y, z;
	m = 4 - n;
	for (i=0; i<(1<<n); i++)
		for (j=0; j<(1<<m); j++) {
			a = j + i * (2<<m);
			b = a + (1<<m);
			y = (x[a] + (x[b]<<1)) & ((32<<m)-1);
			z = ((x[a]<<1) + x[b]) & ((32<<m)-1);
			x[a] = tbl[y];
			x[b] = tbl[z];
		}
}

static inline void
_comp128_compression(uint8_t *x)
{
	int n;
	for (n=0; n<5; n++)
		_comp128_compression_round(x, n, _comp128_table[n]);
}

static inline void
_comp128_bitsfrombytes(uint8_t *x, uint8_t *bits)
{
	int i;
	memset(bits, 0x00, 128);
	for (i=0; i<128; i++)
		if (x[i>>2] & (1<<(3-(i&3))))
			bits[i] = 1;
}

static inline void
_comp128_permutation(uint8_t *x, uint8_t *bits)
{
	int i;
	memset(&x[16], 0x00, 16);
	for (i=0; i<128; i++)
		x[(i>>3)+16] |= bits[(i*17) & 127] << (7-(i&7));
}

void
comp128(const uint8_t *ki, const uint8_t *rand, uint8_t *sres, uint8_t *kc)
{
	int i;
	uint8_t x[32], bits[128];

	/* x[16-31] = RAND */
	memcpy(&x[16], rand, 16);

	/* Round 1-7 */
	for (i=0; i<7; i++) {
		/* x[0-15] = Ki */
		memcpy(x, ki, 16);

		/* Compression */
		_comp128_compression(x);

		/* FormBitFromBytes */
		_comp128_bitsfrombytes(x, bits);

		/* Permutation */
		_comp128_permutation(x, bits);
	}

	/* Round 8 (final) */
		/* x[0-15] = Ki */
	memcpy(x, ki, 16);

		/* Compression */
	_comp128_compression(x);

	/* Output stage */
	for (i=0; i<8; i+=2)
		sres[i>>1] = x[i]<<4 | x[i+1];

	for (i=0; i<12; i+=2)
		kc[i>>1] = (x[i + 18] << 6) |
		           (x[i + 19] << 2) |
		           (x[i + 20] >> 2);

	kc[6] = (x[30]<<6) | (x[31]<<2);
	kc[7] = 0;
}

