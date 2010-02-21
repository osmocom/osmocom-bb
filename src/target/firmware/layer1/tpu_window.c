/* TPU window control routines for Layer 1 */

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
#include <debug.h>
#include <stdio.h>

#include <rffe.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <abb/twl3025.h>
#include <rf/trf6151.h>

#include <layer1/tpu_window.h>

/* all units in GSM quarter-bits (923.1ns) */
#define L1_TDMA_LENGTH_Q	5000
#define L1_BURST_LENGTH_Q	625	/* L1_TDMA_LENGTH_Q/8 */

#define L1_NB_MARGIN_Q		(3 * 4)
#define L1_SB_MARGIN_Q		(23 * 4)
#define L1_TAIL_DURATION_Q	(3 * 4)

/* Sample length as required by the Calypso DSP */
#define L1_NB_DURATION_Q	(L1_BURST_LENGTH_Q + 2 * L1_NB_MARGIN_Q - L1_TAIL_DURATION_Q)
#define L1_SB_DURATION_Q	(L1_BURST_LENGTH_Q + 2 * L1_SB_MARGIN_Q - L1_TAIL_DURATION_Q)
#define L1_FB_DURATION_Q	(11 * L1_TDMA_LENGTH_Q + 2057)	/* more than 11 full slots */
#define L1_FB26_DURATION_Q	(L1_TDMA_LENGTH_Q + 798)
#define	L1_PW_DURATION_Q	289

#define DSP_SETUP_TIME		66

static const uint16_t rx_burst_duration[_NUM_L1_RXWIN] = {
	[L1_RXWIN_PW]	= L1_PW_DURATION_Q,
	[L1_RXWIN_FB]	= L1_FB_DURATION_Q,
	[L1_RXWIN_SB]	= L1_SB_DURATION_Q,
	[L1_RXWIN_NB]	= L1_NB_DURATION_Q,
};

#define L1_TX_NB_DURATION_Q	626
#define L1_TX_AB_DURATION_Q	386

static const uint16_t tx_burst_duration[_NUM_L1_TXWIN] = {
	[L1_TXWIN_NB]	= L1_TX_NB_DURATION_Q,
	[L1_TXWIN_AB]	= L1_TX_AB_DURATION_Q,
};

void l1s_rx_win_ctrl(uint16_t arfcn, enum l1_rxwin_type wtype)
{
	int16_t start = DSP_SETUP_TIME;
	int16_t stop = start + rx_burst_duration[wtype] - 1;

	/* FIXME: AGC */
	/* FIXME: RF PLL */

	/* window open for TRF6151 */
	/* FIXME: why do we need the magic value 100 ? */
	rffe_mode(gsm_arfcn2band(arfcn), 0);
	trf6151_rx_window(start - 100, arfcn, 40, 0);

	/* Window open for ABB */
	twl3025_downlink(1, start);

	/* Delay 11 full TDMA frames */
	if (wtype == L1_RXWIN_FB) {
		uint8_t i;
		for (i = 0; i < 11; i++)
			tpu_enq_at(0);

		stop -= 11 * L1_TDMA_LENGTH_Q;
	}

	/* Window close for ABB */
	twl3025_downlink(0, stop);

	/* window close for TRF6151 */
	trf6151_set_mode(TRF6151_IDLE);
}

void l1s_tx_win_ctrl(uint16_t arfcn, enum l1_txwin_type wtype, uint8_t pwr)
{
	/* uplink is three TS after downlink ( "+ 32" gives a TA of 1) */
	uint16_t offset = (L1_BURST_LENGTH_Q * 3) + 28;

	/* FIXME: window open for TRF6151 and RFFE */

	/* Window open for ABB */
	twl3025_uplink(1, offset);

	/* Window close for ABB */
	twl3025_uplink(0, tx_burst_duration[wtype] + offset + 2); // TODO: "+ 2"

	/* FIXME: window close for TRF6151 and RFFE */
}

void tpu_end_scenario(void)
{
	tpu_enq_sleep();
	tpu_enable(1);
}
