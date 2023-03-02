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

#include <stdint.h>
#include <errno.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/gsmtap.h>

#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/trxcon_fsm.h>
#include <osmocom/bb/trxcon/phyif.h>
#include <osmocom/bb/l1sched/l1sched.h>

static void trxcon_gsmtap_send(struct gsmtap_inst *gi, uint8_t chan_type,
			       uint32_t fn, uint8_t tn, uint8_t ss,
			       uint16_t band_arfcn,
			       int8_t signal_dbm, uint8_t snr,
			       const uint8_t *data, size_t data_len)
{
	/* Omit frames with unknown channel type */
	if (chan_type == GSMTAP_CHANNEL_UNKNOWN)
		return;

	/* TODO: distinguish GSMTAP_CHANNEL_PCH and GSMTAP_CHANNEL_AGCH */
	gsmtap_send(gi, band_arfcn, tn, chan_type, ss, fn, signal_dbm, snr, data, data_len);
}

/* External L1 API for the scheduler */
int l1sched_handle_config_req(struct l1sched_state *sched,
			      const struct l1sched_config_req *cr)
{
	struct trxcon_inst *trxcon = sched->priv;

	switch (cr->type) {
	case L1SCHED_CFG_PCHAN_COMB:
	{
		struct trxcon_param_set_phy_config_req req = {
			.type = TRXCON_PHY_CFGT_PCHAN_COMB,
			.pchan_comb = {
				.tn = cr->pchan_comb.tn,
				.pchan = cr->pchan_comb.pchan,
			},
		};

		return osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_SET_PHY_CONFIG_REQ, &req);
	}
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled config request (type 0x%02x)\n", cr->type);
		return -ENODEV;
	}
}

int l1sched_handle_burst_req(struct l1sched_state *sched,
			     const struct l1sched_burst_req *br)
{
	struct trxcon_inst *trxcon = sched->priv;
	const struct trxcon_phyif_burst_req phybr = {
		.fn = br->fn,
		.tn = br->tn,
		.pwr = br->pwr,
		.burst = &br->burst[0],
		.burst_len = br->burst_len,
	};

	return trxcon_phyif_handle_burst_req(trxcon->phyif, &phybr);
}

/* External L2 API for the scheduler */
int l1sched_handle_data_ind(struct l1sched_lchan_state *lchan,
			    const uint8_t *data, size_t data_len,
			    int n_errors, int n_bits_total,
			    enum l1sched_data_type dt)
{
	const struct l1sched_meas_set *meas = &lchan->meas_avg;
	const struct l1sched_lchan_desc *lchan_desc;
	struct l1sched_state *sched = lchan->ts->sched;
	struct trxcon_inst *trxcon = sched->priv;
	int rc;

	lchan_desc = &l1sched_lchan_desc[lchan->type];

	struct trxcon_param_rx_data_ind ind = {
		/* .traffic is set below */
		.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
		.link_id = lchan_desc->link_id,
		.band_arfcn = trxcon->l1p.band_arfcn,
		.frame_nr = meas->fn,
		.toa256 = meas->toa256,
		.rssi = meas->rssi,
		.n_errors = n_errors,
		.n_bits_total = n_bits_total,
		.data_len = data_len,
		.data = data,
	};

	switch (dt) {
	case L1SCHED_DT_PACKET_DATA:
	case L1SCHED_DT_TRAFFIC:
		ind.traffic = true;
		/* fall-through */
	case L1SCHED_DT_SIGNALING:
		rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_RX_DATA_IND, &ind);
		break;
	case L1SCHED_DT_OTHER:
		if (lchan->type == L1SCHED_SCH) {
			if (trxcon->fi->state != TRXCON_ST_FBSB_SEARCH)
				return 0;
			rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_FBSB_SEARCH_RES, NULL);
			break;
		}
		/* fall through */
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled L2 DATA.ind (type 0x%02x)\n", dt);
		return -ENODEV;
	}

	if (trxcon->gsmtap != NULL && data != NULL && data_len > 0) {
		trxcon_gsmtap_send(trxcon->gsmtap, lchan_desc->gsmtap_chan_type,
				   meas->fn, lchan->ts->index, lchan_desc->ss_nr,
				   trxcon->l1p.band_arfcn, meas->rssi, 0,
				   data, data_len);
	}

	return rc;
}

