/* RF frontend driver for Sony Ericsson K200i/K220i */

/* (C) 2022 by Steve Markgraf <steve@steve-m.de>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <rffe.h>
#include <calypso/tsp.h>
#include <rf/trf6151.h>

/* This is a value that has been measured on the C123 by Harald: 71dBm,
   it is the difference between the input level at the antenna and what
   the DSP reports, subtracted by the total gain of the TRF6151 */
#define SYSTEM_INHERENT_GAIN	71

/* describe how the RF frontend is wired on the SE K200i/K220i */
#define		RITA_RESET	TSPACT(0)	/* Reset of the Rita TRF6151, correct for K200i */
#define		PA_ENABLE	TSPACT(9)	/* Enable the Power Amplifier */
#define		DCS_TXEN	TSPACT(3)	/* DCS (as opposed to GSM) Transmit */

#define		ASM_VC1		TSPACT(1)	/* GSM1800 TX at antenna switch, inverted */
#define		ASM_VC2		TSPACT(2)	/* GSM900 TX at antenna switch, inverted */

#define		IOTA_STROBE	TSPEN(1)	/* Strobe for the Iota TSP */
#define		RITA_STROBE	TSPEN(2)	/* Strobe for the Rita TSP */

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx)
{
	uint16_t tspact = tsp_act_state();

	/* First we mask off all bits from the state cache */
	tspact &= ~(PA_ENABLE | DCS_TXEN);
	tspact |= ASM_VC1 | ASM_VC2;

#ifdef CONFIG_TX_ENABLE
	/* Then we selectively set the bits on, if required */
	if (tx) {
		if ((band == GSM_BAND_1800) || band == (GSM_BAND_1900)) {
			tspact |= DCS_TXEN;
			tspact &= ~ASM_VC1;
		} else
			tspact &= ~ASM_VC2;

		tspact |= PA_ENABLE;
	}
#endif /* TRANSMIT_SUPPORT */

	tsp_act_update(tspact);
}

uint32_t rffe_get_rx_ports(void)
{
	return (1 << PORT_LO) | (1 << PORT_DCS1800) | (1 << PORT_PCS1900);
}

uint32_t rffe_get_tx_ports(void)
{
	return (1 << PORT_LO) | (1 << PORT_HI);
}

/* Returns need for IQ swap */
int rffe_iq_swapped(uint16_t band_arfcn, int tx)
{
	return trf6151_iq_swapped(band_arfcn, tx);
}

#define MCU_SW_TRACE	0xfffef00e
#define ARM_CONF_REG	0xfffef006
#define ASIC_CONF_REG	0xfffef008

void rffe_init(void)
{
	/* Configure the TSPEN which is connected to the TWL3025 */
	tsp_setup(IOTA_STROBE, 1, 0, 0);

	trf6151_init(RITA_STROBE, RITA_RESET);
}

uint8_t rffe_get_gain(void)
{
	return trf6151_get_gain();
}

void rffe_set_gain(uint8_t dbm)
{
	trf6151_set_gain(dbm);
}

const uint8_t system_inherent_gain = SYSTEM_INHERENT_GAIN;

/* Given the expected input level of exp_inp dBm/8 and the target of target_bb
 * dBm8, configure the RF Frontend with the respective gain */
void rffe_compute_gain(int16_t exp_inp, int16_t target_bb)
{
	trf6151_compute_gain(exp_inp, target_bb);
}

void rffe_rx_win_ctrl(int16_t exp_inp, int16_t target_bb)
{
	/* FIXME */
}
