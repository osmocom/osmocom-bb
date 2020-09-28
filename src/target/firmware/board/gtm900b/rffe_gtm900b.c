/* RF frontend driver for Huawei GTM900-B modems, supporting both
 * MG01GSMT and MGCxGSMT hardware variants */

/* (C) 2019 by Steve Markgraf <steve@steve-m.de>
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
#include <flash/cfi_flash.h>

/* This is a value that has been measured for the GTM900-B: 74dBm,
   it is the difference between the input level at the antenna and what
   the DSP reports, subtracted by the total gain of the TRF6151 */
#define SYSTEM_INHERENT_GAIN	74

/* describe how the RF frontend is wired on the Huawei GTM900-B */
#define		IOTA_STROBE	TSPEN(0)	/* Strobe for the Iota TSP */
#define		RITA_STROBE	TSPEN(2)	/* Strobe for the Rita TSP */

#define		RITA_RESET	TSPACT(0)	/* Reset of the Rita TRF6151 */
#define		PA_BAND_SELECT	TSPACT(3)	/* Low: 850/900, High: 1800/1900 */
#define		PA_TX_ENABLE	TSPACT(9)	/* Enable the Power Amplifier */

/* MGC2GSMT Ver. BRF specific antenna switch signals, low active */
#define		ASM_VC1		TSPACT(1)	/* low on GSM900 TX */
#define		ASM_VC2		TSPACT(2)	/* low on DCS1800 TX */

/* MG01GSMT Ver. C specific antenna switch signals, low active */
#define		CXG_CTLA	TSPACT(4)	/* CXG1192UR CTLA input */
#define		CXG_CTLB	TSPACT(1)	/* CXG1192UR CTLB input */
#define		CXG_CTLC	TSPACT(2)	/* CXG1192UR CTLC input */

/*
 * The Sony CXG1192UR switch is wired as follows on the MG01GSMT Ver. C:
 *
 * Rx1: GSM850 RX filter (Epcos B5013)
 * Rx2: GSM900 RX filter (Epcos B7710)
 * Rx3: DCS1800 RX filter (Epcos 7714)
 * Rx4: PCS1900 RX filter (not populated)
 * Tx1: low band PA output
 * Tx2: high band PA output
 */

extern int gtm900_hw_is_mg01gsmt;	/* set in init.c */

static inline void rffe_mode_mgc2gsmt(enum gsm_band band, int tx)
{
	uint16_t tspact = tsp_act_state();

	/* First we mask off all bits from the state cache */
	tspact &= ~(PA_BAND_SELECT | PA_TX_ENABLE);
	tspact |=  (ASM_VC1 | ASM_VC2);	/* low-active */

#ifdef CONFIG_TX_ENABLE
	/* Then we selectively set the bits on, if required */
	if (tx) {
		tspact |= PA_TX_ENABLE;
		tspact &= ~CXG_CTLA;

		if (band == GSM_BAND_1800 || band == GSM_BAND_1900) {
			tspact |= PA_BAND_SELECT;
			tspact &= ~ASM_VC2;
		} else {
			tspact &= ~ASM_VC1;
		}
	}
#endif /* TRANSMIT_SUPPORT */

	tsp_act_update(tspact);
}

static inline void rffe_mode_mg01gsmt(enum gsm_band band, int tx)
{
	uint16_t tspact = tsp_act_state();

	/* First we mask off all bits from the state cache */
	tspact &= ~(PA_BAND_SELECT | PA_TX_ENABLE);
	tspact |=  (CXG_CTLA | CXG_CTLB | CXG_CTLC);	/* low-active */

	switch (band) {
	case GSM_BAND_850:
		tspact &= ~CXG_CTLB;		  /* select Ant1 - Rx1 */
		break;
	case GSM_BAND_900:
		tspact &= ~CXG_CTLC;		  /* select Ant1 - Rx2 */
		break;
	case GSM_BAND_1800:			  /* select Ant2 - Rx3 */
		break;
	case GSM_BAND_1900:
		tspact &= ~(CXG_CTLB | CXG_CTLC); /* select Ant2 - Rx4 */
		break;
	default:
		break;
	}

#ifdef CONFIG_TX_ENABLE
	/* Then we selectively set the bits on, if required */
	if (tx) {
		tspact |= PA_TX_ENABLE;
		tspact &= ~CXG_CTLA;

		if (band == GSM_BAND_1800 || band == GSM_BAND_1900) {
			tspact |= PA_BAND_SELECT;
			tspact &= ~CXG_CTLB;
		}
	}
#endif /* TRANSMIT_SUPPORT */

	tsp_act_update(tspact);
}

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx)
{
	if (gtm900_hw_is_mg01gsmt)
		rffe_mode_mg01gsmt(band, tx);
	else
		rffe_mode_mgc2gsmt(band, tx);
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

#define ARM_CONF_REG	0xfffef006

void rffe_init(void)
{
	uint16_t reg;

	reg = readw(ARM_CONF_REG);
	reg &= ~ (1 << 7);	/* TSPACT4 I/O function, not nRDYMEM */
	writew(reg, ARM_CONF_REG);

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
