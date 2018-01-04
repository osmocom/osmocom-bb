/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: primitive management
 *
 * (C) 2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <errno.h>
#include <string.h>
#include <talloc.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/linuxlist.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>

#include "scheduler.h"
#include "sched_trx.h"
#include "trx_if.h"
#include "logging.h"

/**
 * Initializes a new primitive by allocating memory
 * and filling some meta-information (e.g. lchan type).
 *
 * @param  trx     TRX instance to be used as initial talloc context
 * @param  prim    external prim pointer (will point to the allocated prim)
 * @param  pl_len  prim payload length
 * @param  chan_nr RSL channel description (used to set a proper chan)
 * @param  link_id RSL link description (used to set a proper chan)
 * @return         zero in case of success, otherwise a error number
 */
int sched_prim_init(struct trx_instance *trx,
	struct trx_ts_prim **prim, size_t pl_len,
	uint8_t chan_nr, uint8_t link_id)
{
	enum trx_lchan_type lchan_type;
	struct trx_ts_prim *new_prim;
	uint8_t len;

	/* Determine lchan type */
	lchan_type = sched_trx_chan_nr2lchan_type(chan_nr, link_id);
	if (!lchan_type) {
		LOGP(DSCH, LOGL_ERROR, "Couldn't determine lchan type "
			"for chan_nr=%02x and link_id=%02x\n", chan_nr, link_id);
		return -EINVAL;
	}

	/* How much memory do we need? */
	len  = sizeof(struct trx_ts_prim); /* Primitive header */
	len += pl_len; /* Requested payload size */

	/* Allocate a new primitive */
	new_prim = talloc_zero_size(trx, len);
	if (new_prim == NULL) {
		LOGP(DSCH, LOGL_ERROR, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Init primitive header */
	new_prim->payload_len = pl_len;
	new_prim->chan = lchan_type;

	/* Set external pointer */
	*prim = new_prim;

	return 0;
}

/**
 * Adds a primitive to the end of transmit queue of a particular
 * timeslot, whose index is parsed from chan_nr.
 *
 * @param  trx     TRX instance
 * @param  prim    to be enqueued primitive
 * @param  chan_nr RSL channel description
 * @return         zero in case of success, otherwise a error number
 */
int sched_prim_push(struct trx_instance *trx,
	struct trx_ts_prim *prim, uint8_t chan_nr)
{
	struct trx_ts *ts;
	uint8_t tn;

	/* Determine TS index */
	tn = chan_nr & 0x7;
	if (tn > 7) {
		LOGP(DSCH, LOGL_ERROR, "Incorrect TS index %u\n", tn);
		return -EINVAL;
	}

	/* Check whether required timeslot is allocated and configured */
	ts = trx->ts_list[tn];
	if (ts == NULL || ts->mf_layout == NULL) {
		LOGP(DSCH, LOGL_ERROR, "Timeslot %u isn't configured\n", tn);
		return -EINVAL;
	}

	/**
	 * Change talloc context of primitive
	 * from trx to the parent ts
	 */
	talloc_steal(ts, prim);

	/* Add primitive to TS transmit queue */
	llist_add_tail(&prim->list, &ts->tx_prims);

	return 0;
}

/**
 * Dequeues a TCH or FACCH frame, prioritizing the second.
 * In case if a FACCH frame is found, a TCH frame is being
 * dropped (i.e. replaced).
 *
 * @param  queue a transmit queue to take a prim from
 * @return       a FACCH or TCH primitive, otherwise NULL
 */
static struct trx_ts_prim *sched_prim_dequeue_tch(struct llist_head *queue)
{
	struct trx_ts_prim *facch = NULL;
	struct trx_ts_prim *tch = NULL;
	struct trx_ts_prim *i;

	/* Attempt to find a pair of FACCH and TCH frames */
	llist_for_each_entry(i, queue, list) {
		/* Find one FACCH frame */
		if (!facch && PRIM_IS_FACCH(i))
			facch = i;

		/* Find one TCH frame */
		if (!tch && PRIM_IS_TCH(i))
			tch = i;

		/* If both are found */
		if (facch && tch)
			break;
	}

	/* Prioritize FACCH */
	if (facch && tch) {
		/* We found a pair, dequeue both */
		llist_del(&facch->list);
		llist_del(&tch->list);

		/* Drop TCH */
		talloc_free(tch);

		/* FACCH replaces TCH */
		return facch;
	} else if (facch) {
		/* Only FACCH was found */
		llist_del(&facch->list);
		return facch;
	} else if (tch) {
		/* Only TCH was found */
		llist_del(&tch->list);
		return tch;
	}

	/**
	 * Nothing was found,
	 * e.g. only SACCH frames are in queue
	 */
	return NULL;
}

/**
 * Dequeues a single primitive of required type
 * from a specified transmit queue.
 *
 * @param  queue      a transmit queue to take a prim from
 * @param  lchan_type required primitive type
 * @return            a primitive or NULL if not found
 */
struct trx_ts_prim *sched_prim_dequeue(struct llist_head *queue,
	enum trx_lchan_type lchan_type)
{
	struct trx_ts_prim *prim;

	/* There is nothing to dequeue */
	if (llist_empty(queue))
		return NULL;

	/* TCH requires FACCH prioritization, so handle it separately */
	if (CHAN_IS_TCH(lchan_type))
		return sched_prim_dequeue_tch(queue);

	llist_for_each_entry(prim, queue, list) {
		if (prim->chan == lchan_type) {
			llist_del(&prim->list);
			return prim;
		}
	}

	return NULL;
}

/**
 * Drops the current primitive of specified logical channel
 *
 * @param lchan a logical channel to drop prim from
 */
void sched_prim_drop(struct trx_lchan_state *lchan)
{
	/* Forget this primitive */
	talloc_free(lchan->prim);
	lchan->prim = NULL;
}

/**
 * Flushes a queue of primitives
 *
 * @param list list of prims going to be flushed
 */
void sched_prim_flush_queue(struct llist_head *list)
{
	struct trx_ts_prim *prim, *prim_next;

	llist_for_each_entry_safe(prim, prim_next, list, list) {
		llist_del(&prim->list);
		talloc_free(prim);
	}
}
