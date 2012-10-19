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
#include <osmocom/gsm/gsm_utils.h>

#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <layer1/agc.h>
#include <rffe.h>

#include <rf/trf6151.h>

/* #define WARN_OUT_OF_SPEC 1 */

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


uint16_t rf_arfcn = 871;	/* TODO: this needs to be private */
static uint16_t rf_band;

static uint8_t trf6151_tsp_uid;
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
	tsp_write(trf6151_tsp_uid, 16, (reg | val));
	trf6151_reg_cache[reg] = val;
}

/* Frontend gain can be switched high or low (dB) */
#define TRF6151_FE_GAIN_LOW	7
#define TRF6151_FE_GAIN_HIGH	27

/* VGA at baseband can be adjusted in this range (dB) */
#define TRF6151_VGA_GAIN_MIN	14
#define TRF6151_VGA_GAIN_MAX	40

/* put current set (or computed) gain to register */
int trf6151_set_gain_reg(uint8_t dbm, int high)
{
	uint16_t reg = trf6151_reg_cache[REG_RX] & 0x07ff;
	printd("trf6151_set_gain_reg(%u, %d)\n", dbm, high);

	if (dbm < TRF6151_VGA_GAIN_MIN || dbm > TRF6151_VGA_GAIN_MAX)
		return -1;

	/* clear the gain bits first */
	reg &= ~((0x1F) << RX_VGA_GAIN_SHIFT);
	/* OR-in the new gain value */
	reg |= (6 + (dbm-TRF6151_VGA_GAIN_MIN)/2) << RX_VGA_GAIN_SHIFT;

	if (high)
		reg |= RX_RF_GAIN_HIGH;
	else
		reg &= ~RX_RF_GAIN_HIGH;

	trf6151_reg_write(REG_RX, reg);

	return 0;
}

int trf6151_set_gain(uint8_t dbm)
{
	int high = 0;

	printd("trf6151_set_gain(%u, %d)\n", dbm);
	/* If this is negative or less than TRF6151_GAIN_MIN, we are pretty
	 * much lost as we cannot reduce the system inherent gain.  If it is
	 * positive, it corresponds to the gain that we need to configure */
	if (dbm < TRF6151_FE_GAIN_LOW + TRF6151_VGA_GAIN_MIN) {
		printd("AGC Input level overflow\n");
		trf6151_vga_dbm = TRF6151_VGA_GAIN_MIN;
		trf6151_gain_high = 0;
		return 0;
	} else if (dbm >= TRF6151_FE_GAIN_HIGH + TRF6151_VGA_GAIN_MIN) {
		high = 1;
		dbm -= TRF6151_FE_GAIN_HIGH;
	} else
		dbm -= TRF6151_FE_GAIN_LOW;
	if (dbm > TRF6151_VGA_GAIN_MAX)
		dbm = TRF6151_VGA_GAIN_MAX;

	/* update the static global variables which are used when programming
	 * the window */
	trf6151_vga_dbm = dbm;
	trf6151_gain_high = high;

	return 0;
}

#define SCALE_100KHZ	100

