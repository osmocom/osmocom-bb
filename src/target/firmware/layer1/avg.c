/* Averaging Implementation */

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

#include <layer1/avg.h>

/* input a new sample into the averaging process */
void runavg_input(struct running_avg *ravg, int32_t val, int valid)
{
	ravg->num_samples++;
	if (valid) {
		ravg->acc_val += val;
		ravg->num_samples_valid++;
	}
}

/* check if sufficient samples have been obtained, and call outfn() */
int runavg_check_output(struct running_avg *ravg)
{
	if (ravg->num_samples < ravg->period)
		return 0;

	if (ravg->num_samples_valid >= ravg->min_valid) {
		int32_t avg = ravg->acc_val / ravg->num_samples_valid;

		ravg->outfn(ravg, avg);

		ravg->num_samples = ravg->num_samples_valid = 0;
		ravg->acc_val = 0;

		return 1;
	}

	return 0;
}


