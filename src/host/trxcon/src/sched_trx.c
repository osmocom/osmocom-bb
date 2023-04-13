/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: GSM PHY routines
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

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

/* Logging categories to be used for common/data messages */
int l1sched_log_cat_common = DLGLOBAL;
int l1sched_log_cat_data = DLGLOBAL;

/* "Dummy" Measurement Report */
static const uint8_t meas_rep_dummy[] = {
	/* L1 SACCH pseudo-header */
	0x0f, 0x00,

	/* LAPDm header */
	0x01, 0x03, 0x49,

	/* RR Management messages, Measurement Report */
	0x06, 0x15,

	/* Measurement results (see 3GPP TS 44.018, section 10.5.2.20):
	 *   0... .... = BA-USED: 0
	 *   .0.. .... = DTX-USED: DTX was not used
	 *   ..11 0110 = RXLEV-FULL-SERVING-CELL: -57 <= x < -56 dBm (54)
	 *   0... .... = 3G-BA-USED: 0
	 *   .1.. .... = MEAS-VALID: The measurement results are not valid
	 *   ..11 0110 = RXLEV-SUB-SERVING-CELL: -57 <= x < -56 dBm (54)
	 *   0... .... = SI23_BA_USED: 0
	 *   .000 .... = RXQUAL-FULL-SERVING-CELL: BER < 0.2%, Mean value 0.14% (0)
	 *   .... 000. = RXQUAL-SUB-SERVING-CELL: BER < 0.2%, Mean value 0.14% (0)
	 *   .... ...1  11.. .... = NO-NCELL-M: Neighbour cell information not available */
	0x36, 0x76, 0x01, 0xc0,

	/* 0** -- Padding with zeroes */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static int l1sched_cfg_pchan_comb_req(struct l1sched_state *sched,
				      uint8_t tn, enum gsm_phys_chan_config pchan)
{
	const struct l1sched_config_req cr = {
		.type = L1SCHED_CFG_PCHAN_COMB,
		.pchan_comb = {
			.tn = tn,
			.pchan = pchan,
		},
	};

	return l1sched_handle_config_req(sched, &cr);
}

static void l1sched_a5_burst_enc(struct l1sched_lchan_state *lchan,
				 struct l1sched_burst_req *br);

/* Pull an Uplink burst from the scheduler and store it to br->burst[].
 * The TDMA Fn advance must be applied by the caller (if needed).
 * The given *br must be initialized by the caller. */
void l1sched_pull_burst(struct l1sched_state *sched, struct l1sched_burst_req *br)
{
	struct l1sched_ts *ts = sched->ts[br->tn];
	const struct l1sched_tdma_frame *frame;
	struct l1sched_lchan_state *lchan;
	l1sched_lchan_tx_func *handler;
	enum l1sched_lchan_type chan;
	unsigned int offset;

	/* Check if the given timeslot is configured */
	if (ts == NULL || ts->mf_layout == NULL)
		return;

	/* Get frame from multiframe */
	offset = br->fn % ts->mf_layout->period;
	frame = &ts->mf_layout->frames[offset];

	/* Get required info from frame */
	br->bid = frame->ul_bid;
	chan = frame->ul_chan;
	handler = l1sched_lchan_desc[chan].tx_fn;

	/* Omit lchans without handler */
	if (handler == NULL)
		return;

	/* Make sure that lchan is allocated and active */
	lchan = l1sched_find_lchan_by_type(ts, chan);
	if (lchan == NULL || !lchan->active)
		return;

	/* If no primitive is being processed, try obtaining one from Tx queue */
	if (lchan->prim == NULL)
		lchan->prim = l1sched_prim_dequeue(&ts->tx_prims, br->fn, lchan);
	if (lchan->prim == NULL) {
		/* If CBTX (Continuous Burst Transmission) is required */
		if (l1sched_lchan_desc[chan].flags & L1SCHED_CH_FLAG_CBTX)
			l1sched_prim_dummy(lchan);
		if (lchan->prim == NULL)
			return;
	}

	/* TODO: report TX buffers health to the higher layers */

	/* Handover RACH needs to be handled regardless of the
	 * current channel type and the associated handler. */
	if (L1SCHED_PRIM_IS_RACH(lchan->prim) && lchan->prim->chan != L1SCHED_RACH)
		handler = l1sched_lchan_desc[L1SCHED_RACH].tx_fn;

	/* Poke lchan handler */
	handler(lchan, br);

	/* Perform A5/X burst encryption if required */
	if (lchan->a5.algo)
		l1sched_a5_burst_enc(lchan, br);
}

/* Pull *and send* Uplink bursts for all timeslots and the current TDMA Fn. */
void l1sched_pull_send_frame(struct l1sched_state *sched)
{
	/* Advance TDMA frame number in order to give the transceiver
	 * more time to handle the burst before the actual transmission. */
	const uint32_t fn = GSM_TDMA_FN_SUM(sched->fn_counter_proc,
					    sched->fn_counter_advance);

	/* Iterate over timeslot list */
	for (unsigned int tn = 0; tn < ARRAY_SIZE(sched->ts); tn++) {
		struct l1sched_burst_req br = {
			.fn = fn,
			.tn = tn,
			.burst_len = 0, /* NOPE.ind */
		};

		l1sched_pull_burst(sched, &br);
		l1sched_handle_burst_req(sched, &br);
	}
}

void l1sched_logging_init(int log_cat_common, int log_cat_data)
{
	l1sched_log_cat_common = log_cat_common;
	l1sched_log_cat_data = log_cat_data;
}

struct l1sched_state *l1sched_alloc(void *ctx, const struct l1sched_cfg *cfg, void *priv)
{
	struct l1sched_state *sched;

	sched = talloc(ctx, struct l1sched_state);
	if (!sched)
		return NULL;

	*sched = (struct l1sched_state) {
		/* .clock_timer is set up in l1sched_clck_correct() */
		.fn_counter_advance = cfg->fn_advance,
		.priv = priv,
	};

	memcpy(&sched->sacch_cache[0], &meas_rep_dummy[0], sizeof(meas_rep_dummy));

	if (cfg->log_prefix == NULL)
		sched->log_prefix = talloc_asprintf(sched, "l1sched[0x%p]: ", sched);
	else
		sched->log_prefix = talloc_strdup(sched, cfg->log_prefix);

	return sched;
}

void l1sched_free(struct l1sched_state *sched)
{
	unsigned int tn;

	if (sched == NULL)
		return;

	LOGP_SCHEDC(sched, LOGL_NOTICE, "Shutdown scheduler\n");

	/* Free all potentially allocated timeslots */
	for (tn = 0; tn < ARRAY_SIZE(sched->ts); tn++)
		l1sched_del_ts(sched, tn);

	l1sched_clck_reset(sched);
	talloc_free(sched);
}

void l1sched_reset(struct l1sched_state *sched, bool reset_clock)
{
	unsigned int tn;

	if (sched == NULL)
		return;

	LOGP_SCHEDC(sched, LOGL_NOTICE, "Reset scheduler %s\n",
		    reset_clock ? "and clock counter" : "");

	/* Free all potentially allocated timeslots */
	for (tn = 0; tn < ARRAY_SIZE(sched->ts); tn++)
		l1sched_del_ts(sched, tn);

	/* Stop and reset clock counter if required */
	if (reset_clock)
		l1sched_clck_reset(sched);

	memcpy(&sched->sacch_cache[0], &meas_rep_dummy[0], sizeof(meas_rep_dummy));
}

struct l1sched_ts *l1sched_add_ts(struct l1sched_state *sched, int tn)
{
	/* Make sure that ts isn't allocated yet */
	if (sched->ts[tn] != NULL) {
		LOGP_SCHEDC(sched, LOGL_ERROR, "Timeslot #%u already allocated\n", tn);
		return NULL;
	}

	LOGP_SCHEDC(sched, LOGL_NOTICE, "Add a new TDMA timeslot #%u\n", tn);

	sched->ts[tn] = talloc_zero(sched, struct l1sched_ts);
	sched->ts[tn]->sched = sched;
	sched->ts[tn]->index = tn;

	return sched->ts[tn];
}

void l1sched_del_ts(struct l1sched_state *sched, int tn)
{
	struct l1sched_lchan_state *lchan, *lchan_next;
	struct l1sched_ts *ts;

	/* Find ts in list */
	ts = sched->ts[tn];
	if (ts == NULL)
		return;

	LOGP_SCHEDC(sched, LOGL_NOTICE, "Delete TDMA timeslot #%u\n", tn);

	/* Deactivate all logical channels */
	l1sched_deactivate_all_lchans(ts);

	/* Free channel states */
	llist_for_each_entry_safe(lchan, lchan_next, &ts->lchans, list) {
		llist_del(&lchan->list);
		talloc_free(lchan);
	}

	/* Flush queue primitives for TX */
	l1sched_prim_flush_queue(&ts->tx_prims);

	/* Remove ts from list and free memory */
	sched->ts[tn] = NULL;
	talloc_free(ts);

	/* Notify transceiver about that */
	l1sched_cfg_pchan_comb_req(sched, tn, GSM_PCHAN_NONE);
}

#define LAYOUT_HAS_LCHAN(layout, lchan) \
	(layout->lchan_mask & ((uint64_t) 0x01 << lchan))

int l1sched_configure_ts(struct l1sched_state *sched, int tn,
			 enum gsm_phys_chan_config config)
{
	struct l1sched_lchan_state *lchan;
	enum l1sched_lchan_type type;
	struct l1sched_ts *ts;

	/* Try to find specified ts */
	ts = sched->ts[tn];
	if (ts != NULL) {
		/* Reconfiguration of existing one */
		l1sched_reset_ts(sched, tn);
	} else {
		/* Allocate a new one if doesn't exist */
		ts = l1sched_add_ts(sched, tn);
		if (ts == NULL)
			return -ENOMEM;
	}

	/* Choose proper multiframe layout */
	ts->mf_layout = l1sched_mframe_layout(config, tn);
	if (!ts->mf_layout)
		return -EINVAL;
	if (ts->mf_layout->chan_config != config)
		return -EINVAL;

	LOGP_SCHEDC(sched, LOGL_NOTICE,
		    "(Re)configure TDMA timeslot #%u as %s\n",
		    tn, ts->mf_layout->name);

	/* Init queue primitives for TX */
	INIT_LLIST_HEAD(&ts->tx_prims);
	/* Init logical channels list */
	INIT_LLIST_HEAD(&ts->lchans);

	/* Allocate channel states */
	for (type = 0; type < _L1SCHED_CHAN_MAX; type++) {
		if (!LAYOUT_HAS_LCHAN(ts->mf_layout, type))
			continue;

		/* Allocate a channel state */
		lchan = talloc_zero(ts, struct l1sched_lchan_state);
		if (!lchan)
			return -ENOMEM;

		/* set backpointer */
		lchan->ts = ts;

		/* Set channel type */
		lchan->type = type;

		/* Add to the list of channel states */
		llist_add_tail(&lchan->list, &ts->lchans);

		/* Enable channel automatically if required */
		if (l1sched_lchan_desc[type].flags & L1SCHED_CH_FLAG_AUTO)
			l1sched_activate_lchan(ts, type);
	}

	/* Notify transceiver about TS activation */
	l1sched_cfg_pchan_comb_req(sched, tn, config);

	return 0;
}

int l1sched_reset_ts(struct l1sched_state *sched, int tn)
{
	struct l1sched_lchan_state *lchan, *lchan_next;
	struct l1sched_ts *ts;

	/* Try to find specified ts */
	ts = sched->ts[tn];
	if (ts == NULL)
		return -EINVAL;

	/* Undefine multiframe layout */
	ts->mf_layout = NULL;

	/* Flush queue primitives for TX */
	l1sched_prim_flush_queue(&ts->tx_prims);

	/* Deactivate all logical channels */
	l1sched_deactivate_all_lchans(ts);

	/* Free channel states */
	llist_for_each_entry_safe(lchan, lchan_next, &ts->lchans, list) {
		llist_del(&lchan->list);
		talloc_free(lchan);
	}

	/* Notify transceiver about that */
	l1sched_cfg_pchan_comb_req(sched, tn, GSM_PCHAN_NONE);

	return 0;
}

int l1sched_start_ciphering(struct l1sched_ts *ts, uint8_t algo,
			    const uint8_t *key, uint8_t key_len)
{
	struct l1sched_lchan_state *lchan;

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

struct l1sched_lchan_state *l1sched_find_lchan_by_type(struct l1sched_ts *ts,
						       enum l1sched_lchan_type type)
{
	struct l1sched_lchan_state *lchan;

	llist_for_each_entry(lchan, &ts->lchans, list)
		if (lchan->type == type)
			return lchan;

	return NULL;
}

int l1sched_set_lchans(struct l1sched_ts *ts, uint8_t chan_nr,
		       int active, uint8_t tch_mode, uint8_t tsc)
{
	const struct l1sched_lchan_desc *lchan_desc;
	struct l1sched_lchan_state *lchan;
	int rc = 0;

	/* Prevent NULL-pointer deference */
	OSMO_ASSERT(ts != NULL);

	/* Iterate over all allocated lchans */
	llist_for_each_entry(lchan, &ts->lchans, list) {
		lchan_desc = &l1sched_lchan_desc[lchan->type];

		if (lchan_desc->chan_nr == (chan_nr & 0xf8)) {
			if (active) {
				rc |= l1sched_activate_lchan(ts, lchan->type);
				lchan->tch_mode = tch_mode;
				lchan->tsc = tsc;
			} else
				rc |= l1sched_deactivate_lchan(ts, lchan->type);
		}
	}

	return rc;
}

int l1sched_lchan_set_amr_cfg(struct l1sched_lchan_state *lchan,
			      uint8_t codecs_bitmask, uint8_t start_codec)
{
	int n = 0;
	int acum = 0;
	int pos;

	while ((pos = ffs(codecs_bitmask)) != 0) {
		acum += pos;
		LOGP_LCHANC(lchan, LOGL_DEBUG, "AMR codec[%u] = %u\n", n, acum - 1);
		lchan->amr.codec[n++] = acum - 1;
		codecs_bitmask >>= pos;
	}
	if (n == 0) {
		LOGP_LCHANC(lchan, LOGL_ERROR, "Empty AMR codec mode bitmask!\n");
		return -EINVAL;
	}

	lchan->amr.codecs = n;
	lchan->amr.dl_ft = start_codec;
	lchan->amr.dl_cmr = start_codec;
	lchan->amr.ul_ft = start_codec;
	lchan->amr.ul_cmr = start_codec;

	return 0;
}

int l1sched_activate_lchan(struct l1sched_ts *ts, enum l1sched_lchan_type chan)
{
	const struct l1sched_lchan_desc *lchan_desc = &l1sched_lchan_desc[chan];
	struct l1sched_lchan_state *lchan;

	/* Try to find requested logical channel */
	lchan = l1sched_find_lchan_by_type(ts, chan);
	if (lchan == NULL)
		return -EINVAL;

	if (lchan->active) {
		LOGP_LCHANC(lchan, LOGL_ERROR, "is already activated\n");
		return -EINVAL;
	}

	LOGP_LCHANC(lchan, LOGL_NOTICE, "activating\n");

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

static void l1sched_reset_lchan(struct l1sched_lchan_state *lchan)
{
	/* Prevent NULL-pointer deference */
	OSMO_ASSERT(lchan != NULL);

	/* Print some TDMA statistics for Downlink */
	if (l1sched_lchan_desc[lchan->type].rx_fn && lchan->active) {
		LOGP_LCHANC(lchan, LOGL_DEBUG, "TDMA statistics: "
			    "%lu DL frames have been processed, "
			    "%lu lost (compensated), last fn=%u\n",
			    lchan->tdma.num_proc,
			    lchan->tdma.num_lost,
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
	l1sched_lchan_prim_drop(lchan);

	/* Channel specific stuff */
	if (L1SCHED_CHAN_IS_TCH(lchan->type)) {
		lchan->dl_ongoing_facch = 0;
		lchan->ul_facch_blocks = 0;

		lchan->tch_mode = GSM48_CMODE_SIGN;

		/* Reset AMR state */
		memset(&lchan->amr, 0x00, sizeof(lchan->amr));
	} else if (L1SCHED_CHAN_IS_SACCH(lchan->type)) {
		/* Reset SACCH state */
		memset(&lchan->sacch, 0x00, sizeof(lchan->sacch));
	}

	/* Reset ciphering state */
	memset(&lchan->a5, 0x00, sizeof(lchan->a5));

	/* Reset TDMA frame statistics */
	memset(&lchan->tdma, 0x00, sizeof(lchan->tdma));
}

int l1sched_deactivate_lchan(struct l1sched_ts *ts, enum l1sched_lchan_type chan)
{
	struct l1sched_lchan_state *lchan;

	/* Try to find requested logical channel */
	lchan = l1sched_find_lchan_by_type(ts, chan);
	if (lchan == NULL)
		return -EINVAL;

	if (!lchan->active) {
		LOGP_LCHANC(lchan, LOGL_ERROR, "is already deactivated\n");
		return -EINVAL;
	}

	LOGP_LCHANC(lchan, LOGL_DEBUG, "deactivating\n");

	/* Reset internal state, free memory */
	l1sched_reset_lchan(lchan);

	/* Update activation flag */
	lchan->active = 0;

	return 0;
}

void l1sched_deactivate_all_lchans(struct l1sched_ts *ts)
{
	struct l1sched_lchan_state *lchan;

	LOGP_SCHEDC(ts->sched, LOGL_DEBUG,
		    "Deactivating all logical channels on ts=%d\n",
		    ts->index);

	llist_for_each_entry(lchan, &ts->lchans, list) {
		/* Omit inactive channels */
		if (!lchan->active)
			continue;

		/* Reset internal state, free memory */
		l1sched_reset_lchan(lchan);

		/* Update activation flag */
		lchan->active = 0;
	}
}

enum gsm_phys_chan_config l1sched_chan_nr2pchan_config(uint8_t chan_nr)
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

enum l1sched_lchan_type l1sched_chan_nr2lchan_type(uint8_t chan_nr,
	uint8_t link_id)
{
	int i;

	/* Iterate over all known lchan types */
	for (i = 0; i < _L1SCHED_CHAN_MAX; i++)
		if (l1sched_lchan_desc[i].chan_nr == (chan_nr & 0xf8))
			if (l1sched_lchan_desc[i].link_id == link_id)
				return i;

	return L1SCHED_IDLE;
}

static void l1sched_a5_burst_dec(struct l1sched_lchan_state *lchan,
				 struct l1sched_burst_ind *bi)
{
	ubit_t ks[114];
	int i;

	/* Generate keystream for a DL burst */
	osmo_a5(lchan->a5.algo, lchan->a5.key, bi->fn, ks, NULL);

	/* Apply keystream over ciphertext */
	for (i = 0; i < 57; i++) {
		if (ks[i])
			bi->burst[i + 3] *= -1;
		if (ks[i + 57])
			bi->burst[i + 88] *= -1;
	}
}

static void l1sched_a5_burst_enc(struct l1sched_lchan_state *lchan,
				 struct l1sched_burst_req *br)
{
	ubit_t ks[114];
	int i;

	/* Generate keystream for an UL burst */
	osmo_a5(lchan->a5.algo, lchan->a5.key, br->fn, NULL, ks);

	/* Apply keystream over plaintext */
	for (i = 0; i < 57; i++) {
		br->burst[i + 3] ^= ks[i];
		br->burst[i + 88] ^= ks[i + 57];
	}
}

static int subst_frame_loss(struct l1sched_lchan_state *lchan,
			    l1sched_lchan_rx_func *handler,
			    uint32_t fn)
{
	const struct l1sched_tdma_multiframe *mf;
	const struct l1sched_tdma_frame *fp;
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
		LOGP_LCHAND(lchan, LOGL_ERROR, "Rx burst with fn=%u older than the last "
			    "processed fn=%u (see OS#4658) => dropping\n",
			    fn, lchan->tdma.last_proc);
		return -EALREADY;
	}

	/* Check how many frames we (potentially) need to compensate */
	if (elapsed > mf->period) {
		LOGP_LCHANC(lchan, LOGL_NOTICE,
			    "Too many (>%u) contiguous TDMA frames elapsed (%d) "
			    "since the last processed fn=%u (current %u)\n",
			    mf->period, elapsed, lchan->tdma.last_proc, fn);
		return -EIO;
	} else if (elapsed == 0) {
		LOGP_LCHANC(lchan, LOGL_ERROR,
			    "No TDMA frames elapsed since the last processed "
			    "fn=%u, must be a bug?\n", lchan->tdma.last_proc);
		return -EIO;
	}

	struct l1sched_burst_ind bi = {
		.fn = lchan->tdma.last_proc,
		.tn = lchan->ts->index,
		.toa256 = 0,
		.rssi = -120,
		.burst = { 0 },
		.burst_len = GSM_NBITS_NB_GMSK_BURST,
	};

	/* Traverse from fp till the current frame */
	for (i = 0; i < elapsed - 1; i++) {
		fp = &mf->frames[GSM_TDMA_FN_INC(bi.fn) % mf->period];
		if (fp->dl_chan != lchan->type)
			continue;

		LOGP_LCHANC(lchan, LOGL_NOTICE,
			    "Substituting lost TDMA frame fn=%u\n", bi.fn);

		bi.bid = fp->dl_bid;
		handler(lchan, &bi);

		/* Update TDMA frame statistics */
		lchan->tdma.last_proc = bi.fn;
		lchan->tdma.num_proc++;
		lchan->tdma.num_lost++;
	}

	return 0;
}

int l1sched_handle_rx_burst(struct l1sched_state *sched,
			    struct l1sched_burst_ind *bi)
{
	struct l1sched_lchan_state *lchan;
	const struct l1sched_tdma_frame *frame;
	struct l1sched_ts *ts = sched->ts[bi->tn];

	l1sched_lchan_rx_func *handler;
	enum l1sched_lchan_type chan;
	uint8_t offset;
	int rc;

	/* Check whether required timeslot is allocated and configured */
	if (ts == NULL || ts->mf_layout == NULL) {
		LOGP_SCHEDD(sched, LOGL_DEBUG,
			    "Timeslot #%u isn't configured, ignoring burst...\n", bi->tn);
		return -EINVAL;
	}

	/* Get frame from multiframe */
	offset = bi->fn % ts->mf_layout->period;
	frame = ts->mf_layout->frames + offset;

	/* Get required info from frame */
	bi->bid = frame->dl_bid;
	chan = frame->dl_chan;
	handler = l1sched_lchan_desc[chan].rx_fn;

	/* Omit bursts which have no handler, like IDLE bursts.
	 * TODO: handle noise indications during IDLE frames. */
	if (!handler)
		return -ENODEV;

	/* Find required channel state */
	lchan = l1sched_find_lchan_by_type(ts, chan);
	if (lchan == NULL)
		return -ENODEV;

	/* Ensure that channel is active */
	if (!lchan->active)
		return 0;

	/* Compensate lost TDMA frames (if any) */
	rc = subst_frame_loss(lchan, handler, bi->fn);
	if (rc == -EALREADY)
		return rc;

	/* Perform A5/X decryption if required */
	if (lchan->a5.algo)
		l1sched_a5_burst_dec(lchan, bi);

	/* Put burst to handler */
	handler(lchan, bi);

	/* Update TDMA frame statistics */
	lchan->tdma.last_proc = bi->fn;

	if (++lchan->tdma.num_proc == 0) {
		/* Theoretically, we may have an integer overflow of num_proc counter.
		 * As a consequence, subst_frame_loss() will be unable to compensate
		 * one (potentionally lost) Downlink burst. On practice, it would
		 * happen once in 4615 * 10e-6 * (2 ^ 32 - 1) seconds or ~6 years. */
		LOGP_LCHAND(lchan, LOGL_NOTICE,
			    "Too many TDMA frames have been processed. "
			    "Are you running trxcon for more than 6 years?!?\n");
		lchan->tdma.num_proc = 1;
	}

	return 0;
}

int l1sched_handle_rx_probe(struct l1sched_state *sched,
			    struct l1sched_probe *probe)
{
	struct l1sched_ts *ts = sched->ts[probe->tn];
	const struct l1sched_tdma_frame *frame;
	struct l1sched_lchan_state *lchan;
	unsigned int offset;

	/* Check whether required timeslot is allocated and configured */
	if (ts == NULL || ts->mf_layout == NULL)
		return -EINVAL;

	/* Get frame from multiframe */
	offset = probe->fn % ts->mf_layout->period;
	frame = &ts->mf_layout->frames[offset];

	if (l1sched_lchan_desc[frame->dl_chan].rx_fn == NULL)
		return -ENODEV;

	/* Find the appropriate logical channel */
	lchan = l1sched_find_lchan_by_type(ts, frame->dl_chan);
	if (lchan == NULL)
		return -ENODEV;

	if (lchan->active)
		probe->flags |= L1SCHED_PROBE_F_ACTIVE;

	return 0;
}

#define MEAS_HIST_FIRST(hist) \
	(&hist->buf[0])
#define MEAS_HIST_LAST(hist) \
	(MEAS_HIST_FIRST(hist) + ARRAY_SIZE(hist->buf) - 1)

/* Add a new set of measurements to the history */
void l1sched_lchan_meas_push(struct l1sched_lchan_state *lchan,
			     const struct l1sched_burst_ind *bi)
{
	struct l1sched_lchan_meas_hist *hist = &lchan->meas_hist;

	/* Find a new position where to store the measurements */
	if (hist->head == MEAS_HIST_LAST(hist) || hist->head == NULL)
		hist->head = MEAS_HIST_FIRST(hist);
	else
		hist->head++;

	*hist->head = (struct l1sched_meas_set) {
		.fn = bi->fn,
		.toa256 = bi->toa256,
		.rssi = bi->rssi,
	};
}

/* Calculate the AVG of n measurements from the history */
void l1sched_lchan_meas_avg(struct l1sched_lchan_state *lchan, unsigned int n)
{
	struct l1sched_lchan_meas_hist *hist = &lchan->meas_hist;
	struct l1sched_meas_set *meas = hist->head;
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
