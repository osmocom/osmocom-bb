/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: primitive management
 *
 * (C) 2017-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * Contributions by sysmocom - s.f.m.c. GmbH
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
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <talloc.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/linuxlist.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

/**
 * Initializes a new primitive by allocating memory
 * and filling some meta-information (e.g. lchan type).
 *
 * @param  ctx     parent talloc context
 * @param  pl_len  prim payload length
 * @param  type    prim payload type
 * @param  chan_nr RSL channel description (used to set a proper chan)
 * @param  link_id RSL link description (used to set a proper chan)
 * @return         allocated primitive or NULL
 */
static struct l1sched_ts_prim *prim_alloc(void *ctx, size_t pl_len,
					  enum l1sched_ts_prim_type type,
					  uint8_t chan_nr, uint8_t link_id)
{
	enum l1sched_lchan_type lchan_type;
	struct l1sched_ts_prim *prim;

	/* Determine lchan type */
	lchan_type = l1sched_chan_nr2lchan_type(chan_nr, link_id);
	if (!lchan_type) {
		/* TODO: use proper logging context */
		LOGP(DLGLOBAL, LOGL_ERROR, "Couldn't determine lchan type "
		     "for chan_nr=%02x and link_id=%02x\n", chan_nr, link_id);
		return NULL;
	}

	/* Allocate a new primitive */
	prim = talloc_zero_size(ctx, sizeof(*prim) + pl_len);
	if (prim == NULL)
		return NULL;

	/* Init primitive header */
	prim->payload_len = pl_len;
	prim->chan = lchan_type;
	prim->type = type;

	return prim;
}

/**
 * Adds a primitive to the end of transmit queue of a particular
 * timeslot, whose index is parsed from chan_nr.
 *
 * @param  sched   scheduler instance
 * @param  chan_nr RSL channel description
 * @param  link_id RSL link description
 * @param  pl      Payload data
 * @param  pl_len  Payload length
 * @return         queued primitive or NULL
 */
struct l1sched_ts_prim *l1sched_prim_push(struct l1sched_state *sched,
					  enum l1sched_ts_prim_type type,
					  uint8_t chan_nr, uint8_t link_id,
					  const uint8_t *pl, size_t pl_len)
{
	struct l1sched_ts_prim *prim;
	struct l1sched_ts *ts;
	uint8_t tn;

	/* Determine TS index */
	tn = chan_nr & 0x7;

	/* Check whether required timeslot is allocated and configured */
	ts = sched->ts[tn];
	if (ts == NULL || ts->mf_layout == NULL) {
		LOGP_SCHEDC(sched, LOGL_ERROR, "Timeslot %u isn't configured\n", tn);
		return NULL;
	}

	prim = prim_alloc(ts, pl_len, type, chan_nr, link_id);
	if (prim == NULL)
		return NULL;

	memcpy(&prim->payload[0], pl, pl_len);

	/* Add primitive to TS transmit queue */
	llist_add_tail(&prim->list, &ts->tx_prims);

	return prim;
}

/**
 * Composes a new primitive from cached RR Measurement Report.
 *
 * @param  lchan lchan to assign a primitive
 * @return       SACCH primitive to be transmitted
 */
static struct l1sched_ts_prim *prim_compose_mr(struct l1sched_lchan_state *lchan)
{
	struct l1sched_ts_prim *prim;
	bool cached;

	/* Allocate a new primitive */
	prim = prim_alloc(lchan, GSM_MACBLOCK_LEN, L1SCHED_PRIM_DATA,
			  l1sched_lchan_desc[lchan->type].chan_nr,
			  L1SCHED_CH_LID_SACCH);
	OSMO_ASSERT(prim != NULL);

	/* Check if the MR cache is populated (verify LAPDm header) */
	cached = (lchan->sacch.mr_cache[2] != 0x00
		&& lchan->sacch.mr_cache[3] != 0x00
		&& lchan->sacch.mr_cache[4] != 0x00);
	if (!cached) {
		memcpy(&lchan->sacch.mr_cache[0],
		       &lchan->ts->sched->sacch_cache[0],
		       sizeof(lchan->sacch.mr_cache));
	}

	/* Compose a new Measurement Report primitive */
	memcpy(&prim->payload[0], &lchan->sacch.mr_cache[0], GSM_MACBLOCK_LEN);

	/* Inform about the cache usage count */
	if (++lchan->sacch.mr_cache_usage > 5) {
		LOGP_LCHAND(lchan, LOGL_NOTICE,
			    "SACCH MR cache usage count=%u > 5 "
			    "=> ancient measurements, please fix!\n",
			    lchan->sacch.mr_cache_usage);
	}

	LOGP_LCHAND(lchan, LOGL_NOTICE, "Using cached Measurement Report\n");

