/*
 * gsm_ab.c
 *
 * GSM access burst SDR support
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <osmocom/core/bits.h>

#include <osmocom/dsp/cxvec.h>
#include <osmocom/dsp/cxvec_math.h>

#include "gmsk.h"
#include "gsm_ab.h"


const ubit_t gsm_ab_train[] = {
	0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
	1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0,
	0, 0, 1, 1, 1, 1, 0, 0, 0,
};


int
gsm_ab_detect(const struct osmo_gmsk_state *gs,
              const struct osmo_gmsk_trainseq *rach,
              const struct osmo_cxvec *burst, float thresh,
              float complex *peak, float *toa)
{
	struct osmo_cxvec *cb = NULL;
	int found = 0;

	cb = osmo_cxvec_correlate(rach->seq, burst, 1, NULL);
	if (!cb)
		return -ENOMEM;

	*toa = osmo_cxvec_peak_energy_find(cb, 5, PEAK_WEIGH_WIN_CENTER, peak);
	if (*toa < 0.0 || *toa >= cb->len)
		goto done;

	*toa  = *toa - rach->toa - 8*gs->sps;
	*peak = *peak / rach->gain;

	found = 1;

done:
	osmo_cxvec_free(cb);

	if (!found) {
		*toa = 0.0f;
		*peak = 1.0f;
	}

	return !found;
}

sbit_t *
gsm_ab_demodulate(struct osmo_gmsk_state *gs,
                  struct osmo_cxvec *burst, float complex channel, float toa)
{
	sbit_t *rv;
	int i;

	/* Return vector */
	rv = malloc(GSM_AB_BITS * sizeof(sbit_t));
	if (!rv)
		return NULL;

	/* Demod */
	osmo_cxvec_scale(burst, (1.0f / channel), burst);
	osmo_cxvec_delay(burst, -toa, burst);
	osmo_gmsk_rotate_rev(gs, burst);

	/* Mapping / Slicing */
	for (i=0; i<GSM_AB_BITS; i++) {
		int v = (int)(-127.0f * crealf(burst->data[i*gs->sps]));
		if (v > 127)
			v = 127;
		else if (v < -127)
			v = -127;
		rv[i] = v;
	}

	return rv;
}
