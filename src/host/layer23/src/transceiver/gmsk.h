/*
 * gmsk.h
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

#ifndef __OSMO_GMSK_H__
#define __OSMO_GMSK_H__


#include <complex.h>

#include <osmocom/core/bits.h>

struct osmo_cxvec;


/* GMSK precomputed state for a given sps */
struct osmo_gmsk_state
{
	int sps;
	struct osmo_cxvec *pulse;
	struct osmo_cxvec *rot_fwd;
	struct osmo_cxvec *rot_rev;
};


/* State init & cleanup */

struct osmo_gmsk_state *
osmo_gmsk_init(int sps);

void
osmo_gmsk_free(struct osmo_gmsk_state *gs);


/* Modulation */

void
osmo_gmsk_rotate_fwd(const struct osmo_gmsk_state *gs, struct osmo_cxvec *v);

void
osmo_gmsk_rotate_rev(const struct osmo_gmsk_state *gs, struct osmo_cxvec *v);

struct osmo_cxvec *
osmo_gmsk_modulate_burst(const struct osmo_gmsk_state *gs,
                         const ubit_t *data, int data_len, int guard_len);


/* Training sequences */

struct osmo_gmsk_trainseq {
	struct osmo_cxvec *seq;
	float complex gain;
	float toa;
};

struct osmo_gmsk_trainseq *
osmo_gmsk_trainseq_generate(struct osmo_gmsk_state *gs, const ubit_t *train, int len);

void
osmo_gmsk_trainseq_free(struct osmo_gmsk_trainseq *gts);


#endif /* __OSMO_GMSK_H__ */