int l1sched_handle_data_cnf(struct l1sched_lchan_state *lchan,
			    uint32_t fn, enum l1sched_data_type dt)
{
	const struct l1sched_lchan_desc *lchan_desc;
	struct l1sched_state *sched = lchan->ts->sched;
	struct trxcon_inst *trxcon = sched->priv;
	const uint8_t *data;
	uint8_t ra_buf[2];
	size_t data_len;
	int rc;

	lchan_desc = &l1sched_lchan_desc[lchan->type];

	switch (dt) {
	case L1SCHED_DT_PACKET_DATA:
		data_len = lchan->prim->payload_len;
		data = lchan->prim->payload;
		rc = 0;
		break; /* do not send DATA.cnf */
	case L1SCHED_DT_SIGNALING:
	case L1SCHED_DT_TRAFFIC:
	{
		struct trxcon_param_tx_data_cnf cnf = {
			.traffic = (dt == L1SCHED_DT_TRAFFIC),
			.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
			.link_id = lchan_desc->link_id,
			.band_arfcn = trxcon->l1p.band_arfcn,
			.frame_nr = fn,
		};

		rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_TX_DATA_CNF, &cnf);
		data_len = lchan->prim->payload_len;
		data = lchan->prim->payload;
		break;
	}
	case L1SCHED_DT_OTHER:
		if (L1SCHED_PRIM_IS_RACH(lchan->prim)) {
			const struct l1sched_ts_prim_rach *rach;
			struct trxcon_param_tx_access_burst_cnf cnf = {
				.band_arfcn = trxcon->l1p.band_arfcn,
				.frame_nr = fn,
			};

			rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_TX_ACCESS_BURST_CNF, &cnf);

			rach = (struct l1sched_ts_prim_rach *)lchan->prim->payload;
			if (lchan->prim->type == L1SCHED_PRIM_RACH11) {
				ra_buf[0] = (uint8_t)(rach->ra >> 3);
				ra_buf[1] = (uint8_t)(rach->ra & 0x07);
				data = &ra_buf[0];
				data_len = 2;
			} else {
				ra_buf[0] = (uint8_t)(rach->ra);
				data = &ra_buf[0];
				data_len = 1;
			}
			break;
		}
		/* fall through */
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled L2 DATA.cnf (type 0x%02x)\n", dt);
		return -ENODEV;
	}

	if (trxcon->gsmtap != NULL) {
		trxcon_gsmtap_send(trxcon->gsmtap, lchan_desc->gsmtap_chan_type,
				   fn, lchan->ts->index, lchan_desc->ss_nr,
				   trxcon->l1p.band_arfcn | ARFCN_UPLINK,
				   0, 0, data, data_len);
	}

	return rc;
}

/* External L1 API for the PHYIF */
int trxcon_phyif_handle_rts_ind(void *priv, const struct trxcon_phyif_rts_ind *rts)
{
	struct trxcon_inst *trxcon = priv;
	struct l1sched_burst_req br = {
		.fn = rts->fn,
		.tn = rts->tn,
		.burst_len = 0, /* NOPE.ind */
	};

	l1sched_pull_burst(trxcon->sched, &br);
	return l1sched_handle_burst_req(trxcon->sched, &br);
}

int trxcon_phyif_handle_rtr_ind(void *priv, const struct trxcon_phyif_rtr_ind *ind,
				struct trxcon_phyif_rtr_rsp *rsp)
{
	struct trxcon_inst *trxcon = priv;
	struct l1sched_probe probe = {
		.fn = ind->fn,
		.tn = ind->tn,
	};

	l1sched_handle_rx_probe(trxcon->sched, &probe);

	memset(rsp, 0x00, sizeof(*rsp));

	if (probe.flags & L1SCHED_PROBE_F_ACTIVE)
		rsp->flags |= TRXCON_PHYIF_RTR_F_ACTIVE;

	return 0;
}

int trxcon_phyif_handle_burst_ind(void *priv, const struct trxcon_phyif_burst_ind *phybi)
{
	struct trxcon_inst *trxcon = priv;
	struct l1sched_burst_ind bi = {
		.fn = phybi->fn,
		.tn = phybi->tn,
		.toa256 = phybi->toa256,
		.rssi = phybi->rssi,
		/* .burst[] is populated below */
		.burst_len = phybi->burst_len,
	};

	OSMO_ASSERT(phybi->burst_len <= sizeof(bi.burst));
	memcpy(&bi.burst[0], phybi->burst, phybi->burst_len);

	/* Poke scheduler */
	return l1sched_handle_rx_burst(trxcon->sched, &bi);
}

int trxcon_phyif_handle_clock_ind(void *priv, uint32_t fn)
{
	struct trxcon_inst *trxcon = priv;

	return l1sched_clck_handle(trxcon->sched, fn);
}

int trxcon_phyif_handle_rsp(void *priv, const struct trxcon_phyif_rsp *rsp)
{
	struct trxcon_inst *trxcon = priv;

	switch (rsp->type) {
	case TRXCON_PHYIF_CMDT_MEASURE:
	{
		const struct trxcon_phyif_rspp_measure *meas = &rsp->param.measure;
		struct trxcon_param_full_power_scan_res res = {
			.band_arfcn = meas->band_arfcn,
			.dbm = meas->dbm,
		};

		return osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_FULL_POWER_SCAN_RES, &res);
	}
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled PHYIF response (type 0x%02x)\n", rsp->type);
		return -ENODEV;
	}
}
