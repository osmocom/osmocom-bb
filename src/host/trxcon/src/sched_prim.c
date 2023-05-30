/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: primitive management
 *
 * (C) 2017-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/core/prim.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/linuxlist.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

#define L1SCHED_PRIM_HEADROOM	64
#define L1SCHED_PRIM_TAILROOM	64

osmo_static_assert(sizeof(struct l1sched_prim) <= L1SCHED_PRIM_HEADROOM, l1sched_prim_size);

const struct value_string l1sched_prim_type_names[] = {
	{ L1SCHED_PRIM_T_DATA,			"DATA" },
	{ L1SCHED_PRIM_T_RACH,			"RACH" },
	{ L1SCHED_PRIM_T_SCH,			"SCH" },
	{ L1SCHED_PRIM_T_PCHAN_COMB,		"PCHAN_COMB" },
	{ 0, NULL },
};

void l1sched_prim_init(struct msgb *msg,
		       enum l1sched_prim_type type,
		       enum osmo_prim_operation op)
{
	struct l1sched_prim *prim;

	msg->l2h = msg->data;
	msg->l1h = msgb_push(msg, sizeof(*prim));

	prim = l1sched_prim_from_msgb(msg);
	osmo_prim_init(&prim->oph, 0, type, op, msg);
}

struct msgb *l1sched_prim_alloc(enum l1sched_prim_type type,
				enum osmo_prim_operation op)
{
	struct msgb *msg;

	msg = msgb_alloc_headroom(L1SCHED_PRIM_HEADROOM + L1SCHED_PRIM_TAILROOM,
				  L1SCHED_PRIM_HEADROOM, "l1sched_prim");
	if (msg == NULL)
		return NULL;

	l1sched_prim_init(msg, type, op);

	return msg;
}

/**
 * Composes a new primitive from cached RR Measurement Report.
 *
 * @param  lchan lchan to assign a primitive
 * @return       SACCH primitive to be transmitted
 */
static struct msgb *prim_compose_mr(struct l1sched_lchan_state *lchan)
{
	struct l1sched_prim *prim;
	struct msgb *msg;
	bool cached;

	/* Allocate a new primitive */
	msg = l1sched_prim_alloc(L1SCHED_PRIM_T_DATA, PRIM_OP_REQUEST);
	OSMO_ASSERT(msg != NULL);

	prim = l1sched_prim_from_msgb(msg);
	prim->data_req = (struct l1sched_prim_chdr) {
		.chan_nr = l1sched_lchan_desc[lchan->type].chan_nr,
		.link_id = L1SCHED_CH_LID_SACCH,
	};

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
	memcpy(msgb_put(msg, GSM_MACBLOCK_LEN),
	       &lchan->sacch.mr_cache[0],
	       GSM_MACBLOCK_LEN);

	/* Inform about the cache usage count */
	if (++lchan->sacch.mr_cache_usage > 5) {
		LOGP_LCHAND(lchan, LOGL_NOTICE,
			    "SACCH MR cache usage count=%u > 5 "
			    "=> ancient measurements, please fix!\n",
			    lchan->sacch.mr_cache_usage);
	}

	LOGP_LCHAND(lchan, LOGL_NOTICE, "Using cached Measurement Report\n");

	return msg;
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
 * @param  lchan lchan to assign a primitive
 * @return       SACCH primitive to be transmitted
 */
static struct msgb *prim_dequeue_sacch(struct l1sched_lchan_state *lchan)
{
	struct msgb *msg_nmr = NULL;
	struct msgb *msg_mr = NULL;
	struct msgb *msg;
	bool mr_now;

	/* Shall we transmit MR now? */
	mr_now = !lchan->sacch.mr_tx_last;

#define PRIM_MSGB_IS_MR(msg) \
	(l1sched_prim_data_from_msgb(msg)[5] == GSM48_PDISC_RR && \
	 l1sched_prim_data_from_msgb(msg)[6] == GSM48_MT_RR_MEAS_REP)

