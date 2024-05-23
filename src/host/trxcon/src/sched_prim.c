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
#define L1SCHED_PRIM_TAILROOM	512

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
		.chan_nr = l1sched_lchan_desc[lchan->type].chan_nr | lchan->ts->index,
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
struct msgb *l1sched_lchan_prim_dequeue_sacch(struct l1sched_lchan_state *lchan)
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
struct msgb *l1sched_lchan_prim_dequeue_tch(struct l1sched_lchan_state *lchan, bool facch)
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
 * Allocate a DATA.req with dummy LAPDm func=UI frame for the given logical channel.
 * To be used when no suitable DATA.req is present in the Tx queue.
 *
 * @param  lchan lchan to allocate a dummy primitive for
 * @return       an msgb with DATA.req primitive, or NULL
 */
struct msgb *l1sched_lchan_prim_dummy_lapdm(const struct l1sched_lchan_state *lchan)
{
	struct l1sched_prim *prim;
	struct msgb *msg;
	uint8_t *ptr;

	/* LAPDm func=UI is not applicable for SACCH */
	OSMO_ASSERT(!L1SCHED_CHAN_IS_SACCH(lchan->type));

	msg = l1sched_prim_alloc(L1SCHED_PRIM_T_DATA, PRIM_OP_REQUEST);
	OSMO_ASSERT(msg != NULL);

	prim = l1sched_prim_from_msgb(msg);
	prim->data_req = (struct l1sched_prim_chdr) {
		.chan_nr = l1sched_lchan_desc[lchan->type].chan_nr | lchan->ts->index,
		.link_id = l1sched_lchan_desc[lchan->type].link_id,
	};

	ptr = msgb_put(msg, GSM_MACBLOCK_LEN);

	/**
	 * TS 144.006, section 8.4.2.3 "Fill frames"
	 * A fill frame is a UI command frame for SAPI 0, P=0
	 * and with an information field of 0 octet length.
	 */
	*(ptr++) = 0x01;
	*(ptr++) = 0x03;
	*(ptr++) = 0x01;

	/**
	 * TS 144.006, section 5.2 "Frame delimitation and fill bits"
	 * Except for the first octet containing fill bits which shall
	 * be set to the binary value "00101011", each fill bit should
	 * be set to a random value when sent by the network.
	 */
	*(ptr++) = 0x2b;
	while (ptr < msg->tail)
		*(ptr++) = (uint8_t)rand();

	return msg;
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

int l1sched_lchan_emit_data_cnf(struct l1sched_lchan_state *lchan,
				struct msgb *msg, uint32_t fn)
{
	struct l1sched_prim *prim;

	if (msg == NULL)
		return -ENODEV;

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
			    "(fn=%u, chan_nr=0x%02x, link_id=0x%02x, len=%u): %s\n",
			    L1SCHED_PRIM_STR_ARGS(prim),
			    chdr->frame_nr, chdr->chan_nr, chdr->link_id,
			    msgb_l2len(msg), msgb_hexdump_l2(msg));
		msgb_free(msg);
		return -ENODEV;
	}

	LOGP_LCHAND(lchan, LOGL_DEBUG,
		    "Enqueue primitive " L1SCHED_PRIM_STR_FMT " "
		    "(fn=%u, chan_nr=0x%02x, link_id=0x%02x, len=%u): %s\n",
		    L1SCHED_PRIM_STR_ARGS(prim),
		    chdr->frame_nr, chdr->chan_nr, chdr->link_id,
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
