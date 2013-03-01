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


/* TRX Helpers **************************************************************/

static void
trx_discarded_burst(__unused struct burst_data *burst,
                    __unused int head, uint32_t fn, __unused void *data)
{
	/* Debug */
	printf("STALE BURST %" PRIu32 "\n", fn);
}


/* TRX Interface ************************************************************/

void
trx_init(void)
{
	/* Init burst queue */
	bq_reset(&g_bq);
	bq_set_discard_fn(&g_bq, trx_discarded_burst, NULL);
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
		rc = burst->type;
		if (burst->type == BURST_NB)
			memcpy(data, burst->data, 15);

		return rc;
	}

	/* no burst, use dummy on non BCCH */
	if ((l1s.bts.type[tn] >> 1) != 2)
		return BURST_DUMMY;

	/* no burst, use dummy,FCCH,SCH on BCCH */
	fn = fn % 51;
	if (fn == 50)
		return BURST_DUMMY;
	fn = fn % 10;
	if (fn == 0)
		return BURST_FB;
	if (fn == 1)
		return BURST_SB;
	return BURST_DUMMY;
}
