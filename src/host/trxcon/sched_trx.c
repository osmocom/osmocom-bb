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

#include <osmocom/gsm/a5.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/linuxlist.h>

#include "scheduler.h"
#include "sched_trx.h"
#include "trx_if.h"
#include "logging.h"

static void sched_frame_clck_cb(struct trx_sched *sched)
{
	struct trx_instance *trx = (struct trx_instance *) sched->data;
	const struct trx_frame *frame;
	struct trx_lchan_state *lchan;
	trx_lchan_tx_func *handler;
	enum trx_lchan_type chan;
	uint8_t offset, bid;
	struct trx_ts *ts;
	uint32_t fn;
	int i;

	/* Iterate over timeslot list */
	for (i = 0; i < TRX_TS_COUNT; i++) {
		/* Timeslot is not allocated */
		ts = trx->ts_list[i];
		if (ts == NULL)
			continue;

		/* Timeslot is not configured */
		if (ts->mf_layout == NULL)
			continue;

		/**
		 * Advance frame number, giving the transceiver more
		 * time until a burst must be transmitted...
		 */
		fn = (sched->fn_counter_proc + sched->fn_counter_advance)
			% GSM_HYPERFRAME;

		/* Get frame from multiframe */
		offset = fn % ts->mf_layout->period;
		frame = ts->mf_layout->frames + offset;

		/* Get required info from frame */
		bid = frame->ul_bid;
		chan = frame->ul_chan;
		handler = trx_lchan_desc[chan].tx_fn;

		/* Omit lchans without handler */
		if (!handler)
			continue;

		/* Make sure that lchan was allocated and activated */
		lchan = sched_trx_find_lchan(ts, chan);
		if (lchan == NULL)
			continue;

		/* Omit inactive lchans */
		if (!lchan->active)
			continue;

		/**
		 * If we aren't processing any primitive yet,
		 * attempt to obtain a new one from queue
		 */
		if (lchan->prim == NULL)
			lchan->prim = sched_prim_dequeue(&ts->tx_prims, chan);

		/* If there is no primitive, do nothing */
		if (lchan->prim == NULL)
			continue;

		/* Poke lchan handler */
		handler(trx, ts, lchan, fn, bid);
	}
}

int sched_trx_init(struct trx_instance *trx, uint32_t fn_advance)
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

	/* Set frame counter advance */
	sched->fn_counter_advance = fn_advance;

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

int sched_trx_reset(struct trx_instance *trx, int reset_clock)
{
	int i;

	if (!trx)
		return -EINVAL;

	LOGP(DSCH, LOGL_NOTICE, "Reset scheduler %s\n",
		reset_clock ? "and clock counter" : "");

	/* Free all potentially allocated timeslots */
	for (i = 0; i < TRX_TS_COUNT; i++)
		sched_trx_del_ts(trx, i);

	/* Stop and reset clock counter if required */
	if (reset_clock)
		sched_clck_reset(&trx->sched);

	return 0;
}

struct trx_ts *sched_trx_add_ts(struct trx_instance *trx, int tn)
{
	/* Make sure that ts isn't allocated yet */
	if (trx->ts_list[tn] != NULL) {
		LOGP(DSCH, LOGL_ERROR, "Timeslot #%u already allocated\n", tn);
		return NULL;
	}

	LOGP(DSCH, LOGL_NOTICE, "Add a new TDMA timeslot #%u\n", tn);

	/* Allocate a new one */
	trx->ts_list[tn] = talloc_zero(trx, struct trx_ts);

	/* Assign TS index */
	trx->ts_list[tn]->index = tn;

	return trx->ts_list[tn];
}

void sched_trx_del_ts(struct trx_instance *trx, int tn)
{
	struct trx_lchan_state *lchan, *lchan_next;
	struct trx_ts *ts;

	/* Find ts in list */
	ts = trx->ts_list[tn];
	if (ts == NULL)
		return;

	LOGP(DSCH, LOGL_NOTICE, "Delete TDMA timeslot #%u\n", tn);

	/* Deactivate all logical channels */
	sched_trx_deactivate_all_lchans(ts);

	/* Free channel states */
	llist_for_each_entry_safe(lchan, lchan_next, &ts->lchans, list) {
		llist_del(&lchan->list);
		talloc_free(lchan);
	}

	/* Flush queue primitives for TX */
	sched_prim_flush_queue(&ts->tx_prims);

	/* Remove ts from list and free memory */
	trx->ts_list[tn] = NULL;
	talloc_free(ts);

	/* Notify transceiver about that */
	trx_if_cmd_setslot(trx, tn, 0);
}

