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

#include <l1ctl_proto.h>

#include <osmocore/signal.h>
#include <osmocore/logging.h>
#include <osmocore/timer.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/gsm_utils.h>
#include <osmocore/gsmtap_util.h>
#include <osmocore/protocol/gsm_04_08.h>
#include <osmocore/protocol/gsm_08_58.h>
#include <osmocore/rsl.h>

#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/lapdm.h>
#include <osmocom/bb/common/logging.h>

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


static int osmo_make_band_arfcn(struct osmocom_ms *ms, uint16_t arfcn)
{
	/* TODO: Include the band */
	return arfcn;
}

static int rx_l1_fbsb_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;
	struct l1ctl_fbsb_conf *sb;
	struct gsm_time tm;

	if (msgb_l3len(msg) < sizeof(*dl) + sizeof(*sb)) {
		LOGP(DL1C, LOGL_ERROR, "FBSB RESP: MSG too short %u\n",
			msgb_l3len(msg));
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	sb = (struct l1ctl_fbsb_conf *) dl->payload;

	printf("snr=%04x, arfcn=%u result=%u\n", dl->snr, ntohs(dl->band_arfcn),
		sb->result);

	if (sb->result != 0) {
		LOGP(DL1C, LOGL_ERROR, "FBSB RESP: result=%u\n", sb->result);
		dispatch_signal(SS_L1CTL, S_L1CTL_FBSB_ERR, ms);
		return 0;
	}

	gsm_fn2gsmtime(&tm, ntohl(dl->frame_nr));
	DEBUGP(DL1C, "SCH: SNR: %u TDMA: (%.4u/%.2u/%.2u) bsic: %d\n",
		dl->snr, tm.t1, tm.t2, tm.t3, sb->bsic);
	dispatch_signal(SS_L1CTL, S_L1CTL_FBSB_RESP, ms);

	return 0;
}

static int rx_l1_rach_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;

	if (msgb_l2len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "RACH CONF: MSG too short %u\n",
			msgb_l3len(msg));
		msgb_free(msg);
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;

	l2_ph_chan_conf(msg, ms, dl);

	return 0;
}

/* Receive L1CTL_DATA_IND (Data Indication from L1) */
static int rx_ph_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl, dl_cpy;
	struct l1ctl_data_ind *ccch;
	struct lapdm_entity *le;
	struct rx_meas_stat *meas = &ms->meas;
	uint8_t chan_type, chan_ts, chan_ss;
	uint8_t gsmtap_chan_type;
	struct gsm_time tm;

	if (msgb_l3len(msg) < sizeof(*ccch)) {
		LOGP(DL1C, LOGL_ERROR, "MSG too short Data Ind: %u\n",
			msgb_l3len(msg));
		msgb_free(msg);
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = dl->payload;
	ccch = (struct l1ctl_data_ind *) msg->l2h;

	gsm_fn2gsmtime(&tm, ntohl(dl->frame_nr));
	rsl_dec_chan_nr(dl->chan_nr, &chan_type, &chan_ss, &chan_ts);
	DEBUGP(DL1C, "%s (%.4u/%.2u/%.2u) %s\n",
		rsl_chan_nr_str(dl->chan_nr), tm.t1, tm.t2, tm.t3,
		hexdump(ccch->data, sizeof(ccch->data)));

	meas->frames++;
	meas->berr += dl->num_biterr;
	meas->rxlev += dl->rx_level;

	if (dl->num_biterr) {
printf("dropping frame with %u bit errors\n", dl->num_biterr);
		LOGP(DL1C, LOGL_NOTICE, "Dropping frame with %u bit errors\n",
			dl->num_biterr);
		return 0;
	}

	/* send CCCH data via GSMTAP */
	gsmtap_chan_type = chantype_rsl2gsmtap(chan_type, dl->link_id);
	gsmtap_sendmsg(ntohs(dl->band_arfcn), chan_ts, gsmtap_chan_type, chan_ss,
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

/* Receive L1CTL_DATA_CONF (Data Confirm from L1) */
static int rx_ph_data_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;
	struct lapdm_entity *le;

	dl = (struct l1ctl_info_dl *) msg->l1h;

	/* determine LAPDm entity based on SACCH or not */
	if (dl->link_id & 0x40)
		le = &ms->l2_entity.lapdm_acch;
	else
		le = &ms->l2_entity.lapdm_dcch;

	/* send it up into LAPDm */
	l2_ph_data_conf(msg, le);

	return 0;
}

/* Transmit L1CTL_DATA_REQ */
int tx_ph_data_req(struct osmocom_ms *ms, struct msgb *msg, uint8_t chan_nr,
	uint8_t link_id)
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

	/* prepend l1 header */
	msg->l1h = msgb_push(msg, sizeof(*l1h));
	l1h = (struct l1ctl_hdr *) msg->l1h;
	l1h->msg_type = L1CTL_DATA_REQ;

	return osmo_send_l1(ms, msg);
}