	return prim;
}

/**
 * Dequeues a SACCH primitive from transmit queue, if present.
 * Otherwise dequeues a cached Measurement Report (the last
 * received one). Finally, if the cache is empty, a "dummy"
 * measurement report is used.
 *
 * According to 3GPP TS 04.08, section 3.4.1, SACCH channel
 * accompanies either a traffic or a signaling channel. It
 * has the particularity that continuous transmission must
 * occur in both directions, so on the Uplink direction
 * measurement result messages are sent at each possible
 * occasion when nothing else has to be sent. The LAPDm
 * fill frames (0x01, 0x03, 0x01, 0x2b, ...) are not
 * applicable on SACCH channels!
 *
 * Unfortunately, 3GPP TS 04.08 doesn't clearly state
 * which "else messages" besides Measurement Reports
 * can be send by the MS on SACCH channels. However,
 * in sub-clause 3.4.1 it's stated that the interval
 * between two successive measurement result messages
 * shall not exceed one L2 frame.
 *
 * @param  queue transmit queue to take a prim from
 * @param  lchan lchan to assign a primitive
 * @return       SACCH primitive to be transmitted
 */
static struct l1sched_ts_prim *prim_dequeue_sacch(struct llist_head *queue,
	struct l1sched_lchan_state *lchan)
{
	struct l1sched_ts_prim *prim_nmr = NULL;
	struct l1sched_ts_prim *prim_mr = NULL;
	struct l1sched_ts_prim *prim;
	bool mr_now;

	/* Shall we transmit MR now? */
	mr_now = !lchan->sacch.mr_tx_last;

#define PRIM_IS_MR(prim) \
	(prim->payload[5] == GSM48_PDISC_RR \
		&& prim->payload[6] == GSM48_MT_RR_MEAS_REP)

	/* Iterate over all primitives in the queue */
	llist_for_each_entry(prim, queue, list) {
		/* We are looking for particular channel */
		if (prim->chan != lchan->type)
			continue;

		/* Look for a Measurement Report */
		if (!prim_mr && PRIM_IS_MR(prim))
			prim_mr = prim;

		/* Look for anything else */
		if (!prim_nmr && !PRIM_IS_MR(prim))
			prim_nmr = prim;

		/* Should we look further? */
		if (mr_now && prim_mr)
			break; /* MR was found */
		else if (!mr_now && prim_nmr)
			break; /* something else was found */
	}

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "SACCH MR selection: mr_tx_last=%d prim_mr=%p prim_nmr=%p\n",
		    lchan->sacch.mr_tx_last, prim_mr, prim_nmr);

	/* Prioritize non-MR prim if possible */
	if (mr_now && prim_mr)
		prim = prim_mr;
	else if (!mr_now && prim_nmr)
		prim = prim_nmr;
	else if (!mr_now && prim_mr)
		prim = prim_mr;
	else /* Nothing was found */
		prim = NULL;

	/* Have we found what we were looking for? */
	if (prim) /* Dequeue if so */
		llist_del(&prim->list);
	else /* Otherwise compose a new MR */
		prim = prim_compose_mr(lchan);

	/* Update the cached report */
	if (prim == prim_mr) {
		memcpy(lchan->sacch.mr_cache,
			prim->payload, GSM_MACBLOCK_LEN);
		lchan->sacch.mr_cache_usage = 0;

		LOGP_LCHAND(lchan, LOGL_DEBUG, "SACCH MR cache has been updated\n");
	}

	/* Update the MR transmission state */
	lchan->sacch.mr_tx_last = PRIM_IS_MR(prim);

	LOGP_LCHAND(lchan, LOGL_DEBUG, "SACCH decision: %s\n",
		    PRIM_IS_MR(prim) ? "Measurement Report" : "data frame");

	return prim;
}

/* Dequeues a primitive of a given channel type */
static struct l1sched_ts_prim *prim_dequeue_one(struct llist_head *queue,
	enum l1sched_lchan_type lchan_type)
{
	struct l1sched_ts_prim *prim;

	/**
	 * There is no need to use the 'safe' list iteration here
	 * as an item removal is immediately followed by return.
	 */
	llist_for_each_entry(prim, queue, list) {
		if (prim->chan == lchan_type) {
			llist_del(&prim->list);
			return prim;
		}
	}

	return NULL;
}

/**
 * Dequeues either a FACCH, or a speech TCH primitive
 * of a given channel type (Lm or Bm).
 *
 * Note: we could avoid 'lchan_type' parameter and just
 * check the prim's channel type using L1SCHED_CHAN_IS_TCH(),
 * but the current approach is a bit more flexible,
 * and allows one to have both sub-slots of TCH/H
 * enabled on same timeslot e.g. for testing...
 *
 * @param  queue      transmit queue to take a prim from
 * @param  lchan_type required channel type of a primitive,
 *                    e.g. L1SCHED_TCHF, L1SCHED_TCHH_0, or L1SCHED_TCHH_1
 * @param  facch      FACCH (true) or speech (false) prim?
 * @return            either a FACCH, or a TCH primitive if found,
 *                    otherwise NULL
 */
