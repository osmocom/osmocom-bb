/*
 * gsm_ab.h
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

#ifndef __OSMO_GSM_AB_H__
#define __OSMO_GSM_AB_H__


#include <complex.h>

#include <osmocom/core/bits.h>

struct osmo_gmsk_state;
struct osmo_gmsk_trainseq;
struct osmo_cxvec;


#define GSM_AB_TRAIN_LEN        41
#define GSM_AB_BITS             88      /* 8 + 41 + 36 + 3 */

extern const ubit_t gsm_ab_train[];


int
gsm_ab_detect(const struct osmo_gmsk_state *gs,
              const struct osmo_gmsk_trainseq *rach,
              const struct osmo_cxvec *burst, float thresh,
              float complex *peak, float *toa);

sbit_t *
gsm_ab_demodulate(struct osmo_gmsk_state *gs,
                  struct osmo_cxvec *burst,
                  float complex channel, float toa);


#endif /* __OSMO_GSM_AB_H__ */
