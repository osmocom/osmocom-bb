/*
 * (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2009,2013 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010-2012 by Nico Golde <nico@ngolde.de>
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

/*! \mainpage libosmogsm Documentation
 *
 * \section sec_intro Introduction
 * This library is a collection of common code used in various
 * GSM related sub-projects inside the Osmocom family of projects.  It
 * includes A5/1 and A5/2 ciphers, COMP128v1, a LAPDm implementation,
 * a GSM TLV parser, SMS utility routines as well as
 * protocol definitions for a series of protocols:
 * 	* Um L2 (04.06)
 * 	* Um L3 (04.08)
 * 	* A-bis RSL (08.58)
 * 	* A-bis OML (08.59, 12.21)
 * 	* A (08.08)
 * \n\n
 * Please note that C language projects inside Osmocom are typically
 * single-threaded event-loop state machine designs.  As such,
 * routines in libosmogsm are not thread-safe.  If you must use them in
 * a multi-threaded context, you have to add your own locking.
 *
 * \section sec_copyright Copyright and License
 * Copyright © 2008-2011 - Harald Welte, Holger Freyther and contributors\n
 * All rights reserved. \n\n
 * The source code of libosmogsm is licensed under the terms of the GNU
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later
 * version.\n
 * See <http://www.gnu.org/licenses/> or COPYING included in the source
 * code package istelf.\n
 * The information detailed here is provided AS IS with NO WARRANTY OF
 * ANY KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.
 * \n\n
 *
 * \section sec_contact Contact and Support
 * Community-based support is available at the OpenBSC mailing list
 * <http://lists.osmocom.org/mailman/listinfo/openbsc>\n
 * Commercial support options available upon request from
 * <http://sysmocom.de/>
 */

//#include <openbsc/gsm_data.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm_utils.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#include "../../config.h"

/* ETSI GSM 03.38 6.2.1 and 6.2.1.1 default alphabet
 * Greek symbols at hex positions 0x10 and 0x12-0x1a
 * left out as they can't be handled with a char and
 * since most phones don't display or write these
 * characters this would only needlessly make the code
 * more complex.
 *
 * Note that this table contains the latin1->7bit mapping _and_ has
 * been merged with the reverse mapping (7bit->latin1) for the
 * extended characters at offset 0x7f.
 */
static unsigned char gsm_7bit_alphabet[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0a, 0xff, 0xff, 0x0d, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x20, 0x21, 0x22, 0x23, 0x02, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c,
	0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
	0x3c, 0x3d, 0x3e, 0x3f, 0x00, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
	0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x3c, 0x2f, 0x3e, 0x14, 0x11, 0xff, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
	0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x28, 0x40, 0x29, 0x3d, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0x0c, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5e, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x40, 0xff, 0x01, 0xff,
	0x03, 0xff, 0x7b, 0x7d, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5c, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5b, 0x7e, 0x5d, 0xff, 0x7c, 0xff, 0xff, 0xff,
	0xff, 0x5b, 0x0e, 0x1c, 0x09, 0xff, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x5d,
	0xff, 0xff, 0xff, 0xff, 0x5c, 0xff, 0x0b, 0xff, 0xff, 0xff, 0x5e, 0xff, 0xff, 0x1e, 0x7f,
	0xff, 0xff, 0xff, 0x7b, 0x0f, 0x1d, 0xff, 0x04, 0x05, 0xff, 0xff, 0x07, 0xff, 0xff, 0xff,
	0xff, 0x7d, 0x08, 0xff, 0xff, 0xff, 0x7c, 0xff, 0x0c, 0x06, 0xff, 0xff, 0x7e, 0xff, 0xff
};

/* GSM 03.38 6.2.1 Character lookup for decoding */
static int gsm_septet_lookup(uint8_t ch)
{
	int i = 0;
	for (; i < sizeof(gsm_7bit_alphabet); i++) {
		if (gsm_7bit_alphabet[i] == ch)
			return i;
	}
	return -1;
}

/* Compute the number of octets from the number of septets, for instance: 47 septets needs 41,125 = 42 octets */
uint8_t gsm_get_octet_len(const uint8_t sept_len){
	int octet_len = (sept_len * 7) / 8;
	if ((sept_len * 7) % 8 != 0)
		octet_len++;

	return octet_len;
}

