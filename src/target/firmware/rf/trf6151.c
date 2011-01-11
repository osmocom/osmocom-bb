/* Driver for RF Transceiver Circuit (TRF6151) */

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
#include <osmocore/gsm_utils.h>

#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <layer1/agc.h>
#include <rffe.h>

#include <rf/trf6151.h>

enum trf6151_reg {
	REG_RX		= 0,	/* RF general settings */
	REG_PLL		= 1,	/* PLL settings */
	REG_PWR		= 2,	/* Power on/off functional blocks */
	REG_CFG		= 3,	/* Transceiver and PA controller settings */
	REG_TEST1	= 4,
	REG_TEST2	= 5,
	REG_TEST3	= 6,
	REG_TEST4	= 7,
	_MAX_REG
};

/* REG_RX */
#define RX_READ_EN		(1 << 7)
#define RX_CAL_MODE		(1 << 8)
#define RX_RF_GAIN_HIGH		(3 << 9)
#define RX_VGA_GAIN_SHIFT	11

/* REG_PWR */
#define PWR_BANDGAP_SHIFT	3
#define PWR_BANDGAP_OFF		(0 << PWR_BANDGAP_SHIFT)
#define PWR_BANDGAP_ON_SPEEDUP	(2 << PWR_BANDGAP_SHIFT)
#define PWR_BANDGAP_ON		(3 << PWR_BANDGAP_SHIFT)
#define PWR_REGUL_ON		(1 << 5)
#define PWR_SYNTHE_OFF		(0)
#define PWR_SYNTHE_RX_ON	(1 << 9)
#define PWR_SYNTHE_TX_ON	(1 << 10)
#define PWR_RX_MODE		(1 << 11)
#define PWR_TX_MODE		(1 << 13)
#define PWR_PACTRL_APC		(1 << 14)
#define PWR_PACTRL_APCEN	(1 << 15)

/* REG_CFG */
#define CFG_TX_LOOP_MANU	(1 << 3)
#define CFG_PACTLR_IDIOD_30uA	(0 << 4)
#define CFG_PACTLR_IDIOD_300uA	(1 << 4)
#define CFG_PACTLR_RES_OPEN	(0 << 10)
#define CFG_PACTLR_RES_150k	(1 << 10)
#define CFG_PACTLR_RES_300k	(2 << 10)
#define CFG_PACTLR_CAP_0pF	(0 << 12)
#define CFG_PACTLR_CAP_12p5F	(1 << 12)
#define CFG_PACTLR_CAP_25pF	(3 << 12)
#define CFG_PACTLR_CAP_50pF	(2 << 12)
#define CFG_TEMP_SENSOR		(1 << 14)
#define CFG_ILOGIC_INIT_DIS	(1 << 15)

/* FIXME: This must be defined in the RFFE configuration */
#define TRF6151_TSP_UID		2
#define TRF6151_PACTRL_CFG	(CFG_PACTLR_RES_OPEN|CFG_PACTLR_CAP_0pF|CFG_PACTLR_IDIOD_30uA)

#define PLL_VAL(a, b)	((a << 3) | (((b)-64) << 9))

/* All values in qbits unless otherwise specified */
#define TRF6151_LDO_DELAY_TS	6	/* six TDMA frames (at least 25ms) */
#define TRF6151_RX_PLL_DELAY	184	/* 170 us */
#define TRF6151_TX_PLL_DELAY	260	/* 240 us */

uint16_t rf_arfcn = 871;	/* TODO: this needs to be private */
static uint16_t rf_band;

static uint8_t trf6151_vga_dbm = 40;
static int trf6151_gain_high = 1;

static uint16_t trf6151_reg_cache[_MAX_REG] = {
	[REG_RX] 	= 0x9E00,
	[REG_PLL]	= 0x0000,
	[REG_PWR]	= 0x0000,
	[REG_CFG]	= 0x2980,
};

/* Write to a TRF6151 register (4 TPU instructions) */
static void trf6151_reg_write(uint16_t reg, uint16_t val)
{
	printd("trf6151_reg_write(reg=%u, val=0x%04x)\n", reg, val);
	/* each TSP write takes 4 TPU instructions */
	tsp_write(TRF6151_TSP_UID, 16, (reg | val));
	trf6151_reg_cache[reg] = val;
}

int trf6151_set_gain(uint8_t dbm, int high)
{
	uint16_t reg = trf6151_reg_cache[REG_RX] & 0x07ff;
	printd("trf6151_set_gain(%u, %d)\n", dbm, high);

	if (dbm < 14 || dbm > 40)
		return -1;

	/* clear the gain bits first */
	reg &= ~((0x1F) << RX_VGA_GAIN_SHIFT);
	/* OR-in the new gain value */
	reg |= (6 + (dbm-14)/2) << RX_VGA_GAIN_SHIFT;

	if (high)
		reg |= RX_RF_GAIN_HIGH;
	else
		reg &= ~RX_RF_GAIN_HIGH;

	trf6151_reg_write(REG_RX, reg);

	return 0;
}

