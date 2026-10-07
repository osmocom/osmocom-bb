/*
 * OsmocomBB <-> SDR connection bridge
 * Transceiver interface handlers
 *
 * Copyright (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
 * Copyright (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
 * Copyright (C) 2022-2026 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/fsm.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/trx/trxc.h>
#include <osmocom/trx/trxc_client.h>
#include <osmocom/trx/trxd.h>
#include <osmocom/trx/ep.h>

#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/logging.h>

/* Highest TRXD PDU version supported by trxcon (TRXDv1).
 *
 * TRXDv2 requires Tx PDUs to contain modulation and training sequence info.
 * struct trxcon_phyif_burst_req currently does not contain this information,
 * so we cannot populate the necessary fields in trx_if_handle_phyif_burst_req(). */
#define TRXD_PDU_VER_MAX	1

/* Give up after 3 retransmissions of a TRXC command */
#define TRX_MAX_RETRANS		3

#define S(x)	(1 << (x))

enum trx_fsm_states {
	TRX_ST_POWEROFF = 0, /* initial state; transceiver powered off */
	TRX_ST_POWERON,      /* transceiver powered on */
};

struct trx_instance {
	struct osmo_trx_ep *ep;
	struct osmo_trxc_client *trxc_client;

	struct osmo_fsm_inst *fi;
	uint32_t fn_advance;

	/* Rx/Tx frequencies (in kHz) tuned via RXTUNE/TXTUNE */
	uint32_t rx_freq_khz;
	uint32_t tx_freq_khz;
	bool hopping;

	/* Some private data */
	void *priv;
};

static struct osmo_fsm trx_fsm;

/* ------------------------------------------------------------------------ */
/* Control (TRXC) interface handlers                                        */
/* ------------------------------------------------------------------------ */

/* Transmit a TRXC message enqueued by the command queue engine */
static int trxc_client_tx_msg_cb(struct osmo_trxc_client *client,
				 const struct osmo_trxc_msg *msg)
{
	struct trx_instance *trx = osmo_trxc_client_get_priv(client);

	return osmo_trx_ep_send_ctrl_msg(trx->ep, 0, msg);
}

/* A critical TRXC command failed (NACKed, mismatched, or timed out) */
static void trxc_client_fatal_error_cb(struct osmo_trxc_client *client,
				       const struct osmo_trxc_msg *rsp)
{
	struct trx_instance *trx = osmo_trxc_client_get_priv(client);

	if (rsp == NULL) { /* no response, retransmissions exhausted */
		LOGPFSML(trx->fi, LOGL_NOTICE, "Transceiver offline\n");
		osmo_fsm_inst_term(trx->fi, OSMO_FSM_TERM_TIMEOUT, NULL);
	} else {
		osmo_fsm_inst_term(trx->fi, OSMO_FSM_TERM_ERROR, NULL);
	}
}

/* Feed responses received on the ctrl socket into the command queue engine */
void osmo_trx_ep_rx_ctrl_msg(struct osmo_trx_ep *ep, unsigned int chan,
			     const struct osmo_trxc_msg *msg)
{
	struct trx_instance *trx = osmo_trx_ep_get_priv(ep);

	osmo_trxc_client_rx_msg(trx->trxc_client, msg);
}

static int trx_poweroff_rsp_cb(struct osmo_trxc_client *client,
			       const struct osmo_trxc_msg *rsp, void *cb_data)
{
	struct trx_instance *trx = cb_data;

	if (rsp->status != 0)
		return -EINVAL; /* critical, escalate to fatal_error */

	osmo_fsm_inst_state_chg(trx->fi, TRX_ST_POWEROFF, 0, 0);
	return 0;
}

static int trx_if_cmd_poweroff(struct trx_instance *trx)
{
	/* The transceiver forgets the tuned Rx/Tx frequencies on POWEROFF */
	trx->rx_freq_khz = 0;
	trx->tx_freq_khz = 0;
	trx->hopping = false;

	return osmo_trxc_client_poweroff(trx->trxc_client, &trx_poweroff_rsp_cb, trx);
}

