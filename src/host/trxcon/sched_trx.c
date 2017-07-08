/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: GSM PHY routines
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

#include <error.h>
#include <errno.h>
#include <string.h>
#include <talloc.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/linuxlist.h>

#include "scheduler.h"
#include "sched_trx.h"
#include "trx_if.h"
#include "logging.h"

static void msgb_queue_flush(struct llist_head *list)
{
	struct msgb *msg, *msg2;

	llist_for_each_entry_safe(msg, msg2, list, list)
		msgb_free(msg);
}

static void sched_frame_clck_cb(struct trx_sched *sched)
{
	struct trx_instance *trx = (struct trx_instance *) sched->data;

	/* If we have no active timeslots, nothing to do */
	if (llist_empty(&trx->ts_list))
		return;

	/* Do nothing for now */
}

int sched_trx_init(struct trx_instance *trx)
{
	struct trx_sched *sched;

	if (!trx)
		return -EINVAL;

	LOGP(DSCH, LOGL_NOTICE, "Init scheduler\n");

	/* Obtain a scheduler instance from TRX */
	sched = &trx->sched;

	/* Register frame clock callback */
	sched->clock_cb = sched_frame_clck_cb;

	/* Set pointers */
	sched = &trx->sched;
	sched->data = trx;

	INIT_LLIST_HEAD(&trx->ts_list);

	return 0;
}

int sched_trx_shutdown(struct trx_instance *trx)
{
	int i;

	if (!trx)
		return -EINVAL;

	LOGP(DSCH, LOGL_NOTICE, "Shutdown scheduler\n");

	/* Free all potentially allocated timeslots */
	for (i = 0; i < TRX_TS_COUNT; i++)
		sched_trx_del_ts(trx, i);

	return 0;
}

int sched_trx_reset(struct trx_instance *trx)
{
	struct trx_sched *sched;
	int i;

	if (!trx)
		return -EINVAL;

	LOGP(DSCH, LOGL_NOTICE, "Reset scheduler\n");

	/* Free all potentially allocated timeslots */
	for (i = 0; i < TRX_TS_COUNT; i++)
		sched_trx_del_ts(trx, i);

	INIT_LLIST_HEAD(&trx->ts_list);

	/* Obtain a scheduler instance from TRX */
	sched = &trx->sched;

	/* Reset clock counter */
	osmo_timer_del(&sched->clock_timer);
	sched->fn_counter_proc = 0;
	sched->fn_counter_lost = 0;

	return 0;
}

struct trx_ts *sched_trx_add_ts(struct trx_instance *trx, int ts_num)
{
	struct trx_ts *ts;

	LOGP(DSCH, LOGL_INFO, "Add a new TDMA timeslot #%u\n", ts_num);

	ts = talloc_zero(trx, struct trx_ts);
	if (!ts)
		return NULL;

	llist_add_tail(&ts->list, &trx->ts_list);

	return ts;
}

struct trx_ts *sched_trx_find_ts(struct trx_instance *trx, int ts_num)
{
	struct trx_ts *ts;

	if (llist_empty(&trx->ts_list))
		return NULL;

	llist_for_each_entry(ts, &trx->ts_list, list) {
		if (ts->index == ts_num)
			return ts;
	}

	return NULL;
}

void sched_trx_del_ts(struct trx_instance *trx, int ts_num)
{
	struct trx_ts *ts;

	/* Find ts in list */
	ts = sched_trx_find_ts(trx, ts_num);
	if (ts == NULL)
		return;

	LOGP(DSCH, LOGL_INFO, "Delete TDMA timeslot #%u\n", ts_num);

	/* Flush queue primitives for TX */
	msgb_queue_flush(&ts->tx_prims);

	/* Remove ts from list */
	llist_del(&ts->list);
	talloc_free(ts);
}

int sched_trx_configure_ts(struct trx_instance *trx, int ts_num,
	enum gsm_phys_chan_config config)
{
	int i, type, lchan_cnt = 0;
	struct trx_ts *ts;

	/* Try to find specified ts */
	ts = sched_trx_find_ts(trx, ts_num);
	if (ts != NULL) {
		/* Reconfiguration of existing one */
		sched_trx_reset_ts(trx, ts_num);
	} else {
		/* Allocate a new one if doesn't exist */
		ts = sched_trx_add_ts(trx, ts_num);
		if (ts == NULL)
			return -ENOMEM;
	}

	/* Init queue primitives for TX */
	INIT_LLIST_HEAD(&ts->tx_prims);

	/* Choose proper multiframe layout */
	ts->mf_layout = sched_mframe_layout(config, ts_num);
	if (ts->mf_layout->chan_config != config)
		return -EINVAL;

	LOGP(DSCH, LOGL_INFO, "(Re)configure TDMA timeslot #%u as %s\n",
		ts_num, ts->mf_layout->name);

	/* Count channel states */
	for (type = 0; type < _TRX_CHAN_MAX; type++)
		if (ts->mf_layout->lchan_mask & ((uint64_t) 0x01 << type))
			lchan_cnt++;

	if (!lchan_cnt)
		return 0;

	/* Allocate channel states */
	ts->lchans = talloc_zero_array(ts, struct trx_lchan_state, lchan_cnt);
	if (ts->lchans == NULL)
		return -ENOMEM;

	/* Init channel states */
	for (type = 0, i = 0; type < _TRX_CHAN_MAX; type++) {
		if (ts->mf_layout->lchan_mask & ((uint64_t) 0x01 << type)) {
			/* Set proper channel type */
			ts->lchans[i++].type = type;

			/* Enable channel automatically if required */
			if (trx_lchan_desc[type].flags & TRX_CH_FLAG_AUTO)
				sched_trx_activate_lchan(ts, type);
		}
	}