static struct l1sched_ts_prim *prim_dequeue_tch(struct llist_head *queue,
	enum l1sched_lchan_type lchan_type, bool facch)
{
	struct l1sched_ts_prim *prim;

	/**
	 * There is no need to use the 'safe' list iteration here
	 * as an item removal is immediately followed by return.
	 */
	llist_for_each_entry(prim, queue, list) {
		if (prim->chan != lchan_type)
			continue;

		/* Either FACCH, or not FACCH */
		if (L1SCHED_PRIM_IS_FACCH(prim) != facch)
			continue;

		llist_del(&prim->list);
		return prim;
	}

	return NULL;
}

/**
 * Dequeues either a TCH/F, or a FACCH/F prim (preferred).
 * If a FACCH/F prim is found, one TCH/F prim is being
 * dropped (i.e. replaced).
 *
 * @param  queue a transmit queue to take a prim from
 * @return       either a FACCH/F, or a TCH/F primitive,
 *               otherwise NULL
 */
static struct l1sched_ts_prim *prim_dequeue_tch_f(struct llist_head *queue)
{
	struct l1sched_ts_prim *facch;
	struct l1sched_ts_prim *tch;

	/* Attempt to find a pair of both FACCH/F and TCH/F frames */
	facch = prim_dequeue_tch(queue, L1SCHED_TCHF, true);
	tch = prim_dequeue_tch(queue, L1SCHED_TCHF, false);

	/* Prioritize FACCH/F, if found */
	if (facch) {
		/* One TCH/F prim is replaced */
		if (tch)
			talloc_free(tch);
		return facch;
	} else if (tch) {
		/* Only TCH/F prim was found */
		return tch;
	} else {
		/* Nothing was found, e.g. when only SACCH frames are in queue */
		return NULL;
	}
}

/**
 * Dequeues either a TCH/H, or a FACCH/H prim (preferred).
 * If a FACCH/H prim is found, two TCH/H prims are being
 * dropped (i.e. replaced).
 *
 * According to GSM 05.02, the following blocks can be used
 * to carry FACCH/H data (see clause 7, table 1 of 9):
 *
 * UL FACCH/H0:
 * B0(0,2,4,6,8,10), B1(8,10,13,15,17,19), B2(17,19,21,23,0,2)
 *
 * UL FACCH/H1:
 * B0(1,3,5,7,9,11), B1(9,11,14,16,18,20), B2(18,20,22,24,1,3)
 *
 * where the numbers within brackets are fn % 26.
 *
 * @param  queue      transmit queue to take a prim from
 * @param  fn         the current frame number
 * @param  lchan_type required channel type of a primitive,
 * @return            either a FACCH/H, or a TCH/H primitive,
 *                    otherwise NULL
 */
static struct l1sched_ts_prim *prim_dequeue_tch_h(struct llist_head *queue,
	uint32_t fn, enum l1sched_lchan_type lchan_type)
{
	struct l1sched_ts_prim *facch;
	struct l1sched_ts_prim *tch;
	bool facch_now;

	/* May we initiate an UL FACCH/H frame transmission now? */
	facch_now = l1sched_tchh_facch_start(lchan_type, fn, true);
	if (!facch_now) /* Just dequeue a TCH/H prim */
		goto no_facch;

	/* If there are no FACCH/H prims in the queue */
	facch = prim_dequeue_tch(queue, lchan_type, true);
	if (!facch) /* Just dequeue a TCH/H prim */
		goto no_facch;

	/* FACCH/H prim replaces two TCH/F prims */
	tch = prim_dequeue_tch(queue, lchan_type, false);
	if (tch) {
		/* At least one TCH/H prim is dropped */
		talloc_free(tch);

		/* Attempt to find another */
		tch = prim_dequeue_tch(queue, lchan_type, false);
		if (tch) /* Drop the second TCH/H prim */
			talloc_free(tch);
	}

	return facch;

no_facch:
	return prim_dequeue_tch(queue, lchan_type, false);
}

/**
 * Dequeues a single primitive of required type
 * from a specified transmit queue.
 *
 * @param  queue      a transmit queue to take a prim from
 * @param  fn         the current frame number (used for FACCH/H)
 * @param  lchan      logical channel state
 * @return            a primitive or NULL if not found
 */