/* Transmit FBSB_REQ */
int l1ctl_tx_fbsb_req(struct osmocom_ms *ms, uint16_t arfcn,
		      uint8_t flags, uint16_t timeout, uint8_t sync_info_idx,
		      uint8_t ccch_mode)
{
	struct msgb *msg;
	struct l1ctl_fbsb_req *req;

	printf("Sync Req\n");

	msg = osmo_l1_alloc(L1CTL_FBSB_REQ);
	if (!msg)
		return -1;

	memset(&ms->meas, 0, sizeof(ms->meas));

	req = (struct l1ctl_fbsb_req *) msgb_put(msg, sizeof(*req));
	req->band_arfcn = htons(osmo_make_band_arfcn(ms, arfcn));
	req->timeout = htons(timeout);
	/* Threshold when to consider FB_MODE1: 4kHz - 1kHz */
	req->freq_err_thresh1 = htons(4000 - 1000);
	/* Threshold when to consider SCH: 1kHz - 200Hz */
	req->freq_err_thresh2 = htons(1000 - 200);
	/* not used yet! */
	req->num_freqerr_avg = 3;
	req->flags = flags;
	req->sync_info_idx = sync_info_idx;
	req->ccch_mode = ccch_mode;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_CCCH_MODE_REQ */
int l1ctl_tx_ccch_mode_req(struct osmocom_ms *ms, uint8_t ccch_mode)
{
	struct msgb *msg;
	struct l1ctl_ccch_mode_req *req;

	printf("CCCH Mode Req\n");

	msg = osmo_l1_alloc(L1CTL_CCCH_MODE_REQ);
	if (!msg)
		return -1;

	req = (struct l1ctl_ccch_mode_req *) msgb_put(msg, sizeof(*req));
	req->ccch_mode = ccch_mode;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_PARAM_REQ */
int l1ctl_tx_ph_param_req(struct osmocom_ms *ms, uint8_t ta, uint8_t tx_power)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_par_req *req;

	msg = osmo_l1_alloc(L1CTL_PARAM_REQ);
	if (!msg)
		return -1;

	DEBUGP(DL1C, "PARAM Req. ta=%d, tx_power=%d\n", ta, tx_power);
	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	req = (struct l1ctl_par_req *) msgb_put(msg, sizeof(*req));
	req->tx_power = tx_power;
	req->ta = ta;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_RACH_REQ */
int tx_ph_rach_req(struct osmocom_ms *ms, uint8_t ra, uint8_t fn51,
	uint8_t mf_off)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_rach_req *req;

	msg = osmo_l1_alloc(L1CTL_RACH_REQ);
	if (!msg)
		return -1;

	DEBUGP(DL1C, "RACH Req. fn51=%d, mf_off=%d\n", fn51, mf_off);
	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	req = (struct l1ctl_rach_req *) msgb_put(msg, sizeof(*req));
	req->ra = ra;
	req->fn51 = fn51;
	req->mf_off = mf_off;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_DM_EST_REQ */
int tx_ph_dm_est_req_h0(struct osmocom_ms *ms, uint16_t band_arfcn,
	uint8_t chan_nr, uint8_t tsc)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_dm_est_req *req;

	msg = osmo_l1_alloc(L1CTL_DM_EST_REQ);
	if (!msg)
		return -1;

	printf("Tx Dedic.Mode Est Req (arfcn=%u, chan_nr=0x%02x)\n",
		band_arfcn, chan_nr);

	memset(&ms->meas, 0, sizeof(ms->meas));

	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	ul->chan_nr = chan_nr;
	ul->link_id = 0;

	req = (struct l1ctl_dm_est_req *) msgb_put(msg, sizeof(*req));
	req->tsc = tsc;
	req->h = 0;
	req->h0.band_arfcn = htons(band_arfcn);

	return osmo_send_l1(ms, msg);
}

int tx_ph_dm_est_req_h1(struct osmocom_ms *ms, uint8_t maio, uint8_t hsn,
	uint16_t *ma, uint8_t ma_len, uint8_t chan_nr, uint8_t tsc)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_dm_est_req *req;
	int i;

	msg = osmo_l1_alloc(L1CTL_DM_EST_REQ);
	if (!msg)
		return -1;

	printf("Tx Dedic.Mode Est Req (maio=%u, hsn=%u, "
		"chan_nr=0x%02x)\n", maio, hsn, chan_nr);

	memset(&ms->meas, 0, sizeof(ms->meas));

	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	ul->chan_nr = chan_nr;
	ul->link_id = 0;

	req = (struct l1ctl_dm_est_req *) msgb_put(msg, sizeof(*req));
	req->tsc = tsc;
	req->h = 1;
	req->h1.maio = maio;
	req->h1.hsn = hsn;
	req->h1.n = ma_len;
	for (i = 0; i < ma_len; i++)
		req->h1.ma[i] = htons(ma[i]);

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_DM_REL_REQ */
int tx_ph_dm_rel_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;

	msg = osmo_l1_alloc(L1CTL_DM_REL_REQ);
	if (!msg)
		return -1;

	printf("Tx Dedic.Mode Rel Req\n");

	memset(&ms->meas, 0, sizeof(ms->meas));

	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));

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

