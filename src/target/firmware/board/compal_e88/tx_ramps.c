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
#include <rf/txcal.h>

/*
 * The following APC offset value and Tx ramp template tables have been
 * extracted out of the one special Mot C11x fw version for which we got the
 * linker map file with symbols, and they also appear to be correct for the
 * closely related C155 hardware, which has exactly the same RF section
 * including the old SKY77324 RF PA.
 *
 * FreeCalypso firmware running with these numbers on both C118 and C155 phones
 * (using per-unit factory calibration records for the Tx levels) produces
 * correct Tx output levels and ramps as verified with the CMU200 RF test
 * instrument.
 */

uint8_t apc_offset = 32;

struct txcal_ramp_def rf_tx_ramps_850[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{ 28,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4, 31, 31, 31,  3,  0},
	/* ramp-down */
	{ 31, 31, 18, 22,  6, 10,  2,  1,  1,  3,  3,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{ 31,  1,  0,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  6,  8,  8,  9,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{ 31,  6,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31, 29,  0,  0},
	/* ramp-down */
	{ 31, 25, 21, 20, 13, 14,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{ 31, 13,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31, 22,  0,  0},
	/* ramp-down */
	{ 27, 28, 23, 19, 13, 14,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{ 31, 21,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31, 14,  0,  0},
	/* ramp-down */
	{ 31, 21, 31,  2, 31,  4,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{ 31, 30,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 21, 31, 31,  2, 31,  4,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{ 31, 31,  7,  0,  0,  0,  0,  0,  0,  0,  0, 31, 28,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 28, 14,  3, 21,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{ 31, 31, 16,  0,  0,  0,  0,  0,  0,  0,  0, 31, 19,  0,  0,  0},
	/* ramp-down */
	{ 20, 30, 30, 10, 28, 10, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{ 31, 31, 26,  0,  0,  0,  0,  0,  0,  0,  0, 31,  9,  0,  0,  0},
	/* ramp-down */
	{ 20, 26, 26, 18, 18, 20,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{ 31, 31, 31,  2,  0,  0,  0,  0,  0,  0,  0, 31,  2,  0,  0,  0},
	/* ramp-down */
	{ 16, 16, 26, 26, 26,  0,  0, 18,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{ 31, 31, 31, 11,  0,  0,  0,  0,  0,  0,  0,  0, 24,  0,  0,  0},
	/* ramp-down */
	{ 10, 12, 31, 26, 29, 20,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{ 31, 31, 31, 18,  0,  0,  0,  0,  0,  0,  0,  0, 17,  0,  0,  0},
	/* ramp-down */
	{  2, 20, 31, 26, 31,  0,  0, 18,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{ 31, 31, 31, 25,  0,  0,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0},
	/* ramp-down */
	{  2, 20, 31, 26, 31,  0,  0,  0, 18,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{ 31, 31, 31, 30,  0,  0,  0,  0,  0,  0,  0,  0,  5,  0,  0,  0},
	/* ramp-down */
	{  1, 16, 31, 31, 31,  0, 18,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{ 31, 31, 31, 31,  0,  0,  0,  0,  0,  0,  0,  4,  0,  0,  0,  0},
	/* ramp-down */
	{  4,  8, 10, 20, 31, 31, 20,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{ 31, 31, 31, 31,  0,  0,  0,  0,  0,  0,  0,  4,  0,  0,  0,  0},
	/* ramp-down */
	{  4,  8, 10, 20, 31, 31, 20,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
};

struct txcal_ramp_def rf_tx_ramps_900[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{ 30,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31, 31,  5,  0},
	/* ramp-down */
	{ 31, 31, 28, 15,  2,  0, 19,  2,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{ 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0, 30, 26, 29,  8,  0},
	/* ramp-down */
	{ 31, 31, 29, 14,  2,  1, 15,  2,  3,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{ 31, 14,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 27, 24,  1,  0},
	/* ramp-down */
	{ 30, 31, 25, 14,  2,  2, 15,  7,  2,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{ 31, 22,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 14, 29,  1,  0},
	/* ramp-down */
	{ 31, 29, 31, 13,  2,  2, 15,  2,  3,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{ 31, 30,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 17, 19,  0,  0},
	/* ramp-down */
	{ 31, 30, 30, 15,  1,  2, 17,  2,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{ 31, 31,  7,  0,  0,  0,  0,  0,  0,  0,  0, 31, 19,  7,  2,  0},
	/* ramp-down */
	{ 29, 31, 29, 16,  4,  0, 14,  2,  1,  2,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{ 31, 31, 16,  0,  0,  0,  0,  0,  0,  0,  0, 30,  0, 20,  0,  0},
	/* ramp-down */
	{ 19, 26, 26, 28, 10,  0, 19,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{ 31, 31, 25,  0,  0,  0,  0,  0,  0,  0,  0, 31,  0,  8,  2,  0},
	/* ramp-down */
	{ 19, 28, 31, 24,  4,  0, 19,  3,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{ 31, 31, 31,  2,  0,  0,  0,  0,  0,  0,  0, 31,  2,  0,  0,  0},
	/* ramp-down */
	{ 19, 28, 31, 24,  4,  0, 17,  5,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{ 31, 31, 31,  9,  0,  0,  0,  0,  0,  0,  0, 26,  0,  0,  0,  0},
	/* ramp-down */
	{ 18, 25, 28, 31,  2,  2, 19,  3,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{ 31, 31, 31, 16,  0,  0,  0,  0,  0,  0,  0,  0, 19,  0,  0,  0},
	/* ramp-down */
	{ 14, 21, 24, 29,  6,  2, 23,  5,  4,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{ 31, 31, 31, 22,  0,  0,  0,  0,  0,  0,  0,  0, 12,  0,  1,  0},
	/* ramp-down */
	{  8, 26, 26, 28, 12, 12,  5,  5,  0,  6,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{ 31, 31, 31, 27,  0,  0,  0,  0,  0,  0,  0,  0,  8,  0,  0,  0},
	/* ramp-down */
	{  8, 14, 27, 30, 20, 19, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{ 31, 31, 31, 31,  0,  0,  0,  0,  0,  0,  0,  0,  3,  0,  1,  0},
	/* ramp-down */
	{  9, 10, 15, 26, 25, 10, 17, 13,  3,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{ 31, 31, 30, 30,  1,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	/* ramp-down */
	{  0,  4, 15, 21, 21, 21, 21, 15, 10,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{ 31, 31, 30, 30,  1,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	/* ramp-down */
	{  0,  4, 15, 21, 21, 21, 21, 15, 10,  0,  0,  0,  0,  0,  0,  0},
      },
};

struct txcal_ramp_def rf_tx_ramps_1800[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{ 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31, 27,  4,  0},
	/* ramp-down */
	{ 28, 31, 18,  8,  8, 13,  9, 13,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{ 31, 11,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31, 24,  0,  0},
	/* ramp-down */
	{ 10, 30, 30, 20,  8, 30,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{ 31, 19,  0,  0,  0,  0,  0,  0,  0,  0,  0, 31, 31, 16,  0,  0},
	/* ramp-down */
	{ 10, 30, 31, 24, 31,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{ 31, 27,  0,  0,  0,  0,  0,  0,  0,  0,  0, 28, 23, 19,  0,  0},
	/* ramp-down */
	{ 31, 14, 31,  5, 24, 13, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{ 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0, 10, 21, 31,  0,  0},
	/* ramp-down */
	{ 20, 22, 31, 10, 22, 13, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{ 31, 31, 13,  0,  0,  0,  0,  0,  0,  0,  0, 31, 22,  0,  0,  0},
	/* ramp-down */
	{ 22, 14, 26, 22, 22, 17,  5,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{ 31, 31, 21,  0,  0,  0,  0,  0,  0,  0,  0, 24, 21,  0,  0,  0},
	/* ramp-down */
	{ 10, 31, 31, 25, 17, 14,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{ 31, 31, 28,  0,  0,  0,  0,  0,  0,  0,  0, 28, 10,  0,  0,  0},
	/* ramp-down */
	{ 17, 24, 28, 21, 24, 14,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{ 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0, 27,  4,  0,  0,  0},
	/* ramp-down */
	{  9, 23, 31, 24, 24, 13,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{ 31, 31, 31, 12,  0,  0,  0,  0,  0,  0,  0,  0, 13, 10,  0,  0},
	/* ramp-down */
	{  9, 23, 31, 24, 24, 13,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{ 31, 31, 31, 17,  0,  0,  0,  0,  0,  0,  0,  0, 12,  6,  0,  0},
	/* ramp-down */
	{ 10, 10, 31, 31, 24, 13,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{ 31, 31, 31, 21,  0,  0,  0,  0,  0,  0,  0,  0, 14,  0,  0,  0},
	/* ramp-down */
	{  4, 14, 31, 31, 26, 13,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{ 31, 31, 31, 27,  0,  0,  0,  0,  0,  0,  0,  0,  8,  0,  0,  0},
	/* ramp-down */
	{  2, 14, 31, 31, 28, 13,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{ 31, 31, 31, 29,  0,  0,  0,  0,  0,  0,  0,  0,  6,  0,  0,  0},
	/* ramp-down */
	{  0,  6, 14, 31, 31, 24, 13,  5,  4,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{ 31, 31, 31, 31,  1,  0,  0,  0,  0,  0,  0,  0,  3,  0,  0,  0},
	/* ramp-down */
	{  2,  4,  4, 18, 31, 31, 24,  5,  5,  4,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{ 31, 31, 31, 31,  0,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	/* ramp-down */
	{  3,  2,  2, 22, 22, 21, 21, 21,  9,  5,  0,  0,  0,  0,  0,  0},
      },
};

struct txcal_ramp_def rf_tx_ramps_1900[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{ 31,  4,  0,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 17,  0,  0},
	/* ramp-down */
	{ 31, 31, 15, 25,  8, 10,  4,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{ 31,  8,  0,  0,  0,  0,  0,  0,  0,  0,  5, 31, 31, 22,  0,  0},
	/* ramp-down */
	{ 31, 21, 31, 20,  4,  0, 21,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{ 31, 16,  0,  0,  0,  0,  0,  0,  0,  0,  6, 31, 31, 13,  0,  0},
	/* ramp-down */
	{ 30, 31, 24, 31, 10,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{ 31, 24,  0,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31,  8,  0,  0},
	/* ramp-down */
	{ 31, 31, 19, 23, 24,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{ 31, 31,  2,  0,  0,  0,  0,  0,  0,  0,  6, 31, 22,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 14, 24,  5, 13, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{ 31, 31, 10,  0,  0,  0,  0,  0,  0,  0,  0, 31, 25,  0,  0,  0},
	/* ramp-down */
	{ 31, 19, 20,  8, 24, 17,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{ 31, 30, 19,  0,  0,  0,  0,  0,  0,  0,  0, 31, 17,  0,  0,  0},
	/* ramp-down */
	{  2, 31, 31, 25, 17, 22,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{ 31, 31, 26,  0,  0,  0,  0,  0,  0,  0,  0, 31,  9,  0,  0,  0},
	/* ramp-down */
	{ 14, 24, 25, 30, 24, 11,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{ 31, 31, 31,  2,  0,  0,  0,  0,  0,  0,  0, 31,  2,  0,  0,  0},
	/* ramp-down */
	{ 12, 17, 27, 31, 24, 13,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{ 31, 31, 30, 10,  0,  0,  0,  0,  0,  0,  0, 25,  1,  0,  0,  0},
	/* ramp-down */
	{ 21, 31, 31, 26, 13,  4,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{ 31, 31, 31, 11,  0,  0,  0,  0,  0,  0,  0, 24,  0,  0,  0,  0},
	/* ramp-down */
	{ 14, 31, 31, 28, 13,  5,  4,  2,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{ 31, 31, 31, 19,  0,  0,  0,  0,  0,  0,  0,  0, 16,  0,  0,  0},
	/* ramp-down */
	{  6, 14, 31, 31, 24, 13,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{ 31, 31, 31, 25,  0,  0,  0,  0,  0,  0,  0,  0, 10,  0,  0,  0},
	/* ramp-down */
	{  6, 14, 31, 31, 24, 13,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{ 31, 31, 31, 29,  0,  0,  0,  0,  0,  0,  0,  0,  6,  0,  0,  0},
	/* ramp-down */
	{  6, 14, 31, 31, 24, 13,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{ 31, 31, 31, 31,  1,  0,  0,  0,  0,  0,  0,  0,  3,  0,  0,  0},
	/* ramp-down */
	{  3, 16, 31, 31, 24, 14,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	/* ramp-down */
	{  4,  6, 21, 21, 21, 21, 15, 15,  4,  0,  0,  0,  0,  0,  0,  0},
      },
};