struct l1sched_ts_prim *l1sched_prim_dequeue(struct llist_head *queue,
	uint32_t fn, struct l1sched_lchan_state *lchan)
{
	/* SACCH is unorthodox, see 3GPP TS 04.08, section 3.4.1 */
	if (L1SCHED_CHAN_IS_SACCH(lchan->type))
		return prim_dequeue_sacch(queue, lchan);

	/* There is nothing to dequeue */
	if (llist_empty(queue))
		return NULL;

	switch (lchan->type) {
	/* TCH/F requires FACCH/F prioritization */
	case L1SCHED_TCHF:
		return prim_dequeue_tch_f(queue);

	/* FACCH/H prioritization is a bit more complex */
	case L1SCHED_TCHH_0:
	case L1SCHED_TCHH_1:
		return prim_dequeue_tch_h(queue, fn, lchan->type);

	/* Other kinds of logical channels */
	default:
		return prim_dequeue_one(queue, lchan->type);
	}
}

/**
 * Drops the current primitive of specified logical channel
 *
 * @param lchan a logical channel to drop prim from
 */
void l1sched_prim_drop(struct l1sched_lchan_state *lchan)
{
	/* Forget this primitive */
	talloc_free(lchan->prim);
	lchan->prim = NULL;
}

/**
 * Assigns a dummy primitive to a lchan depending on its type.
 * Could be used when there is nothing to transmit, but
 * CBTX (Continuous Burst Transmission) is assumed.
 *
 * @param  lchan lchan to assign a primitive
 * @return       zero in case of success, otherwise a error code
 */
int l1sched_prim_dummy(struct l1sched_lchan_state *lchan)
{
	enum l1sched_lchan_type chan = lchan->type;
	uint8_t tch_mode = lchan->tch_mode;
	struct l1sched_ts_prim *prim;
	uint8_t prim_buffer[40];
	size_t prim_len = 0;
	int i;

	/**
	 * TS 144.006, section 8.4.2.3 "Fill frames"
	 * A fill frame is a UI command frame for SAPI 0, P=0
	 * and with an information field of 0 octet length.
	 */
	static const uint8_t lapdm_fill_frame[] = {
		0x01, 0x03, 0x01, 0x2b,
		/* Pending part is to be randomized */
	};

	/* Make sure that there is no existing primitive */
	OSMO_ASSERT(lchan->prim == NULL);
	/* Not applicable for SACCH! */
	OSMO_ASSERT(!L1SCHED_CHAN_IS_SACCH(lchan->type));

	/**
	 * Determine what actually should be generated:
	 * TCH in GSM48_CMODE_SIGN: LAPDm fill frame;
	 * TCH in other modes: silence frame;
	 * other channels: LAPDm fill frame.
	 */
	if (L1SCHED_CHAN_IS_TCH(chan) && L1SCHED_TCH_MODE_IS_SPEECH(tch_mode)) {
		/* Bad frame indication */
		prim_len = l1sched_bad_frame_ind(prim_buffer, lchan);
	} else if (L1SCHED_CHAN_IS_TCH(chan) && L1SCHED_TCH_MODE_IS_DATA(tch_mode)) {
		/* FIXME: should we do anything for CSD? */
		return 0;
	} else {
		/* Copy LAPDm fill frame's header */
		memcpy(prim_buffer, lapdm_fill_frame, sizeof(lapdm_fill_frame));

		/**
		 * TS 144.006, section 5.2 "Frame delimitation and fill bits"
		 * Except for the first octet containing fill bits which shall
		 * be set to the binary value "00101011", each fill bit should
		 * be set to a random value when sent by the network.
		 */
		for (i = sizeof(lapdm_fill_frame); i < GSM_MACBLOCK_LEN; i++)
			prim_buffer[i] = (uint8_t) rand();

		/* Define a prim length */
		prim_len = GSM_MACBLOCK_LEN;
	}

	/* Nothing to allocate / assign */
	if (!prim_len)
		return 0;

	/* Allocate a new primitive */
	prim = talloc_zero_size(lchan, sizeof(struct l1sched_ts_prim) + prim_len);
	if (prim == NULL)
		return -ENOMEM;

	/* Init primitive header */
	prim->payload_len = prim_len;
	prim->chan = lchan->type;

	/* Fill in the payload */
	memcpy(prim->payload, prim_buffer, prim_len);

	/* Assign the current prim */
	lchan->prim = prim;

	LOGP_LCHAND(lchan, LOGL_DEBUG, "Transmitting a dummy / silence frame\n");

	return 0;
}

/**
 * Flushes a queue of primitives
 *
 * @param list list of prims going to be flushed
 */
void l1sched_prim_flush_queue(struct llist_head *list)
{
	struct l1sched_ts_prim *prim, *prim_next;

	llist_for_each_entry_safe(prim, prim_next, list, list) {
		llist_del(&prim->list);
		talloc_free(prim);
	}
}