	return 0;
}

int sched_trx_reset_ts(struct trx_instance *trx, int ts_num)
{
	struct trx_ts *ts;

	/* Try to find specified ts */
	ts = sched_trx_find_ts(trx, ts_num);
	if (ts == NULL)
		return -EINVAL;

	/* FIXME: where do we need it? */
	ts->mf_last_fn = 0;

	/* Undefine multiframe layout */
	ts->mf_layout = NULL;

	/* Flush queue primitives for TX */
	msgb_queue_flush(&ts->tx_prims);

	/* Free channel states */
	talloc_free(ts->lchans);

	return 0;
}

struct trx_lchan_state *sched_trx_find_lchan(struct trx_ts *ts,
	enum trx_lchan_type chan)
{
	int i, len;

	len = talloc_array_length(ts->lchans);
	for (i = 0; i < len; i++)
		if (ts->lchans[i].type == chan)
			return ts->lchans + i;

	return NULL;
}

int sched_trx_activate_lchan(struct trx_ts *ts, enum trx_lchan_type chan)
{
	const struct trx_lchan_desc *lchan_desc = &trx_lchan_desc[chan];
	struct trx_lchan_state *lchan;

	/* Try to find requested logical channel */
	lchan = sched_trx_find_lchan(ts, chan);
	if (lchan == NULL)
		return -EINVAL;

	if (lchan->active) {
		LOGP(DSCH, LOGL_ERROR, "Logical channel %s already activated "
			"on ts=%d\n", trx_lchan_desc[chan].name, ts->index);
		return -EINVAL;
	}

	/* Conditionally allocate memory for bursts */
	if (lchan_desc->rx_fn && lchan_desc->burst_buf_size > 0) {
		lchan->rx_bursts = talloc_zero_size(ts->lchans,
			lchan_desc->burst_buf_size);
		if (lchan->rx_bursts == NULL)
			return -ENOMEM;
	}

	if (lchan_desc->tx_fn && lchan_desc->burst_buf_size > 0) {
		lchan->tx_bursts = talloc_zero_size(ts->lchans,
			lchan_desc->burst_buf_size);
		if (lchan->tx_bursts == NULL)
			return -ENOMEM;
	}

	/* Finally, update channel status */
	lchan->active = 1;

	return 0;
}

int sched_trx_deactivate_lchan(struct trx_ts *ts, enum trx_lchan_type chan)
{
	struct trx_lchan_state *lchan;

	/* Try to find requested logical channel */
	lchan = sched_trx_find_lchan(ts, chan);
	if (lchan == NULL)
		return -EINVAL;

	if (!lchan->active) {
		LOGP(DSCH, LOGL_ERROR, "Logical channel %s already deactivated "
			"on ts=%d\n", trx_lchan_desc[chan].name, ts->index);
		return -EINVAL;
	}

	/* Free memory */
	talloc_free(lchan->rx_bursts);
	talloc_free(lchan->tx_bursts);

	lchan->active = 0;

	return 0;
}

int sched_trx_handle_rx_burst(struct trx_instance *trx, uint8_t ts_num,
	uint32_t burst_fn, sbit_t *bits, uint16_t nbits, int8_t rssi, float toa)
{
	struct trx_lchan_state *lchan;
	const struct trx_frame *frame;
	struct trx_ts *ts;

	trx_lchan_rx_func *handler;
	enum trx_lchan_type chan;
	uint32_t fn, elapsed;
	uint8_t offset, bid;

	/* Check whether required timeslot is enabled / configured */
	ts = sched_trx_find_ts(trx, ts_num);
	if (ts == NULL) {
		LOGP(DSCH, LOGL_ERROR, "TDMA timeslot #%u isn't configured, "
			"ignoring burst...\n", ts_num);
		return -EINVAL;
	}

	/* Calculate how many frames have been elapsed */
	elapsed  = (burst_fn + GSM_HYPERFRAME - ts->mf_last_fn);
	elapsed %= GSM_HYPERFRAME;

	/**
	 * If not too many frames have been elapsed,
	 * start counting from last fn + 1
	 */
	if (elapsed < 10)
		fn = (ts->mf_last_fn + 1) % GSM_HYPERFRAME;
	else
		fn = burst_fn;

	while (1) {
		/* Get frame from multiframe */
		offset = fn % ts->mf_layout->period;
		frame = ts->mf_layout->frames + offset;

		/* Get required info from frame */
		bid = frame->dl_bid;
		chan = frame->dl_chan;
		handler = trx_lchan_desc[chan].rx_fn;

		/* Omit bursts which have no handler, like IDLE bursts */
		if (!handler)
			goto next_frame;

		/* Find required channel state */
		lchan = sched_trx_find_lchan(ts, chan);
		if (lchan == NULL) /* FIXME: what should we do here? */
			goto next_frame;

		/* Ensure that channel is active */
		if (!lchan->active)
			goto next_frame;

		/* Put burst to handler */
		if (fn == burst_fn) {
			/* TODO: decrypt if required */
			handler(trx, ts, fn, chan, bid, bits, nbits, rssi, toa);
		}

next_frame:
		/* Reached current fn */
		if (fn == burst_fn)
			break;

		fn = (fn + 1) % GSM_HYPERFRAME;
	}

	/* Set last processed frame number */
	ts->mf_last_fn = fn;

	return 0;
}
