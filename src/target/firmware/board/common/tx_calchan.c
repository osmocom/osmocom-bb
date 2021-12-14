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
 * On the better FreeCalypso, Openmoko and Pirelli targets, the following
 * Tx channel correction tables are placeholders to be overridden by
 * per-unit calibration, but Compal did their Tx channel calibration
 * in some different way which we haven't been able to grok, hence on
 * these targets we currently always run with these dummy tables,
 * and no channel-dependent corrections are applied.
 */

struct txcal_chan_cal rf_tx_chan_cal_850[RF_TX_CHAN_CAL_TABLE_SIZE]
					[RF_TX_NUM_SUB_BANDS] = {
	{
		{ 134, 128 },
		{ 150, 128 },
		{ 166, 128 },
		{ 182, 128 },
		{ 197, 128 },
		{ 213, 128 },
		{ 229, 128 },
		{ 251, 128 },
	},
	{
		{ 134, 128 },
		{ 150, 128 },
		{ 166, 128 },
		{ 182, 128 },
		{ 197, 128 },
		{ 213, 128 },
		{ 229, 128 },
		{ 251, 128 },
	},
	{
		{ 134, 128 },
		{ 150, 128 },
		{ 166, 128 },
		{ 182, 128 },
		{ 197, 128 },
		{ 213, 128 },
		{ 229, 128 },
		{ 251, 128 },
	},
	{
		{ 134, 128 },
		{ 150, 128 },
		{ 166, 128 },
		{ 182, 128 },
		{ 197, 128 },
		{ 213, 128 },
		{ 229, 128 },
		{ 251, 128 },
	},
};

struct txcal_chan_cal rf_tx_chan_cal_900[RF_TX_CHAN_CAL_TABLE_SIZE]
					[RF_TX_NUM_SUB_BANDS] = {
	{
		{   27, 128 },
		{   47, 128 },
		{   66, 128 },
		{   85, 128 },
		{  104, 128 },
		{  124, 128 },
		{  994, 128 },
		{ 1023, 128 },
	},
	{
		{   27, 128 },
		{   47, 128 },
		{   66, 128 },
		{   85, 128 },
		{  104, 128 },
		{  124, 128 },
		{  994, 128 },
		{ 1023, 128 },
	},
	{
		{   27, 128 },
		{   47, 128 },
		{   66, 128 },
		{   85, 128 },
		{  104, 128 },
		{  124, 128 },
		{  994, 128 },
		{ 1023, 128 },
	},
	{
		{   27, 128 },
		{   47, 128 },
		{   66, 128 },
		{   85, 128 },
		{  104, 128 },
		{  124, 128 },
		{  994, 128 },
		{ 1023, 128 },
	},
};

struct txcal_chan_cal rf_tx_chan_cal_1800[RF_TX_CHAN_CAL_TABLE_SIZE]
					 [RF_TX_NUM_SUB_BANDS] = {
	{
		{ 553, 128 },
		{ 594, 128 },
		{ 636, 128 },
		{ 677, 128 },
		{ 720, 128 },
		{ 760, 128 },
		{ 802, 128 },
		{ 885, 128 },
	},
	{
		{ 553, 128 },
		{ 594, 128 },
		{ 636, 128 },
		{ 677, 128 },
		{ 720, 128 },
		{ 760, 128 },
		{ 802, 128 },
		{ 885, 128 },
	},
	{
		{ 553, 128 },
		{ 594, 128 },
		{ 636, 128 },
		{ 677, 128 },
		{ 720, 128 },
		{ 760, 128 },
		{ 802, 128 },
		{ 885, 128 },
	},
	{
		{ 553, 128 },
		{ 594, 128 },
		{ 636, 128 },
		{ 677, 128 },
		{ 720, 128 },
		{ 760, 128 },
		{ 802, 128 },
		{ 885, 128 },
	},
};

struct txcal_chan_cal rf_tx_chan_cal_1900[RF_TX_CHAN_CAL_TABLE_SIZE]
					 [RF_TX_NUM_SUB_BANDS] = {
	{
		{ 549, 128 },
		{ 586, 128 },
		{ 623, 128 },
		{ 697, 128 },
		{ 726, 128 },
		{ 754, 128 },
		{ 782, 128 },
		{ 810, 128 },
	},
	{
		{ 549, 128 },
		{ 586, 128 },
		{ 623, 128 },
		{ 697, 128 },
		{ 726, 128 },
		{ 754, 128 },
		{ 782, 128 },
		{ 810, 128 },
	},
	{
		{ 549, 128 },
		{ 586, 128 },
		{ 623, 128 },
		{ 697, 128 },
		{ 726, 128 },
		{ 754, 128 },
		{ 782, 128 },
		{ 810, 128 },
	},
	{
		{ 549, 128 },
		{ 586, 128 },
		{ 623, 128 },
		{ 697, 128 },
		{ 726, 128 },
		{ 754, 128 },
		{ 782, 128 },
		{ 810, 128 },
	},
};
