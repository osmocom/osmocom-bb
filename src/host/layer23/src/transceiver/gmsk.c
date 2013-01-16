/*
 * gmsk.c
 *
 * GMSK modulation support
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
#include <stdlib.h>

#include <osmocom/dsp/cxvec.h>
#include <osmocom/dsp/cxvec_math.h>

#include "gmsk.h"



static struct osmo_cxvec *
osmo_gmsk_generate_pulse(int sym_len, int sps);

static int
osmo_gmsk_generate_rotation_tables(int len, int sps,
                                   struct osmo_cxvec **rot_fwd,
                                   struct osmo_cxvec **rot_rev);


/* ------------------------------------------------------------------------ */
/* GMSK State                                                               */
/* ------------------------------------------------------------------------ */

struct osmo_gmsk_state *
osmo_gmsk_init(int sps)
{
	struct osmo_gmsk_state *gs;

	/* Base */
	gs = calloc(1, sizeof(struct osmo_gmsk_state));
	if (!gs)
		return NULL;

	gs->sps = sps;

	/* GMSK pulse */
	gs->pulse = osmo_gmsk_generate_pulse(2, sps);
	if (!gs->pulse)
		goto error;

	/* Rotation fwd/rev */
	osmo_gmsk_generate_rotation_tables(160, sps, &gs->rot_fwd, &gs->rot_rev);
	if (!gs->rot_fwd || !gs->rot_rev)
		goto error;

	return gs;

error:
	osmo_gmsk_free(gs);
	return NULL;
}

void
osmo_gmsk_free(struct osmo_gmsk_state *gs)
{
	if (!gs)
		return;

	if (gs->pulse)
		osmo_cxvec_free(gs->pulse);
	if (gs->rot_fwd)
		osmo_cxvec_free(gs->rot_fwd);
	if (gs->rot_rev)
		osmo_cxvec_free(gs->rot_rev);

	free(gs);
}


/* ------------------------------------------------------------------------ */
/* GMSK Modulation                                                          */
/* ------------------------------------------------------------------------ */

/* Generate a GMSK pulse shape.
 *  - sym_len: length of ISI
 *  - sps: samples per symbol
 */
static struct osmo_cxvec *
osmo_gmsk_generate_pulse(int sym_len, int sps)
{
	struct osmo_cxvec *pulse;
	int n = sps * sym_len + 1;	/* length */
	int c = (n-1)/2;		/* center idx */
	int i;
	float avg;

	pulse = osmo_cxvec_alloc(n);
	if (!pulse)
		return NULL;

	pulse->len = n;
	pulse->flags |= CXVEC_FLG_REAL_ONLY;

	avg = 0.0f;

	for (i=0; i<n; i++) {
		float arg = (float)(i-c) / (float)sps;
		float arg2 = arg * arg;
		float v;
		v = 0.96 * expf( -1.1380 * arg2 - 0.527 * arg2 * arg2); /* GSM pulse approx */
		avg += v * v;
		pulse->data[i] = v;
	}

	avg = sqrtf(avg);

	for (i=0; i<n; i++)
		pulse->data[i] /= avg;

	return pulse;
}

static int
osmo_gmsk_generate_rotation_tables(
	int len, int sps,
	struct osmo_cxvec **rot_fwd, struct osmo_cxvec **rot_rev)
{
	struct osmo_cxvec *rf, *rr;
	float phase;
	int i;

	rf = osmo_cxvec_alloc(160 * sps);
	rr = osmo_cxvec_alloc(160 * sps);

	if (!rf || !rr)
		return -1;

	phase = 0.0f;

	for (i=0; i<160*sps; i++) {
		rf->data[i] = cexp(  I * phase);
		rr->data[i] = cexp(- I * phase);
		phase += (M_PIf / 2.0f) / (float)sps;
	}

	rf->len = 160 * sps;
	rr->len = 160 * sps;

	*rot_fwd = rf;
	*rot_rev = rr;