/* GSM 03.38 6.2.1 Character unpacking */
int gsm_7bit_decode_n_hdr(char *text, size_t n, const uint8_t *user_data, uint8_t septet_l, uint8_t ud_hdr_ind)
{
	unsigned shift = 0;
	uint8_t c7, c8, next_is_ext = 0, lu, ru;
	const uint8_t maxlen = gsm_get_octet_len(septet_l);
	const char *text_buf_begin = text;
	const char *text_buf_end = text + n;

	OSMO_ASSERT (n > 0);

	/* skip the user data header */
	if (ud_hdr_ind) {
		/* get user data header length + 1 (for the 'user data header length'-field) */
		shift = ((user_data[0] + 1) * 8) / 7;
		if ((((user_data[0] + 1) * 8) % 7) != 0)
			shift++;
		septet_l = septet_l - shift;
	}

	unsigned i, l, r;
	for (i = 0; i < septet_l && text != text_buf_end - 1; i++) {

		l = ((i + shift) * 7 + 7) >> 3;
		r = ((i + shift) * 7) >> 3;

		/* the left side index is always >= right side index
		sometimes it even gets beyond array boundary
		check for that explicitly and force 0 instead
		 */
		if (l >= maxlen)
			lu = 0;
		else
			lu = user_data[l] << (7 - (((i + shift) * 7 + 7) & 7));

		ru = user_data[r] >> (((i + shift) * 7) & 7);

		c7 = (lu | ru) & 0x7f;

		if (next_is_ext) {
			/* this is an extension character */
			next_is_ext = 0;
			c8 = gsm_7bit_alphabet[0x7f + c7];
		} else if (c7 == 0x1b && i + 1 < septet_l) {
			next_is_ext = 1;
			continue;
		} else {
			c8 = gsm_septet_lookup(c7);
		}

		*(text++) = c8;
	}

	*text = '\0';

	return text - text_buf_begin;
}

int gsm_7bit_decode_n(char *text, size_t n, const uint8_t *user_data, uint8_t septet_l)
{
	return gsm_7bit_decode_n_hdr(text, n, user_data, septet_l, 0);
}

int gsm_7bit_decode_n_ussd(char *text, size_t n, const uint8_t *user_data, uint8_t length)
{
	int nchars;

	nchars = gsm_7bit_decode_n_hdr(text, n, user_data, length, 0);
	/* remove last <CR>, if it fits up to the end of last octet */
	if (nchars && (user_data[gsm_get_octet_len(length) - 1] >> 1) == '\r')
		text[--nchars] = '\0';

	return nchars;
}

/* GSM 03.38 6.2.1 Prepare character packing */
int gsm_septet_encode(uint8_t *result, const char *data)
{
	int i, y = 0;
	uint8_t ch;
	for (i = 0; i < strlen(data); i++) {
		ch = data[i];
		switch(ch){
		/* fall-through for extension characters */
		case 0x0c:
		case 0x5e:
		case 0x7b:
		case 0x7d:
		case 0x5c:
		case 0x5b:
		case 0x7e:
		case 0x5d:
		case 0x7c:
			result[y++] = 0x1b;
		default:
			result[y] = gsm_7bit_alphabet[ch];
			break;
		}
		y++;
	}

	return y;
}

/* 7bit to octet packing */
int gsm_septets2octets(uint8_t *result, const uint8_t *rdata, uint8_t septet_len, uint8_t padding)
{
	int i = 0, z = 0;
	uint8_t cb, nb;
	int shift = 0;
	uint8_t *data = calloc(septet_len + 1, sizeof(uint8_t));

	if (padding) {
		shift = 7 - padding;
		/* the first zero is needed for padding */
		memcpy(data + 1, rdata, septet_len);
		septet_len++;
	} else
		memcpy(data, rdata, septet_len);

	for (i = 0; i < septet_len; i++) {
		if (shift == 7) {
			/*
			 * special end case with the. This is necessary if the
			 * last septet fits into the previous octet. E.g. 48
			 * non-extension characters:
			 *   ....ag ( a = 1100001, g = 1100111)
			 * result[40] = 100001 XX, result[41] = 1100111 1 */
			if (i + 1 < septet_len) {
				shift = 0;
				continue;
			} else if (i + 1 == septet_len)
				break;
		}

		cb = (data[i] & 0x7f) >> shift;
		if (i + 1 < septet_len) {
			nb = (data[i + 1] & 0x7f) << (7 - shift);
			cb = cb | nb;
		}

		result[z++] = cb;
		shift++;
	}

	free(data);

	return z;
}