	/* Iterate over all primitives in the queue */
	llist_for_each_entry(msg, &lchan->tx_prims, list) {
		/* Look for a Measurement Report */
		if (!msg_mr && PRIM_MSGB_IS_MR(msg))
			msg_mr = msg;

		/* Look for anything else */
		if (!msg_nmr && !PRIM_MSGB_IS_MR(msg))
			msg_nmr = msg;

		/* Should we look further? */
		if (mr_now && msg_mr)
			break; /* MR was found */
		else if (!mr_now && msg_nmr)
			break; /* something else was found */
	}

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "SACCH MR selection: mr_tx_last=%d msg_mr=%p msg_nmr=%p\n",
		    lchan->sacch.mr_tx_last, msg_mr, msg_nmr);

	/* Prioritize non-MR prim if possible */
	if (mr_now && msg_mr)
		msg = msg_mr;
	else if (!mr_now && msg_nmr)
		msg = msg_nmr;
	else if (!mr_now && msg_mr)
		msg = msg_mr;
	else /* Nothing was found */
		msg = NULL;

	/* Have we found what we were looking for? */
	if (msg) /* Dequeue if so */
		llist_del(&msg->list);
	else /* Otherwise compose a new MR */
		msg = prim_compose_mr(lchan);

	/* Update the cached report */
	if (msg == msg_mr) {
		memcpy(lchan->sacch.mr_cache, msgb_l2(msg), GSM_MACBLOCK_LEN);
		lchan->sacch.mr_cache_usage = 0;

		LOGP_LCHAND(lchan, LOGL_DEBUG, "SACCH MR cache has been updated\n");
	}

	/* Update the MR transmission state */
	lchan->sacch.mr_tx_last = PRIM_MSGB_IS_MR(msg);

	LOGP_LCHAND(lchan, LOGL_DEBUG, "SACCH decision: %s\n",
		    PRIM_MSGB_IS_MR(msg) ? "Measurement Report" : "data frame");

	return msg;
}

/**
 * Dequeues either a FACCH, or a speech TCH primitive
 * of a given channel type (Lm or Bm).
 *
 * @param  lchan      logical channel state
 * @param  facch      FACCH (true) or speech (false) prim?
 * @return            either a FACCH, or a TCH primitive if found,
 *                    otherwise NULL
 */
static struct msgb *prim_dequeue_tch(struct l1sched_lchan_state *lchan, bool facch)
{
	struct msgb *msg;

	/**
	 * There is no need to use the 'safe' list iteration here
	 * as an item removal is immediately followed by return.
	 */
	llist_for_each_entry(msg, &lchan->tx_prims, list) {
		bool is_facch = msgb_l2len(msg) == GSM_MACBLOCK_LEN;
		if (is_facch != facch)
			continue;

		llist_del(&msg->list);
		return msg;
	}

	return NULL;
}

/**
 * Dequeues either a TCH/F, or a FACCH/F prim (preferred).
 * If a FACCH/F prim is found, one TCH/F prim is being
 * dropped (i.e. replaced).
 *
 * @param  lchan logical channel state
 * @return       either a FACCH/F, or a TCH/F primitive,
 *               otherwise NULL
 */
static struct msgb *prim_dequeue_tch_f(struct l1sched_lchan_state *lchan)
{
	struct msgb *facch;
	struct msgb *tch;

	/* Attempt to find a pair of both FACCH/F and TCH/F frames */
	facch = prim_dequeue_tch(lchan, true);
	tch = prim_dequeue_tch(lchan, false);

