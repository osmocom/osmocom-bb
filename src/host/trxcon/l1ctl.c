/*
 * OsmocomBB <-> SDR connection bridge
 * GSM L1 control interface handlers
 *
 * (C) 2014 by Sylvain Munaut <tnt@246tNt.com>
 * (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <arpa/inet.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/gsm/gsm_utils.h>

#include "trxcon.h"
#include "logging.h"
#include "l1ctl_link.h"
#include "l1ctl_proto.h"

#include "trx_if.h"
#include "sched_trx.h"

static struct msgb *l1ctl_alloc_msg(uint8_t msg_type)
{
	struct l1ctl_hdr *l1h;
	struct msgb *msg = msgb_alloc_headroom(256, 4, "osmo_l1");

	if (!msg) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate memory\n");
		return NULL;
	}

	msg->l1h = msgb_put(msg, sizeof(*l1h));
	l1h = (struct l1ctl_hdr *) msg->l1h;
	l1h->msg_type = msg_type;

	return msg;
}

int l1ctl_tx_pm_conf(struct l1ctl_link *l1l, uint16_t band_arfcn,
	int dbm, int last)
{
	struct l1ctl_pm_conf *pmc;
	struct msgb *msg;

	msg = l1ctl_alloc_msg(L1CTL_PM_CONF);
	if (!msg)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Send PM Conf (%s %d = %d dBm)\n",
		gsm_band_name(gsm_arfcn2band(band_arfcn)),
		band_arfcn &~ ARFCN_FLAG_MASK, dbm);

	pmc = (struct l1ctl_pm_conf *) msgb_put(msg, sizeof(*pmc));
	pmc->band_arfcn = htons(band_arfcn);
	pmc->pm[0] = dbm2rxlev(dbm);
	pmc->pm[1] = 0;

	if (last) {
		struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->l1h;
		l1h->flags |= L1CTL_F_DONE;
	}

	return l1ctl_link_send(l1l, msg);
}

int l1ctl_tx_reset_ind(struct l1ctl_link *l1l, uint8_t type)
{
	struct msgb *msg;
	struct l1ctl_reset *res;

	msg = l1ctl_alloc_msg(L1CTL_RESET_IND);
	if (!msg)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Send Reset Ind (%u)\n", type);

	res = (struct l1ctl_reset *) msgb_put(msg, sizeof(*res));
	res->type = type;

	return l1ctl_link_send(l1l, msg);
}

int l1ctl_tx_reset_conf(struct l1ctl_link *l1l, uint8_t type)
{
	struct msgb *msg;
	struct l1ctl_reset *res;

	msg = l1ctl_alloc_msg(L1CTL_RESET_CONF);
	if (!msg)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Send Reset Conf (%u)\n", type);
	res = (struct l1ctl_reset *) msgb_put(msg, sizeof(*res));
	res->type = type;

	return l1ctl_link_send(l1l, msg);
}

int l1ctl_tx_fbsb_conf(struct l1ctl_link *l1l, uint8_t result,
	struct l1ctl_info_dl *dl_info, uint8_t bsic)
{
	struct l1ctl_fbsb_conf *conf;
	struct l1ctl_info_dl *dl;
	struct msgb *msg;
	size_t len;

	msg = l1ctl_alloc_msg(L1CTL_FBSB_CONF);
	if (msg == NULL)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Send FBSB Conf (result=%u, bsic=%u)\n",
		result, bsic);

	/* Copy DL info provided by handler */
	len = sizeof(struct l1ctl_info_dl);
	dl = (struct l1ctl_info_dl *) msgb_put(msg, len);
	memcpy(dl, dl_info, len);
	talloc_free(dl_info);

	/* Fill in FBSB payload: BSIC and sync result */
	conf = (struct l1ctl_fbsb_conf *) msgb_put(msg, sizeof(*conf));
	conf->result = result;
	conf->bsic = bsic;

	/* FIXME: set proper value */
	conf->initial_freq_err = 0;

	/* Ask SCH handler not to send L1CTL_FBSB_CONF anymore */
	l1l->fbsb_conf_sent = 1;

	/* Abort FBSB expire timer */
	if (osmo_timer_pending(&l1l->fbsb_timer))
		osmo_timer_del(&l1l->fbsb_timer);

	return l1ctl_link_send(l1l, msg);
}

