/* Layer1 control code, talking L1CTL protocol with L1 on the phone */

/* (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <arpa/inet.h>

#include <l1a_l23_interface.h>

#include <osmocore/signal.h>
#include <osmocore/logging.h>
#include <osmocore/timer.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/gsm_utils.h>
#include <osmocore/protocol/gsm_04_08.h>
#include <osmocore/protocol/gsm_08_58.h>
#include <osmocore/rsl.h>

#include <osmocom/l1ctl.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/lapdm.h>
#include <osmocom/logging.h>
#include <osmocom/gsmtap_util.h>

static struct msgb *osmo_l1_alloc(uint8_t msg_type)
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


static int osmo_make_band_arfcn(struct osmocom_ms *ms)
{
	/* TODO: Include the band */
	return ms->arfcn;
}

static int rx_l1_ccch_resp(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;
	struct l1ctl_sync_new_ccch_resp *sb;
	struct gsm_time tm;

	if (msgb_l3len(msg) < sizeof(*sb)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short for CCCH RESP: %u\n",
			msgb_l3len(msg));
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	sb = (struct l1ctl_sync_new_ccch_resp *) dl->payload;

	gsm_fn2gsmtime(&tm, ntohl(dl->frame_nr));
	DEBUGP(DL1C, "SCH: SNR: %u TDMA: (%.4u/%.2u/%.2u) bsic: %d\n",
		dl->snr, tm.t1, tm.t2, tm.t3, sb->bsic);

	return 0;
}

char *chan_nr2string(uint8_t chan_nr)
{
	static char str[20];
	uint8_t cbits = chan_nr >> 3;

	str[0] = '\0';

	if (cbits == 0x01)
		sprintf(str, "TCH/F");
	else if ((cbits & 0x1e) == 0x02)
		sprintf(str, "TCH/H(%u)", cbits & 0x01);
	else if ((cbits & 0x1c) == 0x04)
		sprintf(str, "SDCCH/4(%u)", cbits & 0x03);
	else if ((cbits & 0x18) == 0x08)
		sprintf(str, "SDCCH/8(%u)", cbits & 0x07);
	else if (cbits == 0x10)
		sprintf(str, "BCCH");
	else if (cbits == 0x11)
		sprintf(str, "RACH");
	else if (cbits == 0x12)
		sprintf(str, "PCH/AGCH");
	else
		sprintf(str, "UNKNOWN");

	return str;
}

/* Receive L1CTL_DATA_IND (Data Indication from L1) */
static int rx_ph_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl, dl_cpy;
	struct l1ctl_data_ind *ccch;
	struct lapdm_entity *le;
	uint8_t chan_type, chan_ts, chan_ss;
	uint8_t gsmtap_chan_type;
	struct gsm_time tm;

	if (msgb_l3len(msg) < sizeof(*ccch)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Data Ind: %u\n",
			msgb_l3len(msg));
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = dl->payload;
	ccch = (struct l1ctl_data_ind *) msg->l2h;

	gsm_fn2gsmtime(&tm, ntohl(dl->frame_nr));
	rsl_dec_chan_nr(dl->chan_nr, &chan_type, &chan_ss, &chan_ts);
	DEBUGP(DL1C, "%s (%.4u/%.2u/%.2u) %s\n",
		chan_nr2string(dl->chan_nr), tm.t1, tm.t2, tm.t3,
		hexdump(ccch->data, sizeof(ccch->data)));

	/* send CCCH data via GSMTAP */
	gsmtap_chan_type = chantype_rsl2gsmtap(chan_type, dl->link_id);
	gsmtap_sendmsg(dl->band_arfcn, chan_ts, gsmtap_chan_type, chan_ss,
			tm.fn, dl->rx_level-110, dl->snr, ccch->data,
			sizeof(ccch->data));

	/* determine LAPDm entity based on SACCH or not */
	if (dl->link_id & 0x40)
		le = &ms->l2_entity.lapdm_acch;
	else
		le = &ms->l2_entity.lapdm_dcch;
	/* make local stack copy of l1ctl_info_dl, as LAPDm will
	 * overwrite skb hdr */
	memcpy(&dl_cpy, dl, sizeof(dl_cpy));

	/* pull the L1 header from the msgb */
	msgb_pull(msg, msg->l2h - (msg->l1h-sizeof(struct l1ctl_hdr)));
	msg->l1h = NULL;

	/* send it up into LAPDm */
	l2_ph_data_ind(msg, le, &dl_cpy);

	return 0;
}

