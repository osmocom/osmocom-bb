/* TRX implementation of Free Software for Calypso Phone */

/*
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

#include <inttypes.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include <defines.h>
#include <asm/system.h>
#include <layer1/trx.h>
#include <layer1/sync.h>

#include "burst_queue.h"


/* Burst queue */
BURST_QUEUE_STATIC(g_bq, 8, 512, static)

/* Filler table */
static struct burst_data fill[8][52];
static uint8_t fill_size[8];


/* TRX Helpers **************************************************************/

static void
trx_init_filler(uint8_t tn, uint8_t type)
{
	int i;

	if ((type >> 1) == 2) {
		for (i=0; i<51; i++) {
			if ((i % 51) == 50)
				fill[tn][i].type = BURST_DUMMY;
			else if (((i % 51) % 10) == 0)
				fill[tn][i].type = BURST_FB;
			else if (((i % 51) % 10) == 1)
				fill[tn][i].type = BURST_SB;
			else
				fill[tn][i].type = BURST_DUMMY;
		}
		fill_size[tn] = 51;
	} else if ((type >> 1) == 3) {
		for (i=0; i<51; i++)
			fill[tn][i].type = BURST_DUMMY;
		fill_size[tn] = 51;
	} else {
		for (i=0; i<52; i++)
			fill[tn][i].type = BURST_DUMMY;
		fill_size[tn] = 52;
	}
}

static void
trx_discarded_burst(struct burst_data *burst,
                    int head, uint32_t fn, uint8_t tn, __unused void *data)
{
	/* Only TN=0 */
	if (head)
		return;

	/* Debug */
	printf("STALE BURST %" PRIu32 "\n", fn);

	/* Still copy to the filler table */
	memcpy(&fill[tn][fn % fill_size[tn]], burst, sizeof(struct burst_data));
}


/* TRX Interface ************************************************************/

void
trx_init(void)
{
	int i;

	/* Init burst queue */
	bq_reset(&g_bq);
	bq_set_discard_fn(&g_bq, trx_discarded_burst, NULL);

	/* Init filler table */
	for (i = 0; i < 8; i++)
		trx_init_filler(i, l1s.bts.type[i]);
}

int
trx_put_burst(uint32_t fn, uint8_t tn, uint8_t type, uint8_t *data)
{
	struct burst_data *burst;
	unsigned long flags;

	local_firq_save(flags);

	burst = bq_push(&g_bq, tn, fn);
	if (!burst)
		goto exit;

	burst->type = type;
	if (burst->type == BURST_NB)
		memcpy(burst->data, data, 15);

exit:
	local_irq_restore(flags);

	return 0;
}

int
trx_get_burst(uint32_t fn, uint8_t tn, uint8_t *data)
{
	struct burst_data *burst;
	int rc;

	/* Check for new burst */
	burst = bq_pop_head(&g_bq, tn, fn);

	if (burst) {
		/* New burst: Copy to fill table & use it */
		memcpy(&fill[tn][fn % fill_size[tn]], burst,
			sizeof(struct burst_data));

		// printf("O %d %d %p\n", fn, g_bq.used, burst);
	} else {
		/* No data, just use the one from fill table */
		burst = &fill[tn][fn % fill_size[tn]];
	}

	rc = burst->type;
	if (burst->type == BURST_NB)
		memcpy(data, burst->data, 15);

	return rc;
}
