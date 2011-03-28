/* Rx Level statistics */

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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include <osmocom/core/bitvec.h>
#include <osmocom/gsm/rxlev_stat.h>

void rxlev_stat_input(struct rxlev_stats *st, uint16_t arfcn, uint8_t rxlev)
{
	struct bitvec bv;

	if (rxlev >= NUM_RXLEVS)
		rxlev = NUM_RXLEVS-1;

	bv.data_len = NUM_ARFCNS/8;
	bv.data = st->rxlev_buckets[rxlev];

	bitvec_set_bit_pos(&bv, arfcn, ONE);
}

/* get the next ARFCN that has the specified Rxlev */
int16_t rxlev_stat_get_next(const struct rxlev_stats *st, uint8_t rxlev, int16_t arfcn)
{
	struct bitvec bv;

	if (rxlev >= NUM_RXLEVS)
		rxlev = NUM_RXLEVS-1;

	bv.data_len = NUM_ARFCNS/8;

	if (arfcn < 0)
		arfcn = -1;

	bv.data = (uint8_t *) st->rxlev_buckets[rxlev];

	return bitvec_find_bit_pos(&bv, arfcn+1, ONE);
}

void rxlev_stat_reset(struct rxlev_stats *st)
{
	memset(st, 0, sizeof(*st));
}

void rxlev_stat_dump(const struct rxlev_stats *st)
{
	int i;

	for (i = NUM_RXLEVS-1; i >= 0; i--) {
		int16_t arfcn = -1;

		printf("ARFCN with RxLev %u: ", i);
		while ((arfcn = rxlev_stat_get_next(st, i, arfcn)) >= 0) {
			printf("%u ", arfcn);
		}
		printf("\n");
	}
}