#define LAYOUT_HAS_LCHAN(layout, lchan) \
	(layout->lchan_mask & ((uint64_t) 0x01 << lchan))

int sched_trx_configure_ts(struct trx_instance *trx, int tn,
	enum gsm_phys_chan_config config)
{
	struct trx_lchan_state *lchan;
	enum trx_lchan_type type;
	struct trx_ts *ts;

	/* Try to find specified ts */
	ts = trx->ts_list[tn];
	if (ts != NULL) {
		/* Reconfiguration of existing one */
		sched_trx_reset_ts(trx, tn);
	} else {
		/* Allocate a new one if doesn't exist */
		ts = sched_trx_add_ts(trx, tn);
		if (ts == NULL)
			return -ENOMEM;
	}

	/* Choose proper multiframe layout */
	ts->mf_layout = sched_mframe_layout(config, tn);
	if (ts->mf_layout->chan_config != config)
		return -EINVAL;

	LOGP(DSCH, LOGL_NOTICE, "(Re)configure TDMA timeslot #%u as %s\n",
		tn, ts->mf_layout->name);

	/* Init queue primitives for TX */
	INIT_LLIST_HEAD(&ts->tx_prims);
	/* Init logical channels list */
	INIT_LLIST_HEAD(&ts->lchans);

	/* Allocate channel states */
	for (type = 0; type < _TRX_CHAN_MAX; type++) {
		if (!LAYOUT_HAS_LCHAN(ts->mf_layout, type))
			continue;

		/* Allocate a channel state */
		lchan = talloc_zero(ts, struct trx_lchan_state);
		if (!lchan)
			return -ENOMEM;

		/* Set channel type */
		lchan->type = type;

		/* Add to the list of channel states */
		llist_add_tail(&lchan->list, &ts->lchans);

		/* Enable channel automatically if required */
		if (trx_lchan_desc[type].flags & TRX_CH_FLAG_AUTO)
			sched_trx_activate_lchan(ts, type);
	}

	/* Notify transceiver about TS activation */
	/* FIXME: set proper channel type */
	trx_if_cmd_setslot(trx, tn, 1);

	return 0;
}

int sched_trx_reset_ts(struct trx_instance *trx, int tn)
{
	struct trx_lchan_state *lchan, *lchan_next;
	struct trx_ts *ts;

	/* Try to find specified ts */
	ts = trx->ts_list[tn];
	if (ts == NULL)
		return -EINVAL;

	/* Flush TS frame counter */
	ts->mf_last_fn = 0;

	/* Undefine multiframe layout */
	ts->mf_layout = NULL;

	/* Flush queue primitives for TX */
	sched_prim_flush_queue(&ts->tx_prims);

	/* Deactivate all logical channels */
	sched_trx_deactivate_all_lchans(ts);

	/* Free channel states */
	llist_for_each_entry_safe(lchan, lchan_next, &ts->lchans, list) {
		llist_del(&lchan->list);
		talloc_free(lchan);
	}

	/* Notify transceiver about that */
	trx_if_cmd_setslot(trx, tn, 0);

	return 0;
}

int sched_trx_start_ciphering(struct trx_ts *ts, uint8_t algo,
	uint8_t *key, uint8_t key_len)
{
	struct trx_lchan_state *lchan;

	/* Prevent NULL-pointer deference */
	if (!ts)
		return -EINVAL;

	/* Make sure we can store this key */
	if (key_len > MAX_A5_KEY_LEN)
		return -ERANGE;

	/* Iterate over all allocated logical channels */
	llist_for_each_entry(lchan, &ts->lchans, list) {
		/* Omit inactive channels */
		if (!lchan->active)
			continue;

		/* Set key length and algorithm */
		lchan->a5.key_len = key_len;
		lchan->a5.algo = algo;

		/* Copy requested key */
		if (key_len)
			memcpy(lchan->a5.key, key, key_len);
	}

	return 0;
}