int l1ctl_tx_ccch_mode_conf(struct l1ctl_link *l1l, uint8_t mode)
{
	struct l1ctl_ccch_mode_conf *conf;
	struct msgb *msg;

	msg = l1ctl_alloc_msg(L1CTL_CCCH_MODE_CONF);
	if (msg == NULL)
		return -ENOMEM;

	conf = (struct l1ctl_ccch_mode_conf *) msgb_put(msg, sizeof(*conf));
	conf->ccch_mode = mode;

	return l1ctl_link_send(l1l, msg);
}

int l1ctl_tx_data_ind(struct l1ctl_link *l1l, struct l1ctl_info_dl *data)
{
	struct l1ctl_info_dl *dl;
	struct msgb *msg;
	size_t len;

	msg = l1ctl_alloc_msg(L1CTL_DATA_IND);
	if (msg == NULL)
		return -ENOMEM;

	/* We store the 23-byte payload as a flexible array member */
	len = sizeof(struct l1ctl_info_dl) + 23;
	dl = (struct l1ctl_info_dl *) msgb_put(msg, len);

	/* Copy header and data from source message */
	memcpy(dl, data, len);

	/* Put message to upper layers */
	return l1ctl_link_send(l1l, msg);
}

int l1ctl_tx_rach_conf(struct l1ctl_link *l1l, uint32_t fn)
{
	struct l1ctl_info_dl *dl;
	struct msgb *msg;
	size_t len;

	msg = l1ctl_alloc_msg(L1CTL_RACH_CONF);
	if (msg == NULL)
		return -ENOMEM;

	len = sizeof(struct l1ctl_info_dl);
	dl = (struct l1ctl_info_dl *) msgb_put(msg, len);

	memset(dl, 0x00, len);
	dl->band_arfcn = htons(l1l->trx->band_arfcn);
	dl->frame_nr = htonl(fn);

	return l1ctl_link_send(l1l, msg);
}

int l1ctl_tx_data_conf(struct l1ctl_link *l1l)
{
	struct msgb *msg;

	msg = l1ctl_alloc_msg(L1CTL_DATA_CONF);
	if (msg == NULL)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Send Data Conf\n");

	return l1ctl_link_send(l1l, msg);
}

/* FBSB expire timer */
static void fbsb_timer_cb(void *data)
{
	struct l1ctl_link *l1l = (struct l1ctl_link *) data;
	struct l1ctl_fbsb_conf *conf;
	struct l1ctl_info_dl *dl;
	struct msgb *msg;
	size_t len;

	msg = l1ctl_alloc_msg(L1CTL_FBSB_CONF);
	if (msg == NULL)
		return;

	LOGP(DL1C, LOGL_DEBUG, "Send FBSB Conf (result=255, bsic=0)\n");

	/* Compose DL info header */
	len = sizeof(struct l1ctl_info_dl);
	dl = (struct l1ctl_info_dl *) msgb_put(msg, len);
	memset(dl, 0x00, len);

	/* Fill in current ARFCN */
	dl->band_arfcn = htons(l1l->trx->band_arfcn);

	/* Fill in FBSB payload: BSIC and sync result */
	conf = (struct l1ctl_fbsb_conf *) msgb_put(msg, sizeof(*conf));
	conf->result = 255;
	conf->bsic = 0;

	/* Ask SCH handler not to send L1CTL_FBSB_CONF anymore */
	l1l->fbsb_conf_sent = 1;

	l1ctl_link_send(l1l, msg);
}

static int l1ctl_rx_fbsb_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_fbsb_req *fbsb;
	uint16_t band_arfcn;
	uint16_t timeout;
	int rc = 0;

	fbsb = (struct l1ctl_fbsb_req *) msg->l1h;
	if (msgb_l1len(msg) < sizeof(*fbsb)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short FBSB Req: %u\n",
			msgb_l1len(msg));
		rc = -EINVAL;
		goto exit;
	}

	band_arfcn = ntohs(fbsb->band_arfcn);
	timeout = ntohs(fbsb->timeout);

	LOGP(DL1C, LOGL_NOTICE, "Received FBSB request (%s %d)\n",
		gsm_band_name(gsm_arfcn2band(band_arfcn)),
		band_arfcn &~ ARFCN_FLAG_MASK);

	/* Reset scheduler and clock counter */
	sched_trx_reset(l1l->trx, 1);

	/* Configure a single timeslot */
	if (fbsb->ccch_mode == CCCH_MODE_COMBINED)
		sched_trx_configure_ts(l1l->trx, 0, GSM_PCHAN_CCCH_SDCCH4);
	else
		sched_trx_configure_ts(l1l->trx, 0, GSM_PCHAN_CCCH);

	/* Ask SCH handler to send L1CTL_FBSB_CONF */
	l1l->fbsb_conf_sent = 0;

	/* Store current ARFCN */
	l1l->trx->band_arfcn = band_arfcn;

	/* Tune transceiver to required ARFCN */
	trx_if_cmd_rxtune(l1l->trx, band_arfcn);
	trx_if_cmd_txtune(l1l->trx, band_arfcn);
	trx_if_cmd_poweron(l1l->trx);

	/* Start FBSB expire timer */
	/* TODO: share FRAME_DURATION_uS=4615 from scheduler.c */
	l1l->fbsb_timer.data = l1l;
	l1l->fbsb_timer.cb = fbsb_timer_cb;
	osmo_timer_schedule(&l1l->fbsb_timer, 0, timeout * 4615);