/* GSM 03.38 6.2.1 Character packing */
int gsm_7bit_encode_n(uint8_t *result, size_t n, const char *data, int *octets)
{
	int y = 0;
	int o;
	size_t max_septets = n * 8 / 7;

	/* prepare for the worst case, every character expanding to two bytes */
	uint8_t *rdata = calloc(strlen(data) * 2, sizeof(uint8_t));
	y = gsm_septet_encode(rdata, data);

	if (y > max_septets) {
		/*
		 * Limit the number of septets to avoid the generation
		 * of more than n octets.
		 */
		y = max_septets;
	}

	o = gsm_septets2octets(result, rdata, y, 0);

	if (octets)
		*octets = o;

	free(rdata);

	/*
	 * We don't care about the number of octets, because they are not
	 * unique. E.g.:
	 *  1.) 46 non-extension characters + 1 extension character
	 *         => (46 * 7 bit + (1 * (2 * 7 bit))) / 8 bit =  42 octets
	 *  2.) 47 non-extension characters
	 *         => (47 * 7 bit) / 8 bit = 41,125 = 42 octets
	 *  3.) 48 non-extension characters
	 *         => (48 * 7 bit) / 8 bit = 42 octects
	 */
	return y;
}

int gsm_7bit_encode_n_ussd(uint8_t *result, size_t n, const char *data, int *octets)
{
	int y;

	y = gsm_7bit_encode_n(result, n, data, octets);
	/* if last octet contains only one bit, add <CR> */
	if (((y * 7) & 7) == 1)
		result[(*octets) - 1] |= ('\r' << 1);
	/* if last character is <CR> and completely fills last octet, add
	 * another <CR>. */
	if (y && ((y * 7) & 7) == 0 && (result[(*octets) - 1] >> 1) == '\r' && *octets < n - 1) {
		result[(*octets)++] = '\r';
		y++;
	}

	return y;
}

/* convert power class to dBm according to GSM TS 05.05 */
unsigned int ms_class_gmsk_dbm(enum gsm_band band, int class)
{
	switch (band) {
	case GSM_BAND_450:
	case GSM_BAND_480:
	case GSM_BAND_750:
	case GSM_BAND_900:
	case GSM_BAND_810:
	case GSM_BAND_850:
		if (class == 1)
			return 43; /* 20W */
		if (class == 2)
			return 39; /* 8W */
		if (class == 3)
			return 37; /* 5W */
		if (class == 4)
			return 33; /* 2W */
		if (class == 5)
			return 29; /* 0.8W */
		break;
	case GSM_BAND_1800:
		if (class == 1)
			return 30; /* 1W */
		if (class == 2)
			return 24; /* 0.25W */
		if (class == 3)
			return 36; /* 4W */
		break;
	case GSM_BAND_1900:
		if (class == 1)
			return 30; /* 1W */
		if (class == 2)
			return 24; /* 0.25W */
		if (class == 3)
			return 33; /* 2W */
		break;
	}
	return -EINVAL;
}

/* determine power control level for given dBm value, as indicated
 * by the tables in chapter 4.1.1 of GSM TS 05.05 */
int ms_pwr_ctl_lvl(enum gsm_band band, unsigned int dbm)
{
	switch (band) {
	case GSM_BAND_450:
	case GSM_BAND_480:
	case GSM_BAND_750:
	case GSM_BAND_900:
	case GSM_BAND_810:
	case GSM_BAND_850:
		if (dbm >= 39)
			return 0;
		else if (dbm < 5)
			return 19;
		else {
			/* we are guaranteed to have (5 <= dbm < 39) */
			return 2 + ((39 - dbm) / 2);
		}
		break;
	case GSM_BAND_1800:
		if (dbm >= 36)
			return 29;
		else if (dbm >= 34)
			return 30;
		else if (dbm >= 32)
			return 31;
		else if (dbm == 31)
			return 0;
		else {
			/* we are guaranteed to have (0 <= dbm < 31) */
			return (30 - dbm) / 2;
		}
		break;
	case GSM_BAND_1900:
		if (dbm >= 33)
			return 30;
		else if (dbm >= 32)
			return 31;
		else if (dbm == 31)
			return 0;
		else {
			/* we are guaranteed to have (0 <= dbm < 31) */
			return (30 - dbm) / 2;
		}
		break;
	}
	return -EINVAL;
}

int ms_pwr_dbm(enum gsm_band band, uint8_t lvl)
{
	lvl &= 0x1f;

	switch (band) {
	case GSM_BAND_450:
	case GSM_BAND_480:
	case GSM_BAND_750:
	case GSM_BAND_900:
	case GSM_BAND_810:
	case GSM_BAND_850:
		if (lvl < 2)
			return 39;
		else if (lvl < 20)
			return 39 - ((lvl - 2) * 2) ;
		else
			return 5;
		break;
	case GSM_BAND_1800:
		if (lvl < 16)
			return 30 - (lvl * 2);
		else if (lvl < 29)
			return 0;
		else
			return 36 - ((lvl - 29) * 2);
		break;
	case GSM_BAND_1900:
		if (lvl < 16)
			return 30 - (lvl * 2);
		else if (lvl < 30)
			return -EINVAL;
		else
			return 33 - (lvl - 30);
		break;
	}
	return -EINVAL;
}

