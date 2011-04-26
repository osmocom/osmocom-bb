/* AFC (Automatic Frequency Correction) Implementation */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <layer1/toa.h>
#include <layer1/avg.h>
#include <layer1/sync.h>

/* Over how many TDMA frames do we want to average? */
#define TOA_PERIOD		250
/* How many of our measurements have to be valid? */
#define TOA_MIN_MUN_VALID	125

// FIXME:
#define TOA_SNR_THRESHOLD       2560    /* 2.5 dB in fx6.10 */

struct toa_state {
	struct running_avg ravg;		/* running average */
};


static void toa_ravg_output(struct running_avg *ravg, int32_t avg);

static struct toa_state toa_state = {
	.ravg = {
		.outfn = &toa_ravg_output,
		.period = TOA_PERIOD,
		.min_valid = TOA_MIN_MUN_VALID,
	},
};

void toa_input(int32_t offset, uint32_t snr)
{
	int valid = 1;

	if (snr < TOA_SNR_THRESHOLD || offset < 0 || offset >31)
		valid = 0;
	runavg_input(&toa_state.ravg, offset, valid);
	runavg_check_output(&toa_state.ravg);
}

void toa_reset(void)
{
	toa_state.ravg.num_samples = toa_state.ravg.num_samples_valid = 0;
	toa_state.ravg.acc_val = 0;
}

/* callback function for runavg */
static void toa_ravg_output(struct running_avg *ravg, int32_t avg)
{
	if (avg != 16) {
		printf("TOA AVG is not 16 qbits, correcting (got %ld)\n", avg);
		l1s.tpu_offset_correction = avg - 16;
	}
}