#define SCALE_100KHZ	100

/* Compute TRF6151 PLL valuese for all 4 RX bands */
static uint16_t trf6151_pll_rx(uint32_t freq_khz)
{
	uint32_t freq_100khz = freq_khz / SCALE_100KHZ;	/* Scale from *1000 (k) to *100000 (0.1M) */
	uint32_t fb_100khz;	/* frequency of B alone, without A (units of 100kHz) */
	uint32_t l;
	uint32_t a, b;	/* The PLL multipliers we want to compute */

	/* L = 4 for low band, 2 for high band */
	if (freq_khz < 1000000)
		l = 4;
	else
		l = 2;

	/* To compute B, we assume A is zero */
	b = (freq_100khz * 65 * l) / (64 * 26 * 10);

	if ((l == 4 && (b < 135 || b > 150)) ||
	    (l == 2 && (b < 141 || b > 155)))
		printf("Frequency %u kHz is out of spec\n", freq_khz);

	/* Compute PLL frequency assuming A == 0 */
	fb_100khz = (b * 64 * 26 * 10) / (65 * l);

	/* Compute how many 100kHz units A needs to add */
	a = freq_100khz - fb_100khz;

	if (l == 2)
		a = a / 2;

	/* since all frequencies are expanded a factor of 10, we don't need to multiply A */
	printd("Freq %u kHz => A = %u, B = %u\n", freq_khz, a, b);

	/* return value in trf6151 register layout form */
	return PLL_VAL(a, b);
}

/* Compute TRF6151 PLL TX values for GSM900 and GSM1800 only! */
static uint16_t trf6151_pll_tx(uint32_t freq_khz)
{
	uint32_t freq_100khz = freq_khz / SCALE_100KHZ;	/* Scale from *1000 (k) to *100000 (0.1M) */
	uint32_t fb_100khz;	/* frequency of B alone, without A (units of 100kHz) */
	uint32_t l, r, m;
	uint32_t a, b;	/* The PLL multipliers we want to compute */

	/* L = 4 for low band, 2 for high band */
	if (freq_khz < 1000000) {
		r = 35;
		l = 4;
		m = 52;
	} else {
		r = 70;
		l = 2;
		m = 26;
	}

	/* To compute B, we assume A is zero */
	b = (freq_100khz * r * l * m) / (64 * 26 * 10 * (m + l));

	if ((l == 4 && (b <  68 || b >  71)) ||
	    (l == 2 && (b < 133 || b > 149)))
		printf("Frequency %u kHz is out of spec\n", freq_khz);

	/* Compute PLL frequency assuming A == 0 */
	fb_100khz = (b * 64 * 26 * 10 * (m + l)) / (r * l * m);

	/* Compute how many 100kHz units A needs to add */
	a = freq_100khz - fb_100khz;

	a = a / 2;

	/* since all frequencies are expanded a factor of 10, we don't need to multiply A */
	printd("Freq %u kHz => A = %u, B = %u\n", freq_khz, a, b);

	/* return value in trf6151 register layout form */
	return PLL_VAL(a, b);
}

enum trf6151_pwr_unit {
	TRF1651_PACTLR_APC,
	TRF6151_PACTRL_APCEN,
	TRF6151_TRANSMITTER,
	TRF6151_REGULATORS,
};

enum trf6151_gsm_band {
	GSM900		= 1,
	GSM1800		= 2,
	GSM850_LOW	= 4,
	GSM850_HIGH	= 5,
	GSM1900		= 6,
};

static inline void trf6151_reset(void)
{
	/* pull the nRESET line low */
	tsp_act_disable((1 << 0));
	tpu_enq_wait(50);
	/* release nRESET */
	tsp_act_enable((1 << 0));
}

void trf6151_init(void)
{
	/* Configure TSPEN0, which is connected to TWL3025,
	 * FIXME: why is this here and not in the TWL3025 driver? */
	tsp_setup(0, 1, 0, 0);
	/* Configure TSPEN2, which is connected ot TRF6151 STROBE */
	tsp_setup(TRF6151_TSP_UID, 0, 1, 1);

	trf6151_reset();

	/* configure TRF6151 for operation */
	trf6151_power(1);
	trf6151_reg_write(REG_CFG, TRF6151_PACTRL_CFG | CFG_ILOGIC_INIT_DIS);

	/* FIXME: Uplink / Downlink Calibration */
}