exit:
	msgb_free(msg);
	return rc;
}

static int l1ctl_rx_pm_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	uint16_t arfcn_start, arfcn_stop;
	struct l1ctl_pm_req *pmr;
	int rc = 0;

	pmr = (struct l1ctl_pm_req *) msg->l1h;
	if (msgb_l1len(msg) < sizeof(*pmr)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short PM Req: %u\n",
			msgb_l1len(msg));
		rc = -EINVAL;
		goto exit;
	}

	arfcn_start = ntohs(pmr->range.band_arfcn_from);
	arfcn_stop  = ntohs(pmr->range.band_arfcn_to);

	LOGP(DL1C, LOGL_NOTICE, "Received power measurement "
		"request (%s: %d -> %d)\n",
		gsm_band_name(gsm_arfcn2band(arfcn_start)),
		arfcn_start &~ ARFCN_FLAG_MASK,
		arfcn_stop &~ ARFCN_FLAG_MASK);

	/* Send measurement request to transceiver */
	rc = trx_if_cmd_measure(l1l->trx, arfcn_start, arfcn_stop);

exit:
	msgb_free(msg);
	return rc;
}

static int l1ctl_rx_reset_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_reset *res;
	int rc = 0;

	res = (struct l1ctl_reset *) msg->l1h;
	if (msgb_l1len(msg) < sizeof(*res)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Reset Req: %u\n",
			msgb_l1len(msg));
		rc = -EINVAL;
		goto exit;
	}

	LOGP(DL1C, LOGL_NOTICE, "Received reset request (%u)\n",
		res->type);

	switch (res->type) {
	case L1CTL_RES_T_FULL:
		/* TODO: implement trx_if_reset() */
		trx_if_cmd_poweroff(l1l->trx);
		trx_if_cmd_echo(l1l->trx);

		/* Fall through */
	case L1CTL_RES_T_SCHED:
		sched_trx_reset(l1l->trx, 1);
		break;
	default:
		LOGP(DL1C, LOGL_ERROR, "Unknown L1CTL_RESET_REQ type\n");
		goto exit;
	}

	/* Confirm */
	rc = l1ctl_tx_reset_conf(l1l, res->type);

exit:
	msgb_free(msg);
	return rc;
}

static int l1ctl_rx_echo_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_hdr *l1h;

	LOGP(DL1C, LOGL_NOTICE, "Recv Echo Req\n");
	LOGP(DL1C, LOGL_NOTICE, "Send Echo Conf\n");

	/* Nothing to do, just send it back */
	l1h = (struct l1ctl_hdr *) msg->l1h;
	l1h->msg_type = L1CTL_ECHO_CONF;
	msg->data = msg->l1h;

	return l1ctl_link_send(l1l, msg);
}

static int l1ctl_rx_ccch_mode_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_ccch_mode_req *req;
	int mode, rc = 0;

	req = (struct l1ctl_ccch_mode_req *) msg->l1h;
	if (msgb_l1len(msg) < sizeof(*req)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Reset Req: %u\n",
			msgb_l1len(msg));
		rc = -EINVAL;
		goto exit;
	}

	LOGP(DL1C, LOGL_NOTICE, "Received CCCH mode request (%s)\n",
		req->ccch_mode == CCCH_MODE_COMBINED ?
			"combined" : "not combined");

	/* Reconfigure TS0 */
	mode = req->ccch_mode == CCCH_MODE_COMBINED ?
		GSM_PCHAN_CCCH_SDCCH4 : GSM_PCHAN_CCCH;
	rc = sched_trx_configure_ts(l1l->trx, 0, mode);

	/* Confirm reconfiguration */
	if (!rc)
		rc = l1ctl_tx_ccch_mode_conf(l1l, req->ccch_mode);