static int trx_poweron_rsp_cb(struct osmo_trxc_client *client,
			      const struct osmo_trxc_msg *rsp, void *cb_data)
{
	struct trx_instance *trx = cb_data;

	if (rsp->status != 0)
		return -EINVAL; /* critical, escalate to fatal_error */

	osmo_fsm_inst_state_chg(trx->fi, TRX_ST_POWERON, 0, 0);
	return 0;
}

static int trx_if_cmd_poweron(struct trx_instance *trx)
{
	return osmo_trxc_client_poweron(trx->trxc_client, &trx_poweron_rsp_cb, trx);
}

static void trx_setformat_cb(struct osmo_trxc_client *client,
			     uint8_t ver_use, void *cb_data)
{
	struct trx_instance *trx = cb_data;

	LOGPFSML(trx->fi, LOGL_INFO, "Using TRXD PDU version %u\n", ver_use);
	osmo_trx_ep_set_pdu_ver(trx->ep, 0, ver_use);
}

static int trx_if_cmd_setslot(struct trx_instance *trx,
			      const struct trxcon_phyif_cmdp_setslot *cmdp)
{
	static const uint8_t chan_types[_GSM_PCHAN_MAX] = {
		[GSM_PCHAN_UNKNOWN]             = OSMO_TRXC_CHAN_COMB_UNUSED,
		[GSM_PCHAN_NONE]                = OSMO_TRXC_CHAN_COMB_UNUSED,
		[GSM_PCHAN_CCCH]                = OSMO_TRXC_CHAN_COMB_BCCH,
		[GSM_PCHAN_CCCH_SDCCH4]         = OSMO_TRXC_CHAN_COMB_BCCH_SDCCH4,
		[GSM_PCHAN_CCCH_SDCCH4_CBCH]    = OSMO_TRXC_CHAN_COMB_BCCH_SDCCH4,
		[GSM_PCHAN_TCH_F]               = OSMO_TRXC_CHAN_COMB_TCHF,
		[GSM_PCHAN_TCH_H]               = OSMO_TRXC_CHAN_COMB_TCHH,
		[GSM_PCHAN_SDCCH8_SACCH8C]      = OSMO_TRXC_CHAN_COMB_SDCCH8,
		[GSM_PCHAN_SDCCH8_SACCH8C_CBCH] = OSMO_TRXC_CHAN_COMB_SDCCH8,
		[GSM_PCHAN_PDCH]                = OSMO_TRXC_CHAN_COMB_PDTCH,
	};

	/* SETSLOT may be pending for different timeslots at once, so the
	 * response needs to be matched against the params, too. */
	return osmo_trxc_client_send_cmd(trx->trxc_client,
					 OSMO_TRXC_F_CRITICAL | OSMO_TRXC_F_MATCH_PARAMS,
					 NULL, NULL, OSMO_TRXC_CMD_SETSLOT,
					 "%u %u", cmdp->tn, chan_types[cmdp->pchan]);
}

/*
 * Tuning Control
 *
 * RXTUNE/TXTUNE tune the transceiver to a given Rx/Tx frequency in kHz.
 * Both osmo-trx-proxy and fake_trx.py permit this command regardless
 * of the power state (before and after POWERON).
 * CMD (RX/TX)TUNE <kHz>
 * RSP (RX/TX)TUNE <status> <kHz>
 */

static int trx_if_cmd_rxtune(struct trx_instance *trx,
			     const struct trxcon_phyif_cmdp_setfreq_h0 *cmdp)
{
	uint16_t freq10;
	int rc;

	/* RX is downlink on MS side */
	freq10 = gsm_arfcn2freq10(cmdp->band_arfcn, 0);
	if (freq10 == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR, "ARFCN %d not defined\n", cmdp->band_arfcn);
		return -ENOTSUP;
	}

	rc = osmo_trxc_client_rxtune(trx->trxc_client, freq10 * 100, NULL, NULL);
	if (rc == 0) {
		trx->rx_freq_khz = freq10 * 100;
		trx->hopping = false;
	}
	return rc;
}