void trf6151_power(int on)
{
	if (on) {
		trf6151_reg_write(REG_PWR, PWR_REGUL_ON | PWR_BANDGAP_ON);
		/* wait until regulators are stable (25ms == 27100 qbits) */
		tpu_enq_wait(5000);
		tpu_enq_wait(5000);
		tpu_enq_wait(5000);
		tpu_enq_wait(5000);
		tpu_enq_wait(5000);
		tpu_enq_wait(2100);
	} else
		trf6151_reg_write(REG_PWR, PWR_BANDGAP_ON);
}

/* Set the operational mode of the TRF6151 chip */
void trf6151_set_mode(enum trf6151_mode mode)
{
	uint16_t pwr = (PWR_REGUL_ON | PWR_BANDGAP_ON | (rf_band<<6));

	switch (mode) {
	case TRF6151_IDLE:
		/* should we switch of the RF gain for power saving? */
		break;
	case TRF6151_RX:
		pwr |= (PWR_SYNTHE_RX_ON | PWR_RX_MODE);
		break;
	case TRF6151_TX:
#if 0
		pwr |= (PWR_SYNTHE_TX_ON | PWR_TX_MODE);
#else // Dieter: we should turn power control on (for TPU: check timing and order !)
		pwr |= (PWR_SYNTHE_TX_ON | PWR_TX_MODE | PWR_PACTRL_APC | PWR_PACTRL_APCEN); // Dieter: TODO
#endif
		break;
	}
	trf6151_reg_write(REG_PWR, pwr);
}

static void trf6151_band_select(enum trf6151_gsm_band band)
{
	uint16_t pwr = trf6151_reg_cache[REG_PWR];

	pwr &= ~(3 << 6);
	pwr |= (band << 6);

	trf6151_reg_write(REG_PWR, pwr);
}

/* Set ARFCN.  Takes 2 reg_write, i.e. 8 TPU instructions */
void trf6151_set_arfcn(uint16_t arfcn, int uplink)
{
	uint32_t freq_khz;

	switch (gsm_arfcn2band(arfcn)) {
	case GSM_BAND_850:
		rf_band = GSM850_LOW;	/* FIXME: what about HIGH */
		break;
	case GSM_BAND_900:
		rf_band = GSM900;
		break;
	case GSM_BAND_1800:
		rf_band = GSM1800;
		break;
	case GSM_BAND_1900:
		rf_band = GSM1900;
		break;
	case GSM_BAND_450:
	case GSM_BAND_480:
	case GSM_BAND_750:
	case GSM_BAND_810:
		printf("Unsupported rf_band.\n");
		break;
	}

	trf6151_band_select(rf_band);

	freq_khz = gsm_arfcn2freq10(arfcn, uplink) * 100;
	printd("ARFCN %u -> %u kHz\n", arfcn, freq_khz);

	if (uplink == 0)
		trf6151_reg_write(REG_PLL, trf6151_pll_rx(freq_khz));
	else {
		if (rf_band != GSM900 && rf_band != GSM1800) {
			printf("TX only supports GSM900/1800\n");
			return;
		}
		trf6151_reg_write(REG_PLL, trf6151_pll_tx(freq_khz));
	}

	rf_arfcn = arfcn; // TODO: arfcn is referenced at other places
}

void trf6151_calib_dc_offs(void)
{
	uint16_t rx = trf6151_reg_cache[REG_RX];

	/* Set RX CAL Mode bit, it will re-set automatically */
	trf6151_reg_write(REG_RX, rx | RX_CAL_MODE);
	/* DC offset calibration can take up to 50us, i.e. 54.16 * 923ns*/
	tpu_enq_wait(55);
}

/* Frontend gain can be switched high or low (dB) */
#define TRF6151_FE_GAIN_LOW	7
#define TRF6151_FE_GAIN_HIGH	27

/* VGA at baseband can be adjusted in this range (dB) */
#define TRF6151_VGA_GAIN_MIN	14
#define TRF6151_VGA_GAIN_MAX	40

uint8_t trf6151_get_gain(void)
{
	uint16_t vga, reg_rx = trf6151_reg_cache[REG_RX];
	uint8_t gain = 0;

	switch ((reg_rx >> 9) & 3) {
	case 0:
		gain += TRF6151_FE_GAIN_LOW;
		break;
	case 3:
		gain += TRF6151_FE_GAIN_HIGH;
		break;
	}

	vga = (reg_rx >> RX_VGA_GAIN_SHIFT) & 0x1f;
	if (vga < 6)
		vga = 6;

	gain += TRF6151_VGA_GAIN_MIN + (vga - 6) * 2;

	return gain;
}

