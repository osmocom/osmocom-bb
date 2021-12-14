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
 */

#include <stdint.h>
#include <rf/txcal.h>

/*
 * The APC offset value used by SE J100 official fw is different from the
 * Mot C1xx value, and this different APC offset value must be used by any
 * aftermarket fw on this target in order for the Tx levels from Compal's
 * factory records to apply correctly.	This correct value has been determined
 * by breaking into a running SE J100 fw with tfc139, running fc-loadtool
 * with all ABB register state still intact from the interrupted official fw,
 * and reading the APCOFF register with the abbr command.
 */
uint8_t apc_offset = 52;

/*
 * The following tables of Tx ramp templates have been extracted from
 * SE J100 fw version R1C004 (R1C004-se.bin in the j100-flashimg-r1.zip
 * FTP release); Compal's modified versions of TI's rf_XXX structures
 * begin at address 0x5E898 in this J100 fw version.
 *
 * Please note that these SE J100 Tx ramp templates are different from
 * both C11x/12x/155/156 (SKY77324) and C139/140 (SKY77325) versions.
 */
struct txcal_ramp_def rf_tx_ramps_850[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0,  0},
	/* ramp-down */
	{ 15, 31, 31, 20, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 31, 20,  0,  0,  0},
	/* ramp-down */
	{ 20, 31, 31, 15, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 31, 20,  0,  0,  0},
	/* ramp-down */
	{ 25, 31, 31, 10, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  9, 31, 31, 31, 25,  0,  0,  0},
	/* ramp-down */
	{ 29, 31, 31, 31,  6,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  9, 31, 31, 31, 25,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31, 31,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31, 31,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	/* ramp-down */
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
};

struct txcal_ramp_def rf_tx_ramps_900[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0,  0},
	/* ramp-down */
	{ 25, 25, 31, 16, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 31, 20,  0,  0,  0},
	/* ramp-down */
	{ 27, 31, 31,  8, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 31, 20,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  9, 31, 31, 31, 25,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31, 31,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
	/* ramp-down */
	{  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
};

struct txcal_ramp_def rf_tx_ramps_1800[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 31, 20,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 30, 31, 31, 31,  4,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 30, 31, 31, 31,  4,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  9, 31, 31, 31, 25,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  9, 31, 31, 31, 25,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31, 31,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31, 31,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
};

struct txcal_ramp_def rf_tx_ramps_1900[RF_TX_RAMP_SIZE] = {
      { /* profile 0 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 1 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 2 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 3 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 31, 20,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 4 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0, 14, 31, 31, 31, 20,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 5 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31, 31,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 6 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31, 31,  0,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 7 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31,  4, 31,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 8 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 29, 31, 31, 31,  5,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 9 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 10 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 24, 31, 31, 31, 10,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 11 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0, 19, 31, 31, 31, 15,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 12 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  9, 31, 31, 31, 25,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 13 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  9, 31, 31, 31, 25,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 14 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
      { /* profile 15 */
	/* ramp-up */
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3, 31, 31, 31,  0,  0},
	/* ramp-down */
	{ 31, 31, 31, 31,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0},
      },
};