static int trx_if_cmd_txtune(struct trx_instance *trx,
			     const struct trxcon_phyif_cmdp_setfreq_h0 *cmdp)
{
	uint16_t freq10;
	int rc;

	/* TX is uplink on MS side */
	freq10 = gsm_arfcn2freq10(cmdp->band_arfcn, 1);
	if (freq10 == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR, "ARFCN %d not defined\n", cmdp->band_arfcn);
		return -ENOTSUP;
	}

	rc = osmo_trxc_client_txtune(trx->trxc_client, freq10 * 100, NULL, NULL);
	if (rc == 0) {
		trx->tx_freq_khz = freq10 * 100;
		trx->hopping = false;
	}
	return rc;
}

/*
 * Power measurement
 *
 * MEASURE instructs the transceiver to perform a power
 * measurement on specified frequency. After receiving this
 * request, transceiver should quickly re-tune to requested
 * frequency, measure power level and re-tune back to the
 * previous frequency.
 * CMD MEASURE <kHz>
 * RSP MEASURE <status> <kHz> <dB>
 */

static int trx_measure_rsp_cb(struct osmo_trxc_client *client,
			      const struct osmo_trxc_msg *rsp, void *cb_data)
{
	struct trx_instance *trx = cb_data;
	unsigned int freq10;
	uint16_t band_arfcn;
	int dbm;

	if (rsp->status != 0)
		return 0; /* not critical */

	/* Parse freq. and power level */
	if (osmo_trxc_msg_params_scan(rsp, "%u %d", &freq10, &dbm) != 2) {
		LOGPFSML(trx->fi, LOGL_ERROR,
			 "Failed to parse RSP MEASURE params: '%s'\n", rsp->params);
		return 0;
	}
	freq10 /= 100;

	band_arfcn = gsm_freq102arfcn((uint16_t)freq10, 0);
	if (band_arfcn == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR,
			 "Failed to parse ARFCN from RSP MEASURE: %s\n", rsp->params);
		return 0;
	}

	const struct trxcon_phyif_rsp phyrsp = {
		.type = TRXCON_PHYIF_CMDT_MEASURE,
		.param.measure = {
			.band_arfcn = band_arfcn,
			.dbm = dbm,
		},
	};

	trxcon_phyif_handle_rsp(trx->priv, &phyrsp);
	return 0;
}

static int trx_if_cmd_measure(struct trx_instance *trx,
			      const struct trxcon_phyif_cmdp_measure *cmdp)
{
	uint16_t freq10;

	/* Calculate a frequency for current ARFCN (DL) */
	freq10 = gsm_arfcn2freq10(cmdp->band_arfcn, 0);
	if (freq10 == 0xffff) {
		LOGPFSML(trx->fi, LOGL_ERROR,
			 "ARFCN %d not defined\n", cmdp->band_arfcn);
		return -ENOTSUP;
	}

	/* MEASURE may be pending for different frequencies at once, so the
	 * response needs to be matched against the params, too. */
	return osmo_trxc_client_send_cmd(trx->trxc_client, OSMO_TRXC_F_MATCH_PARAMS,
					 &trx_measure_rsp_cb, trx,
					 "MEASURE", "%u", freq10 * 100);
}

/*
 * Timing Advance control
 *
 * SETTA instructs the transceiver to transmit bursts in
 * advance calculated from requested TA value. This value is
 * normally between 0 and 63, with each step representing
 * an advance of one bit period (about 3.69 microseconds).
 * Since OsmocomBB has a special feature, which allows one
 * to spoof the distance from BTS, the range is extended.
 * CMD SETTA <-128..127>
 * RSP SETTA <status> <TA>
 */

static int trx_if_cmd_setta(struct trx_instance *trx,
			    const struct trxcon_phyif_cmdp_setta *cmdp)
{
	return osmo_trxc_client_send_cmd(trx->trxc_client, 0, NULL, NULL,
					 "SETTA", "%d", cmdp->ta);
}

/*
 * Frequency Hopping parameters indication.
 *
 * SETFH instructs transceiver to enable frequency hopping mode
 * using the given HSN, MAIO, and Mobile Allocation parameters.
 *
 * CMD SETFH <HSN> <MAIO> <RXF1> <TXF1> [... <RXFN> <TXFN>]
 *
 * where <RXFN> and <TXFN> is a pair of Rx/Tx frequencies (in kHz)
 * corresponding to one ARFCN the Mobile Allocation. Note that the
 * channel list is expected to be sorted in ascending order.
 */

