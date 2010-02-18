/* Generic GSM utility routines */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <gsm.h>

enum gsm_band gsm_arfcn2band(uint16_t arfcn)
{
	if (arfcn & ARFCN_PCS)
		return GSM_1900;
	else if (arfcn <= 124)
		return GSM_900;
	else if (arfcn >= 955 && arfcn <= 1023)
		return GSM_900;
	else if (arfcn >= 128 && arfcn <= 251)
		return GSM_850;
	else if (arfcn >= 512 && arfcn <= 885)
		return GSM_1800;
	else if (arfcn >= 259 && arfcn <= 293)
		return GSM_450;
	else if (arfcn >= 306 && arfcn <= 340)
		return GSM_480;
	else if (arfcn >= 350 && arfcn <= 425)
		return GSM_810;
	else if (arfcn >= 438 && arfcn <= 511)
		return GSM_750;
	else
		return GSM_1800;
}

/* Convert an ARFCN to the frequency in MHz * 10 */
uint16_t gsm_arfcn2freq10(uint16_t arfcn, int uplink)
{
	uint16_t freq10_ul;
	uint16_t freq10_dl;

	if (arfcn & ARFCN_PCS) {
		/* DCS 1900 */
		arfcn &= ~ARFCN_PCS;
		freq10_ul = 18502 + 2 * (arfcn-512);
		freq10_dl = freq10_ul + 800;
	} else if (arfcn <= 124) {
		/* Primary GSM + ARFCN 0 of E-GSM */
		freq10_ul = 8900 + 2 * arfcn;
		freq10_dl = freq10_ul + 450;
	} else if (arfcn >= 955 && arfcn <= 1023) {
		/* E-GSM and R-GSM */
		freq10_ul = 8900 + 2 * (arfcn - 1024);
		freq10_dl = freq10_ul + 450;
	} else if (arfcn >= 128 && arfcn <= 251) {
		/* GSM 850 */
		freq10_ul = 8242 + 2 * (arfcn - 128);
		freq10_dl = freq10_ul + 450;
	} else if (arfcn >= 512 && arfcn <= 885) {
		/* DCS 1800 */
		freq10_ul = 17102 + 2 * (arfcn - 512);
		freq10_dl = freq10_ul + 950;
	} else if (arfcn >= 259 && arfcn <= 293) {
		/* GSM 450 */
		freq10_ul = 4506 + 2 * (arfcn - 259);
		freq10_dl = freq10_ul + 100;
	} else if (arfcn >= 306 && arfcn <= 340) {
		/* GSM 480 */
		freq10_ul = 4790 + 2 * (arfcn - 306);
		freq10_dl = freq10_ul + 100;
	} else if (arfcn >= 350 && arfcn <= 425) {
		/* GSM 810 */
		freq10_ul = 8060 + 2 * (arfcn - 350);
		freq10_dl = freq10_ul + 450;
	} else if (arfcn >= 438 && arfcn <= 511) {
		/* GSM 750 */
		freq10_ul = 7472 + 2 * (arfcn - 438);
		freq10_dl = freq10_ul + 300;
	} else
		return 0xffff;

	if (uplink)
		return freq10_ul;
	else
		return freq10_dl;
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