exit:
	msgb_free(msg);
	return rc;
}

static int l1ctl_rx_rach_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_rach_req *req;
	struct l1ctl_info_ul *ul;
	struct trx_ts_prim *prim;
	struct trx_ts *ts;
	int len, rc = 0;

	ul = (struct l1ctl_info_ul *) msg->l1h;
	req = (struct l1ctl_rach_req *) ul->payload;
	len = sizeof(struct l1ctl_rach_req);

	/* Convert offset value to host format */
	req->offset = ntohs(req->offset);

	LOGP(DL1C, LOGL_NOTICE, "Received RACH request "
		"(offset=%u ra=0x%02x)\n", req->offset, req->ra);

	/* FIXME: can we use other than TS0? */
	ts = l1l->trx->ts_list[0];
	if (ts == NULL) {
		LOGP(DL1C, LOGL_DEBUG, "Couldn't send RACH: "
			"TS0 is not active\n");
		rc = -EINVAL;
		goto exit;
	}

	/* Allocate a new primitive */
	prim = talloc_zero_size(ts, sizeof(struct trx_ts_prim) + len);
	if (prim == NULL) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate memory\n");
		rc = -ENOMEM;
		goto exit;
	}

	/* Set logical channel of primitive */
	prim->chan = TRXC_RACH;

	/* Fill in the payload */
	memcpy(prim->payload, req, len);

	/* Add to TS queue */
	llist_add_tail(&prim->list, &ts->tx_prims);

exit:
	msgb_free(msg);
	return rc;
}

static int l1ctl_rx_dm_est_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	enum gsm_phys_chan_config config;
	enum trx_lchan_type lchan_type;
	struct l1ctl_dm_est_req *est_req;
	struct l1ctl_info_ul *ul;
	struct trx_ts *ts;
	uint16_t band_arfcn;
	uint8_t chan_nr, tn;
	int rc = 0;

	ul = (struct l1ctl_info_ul *) msg->l1h;
	est_req = (struct l1ctl_dm_est_req *) ul->payload;

	band_arfcn = ntohs(est_req->h0.band_arfcn);
	chan_nr = ul->chan_nr;

	LOGP(DL1C, LOGL_NOTICE, "Received L1CTL_DM_EST_REQ (arfcn=%u, "
		"chan_nr=0x%02x, tsc=%u)\n", (band_arfcn &~ ARFCN_FLAG_MASK),
		chan_nr, est_req->tsc);

	if (est_req->h) {
		LOGP(DL1C, LOGL_ERROR, "FHSS is not supported\n");
		rc = -ENOTSUP;
		goto exit;
	}

	/* Update TSC (Training Sequence Code) */
	l1l->trx->tsc = est_req->tsc;

	/* Determine channel config */
	config = sched_trx_chan_nr2pchan_config(chan_nr);
	if (config == GSM_PCHAN_NONE) {
		LOGP(DL1C, LOGL_ERROR, "Couldn't determine channel config\n");
		rc = -EINVAL;
		goto exit;
	}

	/* Determine TS index */
	tn = chan_nr & 0x7;
	if (tn > 7) {
		LOGP(DL1C, LOGL_ERROR, "Incorrect TS index %u\n", tn);
		rc = -EINVAL;
		goto exit;
	}

	/* Determine lchan type */
	lchan_type = sched_trx_chan_nr2lchan_type(chan_nr);
	if (!lchan_type) {
		LOGP(DL1C, LOGL_ERROR, "Couldn't determine lchan type\n");
		rc = -EINVAL;
		goto exit;
	}

	/* Configure requested TS */
	rc = sched_trx_configure_ts(l1l->trx, tn, config);
	ts = l1l->trx->ts_list[tn];
	if (rc) {
		rc = -EINVAL;
		goto exit;
	}

	/* Activate only requested lchan, disabling others */
	sched_trx_deactivate_all_lchans(ts);
	rc = sched_trx_activate_lchan(ts, lchan_type);
	if (rc) {
		LOGP(DL1C, LOGL_ERROR, "Couldn't activate lchan\n");
		rc = -EINVAL;
		goto exit;
	}

exit:
	msgb_free(msg);
	return rc;
}

static int l1ctl_rx_dm_rel_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	LOGP(DL1C, LOGL_NOTICE, "Received L1CTL_DM_REL_REQ, "
		"switching back to CCCH\n");

	/* Reset scheduler */
	sched_trx_reset(l1l->trx, 0);

	msgb_free(msg);
	return 0;
}

