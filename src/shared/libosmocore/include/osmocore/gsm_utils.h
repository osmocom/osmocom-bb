/* GSM utility functions, e.g. coding and decoding */
/*
 * (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009 by Harald Welte <laforge@gnumonks.org>
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

#ifndef GSM_UTILS_H
#define GSM_UTILS_H

#include <stdint.h>

enum gsm_band {
	GSM_BAND_850	= 1,
	GSM_BAND_900	= 2,
	GSM_BAND_1800	= 4,
	GSM_BAND_1900	= 8,
	GSM_BAND_450	= 0x10,
	GSM_BAND_480	= 0x20,
	GSM_BAND_750	= 0x40,
	GSM_BAND_810	= 0x80,
};

int gsm_7bit_decode(char *decoded, const uint8_t *user_data, uint8_t length);
int gsm_7bit_encode(uint8_t *result, const char *data);

int ms_pwr_ctl_lvl(enum gsm_band band, unsigned int dbm);
int ms_pwr_dbm(enum gsm_band band, uint8_t lvl);

/* According to TS 08.05 Chapter 8.1.4 */
int rxlev2dbm(uint8_t rxlev);
uint8_t dbm2rxlev(int dbm);

/* According to GSM 04.08 Chapter 10.5.2.29 */
static inline int rach_max_trans_val2raw(int val) { return (val >> 1) & 3; }
static inline int rach_max_trans_raw2val(int raw) {
	const int tbl[4] = { 1, 2, 4, 7 };
	return tbl[raw & 3];
}

void generate_backtrace();
#endif
