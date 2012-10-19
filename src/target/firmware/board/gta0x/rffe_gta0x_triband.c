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

/* describe how the RF frontend is wired on the Openmoko GTA0x boards */

#define		RITA_RESET	TSPACT(0)	/* Reset of the Rita TRF6151 */
#define		PA_ENABLE	TSPACT(9)	/* Enable the Power Amplifier */
#define		GSM_TXEN	TSPACT(3)	/* PA GSM switch, low-active */

/* All VCn controls are low-active */
#define		ASM_VC1		TSPACT(2)	/* Antenna switch VC1 */
#define		ASM_VC2		TSPACT(1)	/* Antenna switch VC2 */
#define		ASM_VC3		TSPACT(4)	/* Antenna switch VC3 */

#define		IOTA_STROBE	TSPEN(0)	/* Strobe for the Iota TSP */
#define		RITA_STROBE	TSPEN(2)	/* Strobe for the Rita TSP */

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx)
{
	uint16_t tspact = tsp_act_state();

	/* First we mask off all bits from the state cache */
	tspact &= ~PA_ENABLE;
	tspact &= ~GSM_TXEN;
	tspact |= ASM_VC1 | ASM_VC2 | ASM_VC3; /* low-active */

	switch (band) {
	case GSM_BAND_850:
	case GSM_BAND_900:
	case GSM_BAND_1800:
		break;
	case GSM_BAND_1900:
		tspact &= ~ASM_VC2;
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
			tspact &= ~ASM_VC3;
			break;
		case GSM_BAND_1800:
		case GSM_BAND_1900:
			tspact &= ~ASM_VC1;
			tspact |= ASM_VC2;
			tspact |= GSM_TXEN;
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
	reg &= ~ (1 << 7);	/* TSPACT4 I/O function, not nRDYMEM */
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