struct trx_lchan_state *sched_trx_find_lchan(struct trx_ts *ts,
	enum trx_lchan_type chan)
{
	struct trx_lchan_state *lchan;

	llist_for_each_entry(lchan, &ts->lchans, list)
		if (lchan->type == chan)
			return lchan;

	return NULL;
}

int sched_trx_set_lchans(struct trx_ts *ts, uint8_t chan_nr, int active)
{
	const struct trx_lchan_desc *lchan_desc;
	struct trx_lchan_state *lchan;
	int rc = 0;

	/* Prevent NULL-pointer deference */
	if (ts == NULL) {
		LOGP(DSCH, LOGL_ERROR, "Timeslot isn't configured\n");
		return -EINVAL;
	}

	/* Iterate over all allocated lchans */
	llist_for_each_entry(lchan, &ts->lchans, list) {
		lchan_desc = &trx_lchan_desc[lchan->type];

		if (lchan_desc->chan_nr == (chan_nr & 0xf8)) {
			if (active)
				rc |= sched_trx_activate_lchan(ts, lchan->type);
			else
				rc |= sched_trx_deactivate_lchan(ts, lchan->type);
		}
	}

	return rc;
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

	LOGP(DSCH, LOGL_NOTICE, "Activating lchan=%s "
		"on ts=%d\n", trx_lchan_desc[chan].name, ts->index);

	/* Conditionally allocate memory for bursts */
	if (lchan_desc->rx_fn && lchan_desc->burst_buf_size > 0) {
		lchan->rx_bursts = talloc_zero_size(lchan,
			lchan_desc->burst_buf_size);
		if (lchan->rx_bursts == NULL)
			return -ENOMEM;
	}

	if (lchan_desc->tx_fn && lchan_desc->burst_buf_size > 0) {
		lchan->tx_bursts = talloc_zero_size(lchan,
			lchan_desc->burst_buf_size);
		if (lchan->tx_bursts == NULL)
			return -ENOMEM;
	}

	/* Finally, update channel status */
	lchan->active = 1;

	return 0;
}

static void sched_trx_reset_lchan(struct trx_lchan_state *lchan)
{
	/* Prevent NULL-pointer deference */
	OSMO_ASSERT(lchan != NULL);

	/* Reset internal state variables */
	lchan->rx_burst_mask = 0x00;
	lchan->tx_burst_mask = 0x00;
	lchan->rx_first_fn = 0;

	/* Free burst memory */
	talloc_free(lchan->rx_bursts);
	talloc_free(lchan->tx_bursts);

	lchan->rx_bursts = NULL;
	lchan->tx_bursts = NULL;

	/* Forget the current prim */
	sched_prim_drop(lchan);

	/* TCH specific variables */
	if (CHAN_IS_TCH(lchan->type)) {
		lchan->dl_ongoing_facch = 0;
		lchan->ul_ongoing_facch = 0;

		lchan->rsl_cmode = 0x00;
		lchan->tch_mode = 0x00;

		/* Reset AMR state */
		memset(&lchan->amr, 0x00, sizeof(lchan->amr));
	}

	/* Reset ciphering state */
	memset(&lchan->a5, 0x00, sizeof(lchan->a5));
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

	LOGP(DSCH, LOGL_DEBUG, "Deactivating lchan=%s "
		"on ts=%d\n", trx_lchan_desc[chan].name, ts->index);

	/* Reset internal state, free memory */
	sched_trx_reset_lchan(lchan);

	/* Update activation flag */
	lchan->active = 0;

	return 0;
}

void sched_trx_deactivate_all_lchans(struct trx_ts *ts)
{
	struct trx_lchan_state *lchan;

	LOGP(DSCH, LOGL_DEBUG, "Deactivating all logical channels "
		"on ts=%d\n", ts->index);

	llist_for_each_entry(lchan, &ts->lchans, list) {
		/* Omit inactive channels */
		if (!lchan->active)
			continue;

		/* Reset internal state, free memory */
		sched_trx_reset_lchan(lchan);

		/* Update activation flag */
		lchan->active = 0;
	}
}

