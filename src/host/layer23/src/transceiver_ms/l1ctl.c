/*
 * l1ctl.c
 *
 * L1CTL interface
 *
 * Copyright (C) 2014  Sylvain Munaut <tnt@246tNt.com>
 *
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

#include <errno.h>
#include <arpa/inet.h>

#include <osmocom/core/select.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/bb/common/logging.h>
#include <l1ctl_proto.h>

#include "app.h"
#include "l1ctl.h"
#include "l1ctl_link.h"
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
l1ctl_tx_pm_conf(struct l1ctl_link *l1l, uint16_t band_arfcn, int dbm, int last)
{
	struct msgb *msg;
	struct l1ctl_pm_conf *pmc;

	msg = _l1ctl_alloc(L1CTL_PM_CONF);
	if (!msg)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Tx PM Conf (%s %d = %d dBm)\n",
		gsm_band_name(gsm_arfcn2band(band_arfcn)),
		band_arfcn &~ ARFCN_FLAG_MASK,
		dbm
	);

	pmc = (struct l1ctl_pm_conf *) msgb_put(msg, sizeof(*pmc));
	pmc->band_arfcn = htons(band_arfcn);
	pmc->pm[0] = dbm2rxlev(dbm);
	pmc->pm[1] = 0;

	if (last) {
		struct l1ctl_hdr *l1h = ( struct l1ctl_hdr *)msg->l1h;
		l1h->flags |= L1CTL_F_DONE;
	}

	return l1l_send(l1l, msg);
}

int
l1ctl_tx_reset_ind(struct l1ctl_link *l1l, uint8_t type)
{
	struct msgb *msg;
	struct l1ctl_reset *res;

	msg = _l1ctl_alloc(L1CTL_RESET_IND);
	if (!msg)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Tx Reset Ind (%u)\n", type);

	res = (struct l1ctl_reset *) msgb_put(msg, sizeof(*res));
	res->type = type;

	return l1l_send(l1l, msg);
}

int
l1ctl_tx_reset_conf(struct l1ctl_link *l1l, uint8_t type)
{
	struct msgb *msg;
	struct l1ctl_reset *res;

	msg = _l1ctl_alloc(L1CTL_RESET_CONF);
	if (!msg)
		return -ENOMEM;

	LOGP(DL1C, LOGL_DEBUG, "Tx Reset Conf (%u)\n", type);
	res = (struct l1ctl_reset *) msgb_put(msg, sizeof(*res));
	res->type = type;

	return l1l_send(l1l, msg);
}


/* ------------------------------------------------------------------------ */
/* L1CTL Receive handling                                                   */
/* ------------------------------------------------------------------------ */

static int
_l1ctl_rx_fbsb_req(struct app_state *as, struct msgb *msg)
{
	struct l1ctl_fbsb_req *fbsb;
	uint16_t band_arfcn;
	int rc = 0;

	/* Grab message */
	fbsb = (struct l1ctl_fbsb_req *) msg->l1h;

	if (msgb_l1len(msg) < sizeof(*fbsb)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short FBSB Req: %u\n",
		     msgb_l1len(msg));
		rc = -EINVAL;
		goto exit;
	}

	band_arfcn = ntohs(fbsb->band_arfcn);

	LOGP(DL1C, LOGL_DEBUG, "Rx FBSB Req (%s %d)\n",
		gsm_band_name(gsm_arfcn2band(band_arfcn)),
		band_arfcn &~ ARFCN_FLAG_MASK
	);

	/* Send request to TRX */
	trx_ctrl_send_cmd(as->trx, "TXTUNE", "%d",
		(int)gsm_arfcn2freq10(band_arfcn, 1) * 100);
	trx_ctrl_send_cmd(as->trx, "RXTUNE", "%d",
		(int)gsm_arfcn2freq10(band_arfcn, 0) * 100);
	trx_ctrl_send_cmd(as->trx, "POWERON", NULL);
	trx_ctrl_send_cmd(as->trx, "SYNC", NULL);

