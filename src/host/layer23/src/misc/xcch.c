/*
 * xcch.c
 *
 * Copyright (c) 2011  Sylvain Munaut <tnt@246tNt.com>
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/conv.h>


/*
 * GSM xCCH parity (FIRE code)
 *
 * g(x) = (x^23 + 1)(x^17 + x^3 + 1)
 *      = x^40 + x^26 + x^23 + x^17 + x^3 + 1
 */


static int
xcch_parity_check(ubit_t *d)
{
	const uint64_t poly      = 0x0004820009ULL;
	const uint64_t remainder = 0xffffffffffULL;
	uint64_t crc = 0;
	int i;

	/* Compute CRC */
	for (i=0; i<184; i++) {
		uint64_t bit = d[i] & 1;
		crc ^= (bit << 39);
		if (crc & (1ULL<<39)) {
			crc <<= 1;
			crc ^= poly;
		} else {
			crc <<= 1;
		}
		crc &= (1ULL << 40) - 1;
	}

	crc ^= remainder;

	/* Check it */
	for (i=0; i<40; i++)
		if (d[184+i] ^ ((crc >> (39-i)) & 1))
			return 1;

	return 0;
}


/*
 * GSM xCCH convolutional coding
 *
 * G_0 = 1 + x^3 + x^4
 * G_1 = 1 + x + x^3 + x^4
 */

static const uint8_t conv_xcch_next_output[][2] = {
	{ 0, 3 }, { 1, 2 }, { 0, 3 }, { 1, 2 },
	{ 3, 0 }, { 2, 1 }, { 3, 0 }, { 2, 1 },
	{ 3, 0 }, { 2, 1 }, { 3, 0 }, { 2, 1 },
	{ 0, 3 }, { 1, 2 }, { 0, 3 }, { 1, 2 },
};

static const uint8_t conv_xcch_next_state[][2] = {
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
	{  0,  1 }, {  2,  3 }, {  4,  5 }, {  6,  7 },
	{  8,  9 }, { 10, 11 }, { 12, 13 }, { 14, 15 },
};

const struct osmo_conv_code conv_xcch = {
	.N = 2,
	.K = 5,
	.len = 224,
	.next_output = conv_xcch_next_output,
	.next_state  = conv_xcch_next_state,
};


/*
 * GSM xCCH interleaving and burst mapping
 *
 * Interleaving:
 *
 * Given 456 coded input bits, form 4 blocks of 114 bits:
 *
 *      i(B, j) = c(n, k)       k = 0, ..., 455
 *                              n = 0, ..., N, N + 1, ...
 *                              B = B_0 + 4n + (k mod 4)
 *                              j = 2(49k mod 57) + ((k mod 8) div 4)
 *
 * Mapping on Burst:
 *
 *      e(B, j) = i(B, j)
 *      e(B, 59 + j) = i(B, 57 + j)     j = 0, ..., 56
 *      e(B, 57) = h_l(B)
 *      e(B, 58) = h_n(B)
 *
 * Where hl(B) and hn(B) are bits in burst B indicating flags.
 */

static void
xcch_deinterleave(sbit_t *cB, sbit_t *iB)
{
	int j, k, B;

	for (k=0; k<456; k++) {
		B = k & 3;
		j = 2 * ((49 * k) % 57) + ((k & 7) >> 2);
		cB[k] = iB[B * 114 + j];
	}
}

static void
xcch_burst_unmap(sbit_t *iB, sbit_t *eB, sbit_t *hl, sbit_t *hn)
{
	memcpy(iB,    eB,    57);
	memcpy(iB+57, eB+59, 57);

	if (hl)
		*hl = eB[57];

	if (hn)
		*hn = eB[58];
}

int
xcch_decode(uint8_t *l2_data, sbit_t *bursts)
{
	sbit_t iB[456], cB[456];
	ubit_t conv[224];
	int i, rv;

	for (i=0; i<4; i++)
		xcch_burst_unmap(&iB[i * 114], &bursts[i * 116], NULL, NULL);

	xcch_deinterleave(cB, iB);

	osmo_conv_decode(&conv_xcch, cB, conv);

	rv = xcch_parity_check(conv);
	if (rv)
		return -1;

	osmo_ubit2pbit_ext(l2_data, 0, conv, 0, 184, 1);

	return 0;
}
