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
#include <rf/vcxocal.h>

/*
 * The following AFC initial DAC value and AFC slope settings are unchanged
 * from the old OsmocomBB code in which they were hard-coded in layer1/afc.c.
 * This AFC slope setting corresponds very closely to the original Leonardo
 * Psi values which are used by Motorola's official fw at least on the C139,
 * hence I have good reason to believe that they are indeed correct for the
 * Mot C1xx hardware target family.
 */
int16_t afc_initial_dac_value = -700;
int16_t afc_slope = 287;

/*
 * The following Tx levels tables are the ones compiled into Compal's
 * firmwares; more specifically, they were originally extracted out
 * of the one special Mot C11x fw version for which we got the linker
 * map file with symbols and subsequently confirmed to be unchanged in
 * Mot C139 and SE J100 firmwares.  In normal operation the APC DAC values
 * in these levels tables are replaced with the ones read from the per-unit
 * and per-band factory calibration records.
 *
 * It should be noted that these compiled-in numbers are approximately
 * correct for the C11x/12x/155/156 family (SKY77324 RF PA) but are totally
 * wrong for the newer C139/140 (SKY77325) and SE J100 (SKY77328) hardware;
 * it appears that Compal never bothered with changing these compiled-in
 * numbers in their fw for the newer designs because the expectation is
 * that these compiled-in numbers are just dummy placeholders to be
 * overridden by per-unit calibration.
 */
struct txcal_tx_level rf_tx_levels_850[RF_TX_LEVELS_TABLE_SIZE] = {
	{ 560,  0,  0 }, /* 0 */
	{ 560,  0,  0 }, /* 1 */
	{ 560,  0,  0 }, /* 2 */
	{ 560,  0,  0 }, /* 3 */
	{ 560,  0,  0 }, /* 4 */
	{ 638,  0,  0 }, /* 5 */
	{ 554,  1,  0 }, /* 6 */
	{ 467,  2,  0 }, /* 7 */
	{ 395,  3,  0 }, /* 8 */
	{ 337,  4,  0 }, /* 9 */
	{ 290,  5,  0 }, /* 10 */
	{ 253,  6,  0 }, /* 11 */
	{ 224,  7,  0 }, /* 12 */
	{ 201,  8,  0 }, /* 13 */
	{ 183,  9,  0 }, /* 14 */
	{ 168, 10,  0 }, /* 15 */
	{ 157, 11,  0 }, /* 16 */
	{ 148, 12,  0 }, /* 17 */
	{ 141, 13,  0 }, /* 18 */
	{ 136, 14,  0 }, /* 19 */
	{  46, 14,  0 }, /* 20 */
	{  46, 14,  0 }, /* 21 */
	{  46, 14,  0 }, /* 22 */
	{  46, 14,  0 }, /* 23 */
	{  46, 14,  0 }, /* 24 */
	{  46, 14,  0 }, /* 25 */
	{  46, 14,  0 }, /* 26 */
	{  46, 14,  0 }, /* 27 */
	{  46, 14,  0 }, /* 28 */
	{  46, 14,  0 }, /* 29 */
	{  46, 14,  0 }, /* 30 */
	{  46, 14,  0 }, /* 31 */
};

struct txcal_tx_level rf_tx_levels_900[RF_TX_LEVELS_TABLE_SIZE] = {
	{ 550,  0,  0 }, /* 0 */
	{ 550,  0,  0 }, /* 1 */
	{ 550,  0,  0 }, /* 2 */
	{ 550,  0,  0 }, /* 3 */
	{ 550,  0,  0 }, /* 4 */
	{ 550,  0,  0 }, /* 5 */
	{ 476,  1,  0 }, /* 6 */
	{ 402,  2,  0 }, /* 7 */
	{ 338,  3,  0 }, /* 8 */
	{ 294,  4,  0 }, /* 9 */
	{ 260,  5,  0 }, /* 10 */
	{ 226,  6,  0 }, /* 11 */
	{ 204,  7,  0 }, /* 12 */
	{ 186,  8,  0 }, /* 13 */
	{ 172,  9,  0 }, /* 14 */
	{ 161, 10,  0 }, /* 15 */
	{ 153, 11,  0 }, /* 16 */
	{ 146, 12,  0 }, /* 17 */
	{ 141, 13,  0 }, /* 18 */
	{ 137, 14,  0 }, /* 19 */
	{  43, 14,  0 }, /* 20 */
	{  43, 14,  0 }, /* 21 */
	{  43, 14,  0 }, /* 22 */
	{  43, 14,  0 }, /* 23 */
	{  43, 14,  0 }, /* 24 */
	{  43, 14,  0 }, /* 25 */
	{  43, 14,  0 }, /* 26 */
	{  43, 14,  0 }, /* 27 */
	{  43, 14,  0 }, /* 28 */
	{  43, 14,  0 }, /* 29 */
	{  43, 14,  0 }, /* 30 */
	{  43, 14,  0 }, /* 31 */
};