/* Transmit L1CTL_RESET_REQ */
int l1ctl_tx_reset_req(struct osmocom_ms *ms, uint8_t type)
{
	struct msgb *msg;
	struct l1ctl_reset *res;

	msg = osmo_l1_alloc(L1CTL_RESET_REQ);
	if (!msg)
		return -1;

	printf("Tx Reset Req (%u)\n", type);
	res = (struct l1ctl_reset *) msgb_put(msg, sizeof(*res));
	res->type = type;

	return osmo_send_l1(ms, msg);
}

/* Receive L1CTL_RESET_IND */
static int rx_l1_reset(struct osmocom_ms *ms)
{
	printf("Layer1 Reset indication\n");
	dispatch_signal(SS_L1CTL, S_L1CTL_RESET, ms);

	return 0;
}

/* Receive L1CTL_PM_CONF */
static int rx_l1_pm_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_pm_conf *pmr;

	for (pmr = (struct l1ctl_pm_conf *) msg->l1h;
	     (uint8_t *) pmr < msg->tail; pmr++) {
		struct osmobb_meas_res mr;
		DEBUGP(DL1C, "PM MEAS: ARFCN: %4u RxLev: %3d %3d\n",
			ntohs(pmr->band_arfcn), pmr->pm[0], pmr->pm[1]);
		mr.band_arfcn = ntohs(pmr->band_arfcn);
		mr.rx_lev = pmr->pm[0];
		mr.ms = ms;
		dispatch_signal(SS_L1CTL, S_L1CTL_PM_RES, &mr);
	}
	return 0;
}

/* Receive L1CTL_MODE_CONF */
static int rx_l1_ccch_mode_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct osmobb_ccch_mode_conf mc;
	struct l1ctl_ccch_mode_conf *conf;

	if (msgb_l3len(msg) < sizeof(*conf)) {
		LOGP(DL1C, LOGL_ERROR, "MODE CONF: MSG too short %u\n",
			msgb_l3len(msg));
		return -1;
	}

	conf = (struct l1ctl_ccch_mode_conf *) msg->l1h;

	printf("mode=%u\n", conf->ccch_mode);

	mc.ccch_mode = conf->ccch_mode;
	mc.ms = ms;
	dispatch_signal(SS_L1CTL, S_L1CTL_CCCH_MODE_CONF, &mc);

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
		msgb_free(msg);
		return -1;
	}

	l1h = (struct l1ctl_hdr *) msg->l1h;

	/* move the l1 header pointer to point _BEHIND_ l1ctl_hdr,
	   as the l1ctl header is of no interest to subsequent code */
	msg->l1h = l1h->data;

	switch (l1h->msg_type) {
	case L1CTL_FBSB_CONF:
		rc = rx_l1_fbsb_conf(ms, msg);
		msgb_free(msg);
		break;
	case L1CTL_DATA_IND:
		rc = rx_ph_data_ind(ms, msg);
		break;
	case L1CTL_DATA_CONF:
		rc = rx_ph_data_conf(ms, msg);
		break;
	case L1CTL_RESET_IND:
	case L1CTL_RESET_CONF:
		rc = rx_l1_reset(ms);
		msgb_free(msg);
		break;
	case L1CTL_PM_CONF:
		rc = rx_l1_pm_conf(ms, msg);
		msgb_free(msg);
		if (l1h->flags & L1CTL_F_DONE)
			dispatch_signal(SS_L1CTL, S_L1CTL_PM_DONE, ms);
		break;
	case L1CTL_RACH_CONF:
		rc = rx_l1_rach_conf(ms, msg);
		break;
	case L1CTL_CCCH_MODE_CONF:
		rc = rx_l1_ccch_mode_conf(ms, msg);
		msgb_free(msg);
		break;
	default:
		fprintf(stderr, "Unknown MSG: %u\n", l1h->msg_type);
		msgb_free(msg);
		break;
	}

	return rc;
}