/* According to TS 05.08 Chapter 8.1.4 */
int rxlev2dbm(uint8_t rxlev)
{
	if (rxlev > 63)
		rxlev = 63;

	return -110 + rxlev;
}

/* According to TS 05.08 Chapter 8.1.4 */
uint8_t dbm2rxlev(int dbm)
{
	int rxlev = dbm + 110;

	if (rxlev > 63)
		rxlev = 63;
	else if (rxlev < 0)
		rxlev = 0;

	return rxlev;
}

const char *gsm_band_name(enum gsm_band band)
{
	switch (band) {
	case GSM_BAND_450:
		return "GSM450";
	case GSM_BAND_480:
		return "GSM480";
	case GSM_BAND_750:
		return "GSM750";
	case GSM_BAND_810:
		return "GSM810";
	case GSM_BAND_850:
		return "GSM850";
	case GSM_BAND_900:
		return "GSM900";
	case GSM_BAND_1800:
		return "DCS1800";
	case GSM_BAND_1900:
		return "PCS1900";
	}
	return "invalid";
}

enum gsm_band gsm_band_parse(const char* mhz)
{
	while (*mhz && !isdigit(*mhz))
		mhz++;

	if (*mhz == '\0')
		return -EINVAL;

	switch (strtol(mhz, NULL, 10)) {
	case 450:
		return GSM_BAND_450;
	case 480:
		return GSM_BAND_480;
	case 750:
		return GSM_BAND_750;
	case 810:
		return GSM_BAND_810;
	case 850:
		return GSM_BAND_850;
	case 900:
		return GSM_BAND_900;
	case 1800:
		return GSM_BAND_1800;
	case 1900:
		return GSM_BAND_1900;
	default:
		return -EINVAL;
	}
}

enum gsm_band gsm_arfcn2band(uint16_t arfcn)
{
	int is_pcs = arfcn & ARFCN_PCS;

	arfcn &= ~ARFCN_FLAG_MASK;

	if (is_pcs)
		return GSM_BAND_1900;
	else if (arfcn <= 124)
		return GSM_BAND_900;
	else if (arfcn >= 955 && arfcn <= 1023)
		return GSM_BAND_900;
	else if (arfcn >= 128 && arfcn <= 251)
		return GSM_BAND_850;
	else if (arfcn >= 512 && arfcn <= 885)
		return GSM_BAND_1800;
	else if (arfcn >= 259 && arfcn <= 293)
		return GSM_BAND_450;
	else if (arfcn >= 306 && arfcn <= 340)
		return GSM_BAND_480;
	else if (arfcn >= 350 && arfcn <= 425)
		return GSM_BAND_810;
	else if (arfcn >= 438 && arfcn <= 511)
		return GSM_BAND_750;
	else
		return GSM_BAND_1800;
}

struct gsm_freq_range {
	uint16_t arfcn_first;
	uint16_t arfcn_last;
	uint16_t freq_ul_first;
	uint16_t freq_dl_offset;
	uint16_t flags;
};

static struct gsm_freq_range gsm_ranges[] = {
	{ 512,  810, 18502, 800, ARFCN_PCS },	/* PCS 1900 */
	{   0,  124,  8900, 450, 0 },		/* P-GSM + E-GSM ARFCN 0 */
	{ 955, 1023,  8762, 450, 0 },		/* E-GSM + R-GSM */
	{ 128,  251,  8242, 450, 0 },		/* GSM 850  */
	{ 512,  885, 17102, 950, 0 },		/* DCS 1800 */
	{ 259,  293,  4506, 100, 0 },		/* GSM 450  */
	{ 306,  340,  4790, 100, 0 },		/* GSM 480  */
	{ 350,  425,  8060, 450, 0 },		/* GSM 810  */
	{ 438,  511,  7472, 300, 0 },		/* GSM 750  */
	{ /* Guard */ }
};

/* Convert an ARFCN to the frequency in MHz * 10 */
uint16_t gsm_arfcn2freq10(uint16_t arfcn, int uplink)
{
	struct gsm_freq_range *r;
	uint16_t flags = arfcn & ARFCN_FLAG_MASK;
	uint16_t freq10_ul = 0xffff;
	uint16_t freq10_dl = 0xffff;

	arfcn &= ~ARFCN_FLAG_MASK;

	for (r=gsm_ranges; r->freq_ul_first>0; r++) {
		if ((flags == r->flags) &&
		    (arfcn >= r->arfcn_first) &&
		    (arfcn <= r->arfcn_last))
		{
			freq10_ul = r->freq_ul_first + 2 * (arfcn - r->arfcn_first);
			freq10_dl = freq10_ul + r->freq_dl_offset;
			break;
		}
	}

	return uplink ? freq10_ul : freq10_dl;
}

