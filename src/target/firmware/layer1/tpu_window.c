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
#include <defines.h>
#include <stdio.h>

#include <rffe.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <abb/twl3025.h>
#include <rf/trf6151.h>

#include <layer1/sync.h>
#include <layer1/tpu_window.h>
#include <layer1/rfch.h>

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


static int _win_setup(__unused uint8_t p1, __unused uint8_t p2, __unused uint16_t p3)
{
	uint8_t tn;

	rfch_get_params(&l1s.next_time, NULL, NULL, &tn);

	l1s.tpu_offset = (5000 + l1s.tpu_offset + l1s.tpu_offset_correction) % 5000;
	l1s.tpu_offset_correction = 0;

	tpu_enq_at(4740);
	tpu_enq_sync((5000 + l1s.tpu_offset + (L1_BURST_LENGTH_Q * tn)) % 5000);

	return 0;
}

static int _win_cleanup(__unused uint8_t p1, __unused uint8_t p2, __unused uint16_t p3)
{
	uint8_t tn;

	rfch_get_params(&l1s.next_time, NULL, NULL, &tn);

	/* restore offset */
	tpu_enq_offset((5000 + l1s.tpu_offset + (L1_BURST_LENGTH_Q * tn)) % 5000);

	return 0;
}

void l1s_win_init(void)
{
	tdma_schedule(0, _win_setup,   0, 0, 0, -2);
	tdma_schedule(0, _win_cleanup, 0, 0, 0,  9);
}

void l1s_rx_win_ctrl(uint16_t arfcn, enum l1_rxwin_type wtype, uint8_t tn_ofs)
{
	int16_t start;
	int32_t stop;	/* prevent overflow of int16_t in L1_RXWIN_FB */

	/* TN offset & TA adjust */
	start = DSP_SETUP_TIME;
	start += L1_BURST_LENGTH_Q * tn_ofs;

	stop = start + rx_burst_duration[wtype] - 1;

	/* window open for TRF6151 */
	/* FIXME: why do we need the magic value 100 ? */
	rffe_mode(gsm_arfcn2band(arfcn), 0);
	trf6151_rx_window(start - 100, arfcn);

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
	twl3025_downlink(0, stop & 0xffff);

	/* window close for TRF6151 */
	trf6151_set_mode(TRF6151_IDLE);
}

void l1s_tx_win_ctrl(uint16_t arfcn, enum l1_txwin_type wtype, uint8_t pwr, uint8_t tn_ofs)
{
	uint16_t offset;

	/* TN offset & TA adjust */
	offset = 28; /* ("+ 32" gives a TA of 1) */
	offset += L1_BURST_LENGTH_Q * tn_ofs;
	offset -= l1s.ta << 2;

#ifdef CONFIG_TX_ENABLE
	/* window open for TRF6151 */
	trf6151_tx_window(offset, arfcn);
#endif

	/* Window open for ABB */
	twl3025_uplink(1, offset);

#ifdef CONFIG_TX_ENABLE
	/* Window open for RFFE */
	rffe_mode(gsm_arfcn2band(arfcn), 1);
#endif

	/* Window close for ABB */
	twl3025_uplink(0, tx_burst_duration[wtype] + offset + 2); // TODO: "+ 2"

	/* window close for TRF6151 */
	trf6151_set_mode(TRF6151_IDLE);

	/* Window close for RFFE */
	rffe_mode(gsm_arfcn2band(arfcn), 0);
}

void tpu_end_scenario(void)
{
	tpu_enq_sleep();
	tpu_enable(1);
}