	return 0;
}

void
osmo_gmsk_rotate_fwd(const struct osmo_gmsk_state *gs, struct osmo_cxvec *v)
{
	int i;

	if (v->flags & CXVEC_FLG_REAL_ONLY) {
		for (i=0; i<v->len; i++)
			v->data[i] = gs->rot_fwd->data[i] * crealf(v->data[i]);
	} else {
		for (i=0; i<v->len; i++)
			v->data[i] = gs->rot_fwd->data[i] * v->data[i];
	}

	v->flags &= ~CXVEC_FLG_REAL_ONLY;
}

void
osmo_gmsk_rotate_rev(const struct osmo_gmsk_state *gs, struct osmo_cxvec *v)
{
	int i;

	if (v->flags & CXVEC_FLG_REAL_ONLY) {
		for (i=0; i<v->len; i++)
			v->data[i] = gs->rot_rev->data[i] * crealf(v->data[i]);
	} else {
		for (i=0; i<v->len; i++)
			v->data[i] = gs->rot_rev->data[i] * v->data[i];
	}

	v->flags &= ~CXVEC_FLG_REAL_ONLY;
}

struct osmo_cxvec *
osmo_gmsk_modulate_burst(const struct osmo_gmsk_state *gs,
                         const ubit_t *data, int data_len, int guard_len)
{
	struct osmo_cxvec *mod_burst, *shaped_burst;
	int burst_size = gs->sps * (data_len + guard_len);
	int i;

	/* Alloc mod_burst */
	mod_burst = osmo_cxvec_alloc(burst_size);
	if (!mod_burst)
		return NULL;

	/* Fill mod_burst */
	mod_burst->len = burst_size;
	mod_burst->flags |= CXVEC_FLG_REAL_ONLY;

	for (i=0; i<burst_size; i++)
		mod_burst->data[i] = 0.0f;

	for (i=0; i<data_len; i++)
		mod_burst->data[i*gs->sps] = data[i] ? 1.0f : -1.0f;

	/* Rotate up by pi/2 */
	osmo_gmsk_rotate_fwd(gs, mod_burst);

	/* Filter with pulse shape */
	shaped_burst = osmo_cxvec_convolve(gs->pulse, mod_burst, CONV_NO_DELAY, NULL);

	/* Release mod_burst */
	osmo_cxvec_free(mod_burst);

	return shaped_burst;
}


/* ------------------------------------------------------------------------ */
/* GMSK training sequence                                                   */
/* ------------------------------------------------------------------------ */

struct osmo_gmsk_trainseq *
osmo_gmsk_trainseq_generate(struct osmo_gmsk_state *gs, const ubit_t *train, int len)
{
	struct osmo_gmsk_trainseq *gts;
	struct osmo_cxvec *autocorr = NULL;

	gts = calloc(1, sizeof(struct osmo_gmsk_trainseq));
	if (!gts)
		return NULL;

	gts->seq = osmo_gmsk_modulate_burst(gs, train, len, 0);
	if (!gts->seq)
		goto err;

	printf("%d\n", gts->seq->len);

	autocorr = osmo_cxvec_correlate(gts->seq, gts->seq, 1, NULL);
	if (!autocorr)
		goto err;

	gts->toa = osmo_cxvec_peak_energy_find(autocorr, 5, PEAK_EARLY_LATE, &gts->gain);

	printf("%d\n", autocorr->len);
	gts->gain = autocorr->data[0];

	osmo_cxvec_free(autocorr);

	return gts;

err:
	if (gts) {
		if (gts->seq)
			osmo_cxvec_free(gts->seq);
		free(gts);
	}

	if (autocorr)
		osmo_cxvec_free(autocorr);

	return NULL;
}

void
osmo_gmsk_trainseq_free(struct osmo_gmsk_trainseq *gts)
{
	if (gts) {
		osmo_cxvec_free(gts->seq);
		free(gts);
	}
}