/* Transmit L1CTL_DATA_REQ */
int tx_ph_data_req(struct osmocom_ms *ms, struct msgb *msg,
		   uint8_t chan_nr, uint8_t link_id)
{
	struct l1ctl_hdr *l1h;
	struct l1ctl_info_ul *l1i_ul;
	uint8_t chan_type, chan_ts, chan_ss;
	uint8_t gsmtap_chan_type;

	DEBUGP(DL1C, "(%s)\n", hexdump(msg->l2h, msgb_l2len(msg)));

	if (msgb_l2len(msg) > 23) {
		LOGP(DL1C, LOGL_ERROR, "L1 cannot handle message length "
			"> 23 (%u)\n", msgb_l2len(msg));
		msgb_free(msg);
		return -EINVAL;
	} else if (msgb_l2len(msg) < 23)
		LOGP(DL1C, LOGL_ERROR, "L1 message length < 23 (%u) "
			"doesn't seem right!\n", msgb_l2len(msg));

	/* send copy via GSMTAP */
	rsl_dec_chan_nr(chan_nr, &chan_type, &chan_ss, &chan_ts);
	gsmtap_chan_type = chantype_rsl2gsmtap(chan_type, link_id);
	gsmtap_sendmsg(0|0x4000, chan_ts, gsmtap_chan_type, chan_ss,
			0, 127, 255, msg->l2h, msgb_l2len(msg));

	/* prepend uplink info header */
	l1i_ul = (struct l1ctl_info_ul *) msgb_push(msg, sizeof(*l1i_ul));

	l1i_ul->chan_nr = chan_nr;
	l1i_ul->link_id = link_id;

	/* FIXME: where to get this from? */
	l1i_ul->tx_power = 0;

	/* prepend l1 header */
	msg->l1h = msgb_push(msg, sizeof(*l1h));
	l1h = (struct l1ctl_hdr *) msg->l1h;
	l1h->msg_type = L1CTL_DATA_REQ;

	return osmo_send_l1(ms, msg);
}

/* Transmit NEW_CCCH_REQ */
int l1ctl_tx_ccch_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	struct l1ctl_sync_new_ccch_req *req;

	msg = osmo_l1_alloc(L1CTL_NEW_CCCH_REQ);
	if (!msg)
		return -1;

	req = (struct l1ctl_sync_new_ccch_req *) msgb_put(msg, sizeof(*req));
	req->band_arfcn = osmo_make_band_arfcn(ms);

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_RACH_REQ */
int tx_ph_rach_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_rach_req *req;
	static uint8_t i = 0;

	msg = osmo_l1_alloc(L1CTL_RACH_REQ);
	if (!msg)
		return -1;

	DEBUGP(DL1C, "RACH Req.\n");
	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	req = (struct l1ctl_rach_req *) msgb_put(msg, sizeof(*req));
	req->ra = i++;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_DM_EST_REQ */
int tx_ph_dm_est_req(struct osmocom_ms *ms, uint16_t band_arfcn, uint8_t chan_nr)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_dm_est_req *req;

	msg = osmo_l1_alloc(L1CTL_DM_EST_REQ);
	if (!msg)
		return -1;

	DEBUGP(DL1C, "Tx Dedic.Mode Est Req (arfcn=%u, chan_nr=0x%02x)\n",
		band_arfcn, chan_nr);
	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	ul->chan_nr = chan_nr;
	ul->link_id = 0;
	ul->tx_power = 0; /* FIXME: initial TX power */
	req = (struct l1ctl_dm_est_req *) msgb_put(msg, sizeof(*req));
	req->band_arfcn = band_arfcn;

	return osmo_send_l1(ms, msg);
}