	/* Prioritize FACCH/F, if found */
	if (facch) {
		/* One TCH/F prim is replaced */
		if (tch)
			msgb_free(tch);
		return facch;
	} else if (tch) {
		/* Only TCH/F prim was found */
		return tch;
	} else {
		/* Nothing was found */
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
 * @param  lchan      logical channel state
 * @param  fn         the current frame number
 * @return            either a FACCH/H, or a TCH/H primitive,
 *                    otherwise NULL
 */
static struct msgb *prim_dequeue_tch_h(struct l1sched_lchan_state *lchan, uint32_t fn)
{
	struct msgb *facch;
	struct msgb *tch;
	bool facch_now;

	/* May we initiate an UL FACCH/H frame transmission now? */
	facch_now = l1sched_tchh_facch_start(lchan->type, fn, true);
	if (!facch_now) /* Just dequeue a TCH/H prim */
		goto no_facch;

	/* If there are no FACCH/H prims in the queue */
	facch = prim_dequeue_tch(lchan, true);
	if (!facch) /* Just dequeue a TCH/H prim */
		goto no_facch;

	/* FACCH/H prim replaces two TCH/F prims */
	tch = prim_dequeue_tch(lchan, false);
	if (tch) {
		/* At least one TCH/H prim is dropped */
		msgb_free(tch);

		/* Attempt to find another */
		tch = prim_dequeue_tch(lchan, false);
		if (tch) /* Drop the second TCH/H prim */
			msgb_free(tch);
	}

	return facch;

no_facch:
	return prim_dequeue_tch(lchan, false);
}

/**
 * Dequeues a primitive from the Tx queue of the given lchan.
 *
 * @param  lchan      logical channel state
 * @param  fn         the current frame number (used for FACCH/H)
 * @return            a primitive or NULL if not found
 */
struct msgb *l1sched_lchan_prim_dequeue(struct l1sched_lchan_state *lchan, uint32_t fn)
{
	/* SACCH is unorthodox, see 3GPP TS 04.08, section 3.4.1 */
	if (L1SCHED_CHAN_IS_SACCH(lchan->type))
		return prim_dequeue_sacch(lchan);

	/* There is nothing to dequeue */
	if (llist_empty(&lchan->tx_prims))
		return NULL;

	switch (lchan->type) {
	/* TCH/F requires FACCH/F prioritization */
	case L1SCHED_TCHF:
		return prim_dequeue_tch_f(lchan);

	/* FACCH/H prioritization is a bit more complex */
	case L1SCHED_TCHH_0:
	case L1SCHED_TCHH_1:
		return prim_dequeue_tch_h(lchan, fn);

	/* PDCH is timing critical, we need to check TDMA Fn */
	case L1SCHED_PDTCH:
	{
		struct msgb *msg = msgb_dequeue(&lchan->tx_prims);
		const struct l1sched_prim *prim;

		if (msg == NULL)
			return NULL;
		prim = l1sched_prim_from_msgb(msg);

		if (OSMO_LIKELY(prim->data_req.frame_nr == fn))
			return msg;
		LOGP_LCHAND(lchan, LOGL_ERROR,
			    "%s(): dropping Tx primitive (current Fn=%u, prim Fn=%u)\n",
			    __func__, fn, prim->data_req.frame_nr);
		msgb_free(msg);
		return NULL;
	}

	/* Other kinds of logical channels */
	default:
		return msgb_dequeue(&lchan->tx_prims);
	}
}

/**
 * Drops the current primitive of specified logical channel
 *
 * @param lchan a logical channel to drop prim from
 */
void l1sched_lchan_prim_drop(struct l1sched_lchan_state *lchan)
{
	msgb_free(lchan->prim);
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
void l1sched_lchan_prim_assign_dummy(struct l1sched_lchan_state *lchan)
{
	const struct l1sched_lchan_desc *lchan_desc;
	enum l1sched_lchan_type chan = lchan->type;
	uint8_t tch_mode = lchan->tch_mode;
	struct l1sched_prim *prim;
	struct msgb *msg;
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

	lchan_desc = &l1sched_lchan_desc[lchan->type];

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
		return;
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
		return;

	msg = l1sched_prim_alloc(L1SCHED_PRIM_T_DATA, PRIM_OP_REQUEST);
	OSMO_ASSERT(msg != NULL);

	prim = l1sched_prim_from_msgb(msg);
	prim->data_req = (struct l1sched_prim_chdr) {
		.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
		.link_id = lchan_desc->link_id,
	};

	memcpy(msgb_put(msg, prim_len), &prim_buffer[0], prim_len);

	/* Assign the current prim */
	lchan->prim = msg;

	LOGP_LCHAND(lchan, LOGL_DEBUG, "Transmitting a dummy / silence frame\n");
}

int l1sched_lchan_emit_data_ind(struct l1sched_lchan_state *lchan,
				const uint8_t *data, size_t data_len,
				int n_errors, int n_bits_total,
				bool traffic)
{
	const struct l1sched_meas_set *meas = &lchan->meas_avg;
	const struct l1sched_lchan_desc *lchan_desc;
	struct l1sched_prim *prim;
	struct msgb *msg;

	lchan_desc = &l1sched_lchan_desc[lchan->type];

	msg = l1sched_prim_alloc(L1SCHED_PRIM_T_DATA, PRIM_OP_INDICATION);
	OSMO_ASSERT(msg != NULL);

	prim = l1sched_prim_from_msgb(msg);
	prim->data_ind = (struct l1sched_prim_data_ind) {
		.chdr = {
			.frame_nr = meas->fn,
			.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
			.link_id = lchan_desc->link_id,
			.traffic = traffic,
		},
		.toa256 = meas->toa256,
		.rssi = meas->rssi,
		.n_errors = n_errors,
		.n_bits_total = n_bits_total,
	};

	if (data_len > 0)
		memcpy(msgb_put(msg, data_len), data, data_len);

	return l1sched_prim_to_user(lchan->ts->sched, msg);
}

int l1sched_lchan_emit_data_cnf(struct l1sched_lchan_state *lchan, uint32_t fn)
{
	struct l1sched_prim *prim;
	struct msgb *msg;

	/* take ownership of the prim */
	OSMO_ASSERT(lchan->prim != NULL);
	msg = lchan->prim;
	lchan->prim = NULL;

	/* convert from DATA.req to DATA.cnf */
	prim = l1sched_prim_from_msgb(msg);
	prim->oph.operation = PRIM_OP_CONFIRM;

	switch (prim->oph.primitive) {
	case L1SCHED_PRIM_T_DATA:
		prim->data_cnf.frame_nr = fn;
		break;
	case L1SCHED_PRIM_T_RACH:
		prim->rach_cnf.chdr.frame_nr = fn;
		break;
	default:
		/* shall not happen */
		OSMO_ASSERT(0);
	}

	return l1sched_prim_to_user(lchan->ts->sched, msg);
}

static int prim_enqeue(struct l1sched_state *sched, struct msgb *msg,
		       const struct l1sched_prim_chdr *chdr)
{
	const struct l1sched_prim *prim = l1sched_prim_from_msgb(msg);
	struct l1sched_lchan_state *lchan;

	lchan = l1sched_find_lchan_by_chan_nr(sched, chdr->chan_nr, chdr->link_id);
	if (OSMO_UNLIKELY(lchan == NULL || !lchan->active)) {
		LOGP_SCHEDD(sched, LOGL_ERROR,
			    "No [active] lchan for primitive " L1SCHED_PRIM_STR_FMT " "
			    "(chan_nr=%02x, link_id=%02x, len=%u): %s\n",
			    L1SCHED_PRIM_STR_ARGS(prim),
			    chdr->chan_nr, chdr->link_id,
			    msgb_l2len(msg), msgb_hexdump_l2(msg));
		msgb_free(msg);
		return -ENODEV;
	}

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "Enqueue primitive " L1SCHED_PRIM_STR_FMT " "
		    "(chan_nr=%02x, link_id=%02x, len=%u): %s\n",
		    L1SCHED_PRIM_STR_ARGS(prim),
		    chdr->chan_nr, chdr->link_id,
		    msgb_l2len(msg), msgb_hexdump_l2(msg));

	msgb_enqueue(&lchan->tx_prims, msg);
	return 0;
}

int l1sched_prim_from_user(struct l1sched_state *sched, struct msgb *msg)
{
	const struct l1sched_prim *prim = l1sched_prim_from_msgb(msg);

	LOGP_SCHEDD(sched, LOGL_DEBUG,
		    "%s(): Rx " L1SCHED_PRIM_STR_FMT "\n",
		    __func__, L1SCHED_PRIM_STR_ARGS(prim));

	switch (OSMO_PRIM_HDR(&prim->oph)) {
	case OSMO_PRIM(L1SCHED_PRIM_T_DATA, PRIM_OP_REQUEST):
		return prim_enqeue(sched, msg, &prim->data_req);
	case OSMO_PRIM(L1SCHED_PRIM_T_RACH, PRIM_OP_REQUEST):
		return prim_enqeue(sched, msg, &prim->rach_req.chdr);
	default:
		LOGP_SCHEDD(sched, LOGL_ERROR,
			 "%s(): Unhandled primitive " L1SCHED_PRIM_STR_FMT "\n",
			 __func__, L1SCHED_PRIM_STR_ARGS(prim));
		msgb_free(msg);
		return -ENOTSUP;
	}
}
