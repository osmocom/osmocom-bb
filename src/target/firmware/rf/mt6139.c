/* Driver for RF Transceiver Circuit (MT6139) */

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
#include <keypad.h>
#include <osmocom/gsm/gsm_utils.h>

#include <layer1/agc.h>
#include <rffe.h>

#include <mtk/mt6139.h>

static void mt6139_compute_pll(uint32_t f_vco_100khz,
			       uint16_t *nint, uint16_t *nfrac)
{
	/* To compute Nint, we assume Nfrac is zero */
	*nint = (fvco_100khz / (10 * 2 * 26)) - (0 / 130);

	if (*nint > 127)
		printf("VCO Frequency %u kHz is out of spec\n", f_vco_100khz);

	/* Compute Nfract using the pre-computed Nint */
	/* Nfrac = ( (Fvco/2*26) - Nint) * 130 */
	/* Nfrac = ( (Fvco*130)/(2*26) - (Nint * 130) */
	*nfrac = (f_vco_100khz*130)/(52*10) - (*nint * 130);
}

/* Set ARFCN.  Takes 2 reg_write, i.e. 8 TPU instructions */
void mt6139_set_arfcn(uint16_t arfcn, int uplink)
{
	uint32_t regval = 0;
	uint32_t vco_mult;
	uint32_t freq_khz, f_vco_100khz;
	uint16_t nint, nfrac;

	freq_khz = gsm_arfcn2freq10(arfcn, uplink) * 100;
	printd("ARFCN %u -> %u kHz\n", arfcn, freq_khz);

	switch (gsm_arfcn2band(arfcn)) {
	case GSM_BAND_850:
		if (uplink)
			regval |= MT6139_CW1_TRX_850;
		regval |= (0 << MT6139_CW1_BAND_SHIFT);
		vco_mult = 4;
		break;
	case GSM_BAND_900:
		regval |= (1 << MT6139_CW1_BAND_SHIFT);
		vco_mult = 4;
		break;
	case GSM_BAND_1800:
		regval |= (2 << MT6139_CW1_BAND_SHIFT);
		vco_mult = 2;
		break;
	case GSM_BAND_1900:
		regval |= (3 << MT6139_CW1_BAND_SHIFT);
		vco_mult = 2;
		break;
	default:
		printf("Unsupported rf_band.\n");
		break;
	}

	/* Compute VCO frequency for channel frequency */
	f_vco_100khz = (freq_khz / 100) * vco_mult;

	/* Compute Nint and Nfract */
	mt6139_compute_pll(f_vco_100khz, &nint, &nfrac);

	/* mask-in the Nint / Nfrac bits in CW1 */
	regval |= (nfrac & 0xff) << MT6139_CW1_NFRACT_SHIFT;
	regval |= (nint & 0x7f) << MT6139_CW1_NINT_SHIFT;

}

void mt6139_init()
{
	uint32_t val;

	/* reset and get it out of reset again */
	val = MT6139_CW0_DIEN | (0x20 << MT6139_CW0_AFC_SHIFT);
	mt6139_reg_write(0, val | MT6139_CW0_POR);
	mt6139_reg_write(0, val);

	/* Turn off AM and A loop calibration function (CM9) */
	val = (0x40 << MT6139_CW9_DCD_CQ_SHIFT) |
	      (0x40 << MT6139_CW9_DCD_BQ_SHIFT) |
	      MT6139_CW9_PWR_DAC_C | MT6139_CW9_PWR_DAC_B;
	mt6139_reg_write(9, val);

	/* Move to SLEEP mode */
	val = (0x3e << MT6139_CW2_GAINTBL_SHIFT) |
	      (MODE_SLEEP << MT6139_CW2_MODE_SHIFT) |
	      MT6139_CW2_AUTO_CAL |
	      (0x20 << MT6139_CW2_DCD_AQ_SHIFT) |
	      (0x20 << MT6139_CW2_DCD_AI_SHIFT);
	mt6139_reg_write(2, val);
}

