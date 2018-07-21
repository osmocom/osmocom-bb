/* Utilities used by mobile */

/* (C) 2018 by Holger Hans Peter Freyther
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

#include <osmocom/bb/common/utils.h>

#include <osmocom/gsm/gsm_utils.h>

#include <stdlib.h>
#include <stdint.h>


/**
 * A secure replacement for random(3).
 *
 * \return a secure random number using osmo_get_rand_id between
 * 0 and RAND_MAX.
 */
int layer23_random(void)
{
	unsigned int r;

	if (osmo_get_rand_id((uint8_t *) &r, sizeof(r)) != 0)
		return random();

	r &= ~(1U << 31);
	r %= RAND_MAX;
	return (int) r;
}
