/* APC (Automatic Power Control) Implementation */

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

#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm_utils.h>

#include <layer1/apc.h>

/* calibration table defined in board file */
extern const int16_t dbm2apc_gsm900[];
extern const int dbm2apc_gsm900_max;


/* determine the AUXAPC value by the Tx Power Level */
int16_t apc_tx_dbm2auxapc(enum gsm_band band, int8_t dbm)
{
	if (dbm < 0)
		return -ERANGE;

	/* FIXME: offsets for different bands! */
	if (dbm > dbm2apc_gsm900_max)
		dbm = dbm2apc_gsm900_max;

	return dbm2apc_gsm900[dbm];
}

/* determine the AUXAPC value by the Tx Power Level */
int16_t apc_tx_pwrlvl2auxapc(enum gsm_band band, uint8_t lvl)
{
	/* convert tx power level to dBm */
	int dbm = ms_pwr_dbm(band, lvl);
	if (dbm < 0)
		return dbm;

	return apc_tx_dbm2auxapc(band, dbm);
}
