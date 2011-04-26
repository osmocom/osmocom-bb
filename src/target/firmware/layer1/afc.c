/* AFC (Automatic Frequency Correction) Implementation */

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
#include <osmocom/gsm/gsm_utils.h>

#include <layer1/afc.h>
#include <layer1/avg.h>
#include <calypso/dsp.h>

#define AFC_INITIAL_DAC_VALUE	-700

/* Over how many TDMA frames do we want to average? (this may change in dedicated mode) */
#define AFC_PERIOD		40
/* How many of our measurements have to be valid? */
#define AFC_MIN_MUN_VALID	8

/* The actual AFC code */

struct afc_state {
	struct running_avg ravg;		/* running average */
	int16_t		dac_value;		/* current DAC output value */
	uint16_t	arfcn;
};

static void afc_ravg_output(struct running_avg *ravg, int32_t avg);

static struct afc_state afc_state = {
	.ravg = {
		.outfn = &afc_ravg_output,
		.period = AFC_PERIOD,
		.min_valid = AFC_MIN_MUN_VALID,
	},
	.dac_value = AFC_INITIAL_DAC_VALUE,
};

/* The AFC DAC in the ABB has to be configured as follows:
 * DAC = 1MHz / 947MHz * FreqErr(Hz) / AFCslop(ppm/LSB)
 * where:
 *	947 MHz is the center of EGSM
 *	AFCslope is coded F1.15, thus a normalization factor of 2^15 applies
 */

#define AFC_NORM_FACTOR_GSM	((1<<15) / 947)
#define AFC_NORM_FACTOR_DCS	((1<<15) / 1894)

/* we assume 8.769ppb per LSB, equals 0.008769 * 32768 == 287 */
//#define AFC_SLOPE		320
#define AFC_SLOPE		287

/* The DSP can measure the frequency error in the following ranges:
 * 	FB_MODE0:	+/- 20 kHz
 *	FB_MODE1:	+/-  4 kHz
 *	Sync Burst:	+/-  1 kHz
 *	Normal Burst:	+/- 400 Hz
 */

/* Update the AFC with a frequency error, bypassing averaging */
void afc_correct(int16_t freq_error, uint16_t arfcn)
{
	int32_t afc_norm_factor;
	int16_t delta;

	switch (gsm_arfcn2band(arfcn)) {
	case GSM_BAND_900:
	case GSM_BAND_850:
		afc_norm_factor = AFC_NORM_FACTOR_GSM;
		break;
	default:
		afc_norm_factor = AFC_NORM_FACTOR_DCS;
	}

	delta = (int16_t) ((afc_norm_factor * (int32_t)freq_error) / AFC_SLOPE);
	printd("afc_correct(error=%dHz, arfcn=%u): delta=%d, afc_dac(old=%d,new=%d)\n",
		freq_error, arfcn, delta, afc_state.dac_value, afc_state.dac_value+delta);
	afc_state.dac_value += delta;

	/* The AFC DAC has only 13 bits */
	if (afc_state.dac_value > 4095)
		afc_state.dac_value = 4095;
	else if (afc_state.dac_value < -4096)
		afc_state.dac_value = -4096;
}

void afc_reset(void)
{
	afc_state.dac_value = AFC_INITIAL_DAC_VALUE;
}

void afc_input(int32_t freq_error, uint16_t arfcn, int valid)
{
	afc_state.arfcn = arfcn;
	runavg_input(&afc_state.ravg, freq_error, valid);
	runavg_check_output(&afc_state.ravg);
}

/* callback function for runavg */
static void afc_ravg_output(struct running_avg *ravg, int32_t avg)
{
	afc_correct(avg, afc_state.arfcn);
}

/* Update DSP with new AFC DAC value to be used for next TDMA frame */
void afc_load_dsp(void)
{
	dsp_api.db_w->d_afc = afc_state.dac_value;
	dsp_api.db_w->d_ctrl_abb |= (1 << B_AFC);
}
