/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
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
 */

/*
 * This module implements RFFE control for TI's original Leonardo+
 * quadband RFFE, depicted on page 4 of this 2011-find schematic drawing:
 *
 * https://www.freecalypso.org/pub/GSM/Calypso/Leonardo_plus_quadband_schem.pdf
 *
 * This TI-original quadband RFFE is reproduced verbatim on the TR-800
 * packaged module by iWOW.
 *
 * The present C code is based on ../gta0x/rffe_gta0x_triband.c,
 * controlling Openmoko's triband RFFE which is very closely based on
 * Leonardo, with only a few control signal permutations.
 *
 * The present code addition by Mother Mychaela merely brings the TR-800 hw
 * target to the same level of support that already existed in OBB since
 * forever for Compal/Motorola and Openmoko GTA01/02 targets, and more
 * recently GTM900 and FCDEV3B - it does NOT fix the problem of overly
 * simplistic RFFE control timing and other oversimplifications which OBB
 * exhibits in comparison to the official firmware maintained by the
 * custodians of the Calypso+Iota+RF chipset (formerly TI, now FreeCalypso).
 * These massive oversimplifications which OBB exhibits in comparison to
 * officially approved production firmwares result in OBB's radio transmissions
 * being SEVERELY out of compliance (as observed with even the simplest tests
 * with a CMU200 RF test instrument), thus anyone who runs the present code
 * with Tx enabled outside of a Faraday cage will very likely cause
 * interference and disruption to public communication networks!  Furthermore,
 * if you go on with running OBB with Tx enabled after having read this
 * warning, the resulting interference and disruption to public communication
 * networks can be considered intentional on your part, which is likely to be
 * seen as a more severe offense.
 */

#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <rffe.h>
#include <calypso/tsp.h>
#include <rf/trf6151.h>

/*
 * OsmocomBB's definition of system inherent gain is similar to what is
 * called "magic gain" (GMagic) in TI's architecture, except that TI's
 * GMagic includes TRF6151 LNA gain whereas OBB's definition of system
 * inherent gain does not.  TI's GMagic is also reckoned in half-dB units
 * instead of integral dB.
 *
 * The canonical GMagic number for Leonardo/TR-800 RFFE is 200, both in
 * iWOW's original calibration and as confirmed with CMU200 measurements
 * at FreeCalypso HQ.  GMagic=200 in TI's universe is equivalent to
 * OsmocomBB's "system inherent gain" of 73 dB.
 */
#define SYSTEM_INHERENT_GAIN	73

/* describe how the RF frontend is wired on Leonardo and TR-800 */

#define		RITA_RESET	TSPACT(0)	/* Reset of the Rita TRF6151 */
#define		PA_ENABLE	TSPACT(9)	/* Enable the Power Amplifier */
#define		PA_BAND_SEL	TSPACT(3)	/* PA band select, 1=DCS/PCS */

/* All FEM controls are low-active */
#define		FEM_7		TSPACT(2)	/* FEM pin 7 */
#define		FEM_8		TSPACT(1)	/* FEM pin 8 */
#define		FEM_9		TSPACT(4)	/* FEM pin 9 */

#define		IOTA_STROBE	TSPEN(0)	/* Strobe for the Iota TSP */
#define		RITA_STROBE	TSPEN(2)	/* Strobe for the Rita TSP */

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx)
{
	uint16_t tspact = tsp_act_state();

	/* First we mask off all bits from the state cache */
	tspact &= ~PA_ENABLE;
	tspact &= ~PA_BAND_SEL;
	tspact |= FEM_7 | FEM_8 | FEM_9; /* low-active */

	switch (band) {
	case GSM_BAND_850:
		tspact &= ~FEM_9;
		break;
	case GSM_BAND_900:
	case GSM_BAND_1800:
	case GSM_BAND_1900:
		break;
	default:
		/* TODO return/signal error here */
		break;
	}

#ifdef CONFIG_TX_ENABLE
	/* Then we selectively set the bits on, if required */
	if (tx) {
		switch (band) {
		case GSM_BAND_850:
		case GSM_BAND_900:
			tspact |= FEM_9;
			tspact &= ~FEM_7;
			break;
		case GSM_BAND_1800:
		case GSM_BAND_1900:
			tspact &= ~FEM_8;
			tspact |= PA_BAND_SEL;
			break;
		default:
			break;
		}
		tspact |= PA_ENABLE;
	}
#endif /* TRANSMIT_SUPPORT */

	tsp_act_update(tspact);
}

/* Returns RF wiring */
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

void rffe_init(void)
{
	uint16_t reg;

	reg = readw(ARM_CONF_REG);
	reg &= ~(1 << 7);	/* TSPACT4 I/O function, not nRDYMEM */
	writew(reg, ARM_CONF_REG);

	reg = readw(MCU_SW_TRACE);
	reg &= ~(1 << 1);	/* TSPACT9 I/O function, not MAS(1) */
	writew(reg, MCU_SW_TRACE);

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
