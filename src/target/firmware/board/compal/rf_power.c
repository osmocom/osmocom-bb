/* Tx RF power calibration for the Compal/Motorola dualband phones */

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
#include <osmocom/core/utils.h>

/* GSM900 ARFCN 33, Measurements by Steve Markgraf / May 2010 */
const int16_t dbm2apc_gsm900[] = {
	[0]     = 151,
	[1]     = 152,
	[2]     = 153,
	[3]     = 155,
	[4]     = 156,
	[5]     = 158,
	[6]     = 160,
	[7]     = 162,
	[8]     = 164,
	[9]     = 167,
	[10]    = 170,
	[11]    = 173,
	[12]    = 177,
	[13]    = 182,
	[14]    = 187,
	[15]    = 192,
	[16]    = 199,
	[17]    = 206,
	[18]    = 214,
	[19]    = 223,
	[20]    = 233,
	[21]    = 244,
	[22]    = 260,
	[23]    = 271,
	[24]    = 288,
	[25]    = 307,
	[26]    = 327,
	[27]    = 350,
	[28]    = 376,
	[29]    = 407,
	[30]    = 456,
	[31]    = 575,
};

const int dbm2apc_gsm900_max = ARRAY_SIZE(dbm2apc_gsm900) - 1;
