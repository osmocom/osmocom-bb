/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
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
 * Here is a representative set of AFC Psi parameters that has been
 * calibrated by Huawei on a GTM900-B MGC2GSMT module, as recorded
 * in the /gsm/rf/afcparams file:
 *
 * Psi_sta_inv: 13626
 * Psi_st: 4
 * Psi_st_32: 252168
 * Psi_st_inv: 17032
 *
 * The following AFC slope number is the closest OsmocomBB-style afc_slope
 * integer corresponding to these Psi numbers; the true value is somewhere
 * between 115 and 116.
 */
int16_t afc_slope = 115;

/*
 * The compiled-in AFC initial DAC value below is the same as was used by
 * the old OsmocomBB code written for Mot C1xx phones, but it will normally
 * be overridden by the per-unit factory calibration value read from the
 * /gsm/rf/afcdac file in FFS.
 */
int16_t afc_initial_dac_value = -700;