exit:
	msgb_free(msg);

	return rc;
}

static int
_l1ctl_rx_pm_req(struct app_state *as, struct msgb *msg)
{
	struct l1ctl_pm_req *pmr;
	uint16_t arfcn_start, arfcn_stop, arfcn;
	int rc = 0;

	/* Grab message */
	pmr = (struct l1ctl_pm_req *) msg->l1h;

	if (msgb_l1len(msg) < sizeof(*pmr)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short PM Req: %u\n",
		     msgb_l1len(msg));
		rc = -EINVAL;
		goto exit;
	}

	arfcn_start = ntohs(pmr->range.band_arfcn_from);
	arfcn_stop  = ntohs(pmr->range.band_arfcn_to);

	LOGP(DL1C, LOGL_DEBUG, "Rx PM Req (%s: %d -> %d)\n",
		gsm_band_name(gsm_arfcn2band(arfcn_start)),
		arfcn_start &~ ARFCN_FLAG_MASK,
		arfcn_stop  &~ ARFCN_FLAG_MASK
	);

	/* Send fake responses */
	for (arfcn=arfcn_start; arfcn<=arfcn_stop; arfcn++)
	{
		l1ctl_tx_pm_conf(as->l1l, arfcn, arfcn == 36 ? -60 : -120, arfcn == arfcn_stop);
	}

exit:
	msgb_free(msg);

	return rc;
}

static int
_l1ctl_rx_reset_req(struct app_state *as, struct msgb *msg)
{
	struct l1ctl_reset *res;
	int rc = 0;

	/* Grab message */
	res = (struct l1ctl_reset *) msg->l1h;

	if (msgb_l1len(msg) < sizeof(*res)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Reset Req: %u\n",
		     msgb_l1len(msg));
		rc = -EINVAL;
		goto exit;
	}

	LOGP(DL1C, LOGL_DEBUG, "Rx Reset Req (%u)\n", res->type);

	/* Request power off */
	trx_ctrl_send_cmd(as->trx, "POWEROFF", NULL);

	/* Simply confirm */
	rc = l1ctl_tx_reset_conf(as->l1l, res->type);

exit:
	msgb_free(msg);

	return rc;
}


static int
_l1ctl_recv(void *data, struct msgb *msg)
{
	struct app_state *as = data;
	struct l1ctl_hdr *l1h;
	int rc = 0;

	/* move the l1 header pointer to point _BEHIND_ l1ctl_hdr,
	   as the l1ctl header is of no interest to subsequent code */
	l1h = (struct l1ctl_hdr *) msg->l1h;
	msg->l1h = l1h->data;

	/* Act */
	switch (l1h->msg_type) {
	case L1CTL_FBSB_REQ:
		rc = _l1ctl_rx_fbsb_req(as, msg);
		break;
	case L1CTL_PM_REQ:
		rc = _l1ctl_rx_pm_req(as, msg);
		break;
	case L1CTL_RESET_REQ:
		rc = _l1ctl_rx_reset_req(as, msg);
		break;
	default:
		LOGP(DL1C, LOGL_ERROR, "Unknown MSG: %u\n", l1h->msg_type);
		msgb_free(msg);
	}

	return rc;
}

int
l1ctl_new_cb(void *data, struct l1ctl_link *l1l)
{
	struct app_state *as = data;

	LOGP(DL1C, LOGL_INFO, "New L1CTL connection\n");

	/* Close previous link */
	if (as->l1l) {
		LOGP(DL1C, LOGL_INFO, "Closing old L1CTL connection\n");
		l1l_close(as->l1l);
	}

	/* Setup new one */
	as->l1l = l1l;

	l1l->cb =  _l1ctl_recv;
	l1l->cb_data = data;

	/* Send reset ind */
	l1ctl_tx_reset_ind(l1l, L1CTL_RES_T_BOOT);

	/* Done */
	return 0;
}
