/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: handlers for DL / UL bursts on logical channels
 *
 * (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
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
#include <stdint.h>
#include <stdbool.h>

#include "scheduler.h"
#include "sched_trx.h"

static const uint8_t tch_h0_traffic_block_map[3][4] = {
	/* B0(0,2,4,6), B1(4,6,8,10), B2(8,10,0,2) */
	{ 0, 2, 4, 6 },
	{ 4, 6, 8, 10 },
	{ 8, 10, 0, 2 },
};

static const uint8_t tch_h1_traffic_block_map[3][4] = {
	/* B0(1,3,5,7), B1(5,7,9,11), B2(9,11,1,3) */
	{ 1, 3, 5, 7 },
	{ 5, 7, 9, 11 },
	{ 9, 11, 1, 3 },
};

static const uint8_t tch_h0_dl_facch_block_map[3][6] = {
	/* B0(4,6,8,10,13,15), B1(13,15,17,19,21,23), B2(21,23,0,2,4,6) */
	{ 4, 6, 8, 10, 13, 15 },
	{ 13, 15, 17, 19, 21, 23 },
	{ 21, 23, 0, 2, 4, 6 },
};

static const uint8_t tch_h0_ul_facch_block_map[3][6] = {
	/* B0(0,2,4,6,8,10), B1(8,10,13,15,17,19), B2(17,19,21,23,0,2) */
	{ 0, 2, 4, 6, 8, 10 },
	{ 8, 10, 13, 15, 17, 19 },
	{ 17, 19, 21, 23, 0, 2 },
};

static const uint8_t tch_h1_dl_facch_block_map[3][6] = {
	/* B0(5,7,9,11,14,16), B1(14,16,18,20,22,24), B2(22,24,1,3,5,7) */
	{ 5, 7, 9, 11, 14, 16 },
	{ 14, 16, 18, 20, 22, 24 },
	{ 22, 24, 1, 3, 5, 7 },
};

const uint8_t tch_h1_ul_facch_block_map[3][6] = {
	/* B0(1,3,5,7,9,11), B1(9,11,14,16,18,20), B2(18,20,22,24,1,3) */
	{ 1, 3, 5, 7, 9, 11 },
	{ 9, 11, 14, 16, 18, 20 },
	{ 18, 20, 22, 24, 1, 3 },
};

/**
 * Can a TCH/H block transmission be initiated / finished
 * on a given frame number and a given channel type?
 *
 * See GSM 05.02, clause 7, table 1
 *
 * @param  chan   channel type (TRXC_TCHH_0 or TRXC_TCHH_1)
 * @param  fn     the current frame number
 * @param  ul     Uplink or Downlink?
 * @param  facch  FACCH/H or traffic?
 * @param  start  init or end of transmission?
 * @return        true (yes) or false (no)
 */
bool sched_tchh_block_map_fn(enum trx_lchan_type chan,
	uint32_t fn, bool ul, bool facch, bool start)
{
	uint8_t fn_mf;
	int i = 0;

	/* Just to be sure */
	OSMO_ASSERT(chan == TRXC_TCHH_0 || chan == TRXC_TCHH_1);

	/* Calculate a modulo */
	fn_mf = facch ? (fn % 26) : (fn % 13);

#define MAP_GET_POS(map) \
	(start ? 0 : ARRAY_SIZE(map[i]) - 1)

#define BLOCK_MAP_FN(map) \
	do { \
		if (map[i][MAP_GET_POS(map)] == fn_mf) \
			return true; \
	} while (++i < ARRAY_SIZE(map))

	/* Choose a proper block map */
	if (facch) {
		if (ul) {
			if (chan == TRXC_TCHH_0)
				BLOCK_MAP_FN(tch_h0_ul_facch_block_map);
			else
				BLOCK_MAP_FN(tch_h1_ul_facch_block_map);
		} else {
			if (chan == TRXC_TCHH_0)
				BLOCK_MAP_FN(tch_h0_dl_facch_block_map);
			else
				BLOCK_MAP_FN(tch_h1_dl_facch_block_map);
		}
	} else {
		if (chan == TRXC_TCHH_0)
			BLOCK_MAP_FN(tch_h0_traffic_block_map);
		else
			BLOCK_MAP_FN(tch_h1_traffic_block_map);
	}

	return false;
}

/**
 * Calculates a frame number of the first burst
 * using given frame number of the last burst.
 *
 * See GSM 05.02, clause 7, table 1
 *
 * @param  chan      channel type (TRXC_TCHH_0 or TRXC_TCHH_1)
 * @param  last_fn   frame number of the last burst
 * @param  facch     FACCH/H or traffic?
 * @return           either frame number of the first burst,
 *                   or fn=last_fn if calculation failed
 */
uint32_t sched_tchh_block_dl_first_fn(enum trx_lchan_type chan,
	uint32_t last_fn, bool facch)
{
	uint8_t fn_mf, fn_diff;
	int i = 0;

	/* Just to be sure */
	OSMO_ASSERT(chan == TRXC_TCHH_0 || chan == TRXC_TCHH_1);

	/* Calculate a modulo */
	fn_mf = facch ? (last_fn % 26) : (last_fn % 13);

#define BLOCK_FIRST_FN(map) \
	do { \
		if (map[i][ARRAY_SIZE(map[i]) - 1] == fn_mf) { \
			fn_diff = TDMA_FN_DIFF(fn_mf, map[i][0]); \
			return TDMA_FN_SUB(last_fn, fn_diff); \
		} \
	} while (++i < ARRAY_SIZE(map))

	/* Choose a proper block map */
	if (facch) {
		if (chan == TRXC_TCHH_0)
			BLOCK_FIRST_FN(tch_h0_dl_facch_block_map);
		else
			BLOCK_FIRST_FN(tch_h1_dl_facch_block_map);
	} else {
		if (chan == TRXC_TCHH_0)
			BLOCK_FIRST_FN(tch_h0_traffic_block_map);
		else
			BLOCK_FIRST_FN(tch_h1_traffic_block_map);
	}

	LOGP(DSCHD, LOGL_ERROR, "Failed to calculate TDMA "
		"frame number of the first burst of %s block, "
		"using the current fn=%u\n", facch ?
			"FACCH/H" : "TCH/H", last_fn);

	/* Couldn't calculate the first fn, return the last */
	return last_fn;
}
