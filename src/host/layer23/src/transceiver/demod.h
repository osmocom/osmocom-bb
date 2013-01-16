/*
 * demod.h
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

#ifndef __TRX_DEMOD_H__
#define __TRX_DEMOD_H__


#include <osmocom/core/bits.h>

struct app_state;
struct l1ctl_bts_burst_ab_ind;


int gsm_ab_ind_process(struct app_state *as,
	struct l1ctl_bts_burst_ab_ind *bi, sbit_t *data, float *toa_p);


#endif /* __TRX_DEMOD_H__ */
