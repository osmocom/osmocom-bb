/*
 * OsmocomBB <-> SDR connection bridge
 *
 * (C) 2022 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>

#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/trxcon_fsm.h>
#include <osmocom/bb/trxcon/phyif.h>
#include <osmocom/bb/trxcon/logging.h>
#include <osmocom/bb/trxcon/l1ctl.h>
#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

#define S(x)	(1 << (x))

static void trxcon_allstate_action(struct osmo_fsm_inst *fi,
				   uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;
	struct phyif_cmd phycmd = { };

	switch (event) {
	case TRXCON_EV_PHYIF_FAILURE:
		trxcon->phyif = NULL;
		osmo_fsm_inst_term(fi, OSMO_FSM_TERM_ERROR, NULL);
		break;
	case TRXCON_EV_L2IF_FAILURE:
		trxcon->l2if = NULL;
		osmo_fsm_inst_term(fi, OSMO_FSM_TERM_ERROR, NULL);
		break;
	case TRXCON_EV_RESET_FULL_REQ:
		TALLOC_FREE(trxcon->fi_data);
		if (fi->state != TRXCON_ST_RESET)
			osmo_fsm_inst_state_chg(fi, TRXCON_ST_RESET, 0, 0);
		l1sched_reset(trxcon->sched, true);

		phycmd.type = PHYIF_CMDT_RESET;
		phyif_handle_cmd(trxcon->phyif, &phycmd);
		break;
	case TRXCON_EV_RESET_SCHED_REQ:
		l1sched_reset(trxcon->sched, false);
		break;
	case TRXCON_EV_SET_PHY_CONFIG_REQ:
	{
		const struct trxcon_param_set_phy_config_req *req = data;

		switch (req->type) {
		case TRXCON_PHY_CFGT_PCHAN_COMB:
			phycmd.type = PHYIF_CMDT_SETSLOT;
			phycmd.param.setslot.tn = req->pchan_comb.tn;
			phycmd.param.setslot.pchan = req->pchan_comb.pchan;
			phyif_handle_cmd(trxcon->phyif, &phycmd);
			break;
		case TRXCON_PHY_CFGT_TX_PARAMS:
			if (trxcon->l1p.ta != req->tx_params.timing_advance) {
				phycmd.type = PHYIF_CMDT_SETTA;
				phycmd.param.setta.ta = req->tx_params.timing_advance;
				phyif_handle_cmd(trxcon->phyif, &phycmd);
			}
			trxcon->l1p.tx_power = req->tx_params.tx_power;
			trxcon->l1p.ta = req->tx_params.timing_advance;
			break;
		}
		break;
	}
	case TRXCON_EV_UPDATE_SACCH_CACHE_REQ:
	{
		const struct trxcon_param_tx_data_req *req = data;

		if (req->link_id != L1SCHED_CH_LID_SACCH) {
			LOGPFSML(fi, LOGL_ERROR, "Unexpected link_id=0x%02x\n", req->link_id);
			break;
		}
		if (req->data_len != GSM_MACBLOCK_LEN) {
			LOGPFSML(fi, LOGL_ERROR, "Unexpected data length=%zu\n", req->data_len);
			break;
		}

		memcpy(&trxcon->sched->sacch_cache[0], req->data, req->data_len);
		break;
	}
	default:
		OSMO_ASSERT(0);
	}
}

static int trxcon_timer_cb(struct osmo_fsm_inst *fi)
{
	struct trxcon_inst *trxcon = fi->priv;

	switch (fi->state) {
	case TRXCON_ST_FBSB_SEARCH:
		l1ctl_tx_fbsb_fail(trxcon, trxcon->l1p.band_arfcn);
		osmo_fsm_inst_state_chg(fi, TRXCON_ST_RESET, 0, 0);
		return 0;
	default:
		OSMO_ASSERT(0);
	}
}

static void handle_full_power_scan_req(struct osmo_fsm_inst *fi,
				       const struct trxcon_param_full_power_scan_req *req)
{
	struct trxcon_inst *trxcon = fi->priv;
	enum gsm_band band_start, band_stop;

	if (trxcon->fi_data != NULL) {
		LOGPFSML(fi, LOGL_ERROR, "Full power scan is already in progress\n");
		return;
	}

	/* The start and stop ARFCNs must be in the same band */
	if (gsm_arfcn2band_rc(req->band_arfcn_start, &band_start) != 0 ||
	    gsm_arfcn2band_rc(req->band_arfcn_stop, &band_stop) != 0 ||
	    band_start != band_stop) {
		LOGPFSML(fi, LOGL_ERROR, "Full power scan request has invalid params\n");
		return;
	}

	trxcon->fi_data = talloc_memdup(fi, req, sizeof(*req));
	OSMO_ASSERT(trxcon->fi_data != NULL);

	/* trxcon_st_full_power_scan_onenter() sends the initial PHYIF_CMDT_MEASURE */
	osmo_fsm_inst_state_chg(fi, TRXCON_ST_FULL_POWER_SCAN, 0, 0); /* TODO: timeout */
}

