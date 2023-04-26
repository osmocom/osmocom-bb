/*
 * OsmocomBB <-> SDR connection bridge
 *
 * (C) 2022-2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/gsm/rsl.h>

#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/trxcon_fsm.h>
#include <osmocom/bb/trxcon/phyif.h>
#include <osmocom/bb/l1sched/l1sched.h>

static void trxcon_gsmtap_send(struct trxcon_inst *trxcon,
			       const struct l1sched_prim_chdr *chdr,
			       const uint8_t *data, size_t data_len,
			       int8_t signal_dbm, uint8_t snr, bool uplink)
{
	uint16_t band_arfcn = trxcon->l1p.band_arfcn;
	uint8_t chan_type, ss, tn;

	if (uplink)
		band_arfcn |= ARFCN_UPLINK;
	if (rsl_dec_chan_nr(chdr->chan_nr, &chan_type, &ss, &tn) != 0)
		return;
	chan_type = chantype_rsl2gsmtap2(chan_type, chdr->link_id, chdr->traffic);

	gsmtap_send(trxcon->gsmtap, band_arfcn, tn, chan_type, ss,
		    chdr->frame_nr, signal_dbm, snr,
		    data, data_len);
}

/* External L1 API for the scheduler */
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
static int handle_prim_data_ind(struct trxcon_inst *trxcon, struct msgb *msg)
{
	const struct l1sched_prim *prim = l1sched_prim_from_msgb(msg);
	struct trxcon_param_rx_data_ind ind = {
		.traffic = prim->data_ind.chdr.traffic,
		.chan_nr = prim->data_ind.chdr.chan_nr,
		.link_id = prim->data_ind.chdr.link_id,
		.band_arfcn = trxcon->l1p.band_arfcn,
		.frame_nr = prim->data_ind.chdr.frame_nr,
		.toa256 = prim->data_ind.toa256,
		.rssi = prim->data_ind.rssi,
		.n_errors = prim->data_ind.n_errors,
		.n_bits_total = prim->data_ind.n_bits_total,
		.data_len = msgb_l2len(msg),
		.data = msgb_l2(msg),
	};

	if (trxcon->gsmtap != NULL && ind.data_len > 0) {
		trxcon_gsmtap_send(trxcon, &prim->data_ind.chdr,
				   ind.data, ind.data_len,
				   ind.rssi, 0, false);
	}

	return osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_RX_DATA_IND, &ind);
}

static int handle_prim_data_cnf(struct trxcon_inst *trxcon, struct msgb *msg)
{
	const struct l1sched_prim *prim = l1sched_prim_from_msgb(msg);
	struct trxcon_param_tx_data_cnf cnf = {
		.traffic = prim->data_cnf.traffic,
		.chan_nr = prim->data_cnf.chan_nr,
		.link_id = prim->data_cnf.link_id,
		.band_arfcn = trxcon->l1p.band_arfcn,
		.frame_nr = prim->data_cnf.frame_nr,
	};

	if (trxcon->gsmtap != NULL) {
		trxcon_gsmtap_send(trxcon, &prim->data_cnf,
				   msgb_l2(msg), msgb_l2len(msg),
				   0, 0, true);
	}

	return osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_TX_DATA_CNF, &cnf);
}

static int handle_prim_rach_cnf(struct trxcon_inst *trxcon, struct msgb *msg)
{
	const struct l1sched_prim *prim = l1sched_prim_from_msgb(msg);
	struct trxcon_param_tx_access_burst_cnf cnf = {
		.band_arfcn = trxcon->l1p.band_arfcn,
		.frame_nr = prim->rach_cnf.chdr.frame_nr,
	};

	if (trxcon->gsmtap != NULL) {
		if (prim->rach_cnf.is_11bit) {
			msgb_put_u8(msg, (uint8_t)(prim->rach_cnf.ra >> 3));
			msgb_put_u8(msg, (uint8_t)(prim->rach_cnf.ra & 0x07));
		} else {
			msgb_put_u8(msg, (uint8_t)(prim->rach_cnf.ra));
		}

		trxcon_gsmtap_send(trxcon, &prim->rach_cnf.chdr,
				   msgb_l2(msg), msgb_l2len(msg),
				   0, 0, true);
	}

	return osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_TX_ACCESS_BURST_CNF, &cnf);
}

int l1sched_prim_to_user(struct l1sched_state *sched, struct msgb *msg)
{
	const struct l1sched_prim *prim = l1sched_prim_from_msgb(msg);
	struct trxcon_inst *trxcon = sched->priv;
	int rc = 0;

	LOGPFSML(trxcon->fi, LOGL_DEBUG,
		 "%s(): Rx " L1SCHED_PRIM_STR_FMT "\n",
		 __func__, L1SCHED_PRIM_STR_ARGS(prim));

	switch (OSMO_PRIM_HDR(&prim->oph)) {
	case OSMO_PRIM(L1SCHED_PRIM_T_DATA, PRIM_OP_INDICATION):
		rc = handle_prim_data_ind(trxcon, msg);
		break;
	case OSMO_PRIM(L1SCHED_PRIM_T_DATA, PRIM_OP_CONFIRM):
		rc = handle_prim_data_cnf(trxcon, msg);
		break;
	case OSMO_PRIM(L1SCHED_PRIM_T_RACH, PRIM_OP_CONFIRM):
		rc = handle_prim_rach_cnf(trxcon, msg);
		break;
	case OSMO_PRIM(L1SCHED_PRIM_T_SCH, PRIM_OP_INDICATION):
		if (trxcon->fi->state == TRXCON_ST_FBSB_SEARCH)
			rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_FBSB_SEARCH_RES, NULL);
		break;
	case OSMO_PRIM(L1SCHED_PRIM_T_PCHAN_COMB, PRIM_OP_INDICATION):
	{
		struct trxcon_param_set_phy_config_req req = {
			.type = TRXCON_PHY_CFGT_PCHAN_COMB,
			.pchan_comb = {
				.tn = prim->pchan_comb_ind.tn,
				.pchan = prim->pchan_comb_ind.pchan,
			},
		};

		rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_SET_PHY_CONFIG_REQ, &req);
		break;
	}
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "%s(): Unhandled primitive " L1SCHED_PRIM_STR_FMT "\n",
			 __func__, L1SCHED_PRIM_STR_ARGS(prim));
		rc = -ENOTSUP;
	}

	msgb_free(msg);
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
