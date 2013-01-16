/*
 * demod.c
 *
 * Demodulation routines for tranceiver
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <osmocom/dsp/cxvec.h>
#include <osmocom/dsp/cxvec_math.h>

#include <l1ctl_proto.h>

#include "app.h"
#include "gmsk.h"
#include "gsm_ab.h"

int
gsm_ab_ind_process(struct app_state *as,
                   struct l1ctl_bts_burst_ab_ind *bi,
                   sbit_t *data, float *toa_p)
{
	struct osmo_cxvec *burst = NULL;
	sbit_t *bits = NULL;
	float complex chan;
	float toa;
	int i, rv;

	/* Load burst */
	burst = osmo_cxvec_alloc(88);
	burst->len = 88;

	for (i=0; i<88; i++) {
		float f[2];
		f[0] = ((float)((char)bi->iq[(i<<1)  ])) / 128.0f;
		f[1] = ((float)((char)bi->iq[(i<<1)+1])) / 128.0f;
		burst->data[i] = f[0] + f[1] * I;
	}

	/* Normalize burst */
	osmo_cxvec_sig_normalize(burst, 1, 0.0f, burst);

	/* Detect potential RACH */
	rv = gsm_ab_detect(as->gs, as->train_ab, burst, 2.0f, &chan, &toa);
	if (rv)
		goto err;

	printf("TOA  : %f\n", toa);
	printf("chan : (%f %f) => %f\n", crealf(chan), cimagf(chan), cabsf(chan));

	/* Demodulate */
	bits = gsm_ab_demodulate(as->gs, burst, chan, toa);
	if (!bits)
		goto err;

	/* Copy */
	memset(data, 0x00, 148);
	memcpy(data, bits, GSM_AB_BITS);

	/* Print result */
	for (i=0; i<GSM_AB_BITS; i++)
		printf("%d", !!(bits[i] & 0x80));
	printf("\n");

	*toa_p = toa;

	return 0;

err:
	return -1;
}
