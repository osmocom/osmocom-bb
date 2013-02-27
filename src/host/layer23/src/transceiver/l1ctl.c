/*
 * l1ctl.c
 *
 * Minimal L1CTL
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
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

#include <stdlib.h>
#include <string.h>

#include <osmocom/core/select.h>
#include <osmocom/core/talloc.h>

#include <osmocom/bb/common/logging.h>

#include <errno.h>
#include <arpa/inet.h>

#include <osmocom/gsm/gsm_utils.h>

#include <l1ctl_proto.h>

#include "app.h"
#include "burst.h"
#include "demod.h"
#include "trx.h"


/* ------------------------------------------------------------------------ */
/* L1CTL exported API                                                       */
/* ------------------------------------------------------------------------ */

static struct msgb *
_l1ctl_alloc(uint8_t msg_type)
{
	struct l1ctl_hdr *l1h;
	struct msgb *msg = msgb_alloc_headroom(256, 4, "osmo_l1");

	if (!msg) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate memory.\n");
		return NULL;
	}

	msg->l1h = msgb_put(msg, sizeof(*l1h));
	l1h = (struct l1ctl_hdr *) msg->l1h;
	l1h->msg_type = msg_type;

	return msg;
}

int
l1ctl_tx_reset_req(struct l1ctl_link *l1l, uint8_t type)
{
	struct msgb *msg;
	struct l1ctl_reset *res;

	msg = _l1ctl_alloc(L1CTL_RESET_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "Tx Reset Req (%u)\n", type);
	res = (struct l1ctl_reset *) msgb_put(msg, sizeof(*res));
	res->type = type;

	return l1l_send(l1l, msg);
}

int
l1ctl_tx_fbsb_req(struct l1ctl_link *l1l,
	uint16_t arfcn, uint8_t flags, uint16_t timeout,
	uint8_t sync_info_idx, uint8_t ccch_mode)
{
	struct msgb *msg;
	struct l1ctl_fbsb_req *req;

	LOGP(DL1C, LOGL_INFO, "Sync Req\n");

	msg = _l1ctl_alloc(L1CTL_FBSB_REQ);
	if (!msg)
		return -1;

	req = (struct l1ctl_fbsb_req *) msgb_put(msg, sizeof(*req));
	req->band_arfcn = htons(arfcn);
	req->timeout = htons(timeout);
	/* Threshold when to consider FB_MODE1: 4kHz - 1kHz */
	req->freq_err_thresh1 = htons(11000 - 1000);
	/* Threshold when to consider SCH: 1kHz - 200Hz */
	req->freq_err_thresh2 = htons(1000 - 200);
	/* not used yet! */
	req->num_freqerr_avg = 3;
	req->flags = flags;
	req->sync_info_idx = sync_info_idx;
	req->ccch_mode = ccch_mode;

	return l1l_send(l1l, msg);
}

int
l1ctl_tx_bts_mode(struct l1ctl_link *l1l, uint8_t enabled, uint8_t *type,
	uint8_t bsic, uint16_t band_arfcn, int gain, uint8_t tx_mask,
	uint8_t rx_mask)
{
	struct msgb *msg;
	struct l1ctl_bts_mode *be;
	int i;

	msg = _l1ctl_alloc(L1CTL_BTS_MODE);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "BTS Mode (enabled=%u, bsic=%u, arfcn=%u "
		"gain=%d)\n", enabled, bsic, band_arfcn, gain);

	be = (struct l1ctl_bts_mode *) msgb_put(msg, sizeof(*be));
	be->enabled = enabled;
	for (i = 0; i < 8; i++)
		be->type[i] = type[i];
	be->bsic = bsic;
	be->band_arfcn = htons(band_arfcn);
	be->gain = gain;
	be->tx_mask = tx_mask;
	be->rx_mask = rx_mask;

	return l1l_send(l1l, msg);
}

int
l1ctl_tx_bts_burst_req(struct l1ctl_link *l1l,
                       uint32_t fn, uint8_t tn, struct burst_data *burst)
{
	struct msgb *msg;
	struct l1ctl_bts_burst_req *br;

	msg = _l1ctl_alloc(L1CTL_BTS_BURST_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "BTS Tx Burst (%d:%d)\n", fn, tn);

	br = (struct l1ctl_bts_burst_req *) msgb_put(msg, sizeof(*br));
	br->fn = htonl(fn);
	br->tn = tn;
	br->type = burst->type;

	if (burst->type == BURST_NB)
		memcpy(msgb_put(msg, 15), burst->data, 15);

	return l1l_send(l1l, msg);
}


/* ------------------------------------------------------------------------ */
/* L1CTL Receive handling                                                   */
/* ------------------------------------------------------------------------ */

static int
_l1ctl_rx_bts_burst_nb_ind(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_bts_burst_nb_ind *bi;
	uint32_t fn;
	int rc, i;
	sbit_t data[148], steal[2];
	float toa;
	int8_t rssi;

	bi = (struct l1ctl_bts_burst_nb_ind *) msg->l1h;

	if (msgb_l1len(msg) < sizeof(*bi)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Burst NB Ind: %u\n",
		     msgb_l2len(msg));
		rc = -EINVAL;
		goto exit;
	}

	fn = ntohl(bi->fn);

	LOGP(DL1C, LOGL_INFO, "Normal Burst Indication (fn=%d)\n", fn);

	toa = bi->toa * 1.0F;

	rssi = bi->rssi;

	memset(data, 0x00, 148);

	osmo_pbit2ubit_ext((ubit_t*)data,  3, bi->data,  0, 57, 0);
	osmo_pbit2ubit_ext((ubit_t*)data, 88, bi->data, 57, 57, 0);
	osmo_pbit2ubit_ext((ubit_t*)steal, 0, bi->data, 114, 2, 0);
	data[60] = steal[1];
	data[87] = steal[0];

	for (i=0; i<148; i++)
		data[i] = data[i] ? -127 : 127;

	trx_data_ind(l1l->trx, fn, bi->tn, data, toa, rssi);