void mt6139_rx_burst()
{
	uint8_t pga_gain;

	/* Turn on the synthesizer and move into Warm-up mode */
	val = (0x3e << MT6139_CW2_GAINTBL_SHIFT) |
	      (MODE_WARM_UP << MT6139_CW2_MODE_SHIFT) |
	      MT6139_CW2_AUTO_CAL |
	      (0x20 << MT6139_CW2_DCD_AQ_SHIFT) |
	      (0x20 << MT6139_CW2_DCD_AI_SHIFT);
	mt6139_reg_write(2, val);

	/* Program the frequency synthesizer N counter and band selection */
	/* FIXME: see above for mt6139_set_arfcn() */

	/* Set receive mode, PGA gain */
	val = (pga_gain << MT6139_CW2_GAINTBL_SHIFT) |
	      (MODE_RECEIVE << MT6139_CW2_MODE_SHIFT) |
	      MT6139_CW2_AUTO_CAL |
	      (0x20 << MT6139_CW2_DCD_AQ_SHIFT) |
	      (0x20 << MT6139_CW2_DCD_AI_SHIFT);
	mt6139_reg_write(2, val);

	/* FIXME: Do the actual burst Rx */

	/* Set Sleep mode */
	val = (0x3e << MT6139_CW2_GAINTBL_SHIFT) |
	      (MODE_SLEEP << MT6139_CW2_MODE_SHIFT) |
	      MT6139_CW2_AUTO_CAL |
	      (0x20 << MT6139_CW2_DCD_AQ_SHIFT) |
	      (0x20 << MT6139_CW2_DCD_AI_SHIFT);
	mt6139_reg_write(2, val);
}

void mt6139_tx_burst()
{
	/* Turn on the synthesizer and move into Warm-up mode */
	val = (0x3e << MT6139_CW2_GAINTBL_SHIFT) |
	      (MODE_WARM_UP << MT6139_CW2_MODE_SHIFT) |
	      MT6139_CW2_AUTO_CAL |
	      (0x20 << MT6139_CW2_DCD_AQ_SHIFT) |
	      (0x20 << MT6139_CW2_DCD_AI_SHIFT);
	mt6139_reg_write(2, val);

	/* Program the frequency synthesizer N counter and band selection */
	/* FIXME: see above for mt6139_set_arfcn() */

	/* Send Tx setting */
	val = MT6139_CW11_TX_CTL |
	      MT6139_CW11_TXG_IQM |
	      MT6139_CW11_TXD_IQM |
	      MT6139_CW11_TX_DIV2 |
	      MT6139_CW11_TX_DIV4 |
	      MT6139_CW11_TXG_BUF |
	      MT6139_CW11_TXD_BUF |
	      (3 << MT6139_CW11_TX_FLT_SHIFT) |
	      (1 << MT6139_CW11_TXAPC_SHIFT) |
	      (3 << MT6139_CW11_TXPW_SHIFT) |
	      (2 << MT6139_CW11_TXBIAST_SHIFT) |
	      MT6139_CW11_TXDIV_GC0;
	if (1) // low band
		mt6139_reg_write(11, val | (0 << MT6139_CW11_TXMODGAIN_SHIFT));
	else
		mt6139_reg_write(11, val | (4 << MT6139_CW11_TXMODGAIN_SHIFT));

	/* Set Transmit mode */
	val = (0x3e << MT6139_CW2_GAINTBL_SHIFT) |
	      (MODE_TRANSMIT << MT6139_CW2_MODE_SHIFT) |
	      MT6139_CW2_AUTO_CAL |
	      (0x20 << MT6139_CW2_DCD_AQ_SHIFT) |
	      (0x20 << MT6139_CW2_DCD_AI_SHIFT);
	mt6139_reg_write(2, val);

	/* FIXME: Do the actual burst Tx */

	/* Set Sleep mode */
	val = (0x3e << MT6139_CW2_GAINTBL_SHIFT) |
	      (MODE_SLEEP << MT6139_CW2_MODE_SHIFT) |
	      MT6139_CW2_AUTO_CAL |
	      (0x20 << MT6139_CW2_DCD_AQ_SHIFT) |
	      (0x20 << MT6139_CW2_DCD_AI_SHIFT);
	mt6139_reg_write(2, val);
}

