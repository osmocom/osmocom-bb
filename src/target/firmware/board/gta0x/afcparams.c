/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
 *
 * Tweaked (coding style changes) by Vadim Yanitskiy <axilirator@gmail.com>
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
#include <rf/vcxocal.h>

/*
 * Here is a typical set of AFC Psi parameters for an FCDEV3B modem board,
 * computed by FreeCalypso fc-rfcal-vcxo calibration tool from frequency
 * offset measurements made with a CMU200 RF tester:
 *
 * Psi_sta_inv: 3462
 * Psi_st: 15
 * Psi_st_32: 992326
 * Psi_st_inv: 4328
 *
 * The following AFC slope number is the closest OsmocomBB-style afc_slope
 * integer corresponding to these Psi numbers; the true value is somewhere
 * between 454 and 455.
 *
 * This AFC slope setting is expected to be correct for both Openmoko and
 * FreeCalypso hardware as we use the same VCXO components as were used
 * by Openmoko.
 */
int16_t afc_slope = 454;

/*
 * The compiled-in AFC initial DAC value below is the same as was used by
 * the old OsmocomBB code written for Mot C1xx phones, but it will normally
 * be overridden by the per-unit factory calibration value read from the
 * /gsm/rf/afcdac file in FFS.
 */
int16_t afc_initial_dac_value = -700;
