#include <stdint.h>
#include <stdio.h>

#include <debug.h>
#include <memory.h>
#include <rffe.h>
#include <calypso/tsp.h>
#include <rf/trf6151.h>

/* describe how the RF frontend is wired on the Openmoko GTA0x boards */

#define		RITA_RESET	TSPACT(0)	/* Reset of the Rita TRF6151 */
#define		PA_ENABLE	TSPACT(9)	/* Enable the Power Amplifier */
#define		GSM_TXEN	TSPACT(3)	/* PA GSM switch, low-active */

/* All VCn controls are low-active */
#define		ASM_VC1		TSPACT(2)	/* Antenna switch VC1 */
#define		ASM_VC2		TSPACT(1)	/* Antenna switch VC2 */
#define		ASM_VC3		TSPACT(4)	/* Antenna switch VC3 */

#define		IOTA_STROBE	TSPEN0		/* Strobe for the Iota TSP */
#define		RITA_STROBE	TSPEN2		/* Strobe for the Rita TSP */

/* switch RF Frontend Mode */
void rffe_mode(enum gsm_band band, int tx)
{
	uint16_t tspact = tsp_act_state();

	/* First we mask off all bits from the state cache */
	tspact &= ~PA_ENABLE;
	tspact |=  GSM_TXEN;	/* low-active */
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
		// TODO: Implement tx
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
	reg &= ~ (1 << 7);	/* TSPACT4 I/O function, not nRDYMEM */
	writew(reg, ARM_CONF_REG);

	reg = readw(MCU_SW_TRACE);
	reg &= ~(1 << 1);	/* TSPACT9 I/O function, not MAS(1) */
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