exit:
	msgb_free(msg);

	return rc;
}

static int
_l1ctl_rx_bts_burst_ab_ind(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_bts_burst_ab_ind *bi;
	uint32_t fn;
	int rc;
	sbit_t data[148];
	float toa;

	bi = (struct l1ctl_bts_burst_ab_ind *) msg->l1h;

	if (msgb_l1len(msg) < sizeof(*bi)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Burst AB Ind: %u\n",
		     msgb_l2len(msg));
		rc = -EINVAL;
		goto exit;
	}

	fn = ntohl(bi->fn);

	rc = gsm_ab_ind_process(l1l->as, bi, data, &toa);
	if (rc < 0)
		goto exit;

	toa += bi->toa;

	LOGP(DL1C, LOGL_INFO, "Access Burst Indication (fn=%d iq toa=%f)\n", fn, toa);

	trx_data_ind(l1l->trx, fn, 0, data, toa, 0);
exit:
	msgb_free(msg);

	return rc;
}

static int
_l1ctl_rx_data_ind(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;
	struct l1ctl_data_ind *di;
	int rc;

	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = dl->payload;
	di = (struct l1ctl_data_ind *) msg->l2h;

	if (msgb_l1len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "Short Layer2 message: %u\n",
		     msgb_l2len(msg));
		rc = -EINVAL;
		goto exit;
	}

	if (msgb_l2len(msg) < sizeof(*di)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Data Ind: %u\n",
		     msgb_l3len(msg));
		rc = -EINVAL;
		goto exit;
	}

#if 0
	if (dl->fire_crc > 2) {
		LOGP(DAPP, LOGL_INFO, "Invalid BCCH message, retry sync ...\n");
		rc = l1ctl_tx_fbsb_req(&as->l1l, as->arfcn_sync, L1CTL_FBSB_F_FB01SB, 100, 0, CCCH_MODE_NONE);
	} else {
		LOGP(DAPP, LOGL_INFO, "Valid BCCH message, sync ok -> switch to BTS mode\n");

		rc = 0;
	}
#endif

	/* forward clock of fist l1ctl_link instance only */
	if (l1l == &l1l->as->l1l[0])
		rc = trx_clk_ind(l1l->trx, ntohl(dl->frame_nr));

exit:
	msgb_free(msg);

	return rc;
}

static int
_l1ctl_rx_fbsb_conf(struct l1ctl_link *l1l, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;
	struct l1ctl_fbsb_conf *sc;
	int rc;

	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = dl->payload;
	sc = (struct l1ctl_fbsb_conf *) msg->l2h;

	if (msgb_l1len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "Short Layer2 message: %u\n",
		     msgb_l2len(msg));
		rc = -EINVAL;
		goto exit;
	}

	if (msgb_l2len(msg) < sizeof(*sc)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short FBSB Conf: %u\n",
		     msgb_l3len(msg));
		rc = -EINVAL;
		goto exit;
	}

	if (sc->result != 0) {
		LOGP(DAPP, LOGL_INFO, "Sync failed, retrying ... \n");
		rc = l1ctl_tx_fbsb_req(l1l, l1l->as->arfcn_sync, L1CTL_FBSB_F_FB01SB, 100, 0, CCCH_MODE_NONE);
	} else {
		LOGP(DAPP, LOGL_INFO, "Sync acquired, wait for BCCH ...\n");
	}

	rc = 0;

exit:
	msgb_free(msg);

	return rc;
}

int
l1ctl_recv(void *data, struct msgb *msg)
{
	struct l1ctl_link *l1l = data;
	struct l1ctl_hdr *l1h;
	int rc = 0;

	/* move the l1 header pointer to point _BEHIND_ l1ctl_hdr,
	   as the l1ctl header is of no interest to subsequent code */
	l1h = (struct l1ctl_hdr *) msg->l1h;
	msg->l1h = l1h->data;

	/* Act */
	switch (l1h->msg_type) {
	case L1CTL_BTS_BURST_AB_IND:
		rc = _l1ctl_rx_bts_burst_ab_ind(l1l, msg);
		break;

	case L1CTL_BTS_BURST_NB_IND:
		rc = _l1ctl_rx_bts_burst_nb_ind(l1l, msg);
		break;

	case L1CTL_DATA_IND:
		rc = _l1ctl_rx_data_ind(l1l, msg);
		break;

	case L1CTL_FBSB_CONF:
		rc = _l1ctl_rx_fbsb_conf(l1l, msg);
		break;

	case L1CTL_RESET_IND:
	case L1CTL_RESET_CONF:
		LOGP(DAPP, LOGL_INFO, "Reset received: Starting sync.\n");
		l1ctl_tx_fbsb_req(l1l, l1l->as->arfcn_sync, L1CTL_FBSB_F_FB01SB, 100, 0, CCCH_MODE_NONE);
		msgb_free(msg);
		break;

	default:
		LOGP(DL1C, LOGL_ERROR, "Unknown MSG: %u\n", l1h->msg_type);
		msgb_free(msg);
		break;
	}

	return rc;
}