static int trx_if_cmd_setfh(struct trx_instance *trx,
			    const struct trxcon_phyif_cmdp_setfreq_h1 *cmdp)
{
	/* Reserve some room for '<HSN> <MAIO> ' */
	char ma_buf[OSMO_TRXC_PARAMS_LEN_MAX - 8];
	size_t ma_buf_len = sizeof(ma_buf) - 1;
	uint16_t rx_freq, tx_freq;
	char *ptr;
	int i, rc;

	/* Make sure that Mobile Allocation has at least one ARFCN */
	if (!cmdp->ma_len || cmdp->ma == NULL) {
		LOGPFSML(trx->fi, LOGL_ERROR, "Mobile Allocation is empty?!?\n");
		return -EINVAL;
	}

	/* Compose a sequence of Rx/Tx frequencies (mobile allocation) */
	for (i = 0, ptr = ma_buf; i < cmdp->ma_len; i++) {
		/* Convert ARFCN to a pair of Rx/Tx frequencies (Hz * 10) */
		rx_freq = gsm_arfcn2freq10(cmdp->ma[i], 0); /* Rx: Downlink */
		tx_freq = gsm_arfcn2freq10(cmdp->ma[i], 1); /* Tx: Uplink */
		if (rx_freq == 0xffff || tx_freq == 0xffff) {
			LOGPFSML(trx->fi, LOGL_ERROR, "Failed to convert ARFCN %u "
			     "to a pair of Rx/Tx frequencies\n",
			     cmdp->ma[i] & ~ARFCN_FLAG_MASK);
			return -EINVAL;
		}

		/* Append a pair of Rx/Tx frequencies (in kHz) to the buffer */
		rc = snprintf(ptr, ma_buf_len, "%u %u ", rx_freq * 100, tx_freq * 100);
		if (rc < 0 || rc > ma_buf_len) { /* Prevent buffer overflow */
			LOGPFSML(trx->fi, LOGL_ERROR, "Not enough room to encode "
			     "Mobile Allocation (N=%u)\n", cmdp->ma_len);
			return -ENOSPC;
		}

		/* Move pointer */
		ma_buf_len -= rc;
		ptr += rc;
	}

	/* Overwrite the last space */
	*(ptr - 1) = '\0';

	rc = osmo_trxc_client_send_cmd(trx->trxc_client, OSMO_TRXC_F_CRITICAL,
				       NULL, NULL, OSMO_TRXC_CMD_SETFH,
				       "%u %u %s", cmdp->hsn, cmdp->maio, ma_buf);
	if (rc == 0) {
		trx->rx_freq_khz = 0;
		trx->tx_freq_khz = 0;
		trx->hopping = true;
	}
	return rc;
}

