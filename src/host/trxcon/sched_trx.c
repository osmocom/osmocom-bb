/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: GSM PHY routines
 *
 * (C) 2017-2019 by Vadim Yanitskiy <axilirator@gmail.com>
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
#include <stdbool.h>

#include <osmocom/gsm/a5.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/linuxlist.h>

#include "l1ctl_proto.h"
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
	int i;

	/* Advance TDMA frame number in order to give the transceiver
	 * more time to handle the burst before the actual transmission. */
	const uint32_t fn = GSM_TDMA_FN_SUM(sched->fn_counter_proc,
					    sched->fn_counter_advance);

	/* Iterate over timeslot list */
	for (i = 0; i < TRX_TS_COUNT; i++) {
		/* Timeslot is not allocated */
		ts = trx->ts_list[i];
		if (ts == NULL)
			continue;

		/* Timeslot is not configured */
		if (ts->mf_layout == NULL)
			continue;

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
			lchan->prim = sched_prim_dequeue(&ts->tx_prims, fn, lchan);

		/* TODO: report TX buffers health to the higher layers */

		/* If CBTX (Continuous Burst Transmission) is assumed */
		if (trx_lchan_desc[chan].flags & TRX_CH_FLAG_CBTX) {
			/**
			 * Probably, a TX buffer is empty. Nevertheless,
			 * we shall continuously transmit anything on
			 * CBTX channels.
			 */
			if (lchan->prim == NULL)
				sched_prim_dummy(lchan);
		}

		/* If there is no primitive, do nothing */
		if (lchan->prim == NULL)
			continue;

		/* Handover RACH needs to be handled regardless of the
		 * current channel type and the associated handler. */
		if (PRIM_IS_RACH(lchan->prim) && lchan->prim->chan != TRXC_RACH)
			handler = trx_lchan_desc[TRXC_RACH].tx_fn;

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

int sched_trx_reset(struct trx_instance *trx, bool reset_clock)
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

	/* Add backpointer */
	trx->ts_list[tn]->trx = trx;

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
	if (!ts->mf_layout)
		return -EINVAL;
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

		/* set backpointer */
		lchan->ts = ts;

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

int sched_trx_set_lchans(struct trx_ts *ts, uint8_t chan_nr, int active, uint8_t tch_mode)
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
			if (active) {
				rc |= sched_trx_activate_lchan(ts, lchan->type);
				lchan->tch_mode = tch_mode;
			} else
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

	/* Print some TDMA statistics for Downlink */
	if (trx_lchan_desc[lchan->type].rx_fn && lchan->active) {
		LOGP(DSCH, LOGL_DEBUG, "TDMA statistics for lchan=%s on ts=%u: "
				       "%lu DL frames have been processed, "
				       "%lu lost (compensated), last fn=%u\n",
		     trx_lchan_desc[lchan->type].name, lchan->ts->index,
		     lchan->tdma.num_proc, lchan->tdma.num_lost,
		     lchan->tdma.last_proc);
	}

	/* Reset internal state variables */
	lchan->rx_burst_mask = 0x00;
	lchan->tx_burst_mask = 0x00;

	/* Free burst memory */
	talloc_free(lchan->rx_bursts);
	talloc_free(lchan->tx_bursts);

	lchan->rx_bursts = NULL;
	lchan->tx_bursts = NULL;

	/* Forget the current prim */
	sched_prim_drop(lchan);

	/* Channel specific stuff */
	if (CHAN_IS_TCH(lchan->type)) {
		lchan->dl_ongoing_facch = 0;
		lchan->ul_facch_blocks = 0;

		lchan->tch_mode = GSM48_CMODE_SIGN;

		/* Reset AMR state */
		memset(&lchan->amr, 0x00, sizeof(lchan->amr));
	} else if (CHAN_IS_SACCH(lchan->type)) {
		/* Reset SACCH state */
		memset(&lchan->sacch, 0x00, sizeof(lchan->sacch));
	}

	/* Reset ciphering state */
	memset(&lchan->a5, 0x00, sizeof(lchan->a5));

	/* Reset TDMA frame statistics */
	memset(&lchan->tdma, 0x00, sizeof(lchan->tdma));
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

	if (cbits == ABIS_RSL_CHAN_NR_CBITS_Bm_ACCHs)
		return GSM_PCHAN_TCH_F;
	else if ((cbits & 0x1e) == ABIS_RSL_CHAN_NR_CBITS_Lm_ACCHs(0))
		return GSM_PCHAN_TCH_H;
	else if ((cbits & 0x1c) == ABIS_RSL_CHAN_NR_CBITS_SDCCH4_ACCH(0))
		return GSM_PCHAN_CCCH_SDCCH4;
	else if ((cbits & 0x18) == ABIS_RSL_CHAN_NR_CBITS_SDCCH8_ACCH(0))
		return GSM_PCHAN_SDCCH8_SACCH8C;
	else if ((cbits & 0x1f) == ABIS_RSL_CHAN_NR_CBITS_OSMO_CBCH4)
		return GSM_PCHAN_CCCH_SDCCH4_CBCH;
	else if ((cbits & 0x1f) == ABIS_RSL_CHAN_NR_CBITS_OSMO_CBCH8)
		return GSM_PCHAN_SDCCH8_SACCH8C_CBCH;
	else if ((cbits & 0x1f) == ABIS_RSL_CHAN_NR_CBITS_OSMO_PDCH)
		return GSM_PCHAN_PDCH;

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

static int subst_frame_loss(struct trx_lchan_state *lchan,
			    trx_lchan_rx_func *handler,
			    uint32_t fn)
{
	const struct trx_multiframe *mf;
	const struct trx_frame *fp;
	int elapsed, i;

	/* Wait until at least one TDMA frame is processed */
	if (lchan->tdma.num_proc == 0)
		return -EAGAIN;

	/* Short alias for the current multiframe */
	mf = lchan->ts->mf_layout;

	/* Calculate how many frames elapsed since the last received one.
	 * The algorithm is based on GSM::FNDelta() from osmo-trx. */
	elapsed = fn - lchan->tdma.last_proc;
	if (elapsed >= GSM_TDMA_HYPERFRAME / 2)
		elapsed -= GSM_TDMA_HYPERFRAME;
	else if (elapsed < -GSM_TDMA_HYPERFRAME / 2)
		elapsed += GSM_TDMA_HYPERFRAME;

	/* Check TDMA frame order (wrong order is possible with fake_trx.py, see OS#4658) */
	if (elapsed < 0) {
		/* This burst has already been substituted by a dummy burst (all bits set to zero),
		 * so better drop it. Otherwise we risk to get undefined behavior in handler(). */
		LOGP(DSCHD, LOGL_ERROR, "(%s) Rx burst with fn=%u older than the last "
					"processed fn=%u (see OS#4658) => dropping\n",
					trx_lchan_desc[lchan->type].name,
					fn, lchan->tdma.last_proc);
		return -EALREADY;
	}

	/* Check how many frames we (potentially) need to compensate */
	if (elapsed > mf->period) {
		LOGP(DSCHD, LOGL_NOTICE, "Too many (>%u) contiguous TDMA frames elapsed (%d) "
					 "since the last processed fn=%u (current %u)\n",
					 mf->period, elapsed, lchan->tdma.last_proc, fn);
		return -EIO;
	} else if (elapsed == 0) {
		LOGP(DSCHD, LOGL_ERROR, "No TDMA frames elapsed since the last processed "
					"fn=%u, must be a bug?\n", lchan->tdma.last_proc);
		return -EIO;
	}

	static const sbit_t bits[148] = { 0 };
	struct trx_meas_set fake_meas = {
		.fn = lchan->tdma.last_proc,
		.rssi = -120,
		.toa256 = 0,
	};

	/* Traverse from fp till the current frame */
	for (i = 0; i < elapsed - 1; i++) {
		fp = &mf->frames[GSM_TDMA_FN_INC(fake_meas.fn) % mf->period];
		if (fp->dl_chan != lchan->type)
			continue;

		LOGP(DSCHD, LOGL_NOTICE, "Substituting lost TDMA frame %u on %s\n",
		     fake_meas.fn, trx_lchan_desc[lchan->type].name);

		handler(lchan->ts->trx, lchan->ts, lchan,
			fake_meas.fn, fp->dl_bid,
			bits, &fake_meas);

		/* Update TDMA frame statistics */
		lchan->tdma.last_proc = fake_meas.fn;
		lchan->tdma.num_proc++;
		lchan->tdma.num_lost++;
	}

	return 0;
}

int sched_trx_handle_rx_burst(struct trx_instance *trx, uint8_t tn,
	uint32_t fn, sbit_t *bits, uint16_t nbits,
	const struct trx_meas_set *meas)
{
	struct trx_lchan_state *lchan;
	const struct trx_frame *frame;
	struct trx_ts *ts;

	trx_lchan_rx_func *handler;
	enum trx_lchan_type chan;
	uint8_t offset, bid;
	int rc;

	/* Check whether required timeslot is allocated and configured */
	ts = trx->ts_list[tn];
	if (ts == NULL || ts->mf_layout == NULL) {
		LOGP(DSCHD, LOGL_DEBUG, "TDMA timeslot #%u isn't configured, "
			"ignoring burst...\n", tn);
		return -EINVAL;
	}

	/* Get frame from multiframe */
	offset = fn % ts->mf_layout->period;
	frame = ts->mf_layout->frames + offset;

	/* Get required info from frame */
	bid = frame->dl_bid;
	chan = frame->dl_chan;
	handler = trx_lchan_desc[chan].rx_fn;

	/* Omit bursts which have no handler, like IDLE bursts.
	 * TODO: handle noise indications during IDLE frames. */
	if (!handler)
		return -ENODEV;

	/* Find required channel state */
	lchan = sched_trx_find_lchan(ts, chan);
	if (lchan == NULL)
		return -ENODEV;

	/* Ensure that channel is active */
	if (!lchan->active)
		return 0;

	/* Compensate lost TDMA frames (if any) */
	rc = subst_frame_loss(lchan, handler, fn);
	if (rc == -EALREADY)
		return rc;

	/* Perform A5/X decryption if required */
	if (lchan->a5.algo)
		sched_trx_a5_burst_dec(lchan, fn, bits);

	/* Put burst to handler */
	handler(trx, ts, lchan, fn, bid, bits, meas);

	/* Update TDMA frame statistics */
	lchan->tdma.last_proc = fn;

	if (++lchan->tdma.num_proc == 0) {
		/* Theoretically, we may have an integer overflow of num_proc counter.
		 * As a consequence, subst_frame_loss() will be unable to compensate
		 * one (potentionally lost) Downlink burst. On practice, it would
		 * happen once in 4615 * 10e-6 * (2 ^ 32 - 1) seconds or ~6 years. */
		LOGP(DSCHD, LOGL_NOTICE, "Too many TDMA frames have been processed. "
					 "Are you running trxcon for more than 6 years?!?\n");
		lchan->tdma.num_proc = 1;
	}

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

#define MEAS_HIST_FIRST(hist) \
	(&hist->buf[0])
#define MEAS_HIST_LAST(hist) \
	(MEAS_HIST_FIRST(hist) + ARRAY_SIZE(hist->buf) - 1)

/* Add a new set of measurements to the history */
void sched_trx_meas_push(struct trx_lchan_state *lchan, const struct trx_meas_set *meas)
{
	struct trx_lchan_meas_hist *hist = &lchan->meas_hist;

	/* Find a new position where to store the measurements */
	if (hist->head == MEAS_HIST_LAST(hist) || hist->head == NULL)
		hist->head = MEAS_HIST_FIRST(hist);
	else
		hist->head++;

	*hist->head = *meas;
}

/* Calculate the AVG of n measurements from the history */
void sched_trx_meas_avg(struct trx_lchan_state *lchan, unsigned int n)
{
	struct trx_lchan_meas_hist *hist = &lchan->meas_hist;
	struct trx_meas_set *meas = hist->head;
	int toa256_sum = 0;
	int rssi_sum = 0;
	int i;

	OSMO_ASSERT(n > 0 && n <= ARRAY_SIZE(hist->buf));
	OSMO_ASSERT(meas != NULL);

	/* Traverse backwards up to n entries, calculate the sum */
	for (i = 0; i < n; i++) {
		toa256_sum += meas->toa256;
		rssi_sum += meas->rssi;

		/* Do not go below the first burst */
		if (i + 1 == n)
			break;

		if (meas == MEAS_HIST_FIRST(hist))
			meas = MEAS_HIST_LAST(hist);
		else
			meas--;
	}

	/* Calculate the AVG */
	lchan->meas_avg.toa256 = toa256_sum / n;
	lchan->meas_avg.rssi = rssi_sum / n;

	/* As a bonus, store TDMA frame number of the first burst */
	lchan->meas_avg.fn = meas->fn;
}