void trf6151_test(uint16_t arfcn)
{
	/* Select ARFCN 871 downlink */
	trf6151_set_arfcn(arfcn, 0);

	trf6151_set_mode(TRF6151_RX);
	//trf6151_reg_write(REG_PWR, (PWR_SYNTHE_RX_ON | PWR_RX_MODE | PWR_REGUL_ON | (rf_band<<6) | PWR_BANDGAP_ON));
	/* Wait for PLL stabilization (170us max) */
	tpu_enq_wait(TRF6151_RX_PLL_DELAY);

	/* Use DC offset calibration after RX mode has been switched on
	 * (might not be needed) */
	trf6151_calib_dc_offs();

	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

void trf6151_tx_test(uint16_t arfcn)
{
	/* Select ARFCN uplink */
	trf6151_set_arfcn(arfcn, 1);

	trf6151_set_mode(TRF6151_TX);
	tpu_enq_wait(TRF6151_RX_PLL_DELAY);

	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

#define TRF6151_REGWR_QBITS	8	/* 4 GSM qbits + 4 TPU instructions */
#define TRF6151_RX_TPU_INSTR	4	/* set_gain(1), set_arfcn(2), set_mode(1) */

/* delay caused by this driver programming the TPU for RX mode */
#define TRF6151_RX_TPU_DELAY	(TRF6151_RX_TPU_INSTR * TRF6151_REGWR_QBITS)

/* prepare a Rx window with the TRF6151 finished at time 'start' (in qbits) */
void trf6151_rx_window(int16_t start_qbits, uint16_t arfcn)
{
	int16_t start_pll_qbits;

	/* power up at the right time _before_ the 'start_qbits' point in time */
	start_pll_qbits = add_mod5000(start_qbits,  -(TRF6151_RX_PLL_DELAY + TRF6151_RX_TPU_DELAY));
	tpu_enq_at(start_pll_qbits);

	/* Set the AGC and PLL registers */
	trf6151_set_arfcn(arfcn, 0);
	trf6151_set_gain(trf6151_vga_dbm, trf6151_gain_high);
	trf6151_set_mode(TRF6151_RX);

	/* FIXME: power down at the right time again */
}

/* prepare a Tx window with the TRF6151 finished at time 'start' (in qbits) */
void trf6151_tx_window(int16_t start_qbits, uint16_t arfcn)
{
#ifdef CONFIG_TX_ENABLE
	int16_t start_pll_qbits;

	/* power up at the right time _before_ the 'start_qbits' point in time */
	start_pll_qbits = add_mod5000(start_qbits,  -(TRF6151_TX_PLL_DELAY + TRF6151_RX_TPU_DELAY));
	tpu_enq_at(start_pll_qbits);

	trf6151_set_arfcn(arfcn, 1);
	trf6151_set_mode(TRF6151_TX);

	/* FIXME: power down at the right time again */
#endif
}

/* Given the expected input level of exp_inp dBm and the target of target_bb
 * dBm, configure the RF Frontend with the respective gain */
void trf6151_compute_gain(int16_t exp_inp, int16_t target_bb)
{
	/* TRF6151 VGA gain between 14 to 40 dB, plus 20db high/low */
	int16_t exp_bb_dbm8, delta_dbm8;
	int16_t exp_inp_dbm8 = to_dbm8(exp_inp);
	int16_t target_bb_dbm8 = to_dbm8(target_bb);
	int16_t vga_gain = TRF6151_VGA_GAIN_MIN;
	int high = 0;

	/* calculate the dBm8 that we expect at the baseband */
	exp_bb_dbm8 = exp_inp_dbm8 + to_dbm8(system_inherent_gain);

	/* calculate the error that we expect. */
	delta_dbm8 = target_bb_dbm8 - exp_bb_dbm8;

	/* If this is negative or less than TRF6151_GAIN_MIN, we are pretty
	 * much lost as we cannot reduce the system inherent gain.  If it is
	 * positive, it corresponds to the gain that we need to configure */
	if (delta_dbm8 < to_dbm8(TRF6151_FE_GAIN_LOW + TRF6151_VGA_GAIN_MIN)) {
		printd("AGC Input level overflow\n");
		high = 0;
		vga_gain = TRF6151_VGA_GAIN_MIN;
	} else if (delta_dbm8 > to_dbm8(TRF6151_FE_GAIN_HIGH +
					TRF6151_VGA_GAIN_MIN)) {
		high = 1;
		delta_dbm8 -= to_dbm8(TRF6151_FE_GAIN_HIGH);
	}
	vga_gain = delta_dbm8/8;
	if (vga_gain > TRF6151_VGA_GAIN_MAX)
		vga_gain = TRF6151_VGA_GAIN_MAX;

	/* update the static global variables which are used when programming
	 * the window */
	trf6151_vga_dbm = vga_gain;
	trf6151_gain_high = high;
}