/* Compute TRF6151 PLL valuese */
static void trf6151_pll_rx(uint32_t freq_khz,
                           uint16_t *pll_config, enum trf6151_gsm_band *band)
{
	const uint32_t p=64, r=65;
	uint32_t freq_100khz, vco_freq_100khz;
	uint32_t l, n;
	uint32_t a, b;

	/* Scale into 100kHz unit (avoid overflow in intermediates) */
	freq_100khz = freq_khz / SCALE_100KHZ;

	/* L selects hi/lo band */
	l = (freq_khz > 1350000) ? 2 : 4; /* cut at mid point :) */

	/* VCO frequency */
	vco_freq_100khz = freq_100khz * l;

	/* vco_freq = 26MHz / R * N  with R=65 and N=B*P+A */
	n = (vco_freq_100khz * r) / 260;
	a = n % p;
	b = n / p;

	*pll_config = PLL_VAL(a, b);

	/* Out-of-spec tuning warning */
#ifdef WARN_OUT_OF_SPEC
	if ((l == 4 && (b < 135 || b > 150)) ||
	    (l == 2 && (b < 141 || b > 155)))
		printf("Frequency %u kHz is out of spec\n", (unsigned int)freq_khz);
#endif

	/* Select band */
	if (l==4) {
		/* If in the low band, same port for both GSM850/GSM900, so we
		 * choose the best VCO (VCOMAIN1=3.37GHz, VCOMAIN2=3.8GHz) */
		if (vco_freq_100khz < 35850) /* midpoint */
			*band = GSM850_LOW;
		else
			*band = GSM900;

		/* Out-of-spec freq check */
#ifdef WARN_OUT_OF_SPEC
		if (!(freq_khz >= 869000 && freq_khz <= 894000) &&
		    !(freq_khz >= 921000 && freq_khz <= 960000)) /* include GSM-R */
			printf("Frequency %u outside normal filter range for selected port\n", (unsigned int)freq_khz);
#endif
	} else {
		/* In the high band, different ports for DCS/PCS, so
		 * take what's best and available */
		/* We're stuck to VCOMAIN2=3.8GHz though ... */
		uint32_t rx_ports = rffe_get_rx_ports();
		uint32_t port;

		/* Select port */
		port = (freq_khz < 1905000) ? (1 << PORT_DCS1800) : (1 << PORT_PCS1900);
		port = (port & rx_ports) ? port : rx_ports;

		/* Select band */
		*band = (port & (1 << PORT_DCS1800)) ? GSM1800 : GSM1900;

		/* Out-of-spec freq check */
#ifdef WARN_OUT_OF_SPEC
		if ((*band == GSM1800 && (freq_khz < 1805000 || freq_khz > 1880000)) ||
		    (*band == GSM1900 && (freq_khz < 1930000 || freq_khz > 1990000)))
			printf("Frequency %u outside normal filter range for selected port\n", (unsigned int)freq_khz);
#endif
	}

	/* Debug */
	printd("RX Freq %u kHz => A = %u, B = %u, band = %d, vco_freq = %u kHz\n", freq_khz, a, b, *band, vco_freq_100khz*100);

	/* All done */
	return;
}

/* Compute TRF6151 PLL TX values */
static void trf6151_pll_tx(uint32_t freq_khz,
                           uint16_t *pll_config, enum trf6151_gsm_band *band)
{
	const uint32_t p=64;
	uint32_t r, l, m, m_op_l; /* m_op_l = m +/- l depending on mode */
	uint32_t freq_100khz;
	uint32_t n, a, b, b_min, b_max;

	/* Scale into 100kHz unit (avoid overflow in intermediates) */
	freq_100khz = freq_khz / SCALE_100KHZ;

	/* Select band (and PLL mode) */
	if (freq_khz > 1350000) {
		/* High band, so only 1 real PLL mode. band doesn't matter
		 * that much (or at all) but we still do it :p */
		*band = (freq_khz < 1817500) ? GSM1800 : GSM1900;
		r = 70;
		l = 2;
		m = 26;
		m_op_l = m + l;
		b_min = 133;
		b_max = 149;
	} else {
		/* Low band. We have 3 possible PLL modes that output on
		 * the right port: GSM900, GSM850_HIGH, GSM850_LOW.
		 *
		 * The transistion points have been chosen looking at the VCO
		 * and IF frequencies for various frequencies for theses modes
		 */
		if (freq_khz < 837100) {
			/* GSM850_LOW */
			*band = GSM850_LOW;
			r = 55;
			l = 4;
			m = 26;
			m_op_l = m - l;
			b_min = 128;
			b_max = 130;
		} else if (freq_khz < 850000) {
			/* GSM850_HIGH */
			*band = GSM850_HIGH;
			r = 30;
			l = 4;
			m = 52;
			m_op_l = m - l;
			b_min = 65;
			b_max = 66;
		} else {
			/* GSM900 */
			*band = GSM900;
			r = 35;
			l = 4;
			m = 52;
			m_op_l = m + l;
			b_min = 68;
			b_max = 71;
		}
	}

	/* vco_freq = f * M * L / (M +- L)                 */
	/*          = 26MHz / R * N  with R=65 and N=B*P+A */
	n = (freq_100khz * m * l * r) / (m_op_l * 260);
	a = n % p;
	b = n / p;

	*pll_config = PLL_VAL(a, b);

	/* Debug */
	printd("TX Freq %u kHz => A = %u, B = %u, band = %d\n", freq_khz, a, b, *band);

	/* Out-of-spec tuning warning */
#ifdef WARN_OUT_OF_SPEC
	if (b < b_min || b > b_max)
		printf("Frequency %u kHz is out of spec\n", (unsigned int)freq_khz);
#endif

	/* All done */
	return;
}