/* Convert a Frequency in MHz * 10 to ARFCN */
uint16_t gsm_freq102arfcn(uint16_t freq10, int uplink)
{
	struct gsm_freq_range *r;
	uint16_t freq10_lo, freq10_hi;
	uint16_t arfcn = 0xffff;

	for (r=gsm_ranges; r->freq_ul_first>0; r++) {
		/* Generate frequency limits */
		freq10_lo = r->freq_ul_first;
		freq10_hi = freq10_lo + 2 * (r->arfcn_last - r->arfcn_first);
		if (!uplink) {
			freq10_lo += r->freq_dl_offset;
			freq10_hi += r->freq_dl_offset;
		}

		/* Check if this fits */
		if (freq10 >= freq10_lo && freq10 <= freq10_hi) {
			arfcn  = r->arfcn_first + ((freq10 - freq10_lo) >> 1);
			arfcn |= r->flags;
			break;
		}
	}

	if (uplink)
		arfcn |= ARFCN_UPLINK;

	return arfcn;
}

void gsm_fn2gsmtime(struct gsm_time *time, uint32_t fn)
{
	time->fn = fn;
	time->t1 = time->fn / (26*51);
	time->t2 = time->fn % 26;
	time->t3 = time->fn % 51;
	time->tc = (time->fn / 51) % 8;
}

uint32_t gsm_gsmtime2fn(struct gsm_time *time)
{
	/* TS 05.02 Chapter 4.3.3 TDMA frame number */
	return (51 * ((time->t3 - time->t2 + 26) % 26) + time->t3 + (26 * 51 * time->t1));
}

/* TS 23.003 Chapter 2.6 */
int gprs_tlli_type(uint32_t tlli)
{
	if ((tlli & 0xc0000000) == 0xc0000000)
		return TLLI_LOCAL;
	else if ((tlli & 0xc0000000) == 0x80000000)
		return TLLI_FOREIGN;
	else if ((tlli & 0xf8000000) == 0x78000000)
		return TLLI_RANDOM;
	else if ((tlli & 0xf8000000) == 0x70000000)
		return TLLI_AUXILIARY;
	else if ((tlli & 0xf0000000) == 0x00000000)
		return TLLI_G_RNTI;
	else if ((tlli & 0xf0000000) == 0x10000000)
		return TLLI_RAND_G_RNTI;

	return TLLI_RESERVED;
}

uint32_t gprs_tmsi2tlli(uint32_t p_tmsi, enum gprs_tlli_type type)
{
	uint32_t tlli;
	switch (type) {
	case TLLI_LOCAL:
		tlli = p_tmsi | 0xc0000000;
		break;
	case TLLI_FOREIGN:
		tlli = (p_tmsi & 0x3fffffff) | 0x80000000;
		break;
	default:
		tlli = 0;
		break;
	}
	return tlli;
}

/* Wrappers for deprecated functions: */

int gsm_7bit_decode(char *text, const uint8_t *user_data, uint8_t septet_l)
{
	gsm_7bit_decode_n(text, GSM_7BIT_LEGACY_MAX_BUFFER_SIZE,
			  user_data, septet_l);

	/* Mimic the original behaviour. */
	return septet_l;
}

int gsm_7bit_decode_ussd(char *text, const uint8_t *user_data, uint8_t length)
{
	return gsm_7bit_decode_n_ussd(text, GSM_7BIT_LEGACY_MAX_BUFFER_SIZE,
				      user_data, length);
}

int gsm_7bit_encode(uint8_t *result, const char *data)
{
	int out;
	return gsm_7bit_encode_n(result, GSM_7BIT_LEGACY_MAX_BUFFER_SIZE,
				 data, &out);
}

int gsm_7bit_encode_ussd(uint8_t *result, const char *data, int *octets)
{
	return gsm_7bit_encode_n_ussd(result, GSM_7BIT_LEGACY_MAX_BUFFER_SIZE,
				      data, octets);
}

int gsm_7bit_encode_oct(uint8_t *result, const char *data, int *octets)
{
	return gsm_7bit_encode_n(result, GSM_7BIT_LEGACY_MAX_BUFFER_SIZE,
				 data, octets);
}