static void trx_fsm_allstate_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct trx_instance *trx = fi->priv;
	const struct trxcon_phyif_cmd *cmd = data;

	switch (event) {
	case TRXCON_PHYIF_CMDT_RESET:
	case TRXCON_PHYIF_CMDT_POWEROFF:
		trx_if_cmd_poweroff(trx);
		break;
	case TRXCON_PHYIF_CMDT_MEASURE:
		trx_if_cmd_measure(trx, &cmd->param.measure);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

/* (Re-)negotiate the TRXD PDU format and send POWERON right behind it,
 * since the transceiver may have reset its active format back to legacy
 * on the preceding POWEROFF (or never negotiated it at all, if this is the
 * very first POWERON). No state change here: the POWERON response (see
 * trx_poweron_rsp_cb()) is what moves us to TRX_ST_POWERON. */
static void trx_fsm_handle_poweron_req(struct osmo_fsm_inst *fi)
{
	struct trx_instance *trx = fi->priv;

	if (!trx->hopping && (trx->rx_freq_khz == 0 || trx->tx_freq_khz == 0)) {
		LOGPFSML(fi, LOGL_ERROR, "Refusing POWERON: Rx/Tx frequency not set\n");
		return;
	}

	osmo_trxc_client_negotiate_format(trx->trxc_client,
					  TRXD_PDU_VER_MAX,
					  &trx_setformat_cb, trx);
	trx_if_cmd_poweron(trx);
}

static void trx_fsm_poweroff_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct trx_instance *trx = fi->priv;
	const struct trxcon_phyif_cmd *cmd = data;

	switch (event) {
	case TRXCON_PHYIF_CMDT_POWERON:
		trx_fsm_handle_poweron_req(fi);
		break;
	case TRXCON_PHYIF_CMDT_SETFREQ_H0:
		trx_if_cmd_rxtune(trx, &cmd->param.setfreq_h0);
		trx_if_cmd_txtune(trx, &cmd->param.setfreq_h0);
		break;
	case TRXCON_PHYIF_CMDT_SETFREQ_H1:
		trx_if_cmd_setfh(trx, &cmd->param.setfreq_h1);
		break;
	case TRXCON_PHYIF_CMDT_SETSLOT:
		trx_if_cmd_setslot(trx, &cmd->param.setslot);
		break;
	case TRXCON_PHYIF_CMDT_SETTA:
		trx_if_cmd_setta(trx, &cmd->param.setta);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void trx_fsm_poweron_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct trx_instance *trx = fi->priv;
	const struct trxcon_phyif_cmd *cmd = data;

	switch (event) {
	case TRXCON_PHYIF_CMDT_SETFREQ_H0:
		trx_if_cmd_rxtune(trx, &cmd->param.setfreq_h0);
		trx_if_cmd_txtune(trx, &cmd->param.setfreq_h0);
		break;
	case TRXCON_PHYIF_CMDT_SETFREQ_H1:
		trx_if_cmd_setfh(trx, &cmd->param.setfreq_h1);
		break;
	case TRXCON_PHYIF_CMDT_SETSLOT:
		trx_if_cmd_setslot(trx, &cmd->param.setslot);
		break;
	case TRXCON_PHYIF_CMDT_SETTA:
		trx_if_cmd_setta(trx, &cmd->param.setta);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

/* Dispatch a PHYIF command as an event of the trx_fsm */
int trx_if_handle_phyif_cmd(struct trx_instance *trx, const struct trxcon_phyif_cmd *cmd)
{
	return osmo_fsm_inst_dispatch(trx->fi, cmd->type, (void *)cmd);
}

/* ------------------------------------------------------------------------ */
/* Data (TRXD) interface handlers                                           */
/* ------------------------------------------------------------------------ */

/* Handle a burst indication received on the data socket */
void osmo_trx_ep_rx_burst_ind(struct osmo_trx_ep *ep, unsigned int chan,
			      const struct osmo_trxd_burst_ind *bi)
{
	struct trx_instance *trx = osmo_trx_ep_get_priv(ep);

	LOGPFSMSL(trx->fi, DTRXD, LOGL_DEBUG, "Rx %s\n", osmo_trxd_burst_ind_name(bi));

	const struct trxcon_phyif_burst_ind phybi = {
		.fn = bi->fn,
		.tn = bi->tn,
		.toa256 = bi->toa256,
		.rssi = bi->rssi,
		.burst = (bi->flags & OSMO_TRXD_F_NOPE_IND) ? NULL : &bi->burst[0],
		.burst_len = (bi->flags & OSMO_TRXD_F_NOPE_IND) ? 0 : bi->burst_len,
	};

	trxcon_phyif_handle_burst_ind(trx->priv, &phybi);

	const struct trxcon_phyif_rts_ind rts = {
		.fn = GSM_TDMA_FN_SUM(bi->fn, trx->fn_advance),
		.tn = bi->tn,
	};

	trxcon_phyif_handle_rts_ind(trx->priv, &rts);
}

int trx_if_handle_phyif_burst_req(struct trx_instance *trx,
				  const struct trxcon_phyif_burst_req *br)
{
	struct osmo_trxd_burst_req dbr = {
		.fn = br->fn,
		.tn = br->tn,
		.att = br->pwr,
		.burst_len = br->burst_len,
	};
	int rc;

	/* We must be sure that POWERON has been acknowledged by the
	 * transceiver before attempting to transmit any bursts. */
	if (trx->fi->state != TRX_ST_POWERON) {
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR,
			  "Ignoring TX data, transceiver isn't ready\n");
		return -EAGAIN;
	}

	if (br->burst_len != 0)
		memcpy(&dbr.burst[0], br->burst, br->burst_len);
	else /* a burst request without a payload: NOPE.req */
		dbr.flags |= OSMO_TRXD_F_NOPE_REQ;

	LOGPFSMSL(trx->fi, DTRXD, LOGL_DEBUG, "Tx %s\n", osmo_trxd_burst_req_name(&dbr));

	rc = osmo_trx_ep_send_burst_req(trx->ep, 0, &dbr);
	if (rc < 0) {
		LOGPFSMSL(trx->fi, DTRXD, LOGL_ERROR,
			  "osmo_trx_ep_send_burst_req(%s) failed: %d\n",
			  osmo_trxd_burst_req_name(&dbr), rc);
	}

	return rc;
}

/* Init TRX interface (TRXC, TRXD sockets and FSM) */
struct trx_instance *trx_if_open(const struct trx_if_params *params)
{
	struct trx_instance *trx;
	struct osmo_fsm_inst *fi;
	int rc;

	LOGPFSML(params->parent_fi, LOGL_NOTICE,
		 "Init transceiver interface (%s:%u/%u)\n",
		 params->remote_host, params->base_port,
		 params->instance);

	/* Allocate a new dedicated state machine */
	fi = osmo_fsm_inst_alloc_child(&trx_fsm, params->parent_fi,
				       params->parent_term_event);
	if (fi == NULL) {
		LOGPFSML(params->parent_fi, LOGL_ERROR,
			 "Failed to allocate an instance of FSM '%s'\n",
			 trx_fsm.name);
		return NULL;
	}

	trx = talloc_zero(fi, struct trx_instance);
	if (!trx) {
		LOGPFSML(params->parent_fi, LOGL_ERROR, "Failed to allocate memory\n");
		osmo_fsm_inst_free(fi);
		return NULL;
	}

	trx->fn_advance = params->fn_advance;
	trx->priv = params->priv;
	fi->priv = trx;
	trx->fi = fi;

	/* The TRX protocol endpoint: per-channel ctrl/data UDP sockets.
	 * Shifting the base port by 2 * instance makes the single channel
	 * of this endpoint use the ports of the given TRX instance. */
	trx->ep = osmo_trx_ep_alloc(trx, 1);
	if (trx->ep == NULL)
		goto error;
	osmo_trx_ep_set_name(trx->ep, "trx_ep(%u)", params->instance);
	osmo_trx_ep_set_log_cat(trx->ep, DTRXD);
	osmo_trx_ep_set_priv(trx->ep, trx);
	osmo_trx_ep_set_mode(trx->ep, OSMO_TRX_EP_MODE_L1);
	osmo_trx_ep_set_laddr(trx->ep, params->local_host);
	osmo_trx_ep_set_raddr(trx->ep, params->remote_host);
	osmo_trx_ep_set_base_port(trx->ep, params->base_port + 2 * params->instance);
	osmo_trx_ep_set_clock_socket(trx->ep, false);

	/* The TRXC command queue engine */
	trx->trxc_client = osmo_trxc_client_alloc(trx);
	if (trx->trxc_client == NULL)
		goto error;
	osmo_trxc_client_set_name(trx->trxc_client, "trxc_client(%u)", params->instance);
	osmo_trxc_client_set_log_cat(trx->trxc_client, DTRXC);
	osmo_trxc_client_set_max_retrans(trx->trxc_client, TRX_MAX_RETRANS);
	osmo_trxc_client_set_priv(trx->trxc_client, trx);
	osmo_trxc_client_set_tx_msg_cb(trx->trxc_client, &trxc_client_tx_msg_cb);
	osmo_trxc_client_set_fatal_error_cb(trx->trxc_client, &trxc_client_fatal_error_cb);

	rc = osmo_trx_ep_open(trx->ep);
	if (rc != 0) {
		LOGPFSML(params->parent_fi, LOGL_ERROR,
			 "osmo_trx_ep_open() failed: %d\n", rc);
		goto error;
	}

	return trx;

error:
	osmo_fsm_inst_free(fi);
	return NULL;
}

void trx_if_close(struct trx_instance *trx)
{
	if (trx == NULL || trx->fi == NULL)
		return;
	osmo_fsm_inst_term(trx->fi, OSMO_FSM_TERM_REQUEST, NULL);
}

static void trx_fsm_cleanup_cb(struct osmo_fsm_inst *fi,
			       enum osmo_fsm_term_cause cause)
{
	struct trx_instance *trx = fi->priv;

	/* May be unallocated due to init error */
	if (!trx)
		return;

	LOGPFSML(fi, LOGL_NOTICE, "Shutdown transceiver interface\n");

	/* Flush the TRXC command queue */
	osmo_trxc_client_free(trx->trxc_client);
	trx->trxc_client = NULL;

	/* Power off if the transceiver is up (osmo_trx_ep API takes care of
	 * sending pending TRXC data, even after osmo_trx_ep_{close,free}()). */
	if (fi->state == TRX_ST_POWERON && trx->ep != NULL) {
		const struct osmo_trxc_msg cmd_poweroff = {
			.type = OSMO_TRXC_MT_CMD,
			.cmd = OSMO_TRXC_CMD_POWEROFF,
		};
		osmo_trx_ep_send_ctrl_msg(trx->ep, 0, &cmd_poweroff);
	}

	/* Shutdown the TRX endpoint */
	osmo_trx_ep_close(trx->ep);
	osmo_trx_ep_free(trx->ep);
	trx->ep = NULL;

	/* Free memory */
	trx->fi->priv = NULL;
	talloc_free(trx);
}

/* We re-use TRXCON_PHYIF_CMDT_* as the FSM events */
static const struct value_string trx_event_names[] = {
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_RESET),
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_POWERON),
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_POWEROFF),
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_MEASURE),
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_SETFREQ_H0),
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_SETFREQ_H1),
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_SETSLOT),
	OSMO_VALUE_STRING(TRXCON_PHYIF_CMDT_SETTA),
	{ 0, NULL }
};

