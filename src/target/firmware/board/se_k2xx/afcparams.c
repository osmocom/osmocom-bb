/*
 * (C) 2026 by Vadim Yanitskiy <fixeria@osmocom.org>
 *
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-2.0+
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
 */

#include <stdint.h>

/*
 * The following AFC Psi parameters are averages computed from TIFFS readings
 * across 7 unique SE K2xx units (5 x K200 + 1 x K205 + 1 x K220):
 *
 * Psi_sta_inv: 3880
 * Psi_st: 14
 * Psi_st_32: 885779
 * Psi_st_inv: 4850
 *
 * The following AFC slope number is the closest OsmocomBB-style afc_slope
 * integer corresponding to these Psi numbers.
 */
int16_t afc_slope = 405;

/*
 * The compiled-in AFC initial DAC value below is derived from the average
 * dac_center computed from TIFFS readings across the same 7 unique SE K2xx
 * units.  Note that TI's afcparams dac_center is in fixed-point format with
 * 3 fractional bits, hence the actual DAC value is -2957 >> 3 = -370.
 * It will normally be overridden by the per-unit factory calibration value
 * read from the /gsm/rf/afcdac file in FFS.
 */
int16_t afc_initial_dac_value = -370;