static void trxcon_st_reset_action(struct osmo_fsm_inst *fi,
				   uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;

	switch (event) {
	case TRXCON_EV_FBSB_SEARCH_REQ:
	{
		const struct trxcon_param_fbsb_search_req *req = data;

		osmo_fsm_inst_state_chg_ms(fi, TRXCON_ST_FBSB_SEARCH, req->timeout_ms, 0);

		l1sched_configure_ts(trxcon->sched, 0, req->pchan_config);

		/* Only if current ARFCN differs */
		if (trxcon->l1p.band_arfcn != req->band_arfcn) {
			const struct phyif_cmd phycmd = {
				.type = PHYIF_CMDT_SETFREQ_H0,
				.param.setfreq_h0 = {
					.band_arfcn = req->band_arfcn,
				},
			};

			/* Update current ARFCN */
			trxcon->l1p.band_arfcn = req->band_arfcn;

			/* Tune transceiver to required ARFCN */
			phyif_handle_cmd(trxcon->phyif, &phycmd);
		}

		const struct phyif_cmd phycmd = { .type = PHYIF_CMDT_POWERON };
		phyif_handle_cmd(trxcon->phyif, &phycmd);
		break;
	}
	case TRXCON_EV_FULL_POWER_SCAN_REQ:
		handle_full_power_scan_req(fi, (const struct trxcon_param_full_power_scan_req *)data);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void trxcon_st_full_power_scan_onenter(struct osmo_fsm_inst *fi,
					      uint32_t prev_state)
{
	const struct trxcon_inst *trxcon = fi->priv;
	const struct trxcon_param_full_power_scan_req *req = trxcon->fi_data;

	/* req->band_arfcn_start holds the current ARFCN */
	const struct phyif_cmd phycmd = {
		.type = PHYIF_CMDT_MEASURE,
		.param.measure = {
			.band_arfcn = req->band_arfcn_start,
		},
	};

	phyif_handle_cmd(trxcon->phyif, &phycmd);
}

static void trxcon_st_full_power_scan_action(struct osmo_fsm_inst *fi,
					     uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;

	switch (event) {
	case TRXCON_EV_FULL_POWER_SCAN_RES:
	{
		struct trxcon_param_full_power_scan_req *req = trxcon->fi_data;
		const struct trxcon_param_full_power_scan_res *res = data;

		if (req == NULL) {
			LOGPFSML(fi, LOGL_ERROR, "Rx unexpected power scan result\n");
			break;
		}

		/* req->band_arfcn_start holds the expected ARFCN */
		if (res->band_arfcn != req->band_arfcn_start) {
			LOGPFSML(fi, LOGL_ERROR, "Rx power scan result "
				 "with unexpected ARFCN %u (expected %u)\n",
				 res->band_arfcn & ~ARFCN_FLAG_MASK,
				 req->band_arfcn_start & ~ARFCN_FLAG_MASK);
			break;
		}

		if (res->band_arfcn < req->band_arfcn_stop) {
			l1ctl_tx_pm_conf(trxcon, res->band_arfcn, res->dbm, false);
			/* trxcon_st_full_power_scan_onenter() sends the next PHYIF_CMDT_MEASURE */
			req->band_arfcn_start = res->band_arfcn + 1;
			osmo_fsm_inst_state_chg(fi, TRXCON_ST_FULL_POWER_SCAN, 0, 0); /* TODO: timeout */
		} else {
			l1ctl_tx_pm_conf(trxcon, res->band_arfcn, res->dbm, true);
			LOGPFSML(fi, LOGL_INFO, "Full power scan completed\n");
			TALLOC_FREE(trxcon->fi_data);
		}
		break;
	}
	case TRXCON_EV_FULL_POWER_SCAN_REQ:
		handle_full_power_scan_req(fi, (const struct trxcon_param_full_power_scan_req *)data);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void trxcon_st_fbsb_search_action(struct osmo_fsm_inst *fi,
					 uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;

	switch (event) {
	case TRXCON_EV_FBSB_SEARCH_RES:
		osmo_fsm_inst_state_chg(fi, TRXCON_ST_BCCH_CCCH, 0, 0);
		l1ctl_tx_fbsb_conf(trxcon, trxcon->l1p.band_arfcn, trxcon->sched->bsic);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void handle_tx_access_burst_req(struct osmo_fsm_inst *fi,
				       const struct trxcon_param_tx_access_burst_req *req)
{
	struct trxcon_inst *trxcon = fi->priv;
	enum l1sched_ts_prim_type prim_type;
	const struct l1sched_ts_prim *prim;

	const struct l1sched_ts_prim_rach rach = {
		.synch_seq = req->synch_seq,
		.offset = req->offset,
		.ra = req->ra,
	};

	prim_type = req->is_11bit ? L1SCHED_PRIM_RACH11 : L1SCHED_PRIM_RACH8;
	prim = l1sched_prim_push(trxcon->sched, prim_type,
				 req->chan_nr, req->link_id,
				 (const uint8_t *)&rach, sizeof(rach));
	if (prim == NULL)
		LOGPFSML(fi, LOGL_ERROR, "Failed to enqueue a prim\n");
}

static void trxcon_st_bcch_ccch_action(struct osmo_fsm_inst *fi,
				       uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;
	struct l1sched_ts *ts;
	int rc;

	switch (event) {
	case TRXCON_EV_TX_ACCESS_BURST_REQ:
		handle_tx_access_burst_req(fi, data);
		break;
	case TRXCON_EV_TX_ACCESS_BURST_CNF:
		l1ctl_tx_rach_conf(trxcon, (const struct trxcon_param_tx_access_burst_cnf *)data);
		break;
	case TRXCON_EV_SET_CCCH_MODE_REQ:
	{
		struct trxcon_param_set_ccch_tch_mode_req *req = data;
		enum gsm_phys_chan_config chan_config = req->mode;

		/* Make sure that TS0 is allocated and configured */
		ts = trxcon->sched->ts[0];
		if (ts == NULL || ts->mf_layout == NULL) {
			LOGPFSML(fi, LOGL_ERROR, "TS0 is not configured\n");
			return;
		}

		/* Do nothing if the current mode matches required */
		if (ts->mf_layout->chan_config != chan_config)
			l1sched_configure_ts(trxcon->sched, 0, chan_config);
		req->applied = true;
		break;
	}
	case TRXCON_EV_DEDICATED_ESTABLISH_REQ:
	{
		const struct trxcon_param_dedicated_establish_req *req = data;
		enum gsm_phys_chan_config config;

		config = l1sched_chan_nr2pchan_config(req->chan_nr);
		if (config == GSM_PCHAN_NONE) {
			LOGPFSML(fi, LOGL_ERROR, "Failed to determine channel config\n");
			return;
		}

		if (req->hopping) {
			const struct phyif_cmd phycmd = {
				.type = PHYIF_CMDT_SETFREQ_H1,
				.param.setfreq_h1 = {
					.hsn = req->h1.hsn,
					.maio = req->h1.maio,
					.ma = &req->h1.ma[0],
					.ma_len = req->h1.n,
				},
			};

			/* Apply the freq. hopping parameters */
			if (phyif_handle_cmd(trxcon->phyif, &phycmd) != 0)
				return;

			/* Set current ARFCN to an invalid value */
			trxcon->l1p.band_arfcn = 0xffff;
		} else {
			const struct phyif_cmd phycmd = {
				.type = PHYIF_CMDT_SETFREQ_H0,
				.param.setfreq_h0 = {
					.band_arfcn = req->h0.band_arfcn,
				},
			};

			/* Tune transceiver to required ARFCN */
			if (phyif_handle_cmd(trxcon->phyif, &phycmd) != 0)
				return;

			/* Update current ARFCN */
			trxcon->l1p.band_arfcn = req->h0.band_arfcn;
		}

		rc = l1sched_configure_ts(trxcon->sched, req->chan_nr & 0x07, config);
		if (rc)
			return;
		ts = trxcon->sched->ts[req->chan_nr & 0x07];
		OSMO_ASSERT(ts != NULL);

		l1sched_deactivate_all_lchans(ts);

		/* Activate only requested lchans */
		rc = l1sched_set_lchans(ts, req->chan_nr, 1, req->tch_mode, req->tsc);
		if (rc) {
			LOGPFSML(fi, LOGL_ERROR, "Failed to activate requested lchans\n");
			return;
		}

		if (config == GSM_PCHAN_PDCH)
			osmo_fsm_inst_state_chg(fi, TRXCON_ST_PACKET_DATA, 0, 0);
		else
			osmo_fsm_inst_state_chg(fi, TRXCON_ST_DEDICATED, 0, 0);
		break;
	}
	case TRXCON_EV_RX_DATA_IND:
		l1ctl_tx_dt_ind(trxcon, (const struct trxcon_param_rx_data_ind *)data);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void trxcon_st_dedicated_action(struct osmo_fsm_inst *fi,
				       uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;

	switch (event) {
	case TRXCON_EV_TX_ACCESS_BURST_REQ:
		handle_tx_access_burst_req(fi, data);
		break;
	case TRXCON_EV_TX_ACCESS_BURST_CNF:
		l1ctl_tx_rach_conf(trxcon, (const struct trxcon_param_tx_access_burst_cnf *)data);
		break;
	case TRXCON_EV_DEDICATED_RELEASE_REQ:
		l1sched_reset(trxcon->sched, false);
		osmo_fsm_inst_state_chg(fi, TRXCON_ST_RESET, 0, 0);
		break;
	case TRXCON_EV_SET_TCH_MODE_REQ:
	{
		struct trxcon_param_set_ccch_tch_mode_req *req = data;
		unsigned int tn;

		/* Iterate over timeslot list */
		for (tn = 0; tn < ARRAY_SIZE(trxcon->sched->ts); tn++) {
			struct l1sched_ts *ts = trxcon->sched->ts[tn];
			struct l1sched_lchan_state *lchan;

			/* Timeslot is not allocated */
			if (ts == NULL || ts->mf_layout == NULL)
				continue;

			/* Iterate over all allocated lchans */
			llist_for_each_entry(lchan, &ts->lchans, list) {
				/* Omit inactive channels */
				if (!lchan->active)
					continue;
				lchan->tch_mode = req->mode;
				if (req->mode == GSM48_CMODE_SPEECH_AMR) {
					uint8_t bmask = req->amr.codecs_bitmask;
					int n = 0;
					int acum = 0;
					int pos;
					while ((pos = ffs(bmask)) != 0) {
						acum += pos;
						LOGPFSML(fi, LOGL_DEBUG,
							 LOGP_LCHAN_NAME_FMT " AMR codec[%u] = %u\n",
							 LOGP_LCHAN_NAME_ARGS(lchan), n, acum - 1);
						lchan->amr.codec[n++] = acum - 1;
						bmask >>= pos;
					}
					if (n == 0) {
						LOGPFSML(fi, LOGL_ERROR,
							 LOGP_LCHAN_NAME_FMT " Empty AMR codec mode bitmask!\n",
							 LOGP_LCHAN_NAME_ARGS(lchan));
						continue;
					}
					lchan->amr.codecs = n;
					lchan->amr.dl_ft = req->amr.start_codec;
					lchan->amr.dl_cmr = req->amr.start_codec;
					lchan->amr.ul_ft = req->amr.start_codec;
					lchan->amr.ul_cmr = req->amr.start_codec;
				}
				req->applied = true;
			}
		}
		break;
	}
	case TRXCON_EV_CRYPTO_REQ:
	{
		const struct trxcon_param_crypto_req *req = data;
		unsigned int tn = req->chan_nr & 0x07;
		struct l1sched_ts *ts;

		/* Make sure that required TS is allocated and configured */
		ts = trxcon->sched->ts[tn];
		if (ts == NULL || ts->mf_layout == NULL) {
			LOGPFSML(fi, LOGL_ERROR, "TS%u is not configured\n", tn);
			return;
		}

		if (l1sched_start_ciphering(ts, req->a5_algo, req->key, req->key_len) != 0) {
			LOGPFSML(fi, LOGL_ERROR, "Failed to configure ciphering\n");
			return;
		}
		break;
	}
	case TRXCON_EV_TX_DATA_REQ:
	{
		const struct trxcon_param_tx_data_req *req = data;
		struct l1sched_ts_prim *prim;

		prim = l1sched_prim_push(trxcon->sched, L1SCHED_PRIM_DATA,
					 req->chan_nr, req->link_id,
					 req->data, req->data_len);
		if (prim == NULL) {
			LOGPFSML(fi, LOGL_ERROR, "Failed to enqueue a prim\n");
			return;
		}
		break;
	}
	case TRXCON_EV_TX_DATA_CNF:
		l1ctl_tx_dt_conf(trxcon, (const struct trxcon_param_tx_data_cnf *)data);
		break;
	case TRXCON_EV_RX_DATA_IND:
		l1ctl_tx_dt_ind(trxcon, (const struct trxcon_param_rx_data_ind *)data);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void trxcon_st_packet_data_action(struct osmo_fsm_inst *fi,
					 uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;

	switch (event) {
	case TRXCON_EV_TX_ACCESS_BURST_REQ:
		handle_tx_access_burst_req(fi, data);
		break;
	case TRXCON_EV_TX_ACCESS_BURST_CNF:
		l1ctl_tx_rach_conf(trxcon, (const struct trxcon_param_tx_access_burst_cnf *)data);
		break;
	case TRXCON_EV_RX_DATA_IND:
	{
		const struct trxcon_param_rx_data_ind *ind = data;

		if (ind->link_id == 0x00)
			LOGPFSML(fi, LOGL_NOTICE, "Rx PDTCH/D message\n");
		else
			LOGPFSML(fi, LOGL_NOTICE, "Rx PTCCH/D message\n");
		break;
	}
	case TRXCON_EV_DEDICATED_RELEASE_REQ:
		l1sched_reset(trxcon->sched, false);
		osmo_fsm_inst_state_chg(fi, TRXCON_ST_RESET, 0, 0);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void trxcon_fsm_pre_term_cb(struct osmo_fsm_inst *fi,
				   enum osmo_fsm_term_cause cause)
{
	struct trxcon_inst *trxcon = fi->priv;

	if (trxcon == NULL)
		return;

	/* Shutdown the scheduler */
	if (trxcon->sched != NULL)
		l1sched_free(trxcon->sched);
	/* Close active connections */
	if (trxcon->l2if != NULL)
		trxcon_l1ctl_close(trxcon);
	if (trxcon->phyif != NULL)
		phyif_close(trxcon->phyif);

	talloc_free(trxcon);
	fi->priv = NULL;
}

static const struct osmo_fsm_state trxcon_fsm_states[] = {
	[TRXCON_ST_RESET] = {
		.name = "RESET",
		.out_state_mask = S(TRXCON_ST_FBSB_SEARCH)
				| S(TRXCON_ST_FULL_POWER_SCAN),
		.in_event_mask  = S(TRXCON_EV_FBSB_SEARCH_REQ)
				| S(TRXCON_EV_FULL_POWER_SCAN_REQ),
		.action = &trxcon_st_reset_action,
	},
	[TRXCON_ST_FULL_POWER_SCAN] = {
		.name = "FULL_POWER_SCAN",
		.out_state_mask = S(TRXCON_ST_RESET)
				| S(TRXCON_ST_FULL_POWER_SCAN),
		.in_event_mask  = S(TRXCON_EV_FULL_POWER_SCAN_RES)
				| S(TRXCON_EV_FULL_POWER_SCAN_REQ),
		.onenter = &trxcon_st_full_power_scan_onenter,
		.action = &trxcon_st_full_power_scan_action,
	},
	[TRXCON_ST_FBSB_SEARCH] = {
		.name = "FBSB_SEARCH",
		.out_state_mask = S(TRXCON_ST_RESET)
				| S(TRXCON_ST_BCCH_CCCH),
		.in_event_mask  = S(TRXCON_EV_FBSB_SEARCH_RES),
		.action = &trxcon_st_fbsb_search_action,
	},
	[TRXCON_ST_BCCH_CCCH] = {
		.name = "BCCH_CCCH",
		.out_state_mask = S(TRXCON_ST_RESET)
				| S(TRXCON_ST_FBSB_SEARCH)
				| S(TRXCON_ST_DEDICATED)
				| S(TRXCON_ST_PACKET_DATA),
		.in_event_mask  = S(TRXCON_EV_RX_DATA_IND)
				| S(TRXCON_EV_SET_CCCH_MODE_REQ)
				| S(TRXCON_EV_TX_ACCESS_BURST_REQ)
				| S(TRXCON_EV_TX_ACCESS_BURST_CNF)
				| S(TRXCON_EV_DEDICATED_ESTABLISH_REQ),
		.action = &trxcon_st_bcch_ccch_action,
	},
	[TRXCON_ST_DEDICATED] = {
		.name = "DEDICATED",
		.out_state_mask = S(TRXCON_ST_RESET)
				| S(TRXCON_ST_FBSB_SEARCH)
				| S(TRXCON_ST_BCCH_CCCH),
		.in_event_mask  = S(TRXCON_EV_DEDICATED_RELEASE_REQ)
				| S(TRXCON_EV_TX_ACCESS_BURST_REQ)
				| S(TRXCON_EV_TX_ACCESS_BURST_CNF)
				| S(TRXCON_EV_SET_TCH_MODE_REQ)
				| S(TRXCON_EV_TX_DATA_REQ)
				| S(TRXCON_EV_TX_DATA_CNF)
				| S(TRXCON_EV_RX_DATA_IND)
				| S(TRXCON_EV_CRYPTO_REQ),
		.action = &trxcon_st_dedicated_action,
	},
	[TRXCON_ST_PACKET_DATA] = {
		.name = "PACKET_DATA",
		.out_state_mask = S(TRXCON_ST_RESET)
				| S(TRXCON_ST_FBSB_SEARCH)
				| S(TRXCON_ST_BCCH_CCCH),
		.in_event_mask  = S(TRXCON_EV_DEDICATED_RELEASE_REQ)
				| S(TRXCON_EV_TX_ACCESS_BURST_REQ)
				| S(TRXCON_EV_TX_ACCESS_BURST_CNF)
				| S(TRXCON_EV_RX_DATA_IND),
		.action = &trxcon_st_packet_data_action,
	},
};

static const struct value_string trxcon_fsm_event_names[] = {
	OSMO_VALUE_STRING(TRXCON_EV_PHYIF_FAILURE),
	OSMO_VALUE_STRING(TRXCON_EV_L2IF_FAILURE),
	OSMO_VALUE_STRING(TRXCON_EV_RESET_FULL_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_RESET_SCHED_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_FULL_POWER_SCAN_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_FULL_POWER_SCAN_RES),
	OSMO_VALUE_STRING(TRXCON_EV_FBSB_SEARCH_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_FBSB_SEARCH_RES),
	OSMO_VALUE_STRING(TRXCON_EV_SET_CCCH_MODE_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_SET_TCH_MODE_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_SET_PHY_CONFIG_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_TX_ACCESS_BURST_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_TX_ACCESS_BURST_CNF),
	OSMO_VALUE_STRING(TRXCON_EV_UPDATE_SACCH_CACHE_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_DEDICATED_ESTABLISH_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_DEDICATED_RELEASE_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_TX_DATA_REQ),
	OSMO_VALUE_STRING(TRXCON_EV_TX_DATA_CNF),
	OSMO_VALUE_STRING(TRXCON_EV_RX_DATA_IND),
	OSMO_VALUE_STRING(TRXCON_EV_CRYPTO_REQ),
	{ 0, NULL }
};

struct osmo_fsm trxcon_fsm_def = {
	.name = "trxcon",
	.states = trxcon_fsm_states,
	.num_states = ARRAY_SIZE(trxcon_fsm_states),
	.log_subsys = DAPP,
	.event_names = trxcon_fsm_event_names,
	.allstate_event_mask = S(TRXCON_EV_PHYIF_FAILURE)
			     | S(TRXCON_EV_L2IF_FAILURE)
			     | S(TRXCON_EV_RESET_FULL_REQ)
			     | S(TRXCON_EV_RESET_SCHED_REQ)
			     | S(TRXCON_EV_SET_PHY_CONFIG_REQ)
			     | S(TRXCON_EV_UPDATE_SACCH_CACHE_REQ),
	.allstate_action = &trxcon_allstate_action,
	.timer_cb = &trxcon_timer_cb,
	.pre_term = &trxcon_fsm_pre_term_cb,
};

static __attribute__((constructor)) void on_dso_load(void)
{
	OSMO_ASSERT(osmo_fsm_register(&trxcon_fsm_def) == 0);
}
