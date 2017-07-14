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

extern void *tall_trx_ctx;
extern struct osmo_fsm_inst *trxcon_fsm;

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

	LOGP(DL1C, LOGL_DEBUG, "Recv FBSB Req (%s %d)\n",
		gsm_band_name(gsm_arfcn2band(band_arfcn)),
		band_arfcn &~ ARFCN_FLAG_MASK);

	/* Reset L1 */
	sched_trx_reset(l1l->trx);

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

	LOGP(DL1C, LOGL_DEBUG, "Recv PM Req (%s: %d -> %d)\n",
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

	LOGP(DL1C, LOGL_DEBUG, "Recv Reset Req (%u)\n", res->type);

	switch (res->type) {
	case L1CTL_RES_T_FULL:
		/* TODO: implement trx_if_reset() */
		trx_if_flush_ctrl(l1l->trx);
		trx_if_cmd_poweroff(l1l->trx);
		trx_if_cmd_echo(l1l->trx);

		/* Fall through */
	case L1CTL_RES_T_SCHED:
		sched_trx_reset(l1l->trx);
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

	LOGP(DL1C, LOGL_DEBUG, "Recv CCCH Mode Req (%u)\n", req->ccch_mode);

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

	LOGP(DL1C, LOGL_DEBUG, "Recv RACH Req (offset=%u ra=0x%02x)\n",
		req->offset, req->ra);

	/* FIXME: can we use other than TS0? */
	ts = sched_trx_find_ts(l1l->trx, 0);
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
	default:
		LOGP(DL1C, LOGL_ERROR, "Unknown MSG: %u\n", l1h->msg_type);
		msgb_free(msg);
		return -EINVAL;
	}
}
