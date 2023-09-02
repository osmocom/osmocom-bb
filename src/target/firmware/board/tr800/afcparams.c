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
 */

#include <stdint.h>
#include <rf/vcxocal.h>

/*
 * Here is a representative set of AFC Psi parameters that has been
 * calibrated by iWOW's factory on a TR-800 module, as recorded
 * in the /gsm/rf/afcparams file:
 *
 * Psi_sta_inv: 4387
 * Psi_st: 12
 * Psi_st_32: 783154
 * Psi_st_inv: 5484
 *
 * The following AFC slope number is the closest OsmocomBB-style afc_slope
 * integer corresponding to these Psi numbers; the true value is somewhere
 * between 358 and 359.
 *
 * Please note that all AFC parameters (both Psi and linear) have been
 * calibrated per unit by iWOW's factory, and they do differ from unit
 * to unit.  Both iWOW and FreeCalypso firmwares make direct use of
 * per-unit calibrated numbers, but OsmocomBB architecture cannot make
 * use of them - hence AFC performance with OBB may be significantly
 * poorer than with either iWOW or FC firmware.  The present code has
 * been contributed by Mother Mychaela solely as a harm reduction measure,
 * and does NOT constitute any kind of approved production solution -
 * you've been warned!
 */
int16_t afc_slope = 358;

/*
 * The compiled-in AFC initial DAC value below is the same as was used by
 * the old OsmocomBB code written for Mot C1xx phones, but it will normally
 * be overridden by the per-unit factory calibration value read from the
 * /gsm/rf/afcdac file in FFS.
 */
int16_t afc_initial_dac_value = -700;