struct txcal_tx_level rf_tx_levels_1800[RF_TX_LEVELS_TABLE_SIZE] = {
	{ 480,  0,  0 }, /* 0 */
	{ 416,  1,  0 }, /* 1 */
	{ 352,  2,  0 }, /* 2 */
	{ 308,  3,  0 }, /* 3 */
	{ 266,  4,  0 }, /* 4 */
	{ 242,  5,  0 }, /* 5 */
	{ 218,  6,  0 }, /* 6 */
	{ 200,  7,  0 }, /* 7 */
	{ 186,  8,  0 }, /* 8 */
	{ 175,  9,  0 }, /* 9 */
	{ 167, 10,  0 }, /* 10 */
	{ 160, 11,  0 }, /* 11 */
	{ 156, 12,  0 }, /* 12 */
	{ 152, 13,  0 }, /* 13 */
	{ 145, 14,  0 }, /* 14 */
	{ 142, 15,  0 }, /* 15 */
	{  61, 15,  0 }, /* 16 */
	{  61, 15,  0 }, /* 17 */
	{  61, 15,  0 }, /* 18 */
	{  61, 15,  0 }, /* 19 */
	{  61, 15,  0 }, /* 20 */
	{  61, 15,  0 }, /* 21 */
	{  61, 15,  0 }, /* 22 */
	{  61, 15,  0 }, /* 23 */
	{  61, 15,  0 }, /* 24 */
	{  61, 15,  0 }, /* 25 */
	{  61, 15,  0 }, /* 26 */
	{  61, 15,  0 }, /* 27 */
	{  61, 15,  0 }, /* 28 */
	{ 750,  0,  0 }, /* 29 */
	{ 750,  0,  0 }, /* 30 */
	{ 750,  0,  0 }, /* 31 */
};

struct txcal_tx_level rf_tx_levels_1900[RF_TX_LEVELS_TABLE_SIZE] = {
	{ 520,  0,  0 }, /* 0 */
	{ 465,  1,  0 }, /* 1 */
	{ 390,  2,  0 }, /* 2 */
	{ 330,  3,  0 }, /* 3 */
	{ 285,  4,  0 }, /* 4 */
	{ 250,  5,  0 }, /* 5 */
	{ 225,  6,  0 }, /* 6 */
	{ 205,  7,  0 }, /* 7 */
	{ 190,  8,  0 }, /* 8 */
	{ 177,  9,  0 }, /* 9 */
	{ 168, 10,  0 }, /* 10 */
	{ 161, 11,  0 }, /* 11 */
	{ 155, 12,  0 }, /* 12 */
	{ 150, 13,  0 }, /* 13 */
	{ 147, 14,  0 }, /* 14 */
	{ 143, 15,  0 }, /* 15 */
	{  62, 15,  0 }, /* 16 */
	{  62, 15,  0 }, /* 17 */
	{  62, 15,  0 }, /* 18 */
	{  62, 15,  0 }, /* 19 */
	{  62, 15,  0 }, /* 20 */
	{  62, 15,  0 }, /* 21 */
	{  62, 15,  0 }, /* 22 */
	{  62, 15,  0 }, /* 23 */
	{  62, 15,  0 }, /* 24 */
	{  62, 15,  0 }, /* 25 */
	{  62, 15,  0 }, /* 26 */
	{  62, 15,  0 }, /* 27 */
	{  62, 15,  0 }, /* 28 */
	{ 915,  0,  0 }, /* 29 */
	{ 915,  0,  0 }, /* 30 */
	{ 915,  0,  0 }, /* 31 */
};
