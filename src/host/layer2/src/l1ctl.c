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
#include <l1a_l23_interface.h>

#include <osmocore/timer.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/protocol/gsm_04_08.h>
#include <osmocore/protocol/gsm_08_58.h>

#include <osmocom/l1ctl.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/lapdm.h>
#include <osmocom/debug.h>

#include "gsmtap_util.h"

static struct msgb *osmo_l1_alloc(uint8_t msg_type)
{
	struct l1ctl_info_ul *ul;
	struct msgb *msg = msgb_alloc_headroom(256, 4, "osmo_l1");

	if (!msg) {
		fprintf(stderr, "Failed to allocate memory.\n");
		return NULL;
	}

	msg->l1h = msgb_put(msg, sizeof(*ul));
	ul = (struct l1ctl_info_ul *) msg->l1h;
	ul->msg_type = msg_type;
	
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

	if (msgb_l3len(msg) < sizeof(*sb)) {
		fprintf(stderr, "MSG too short for CCCH RESP: %u\n", msgb_l3len(msg));
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	sb = (struct l1ctl_sync_new_ccch_resp *) msg->l2h;

	printf("SCH: SNR: %u TDMA: (%.4u/%.2u/%.2u) bsic: %d\n",
		dl->snr[0], dl->time.t1, dl->time.t2, dl->time.t3, sb->bsic);

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

	if (msgb_l3len(msg) < sizeof(*ccch)) {
		fprintf(stderr, "MSG too short Data Ind: %u\n", msgb_l3len(msg));
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	ccch = (struct l1ctl_data_ind *) msg->l2h;
	printf("%s (%.4u/%.2u/%.2u) %s\n",
		chan_nr2string(dl->chan_nr), dl->time.t1, dl->time.t2,
		dl->time.t3, hexdump(ccch->data, sizeof(ccch->data)));

	/* send CCCH data via GSMTAP */
	gsmtap_sendmsg(dl->chan_nr & 0x07, dl->band_arfcn, dl->time.fn, ccch->data,
			sizeof(ccch->data));

	/* determine LAPDm entity based on SACCH or not */
	if (dl->link_id & 0x40)
		le = &ms->lapdm_acch;
	else
		le = &ms->lapdm_dcch;
	/* make local stack copy of l1ctl_info_dl, as LAPDm will overwrite skb hdr */
	memcpy(&dl_cpy, dl, sizeof(dl_cpy));

	/* pull the L1 header from the msgb */
	msgb_pull(msg, msg->l2h - msg->l1h);
	msg->l1h = NULL;

	/* send it up into LAPDm */
	l2_ph_data_ind(msg, le, &dl_cpy);

	return 0;
}

/* Transmit L1CTL_DATA_REQ */
int tx_ph_data_req(struct osmocom_ms *ms, struct msgb *msg,
		   uint8_t chan_nr, uint8_t link_id)
{
	struct l1ctl_info_ul *l1i_ul;

	printf("tx_ph_data_req(%s)\n", hexdump(msg->l2h, msgb_l2len(msg)));

	if (msgb_l2len(msg) > 23) {
		printf("L1 cannot handle message length > 23 (%u)\n", msgb_l2len(msg));
		msgb_free(msg);
		return -EINVAL;
	} else if (msgb_l2len(msg) < 23)
		printf("L1 message length < 23 (%u) doesn't seem right!\n", msgb_l2len(msg));

	/* prepend uplink info header */
	printf("sizeof(struct l1ctl_info_ul)=%lu\n", sizeof(*l1i_ul));
	msg->l1h = msgb_push(msg, sizeof(*l1i_ul));
	l1i_ul = (struct l1ctl_info_ul *) msg->l1h;

	l1i_ul->msg_type = L1CTL_DATA_REQ;

	l1i_ul->chan_nr = chan_nr;
	l1i_ul->link_id = link_id;

	/* FIXME: where to get this from? */
	l1i_ul->tx_power = 0;

	return osmo_send_l1(ms, msg);
}

/* Receive L1CTL_RESET */
static int rx_l1_reset(struct osmocom_ms *ms)
{
	struct msgb *msg;
	struct l1ctl_sync_new_ccch_req *req;

	msg = osmo_l1_alloc(L1CTL_NEW_CCCH_REQ);
	if (!msg)
		return -1;

	printf("Layer1 Reset.\n");
	req = (struct l1ctl_sync_new_ccch_req *) msgb_put(msg, sizeof(*req));
	req->band_arfcn = osmo_make_band_arfcn(ms);

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_RACH_REQ */
int tx_ph_rach_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	struct l1ctl_rach_req *req;
	static uint8_t i = 0;

	msg = osmo_l1_alloc(L1CTL_RACH_REQ);
	if (!msg)
		return -1;

	printf("RACH Req.\n");
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

	printf("Tx Dedic.Mode Est Req (arfcn=%u, chan_nr=0x%02x)\n",
		band_arfcn, chan_nr);
	ul = (struct l1ctl_info_ul *) msg->l1h;
	ul->chan_nr = chan_nr;
	ul->link_id = 0;
	ul->tx_power = 0; /* FIXME: initial TX power */
	req = (struct l1ctl_dm_est_req *) msgb_put(msg, sizeof(*req));
	req->band_arfcn = band_arfcn;

	return osmo_send_l1(ms, msg);

}

/* Receive incoming data from L1 using L1CTL format */
int l1ctl_recv(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc = 0;
	struct l1ctl_info_dl *dl;

	if (msgb_l2len(msg) < sizeof(*dl)) {
		fprintf(stderr, "Short Layer2 message: %u\n", msgb_l2len(msg));
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = &msg->l1h[0] + sizeof(*dl);

	switch (dl->msg_type) {
	case L1CTL_NEW_CCCH_RESP:
		rc = rx_l1_ccch_resp(ms, msg);
		break;
	case L1CTL_DATA_IND:
		rc = rx_ph_data_ind(ms, msg);
		break;
	case L1CTL_RESET:
		rc = rx_l1_reset(ms);
		break;
	default:
		fprintf(stderr, "Unknown MSG: %u\n", dl->msg_type);
		break;
	}

	return rc;
}