int l1ctl_tx_echo_req(struct osmocom_ms *ms, unsigned int len)
{
	struct msgb *msg;
	uint8_t *data;
	unsigned int i;

	msg = osmo_l1_alloc(L1CTL_ECHO_REQ);
	if (!msg)
		return -1;

	data = msgb_put(msg, len);
	for (i = 0; i < len; i++)
		data[i] = i % 8;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_PM_REQ */
int l1ctl_tx_pm_req_range(struct osmocom_ms *ms, uint16_t arfcn_from,
			  uint16_t arfcn_to)
{
	struct msgb *msg;
	struct l1ctl_pm_req *pm;

	msg = osmo_l1_alloc(L1CTL_PM_REQ);
	if (!msg)
		return -1;

	printf("Tx PM Req (%u-%u)\n", arfcn_from, arfcn_to);
	pm = (struct l1ctl_pm_req *) msgb_put(msg, sizeof(*pm));
	pm->type = 1;
	pm->range.band_arfcn_from = htons(arfcn_from);
	pm->range.band_arfcn_to = htons(arfcn_to);

	return osmo_send_l1(ms, msg);
}

/* Receive L1CTL_RESET */
static int rx_l1_reset(struct osmocom_ms *ms)
{
	printf("Layer1 Reset.\n");
	dispatch_signal(SS_L1CTL, S_L1CTL_RESET, ms);
}

/* Receive L1CTL_PM_RESP */
static int rx_l1_pm_resp(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_pm_resp *pmr;

	for (pmr = (struct l1ctl_pm_resp *) msg->l1h;
	     (uint8_t *) pmr < msg->tail; pmr++) {
		struct osmobb_meas_res mr;
		DEBUGP(DL1C, "PM MEAS: ARFCN: %4u RxLev: %3d %3d\n",
			ntohs(pmr->band_arfcn), pmr->pm[0], pmr->pm[1]);
		mr.band_arfcn = ntohs(pmr->band_arfcn);
		mr.rx_lev = (pmr->pm[0] + pmr->pm[1]) / 2;
		mr.ms = ms;
		dispatch_signal(SS_L1CTL, S_L1CTL_PM_RES, &mr);
	}
	return 0;
}

/* Receive incoming data from L1 using L1CTL format */
int l1ctl_recv(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc = 0;
	struct l1ctl_hdr *l1h;
	struct l1ctl_info_dl *dl;

	if (msgb_l2len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "Short Layer2 message: %u\n",
			msgb_l2len(msg));
		return -1;
	}

	l1h = (struct l1ctl_hdr *) msg->l1h;

	/* move the l1 header pointer to point _BEHIND_ l1ctl_hdr,
	   as the l1ctl header is of no interest to subsequent code */
	msg->l1h = l1h->data;

	switch (l1h->msg_type) {
	case L1CTL_NEW_CCCH_RESP:
		rc = rx_l1_ccch_resp(ms, msg);
		break;
	case L1CTL_DATA_IND:
		rc = rx_ph_data_ind(ms, msg);
		break;
	case L1CTL_RESET:
		rc = rx_l1_reset(ms);
		break;
	case L1CTL_PM_RESP:
		rc = rx_l1_pm_resp(ms, msg);
		if (l1h->flags & L1CTL_F_DONE)
			dispatch_signal(SS_L1CTL, S_L1CTL_PM_DONE, ms);
		break;
	default:
		fprintf(stderr, "Unknown MSG: %u\n", l1h->msg_type);
		break;
	}

	return rc;
}
