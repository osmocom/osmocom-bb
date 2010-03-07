#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <rffe.h>
#include <calypso/tsp.h>
#include <rf/trf6151.h>

/* describe how the RF frontend is wired on the Motorola E88 board (C117/C118/C121/C123) */

#define		RITA_RESET	TSPACT(0)	/* Reset of the Rita TRF6151 */
#define		PA_ENABLE	TSPACT(1)	/* Enable the Power Amplifier */
#define		TRENA		TSPACT(6)	/* Transmit Enable (Antenna Switch) */
#define		GSM_TXEN	TSPACT(8)	/* GSM (as opposed to DCS) Transmit */

#define		IOTA_STROBE	TSPEN0		/* Strobe for the Iota TSP */
#define 	RITA_STROBE	TSPEN2		/* Strobe for the Rita TSP */

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx)
{
	uint16_t tspact = tsp_act_state();

	/* First we mask off all bits from the state cache */
	tspact &= ~PA_ENABLE;
	tspact |= TRENA | GSM_TXEN;	/* low-active */

#ifdef CONFIG_TX_ENABLE
	/* Then we selectively set the bits on, if required */
	if (tx) {
		tspact &= ~TRENA;
		if (band == GSM_BAND_900)
			tspact &= ~GSM_TXEN;
		tspact |= PA_ENABLE;	/* Dieter: TODO */
	}
#endif /* TRANSMIT_SUPPORT */

	tsp_act_update(tspact);
}

#define MCU_SW_TRACE	0xfffef00e
#define ARM_CONF_REG	0xfffef006

void rffe_init(void)
{
	uint16_t reg;

	reg = readw(ARM_CONF_REG);
	reg &= ~ (1 << 5);	/* TSPACT6 I/O function, not nCS6 */
	writew(reg, ARM_CONF_REG);

	reg = readw(MCU_SW_TRACE);
	reg &= ~(1 << 5);	/* TSPACT8 I/O function, not nMREQ */
	writew(reg, MCU_SW_TRACE);
}

uint8_t rffe_get_gain(void)
{
	return trf6151_get_gain();
}

/* Given the expected input level of exp_inp dBm/8 and the target of target_bb
 * dBm8, configure the RF Frontend with the respective gain */
void rffe_set_gain(int16_t exp_inp, int16_t target_bb)
{
	/* FIXME */
}

void rffe_rx_win_ctrl(int16_t exp_inp, int16_t target_bb)
{
	/* FIXME */
}