static inline void trf6151_reset(uint16_t reset_id)
{
	/* pull the nRESET line low */
	tsp_act_disable(reset_id);
	tpu_enq_wait(50);
	/* release nRESET */
	tsp_act_enable(reset_id);
}

void trf6151_init(uint8_t tsp_uid, uint16_t tsp_reset_id)
{
	trf6151_tsp_uid = tsp_uid;

	/* Configure the TSPEN which is connected to TRF6151 STROBE */
	tsp_setup(trf6151_tsp_uid, 0, 1, 1);

	trf6151_reset(tsp_reset_id);

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
void trf6151_set_arfcn(uint16_t arfcn, int tx)
{
	uint32_t freq_khz;
	uint16_t pll_config;
	int uplink;
	enum trf6151_gsm_band pll_band;

	uplink = !!(arfcn & ARFCN_UPLINK);
	arfcn != ~ARFCN_UPLINK;

	switch (gsm_arfcn2band(arfcn)) {
	case GSM_BAND_850:
	case GSM_BAND_900:
	case GSM_BAND_1800:
	case GSM_BAND_1900:
		/* Supported */
		break;
	case GSM_BAND_450:
	case GSM_BAND_480:
	case GSM_BAND_750:
	case GSM_BAND_810:
		printf("Unsupported band ! YMMV.\n");
		break;
	}

	freq_khz = gsm_arfcn2freq10(arfcn, uplink) * 100;
	printd("ARFCN %u -> %u kHz\n", arfcn, freq_khz);

	if (!tx)
		trf6151_pll_rx(freq_khz, &pll_config, &pll_band);
	else
		trf6151_pll_tx(freq_khz, &pll_config, &pll_band);

	trf6151_band_select(pll_band);
	trf6151_reg_write(REG_PLL, pll_config);

	rf_band = pll_band;
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

uint8_t trf6151_get_gain_reg(void)
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

uint8_t trf6151_get_gain(void)
{
	uint8_t gain;
	
	gain = trf6151_vga_dbm;
	if (trf6151_gain_high)
		gain += TRF6151_FE_GAIN_HIGH;
	else
		gain += TRF6151_FE_GAIN_LOW;

	return gain;
}

void trf6151_test(uint16_t arfcn)
{
	/* Select ARFCN downlink */
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
	trf6151_set_arfcn(arfcn | ARFCN_UPLINK, 1);

	trf6151_set_mode(TRF6151_TX);
	tpu_enq_wait(TRF6151_RX_PLL_DELAY);

	tpu_enq_sleep();
	tpu_enable(1);
	tpu_wait_idle();
}

#define TRF6151_REGWR_QBITS	8	/* 4 GSM qbits + 4 TPU instructions */
#define TRF6151_RX_TPU_INSTR	4	/* set_gain_reg(1), set_arfcn(2), set_mode(1) */

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
	trf6151_set_gain_reg(trf6151_vga_dbm, trf6151_gain_high);
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
	int16_t exp_bb, delta;

	/* calculate the dBm8 that we expect at the baseband */
	exp_bb = exp_inp + system_inherent_gain;

	/* calculate the error that we expect. */
	delta = target_bb - exp_bb;

	printd("computed gain %d\n", delta);
	trf6151_set_gain(delta);
}

int trf6151_iq_swapped(uint16_t band_arfcn, int tx)
{
	if (!tx)
		return 0;

	switch (gsm_arfcn2band(band_arfcn)) {
		case GSM_BAND_850:
			return 1;
		default:
			break;
	}

	return 0;
}
