/* Calypso DBB internal TSP (Time Serial Port) Driver */

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

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>

static uint16_t tspact_state;

/* initiate a TSP write through the TPU */
void tsp_write(uint8_t dev_idx, uint8_t bitlen, uint32_t dout)
{
	if (bitlen <= 8) {
		tpu_enq_move(TPUI_TX_1, dout & 0xff);
	} else if (bitlen <= 16) {
		tpu_enq_move(TPUI_TX_1, (dout >> 8) & 0xff);
		tpu_enq_move(TPUI_TX_2, dout & 0xff);
	} else if (bitlen <= 24) {
		tpu_enq_move(TPUI_TX_1, (dout >> 16) & 0xff);
		tpu_enq_move(TPUI_TX_2, (dout >> 8) & 0xff);
		tpu_enq_move(TPUI_TX_3, dout & 0xff);
	} else {
		tpu_enq_move(TPUI_TX_1, (dout >> 24) & 0xff);
		tpu_enq_move(TPUI_TX_2, (dout >> 16) & 0xff);
		tpu_enq_move(TPUI_TX_3, (dout >> 8) & 0xff);
		tpu_enq_move(TPUI_TX_4, dout & 0xff);
	}
	tpu_enq_move(TPUI_TSP_CTRL1, (dev_idx << 5) | (bitlen - 1));
	tpu_enq_move(TPUI_TSP_CTRL2, TPUI_CTRL2_WR);
}

/* Configure clock edge and chip enable polarity for a device */
void tsp_setup(uint8_t dev_idx, int clk_rising, int en_positive, int en_edge)
{
	uint8_t reg = TPUI_TSP_SET1 + (dev_idx / 2);
	uint8_t val = 0;
	uint8_t shift;

	if (dev_idx & 1)
		shift = 4;
	else
		shift = 0;

	if (clk_rising)
		val |= 1;
	if (en_positive)
		val |= 2;
	if (en_edge)
		val |= 4;

	tpu_enq_move(reg, (val << shift));
}

/* Update the TSPACT state, including enable and disable */
void tsp_act_update(uint16_t new_act)
{
	uint8_t low = new_act & 0xff;
	uint8_t high = new_act >> 8;

	if (low != (tspact_state & 0xff))
		tpu_enq_move(TPUI_TSP_ACT_L, low);
	if (high != (tspact_state >> 8))
		tpu_enq_move(TPUI_TSP_ACT_U, high);

	tspact_state = new_act;
}

/* Enable one or multiple TSPACT signals */
void tsp_act_enable(uint16_t bitmask)
{
	uint16_t new_act = tspact_state | bitmask;
	tsp_act_update(new_act);
}

/* Disable one or multiple TSPACT signals */
void tsp_act_disable(uint16_t bitmask)
{
	uint16_t new_act = tspact_state & ~bitmask;
	tsp_act_update(new_act);
}

/* Obtain the current tspact state */
uint16_t tsp_act_state(void)
{
	return tspact_state;
}

/* Toggle one or multiple TSPACT signals */
void tsp_act_toggle(uint16_t bitmask)
{
	uint16_t new_act = tspact_state ^ bitmask;
	tsp_act_update(new_act);
}

void tsp_init(void)
{
	tsp_act_update(0);
}