static int l1ctl_rx_data_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct trx_ts *ts;
	struct trx_ts_prim *prim;
	struct l1ctl_info_ul *ul;
	struct l1ctl_data_ind *data_ind;
	enum trx_lchan_type lchan_type;
	uint8_t chan_nr, tn;
	size_t len;
	int rc = 0;

	ul = (struct l1ctl_info_ul *) msg->l1h;
	data_ind = (struct l1ctl_data_ind *) ul->payload;
	chan_nr = ul->chan_nr;

	LOGP(DL1C, LOGL_DEBUG, "Recv Data Req (chan_nr=0x%02x)\n", chan_nr);

	/* Determine TS index */
	tn = chan_nr & 0x7;
	if (tn > 7) {
		LOGP(DL1C, LOGL_ERROR, "Incorrect TS index %u\n", tn);
		rc = -EINVAL;
		goto exit;
	}

	/* Determine lchan type */
	lchan_type = sched_trx_chan_nr2lchan_type(chan_nr);
	if (!lchan_type) {
		LOGP(DL1C, LOGL_ERROR, "Couldn't determine lchan type\n");
		rc = -EINVAL;
		goto exit;
	}

	/* Check whether required timeslot is allocated and configured */
	ts = l1l->trx->ts_list[tn];
	if (ts == NULL || ts->mf_layout == NULL) {
		LOGP(DL1C, LOGL_ERROR, "Timeslot %u isn't configured\n", tn);
		rc = -EINVAL;
		goto exit;
	}

	/* Allocate a new primitive */
	len = sizeof(struct trx_ts_prim) + sizeof(struct l1ctl_info_ul) + 23;
	prim = talloc_zero_size(ts, len);
	if (prim == NULL) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate memory\n");
		rc = -ENOMEM;
		goto exit;
	}

	/* Set logical channel of primitive */
	prim->chan = lchan_type;

	/* Fill in both UL info and payload */
	len = sizeof(struct l1ctl_info_ul);
	memcpy(prim->payload, ul, len);
	memcpy(prim->payload + len, data_ind, 23);

	/* Add to TS queue */
	llist_add_tail(&prim->list, &ts->tx_prims);

exit:
	msgb_free(msg);
	return rc;
}

static int l1ctl_rx_param_req(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_par_req *par_req;
	struct l1ctl_info_ul *ul;

	ul = (struct l1ctl_info_ul *) msg->l1h;
	par_req = (struct l1ctl_par_req *) ul->payload;

	LOGP(DL1C, LOGL_NOTICE, "Received L1CTL_PARAM_REQ "
		"(ta=%d, tx_power=%u)\n", par_req->ta, par_req->tx_power);

	l1l->trx->ta = par_req->ta;
	l1l->trx->tx_power = par_req->tx_power;

	msgb_free(msg);
	return 0;
}

int l1ctl_rx_cb(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_hdr *l1h;

	l1h = (struct l1ctl_hdr *) msg->l1h;
	msg->l1h = l1h->data;

	switch (l1h->msg_type) {
	case L1CTL_FBSB_REQ:
		return l1ctl_rx_fbsb_req(l1l, msg);
	case L1CTL_PM_REQ:
		return l1ctl_rx_pm_req(l1l, msg);
	case L1CTL_RESET_REQ:
		return l1ctl_rx_reset_req(l1l, msg);
	case L1CTL_ECHO_REQ:
		return l1ctl_rx_echo_req(l1l, msg);
	case L1CTL_CCCH_MODE_REQ:
		return l1ctl_rx_ccch_mode_req(l1l, msg);
	case L1CTL_RACH_REQ:
		return l1ctl_rx_rach_req(l1l, msg);
	case L1CTL_DM_EST_REQ:
		return l1ctl_rx_dm_est_req(l1l, msg);
	case L1CTL_DM_REL_REQ:
		return l1ctl_rx_dm_rel_req(l1l, msg);
	case L1CTL_DATA_REQ:
		return l1ctl_rx_data_req(l1l, msg);
	case L1CTL_PARAM_REQ:
		return l1ctl_rx_param_req(l1l, msg);
	default:
		LOGP(DL1C, LOGL_ERROR, "Unknown MSG: %u\n", l1h->msg_type);
		msgb_free(msg);
		return -EINVAL;
	}
}

void l1ctl_shutdown_cb(struct l1ctl_link *l1l)
{
	/* Abort FBSB expire timer */
	if (osmo_timer_pending(&l1l->fbsb_timer))
		osmo_timer_del(&l1l->fbsb_timer);
}