static struct osmo_fsm_state trx_fsm_states[] = {
	[TRX_ST_POWEROFF] = {
		.name = "POWEROFF",
		.in_event_mask  = S(TRXCON_PHYIF_CMDT_POWERON)
				| S(TRXCON_PHYIF_CMDT_SETFREQ_H0)
				| S(TRXCON_PHYIF_CMDT_SETFREQ_H1)
				| S(TRXCON_PHYIF_CMDT_SETSLOT)
				| S(TRXCON_PHYIF_CMDT_SETTA),
		.out_state_mask = S(TRX_ST_POWERON)
				| S(TRX_ST_POWEROFF),
		.action = trx_fsm_poweroff_action,
	},
	[TRX_ST_POWERON] = {
		.name = "POWERON",
		.in_event_mask  = S(TRXCON_PHYIF_CMDT_SETFREQ_H0)
				| S(TRXCON_PHYIF_CMDT_SETFREQ_H1)
				| S(TRXCON_PHYIF_CMDT_SETSLOT)
				| S(TRXCON_PHYIF_CMDT_SETTA),
		.out_state_mask = S(TRX_ST_POWEROFF),
		.action = trx_fsm_poweron_action,
	},
};

static struct osmo_fsm trx_fsm = {
	.name = "trx_interface",
	.states = trx_fsm_states,
	.num_states = ARRAY_SIZE(trx_fsm_states),
	.log_subsys = DTRXC,
	.event_names = trx_event_names,
	.allstate_event_mask = S(TRXCON_PHYIF_CMDT_RESET)
			     | S(TRXCON_PHYIF_CMDT_POWEROFF)
			     | S(TRXCON_PHYIF_CMDT_MEASURE),
	.allstate_action = trx_fsm_allstate_action,
	.cleanup = trx_fsm_cleanup_cb,
};

static __attribute__((constructor)) void on_dso_load(void)
{
	OSMO_ASSERT(osmo_fsm_register(&trx_fsm) == 0);
}
