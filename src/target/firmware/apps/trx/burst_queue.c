/* Burst queue */

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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "burst_queue.h"


/* ------------------------------------------------------------------------ */
/* FN handling helpers                                                      */
/* ------------------------------------------------------------------------ */

#define FN_MAX	((2048*26*51)-1)

static inline int fn_cmp(uint32_t fn1, uint32_t fn2)
{
	if (fn1 == fn2)
		return 0;

	if ((fn2 > (FN_MAX >> 1)) && (fn1 < (FN_MAX >> 1)))
		return 1;

	return fn1 < fn2 ? -1 : 1;
}


/* ------------------------------------------------------------------------ */
/* Internal slot management                                                 */
/* ------------------------------------------------------------------------ */

static uint16_t
bq_alloc_slot(struct burst_queue *bq)
{
	int i, j, n;
	uint32_t fm;
	uint16_t s;

	/* Find first zone with free blocks */
	n = (bq->capacity + 31) >> 5;
	for (i=0; i<n; i++) {
		if (bq->freemap[i] != 0xffffffff)
			break;
	}

	/* None ? */
	if (i == n)
		return 0xffff;

	/* Find slot in that zone */
	fm = bq->freemap[i];

	for (j=0; j<32; j++)
		if (!(fm & (1 << j)))
			break;

	/* Final index */
	s = (i << 5) | j;

	if (s >= bq->capacity)
		return 0xffff;

	/* Allocate it */
	bq->freemap[i] |= (1 << j);
	bq->used++;

	return s;
}

static void
bq_free_slot(struct burst_queue *bq, uint16_t slot)
{
	bq->freemap[(slot >> 5)] &= ~(1 << (slot & 0x1f));
	bq->used--;
}


/* ------------------------------------------------------------------------ */
/* Exposed API                                                              */
/* ------------------------------------------------------------------------ */

void
bq_reset(struct burst_queue *bq)
{
	int i;

	for (i=0; i<bq->n_heads; i++)
		bq->head[i] = 0xffff;

	memset(bq->freemap, 0x00, sizeof(uint32_t) * ((bq->capacity+31) >> 5));
	memset(bq->slots, 0x00, sizeof(struct burst_queue_slot) * bq->capacity);
}

void
bq_set_discard_fn(struct burst_queue *bq, bq_discard_fn_t cb, void *cb_data)
{
	bq->cb = cb;
	bq->cb_data = cb_data;
}

struct burst_data *
bq_push(struct burst_queue *bq, int head, uint32_t fn)
{
	uint16_t s;

	/* Get a slot */
	s = bq_alloc_slot(bq);
	if (s == 0xffff)
		return NULL;

	/* Do we need to replace the head ? */
	if ((bq->head[head] == 0xffff) ||
	    (fn_cmp(fn, bq->slots[bq->head[head]].fn) < 0))
	{
		bq->slots[s].fn = fn;
		bq->slots[s].next = bq->head[head];
		bq->head[head] = s;
	}
	else
	{
		/* Insert at the right place */
		uint16_t c, n;

		c = bq->head[head];
		n = bq->slots[c].next;

		while ((n != 0xffff) &&
		       (fn_cmp(fn, bq->slots[n].fn) > 0))
		{
			c = n;
			n = bq->slots[c].next;
		}

		bq->slots[s].fn = fn;
		bq->slots[s].next = n;
		bq->slots[c].next = s;
	}

	/* Done */
	return &bq->slots[s].data;
}

/* Data remains valid until the next bq_push call */
struct burst_data *
bq_pop_head(struct burst_queue *bq, int head, uint32_t fn)
{
	uint16_t h;

	/* Flush any pending burst < fn */
	while ((bq->head[head] != 0xffff) &&
	       (fn_cmp(bq->slots[bq->head[head]].fn, fn) < 0))
	{
		h = bq->head[head];

		if (bq->cb) {
			bq->cb(&bq->slots[h].data,
			       head, bq->slots[h].fn, bq->cb_data);
		}

		bq->head[head] = bq->slots[h].next;
		bq_free_slot(bq, h);
	}

	/* Are we a match ? */
	h = bq->head[head];

	if (h == 0xffff || bq->slots[h].fn != fn)
		return NULL;

	/* Remove it */
	bq->head[head] = bq->slots[h].next;
	bq_free_slot(bq, h);

	/* Return matching result */
	return &bq->slots[h].data;
}
