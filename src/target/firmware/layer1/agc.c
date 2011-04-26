/* AFC (Automatic Gain Control) Implementation */

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
#include <stdio.h>

#include <osmocom/gsm/gsm_utils.h>
#include <debug.h>
#include <rffe.h>

#include <layer1/agc.h>
#include <calypso/dsp.h>

/* compute the input level present at the antenna based on a baseband
 * power measurement of the DSP at baseband */
int16_t agc_inp_dbm8_by_pm(int16_t pm)
{
	/* pm is in 1/8 dBm at baseband */
	int16_t total_gain_dbm8;

	/* compute total current gain */
	total_gain_dbm8 = (system_inherent_gain + rffe_get_gain()) * 8;

	/* subtract gain from power measurement at baseband level */
	return pm - total_gain_dbm8;
}

uint8_t agc_il_by_dbm8(int16_t dbm8)
{
	uint16_t il;

	/* convert from 1/8 dBm to l1c format: [220..0] in -1/2dBm unit */
	if (dbm8 >= 0)
		il = 0;
	else
		il = -dbm8;

	/* saturate */
	if (il > 4 * 255)
		il = 4 * 255;

	return (uint8_t)(il >> 2);
}
