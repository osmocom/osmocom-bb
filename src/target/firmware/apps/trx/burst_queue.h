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

#ifndef __BURST_QUEUE_H__
#define __BURST_QUEUE_H__


#include <stdint.h>


#define BURST_FB	0
#define BURST_SB	1
#define BURST_DUMMY	2
#define BURST_NB	3

struct burst_data
{
	uint8_t type;		/* BURST_??? */
	uint8_t data[15];	/* Only for NB */
} __attribute__((packed));

struct burst_queue_slot
{
	uint32_t fn;
	uint16_t next;		/* Index in underlying storage */
	struct burst_data data;
} __attribute__((packed));

struct burst_queue;

typedef void (*bq_discard_fn_t)(
	struct burst_data *burst, int head, uint32_t fn,
	void *data
);

struct burst_queue
{
	int n_heads;
	int capacity;
	int used;

	bq_discard_fn_t cb;
	void *cb_data;

	uint16_t *head;
	uint32_t *freemap;
	struct burst_queue_slot *slots;
};

#define BURST_QUEUE_STATIC(name, _n_heads, _capacity, qual)		\
	static uint16_t name ## _head[(_capacity)] = {			\
		[ 0 ... ((_capacity)-1) ] = 0xffff			\
	};								\
	static uint32_t name ## _freemap[((_capacity)+31) >> 5];	\
	static struct burst_queue_slot name ## _slots[(_capacity)];	\
	qual struct burst_queue name = {				\
		.n_heads  = (_n_heads),					\
		.capacity = (_capacity),				\
		.used     = 0,						\
		.head     = name ## _head,				\
		.freemap  = name ## _freemap,				\
		.slots    = name ## _slots,				\
	};

void bq_reset(struct burst_queue *bq);
void bq_set_discard_fn(struct burst_queue *bq, bq_discard_fn_t cb, void *cb_data);

struct burst_data *bq_push(struct burst_queue *bq, int head, uint32_t fn);
struct burst_data *bq_pop_head(struct burst_queue *bq, int head, uint32_t fn);

static inline int bq_is_head_empty(struct burst_queue *bq, int head)
{
	return bq->head[head] == 0xffff;
}

static inline uint32_t bq_get_head_fn(struct burst_queue *bq, int head)
{
	if (bq->head[head] != 0xffff)
		return bq->slots[bq->head[head]].fn;
	else
		return (uint32_t)-1;
}


#endif /* __BURST_QUEUE_H__ */