enum gsm_phys_chan_config sched_trx_chan_nr2pchan_config(uint8_t chan_nr)
{
	uint8_t cbits = chan_nr >> 3;

	if (cbits == 0x01)
		return GSM_PCHAN_TCH_F;
	else if ((cbits & 0x1e) == 0x02)
		return GSM_PCHAN_TCH_H;
	else if ((cbits & 0x1c) == 0x04)
		return GSM_PCHAN_CCCH_SDCCH4;
	else if ((cbits & 0x18) == 0x08)
		return GSM_PCHAN_SDCCH8_SACCH8C;

	return GSM_PCHAN_NONE;
}

enum trx_lchan_type sched_trx_chan_nr2lchan_type(uint8_t chan_nr,
	uint8_t link_id)
{
	int i;

	/* Iterate over all known lchan types */
	for (i = 0; i < _TRX_CHAN_MAX; i++)
		if (trx_lchan_desc[i].chan_nr == (chan_nr & 0xf8))
			if (trx_lchan_desc[i].link_id == link_id)
				return i;

	return TRXC_IDLE;
}

static void sched_trx_a5_burst_dec(struct trx_lchan_state *lchan,
	uint32_t fn, sbit_t *burst)
{
	ubit_t ks[114];
	int i;

	/* Generate keystream for a DL burst */
	osmo_a5(lchan->a5.algo, lchan->a5.key, fn, ks, NULL);

	/* Apply keystream over ciphertext */
	for (i = 0; i < 57; i++) {
		if (ks[i])
			burst[i + 3] *= -1;
		if (ks[i + 57])
			burst[i + 88] *= -1;
	}
}

static void sched_trx_a5_burst_enc(struct trx_lchan_state *lchan,
	uint32_t fn, ubit_t *burst)
{
	ubit_t ks[114];
	int i;

	/* Generate keystream for an UL burst */
	osmo_a5(lchan->a5.algo, lchan->a5.key, fn, NULL, ks);

	/* Apply keystream over plaintext */
	for (i = 0; i < 57; i++) {
		burst[i + 3] ^= ks[i];
		burst[i + 88] ^= ks[i + 57];
	}
}

int sched_trx_handle_rx_burst(struct trx_instance *trx, uint8_t tn,
	uint32_t burst_fn, sbit_t *bits, uint16_t nbits,
	int8_t rssi, int16_t toa256)
{
	struct trx_lchan_state *lchan;
	const struct trx_frame *frame;
	struct trx_ts *ts;

	trx_lchan_rx_func *handler;
	enum trx_lchan_type chan;
	uint32_t fn, elapsed;
	uint8_t offset, bid;

	/* Check whether required timeslot is allocated and configured */
	ts = trx->ts_list[tn];
	if (ts == NULL || ts->mf_layout == NULL) {
		LOGP(DSCHD, LOGL_DEBUG, "TDMA timeslot #%u isn't configured, "
			"ignoring burst...\n", tn);
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
		if (lchan == NULL)
			goto next_frame;

		/* Ensure that channel is active */
		if (!lchan->active)
			goto next_frame;

		/* Reached current fn */
		if (fn == burst_fn) {
			/* Perform A5/X decryption if required */
			if (lchan->a5.algo)
				sched_trx_a5_burst_dec(lchan, fn, bits);

			/* Put burst to handler */
			handler(trx, ts, lchan, fn, bid, bits, rssi, toa256);
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

int sched_trx_handle_tx_burst(struct trx_instance *trx,
	struct trx_ts *ts, struct trx_lchan_state *lchan,
	uint32_t fn, ubit_t *bits)
{
	int rc;

	/* Perform A5/X burst encryption if required */
	if (lchan->a5.algo)
		sched_trx_a5_burst_enc(lchan, fn, bits);

	/* Forward burst to transceiver */
	rc = trx_if_tx_burst(trx, ts->index, fn, trx->tx_power, bits);
	if (rc) {
		LOGP(DSCHD, LOGL_ERROR, "Could not send burst to transceiver\n");
		return rc;
	}

	return 0;
}
