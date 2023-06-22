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
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <arpa/inet.h>

#include <l1ctl_proto.h>

#include <osmocom/core/signal.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/lapdm.h>
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/logging.h>

/* determine the CCCH block number based on the frame number */
static unsigned int fn2ccch_block(uint32_t fn)
{
	int rc = gsm0502_fn2ccch_block(fn);
	/* if FN is negative, we were called for something that's not CCCH! */
	OSMO_ASSERT(rc >= 0);
	return rc;
}

static uint8_t chantype_rsl2gsmtap_ext(uint8_t rsl_chantype, uint8_t link_id, uint32_t fn, uint8_t num_agch)
{
	uint8_t ret = chantype_rsl2gsmtap2(rsl_chantype, link_id, false);
	if (ret != GSMTAP_CHANNEL_PCH)
		return ret;

	if (fn2ccch_block(fn) >= num_agch)
		return GSMTAP_CHANNEL_PCH;
	return GSMTAP_CHANNEL_AGCH;
}

static const uint8_t fill_frame[GSM_MACBLOCK_LEN] = {
        0x03, 0x03, 0x01, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B,
        0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B, 0x2B,
        0x2B, 0x2B, 0x2B
};

/* Paging Request 1 with "no identity" content, i.e. empty/dummy paging */
static const uint8_t paging_fill[GSM_MACBLOCK_LEN] = {
	0x15, 0x06, 0x21, 0x00, 0x01, 0xf0, 0x2b, 0x2b, 0x2b, 0x2b,
	0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b,
	0x2b, 0x2b, 0x2b };

static bool is_fill_frame(uint8_t chan_type, const uint8_t *data)
{
	switch (chan_type) {
	case GSMTAP_CHANNEL_AGCH:
		if (!memcmp(data, fill_frame, GSM_MACBLOCK_LEN))
			return true;
		break;
	case GSMTAP_CHANNEL_PCH:
		if (!memcmp(data, paging_fill, GSM_MACBLOCK_LEN))
			return true;
		break;
	/* don't use 'default' case here as the above only conditionally return true */
	}
	return false;
}

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
	struct osmobb_fbsb_res fr;

	if (msgb_l1len(msg) < (sizeof(*dl) + sizeof(*sb))) {
		LOGP(DL1C, LOGL_ERROR, "FBSB RESP: MSG too short (len=%u), "
			"missing UL info header and/or payload\n", msgb_l1len(msg));
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	sb = (struct l1ctl_fbsb_conf *) dl->payload;

	LOGP(DL1C, LOGL_INFO, "snr=%04x, arfcn=%u result=%u\n", dl->snr,
		ntohs(dl->band_arfcn), sb->result);

	if (sb->result != 0) {
		LOGP(DL1C, LOGL_ERROR, "FBSB RESP: result=%u\n", sb->result);
		fr.ms = ms;
		fr.band_arfcn = ntohs(dl->band_arfcn);
		osmo_signal_dispatch(SS_L1CTL, S_L1CTL_FBSB_ERR, &fr);
		return 0;
	}

	gsm_fn2gsmtime(&tm, ntohl(dl->frame_nr));
	DEBUGP(DL1C, "SCH: SNR: %u TDMA: (%.4u/%.2u/%.2u) bsic: %d\n",
		dl->snr, tm.t1, tm.t2, tm.t3, sb->bsic);
	fr.ms = ms;
	fr.snr = dl->snr;
	fr.bsic = sb->bsic;
	fr.band_arfcn = ntohs(dl->band_arfcn);
	osmo_signal_dispatch(SS_L1CTL, S_L1CTL_FBSB_RESP, &fr);

	return 0;
}

static int rx_l1_rach_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct lapdm_entity *le = &ms->lapdm_channel.lapdm_dcch;
	struct osmo_phsap_prim pp;
	struct l1ctl_info_dl *dl;

	if (msgb_l1len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "RACH CONF MSG too short "
			"(len=%u), missing DL info header\n", msgb_l1len(msg));
		msgb_free(msg);
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = msg->l3h = dl->payload;

	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_RACH,
			PRIM_OP_CONFIRM, msg);
	pp.u.rach_ind.fn = ntohl(dl->frame_nr);

	return lapdm_phsap_up(&pp.oph, le);
}

/* Receive L1CTL_DATA_IND (Data Indication from L1) */
static int rx_ph_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct osmo_phsap_prim pp;
	struct l1ctl_info_dl *dl;
	struct l1ctl_data_ind *ccch;
	struct lapdm_entity *le;
	struct rx_meas_stat *meas = &ms->meas;
	uint8_t chan_type, chan_ts, chan_ss;
	uint8_t gsmtap_chan_type;
	uint8_t bs_ag_blks_res;
	struct gsm_time tm;

	if (msgb_l1len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "DATA IND MSG too short (len=%u), "
			"missing UL info header\n", msgb_l1len(msg));
		msgb_free(msg);
		return -1;
	}

	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = dl->payload;
	ccch = (struct l1ctl_data_ind *) msg->l2h;

	gsm_fn2gsmtime(&tm, ntohl(dl->frame_nr));
	if (rsl_dec_chan_nr(dl->chan_nr, &chan_type, &chan_ss, &chan_ts) != 0) {
		LOGP(DL1C, LOGL_ERROR,
		     "%s(): rsl_dec_chan_nr(chan_nr=0x%02x) failed\n",
		     __func__, dl->chan_nr);
		return -EINVAL;
	}

	DEBUGP(DL1C, "%s (%.4u/%.2u/%.2u) %d dBm: %s\n",
		rsl_chan_nr_str(dl->chan_nr), tm.t1, tm.t2, tm.t3,
		(int)dl->rx_level-110,
		osmo_hexdump(ccch->data, sizeof(ccch->data)));

	meas->last_fn = ntohl(dl->frame_nr);
	meas->frames++;
	meas->snr += dl->snr;
	meas->berr += dl->num_biterr;
	meas->rxlev += dl->rx_level;

	/* counting loss criteria */
	if (!(dl->link_id & 0x40)) {
		switch (chan_type) {
		case RSL_CHAN_PCH_AGCH:
			/* only look at one CCCH frame in each 51 multiframe.
			 * FIXME: implement DRX
			 * - select correct paging block that is for us.
			 * - initialize ds_fail according to BS_PA_MFRMS.
			 */
			if ((meas->last_fn % 51) != 6)
				break;
			if (!meas->ds_fail)
				break;
			if (dl->fire_crc >= 2)
				meas->dsc -= 4;
			else
				meas->dsc += 1;
			if (meas->dsc > meas->ds_fail)
				meas->dsc = meas->ds_fail;
			if (meas->dsc < meas->ds_fail)
				LOGP(DL1C, LOGL_INFO, "LOSS counter for CCCH %d\n", meas->dsc);
			if (meas->dsc > 0)
				break;
			meas->ds_fail = 0;
			osmo_signal_dispatch(SS_L1CTL, S_L1CTL_LOSS_IND, ms);
			break;
		}
	} else {
		switch (chan_type) {
		case RSL_CHAN_Bm_ACCHs:
		case RSL_CHAN_Lm_ACCHs:
		case RSL_CHAN_SDCCH4_ACCH:
		case RSL_CHAN_SDCCH8_ACCH:
			if (!meas->rl_fail)
				break;
			if (dl->fire_crc >= 2)
				meas->s -= 1;
			else
				meas->s += 2;
			if (meas->s > meas->rl_fail)
				meas->s = meas->rl_fail;
			if (meas->s < meas->rl_fail)
				LOGP(DL1C, LOGL_NOTICE, "LOSS counter for ACCH %d\n", meas->s);
			if (meas->s > 0)
				break;
			meas->rl_fail = 0;
			osmo_signal_dispatch(SS_L1CTL, S_L1CTL_LOSS_IND, ms);
			break;
		}
	}

	if (dl->fire_crc >= 2) {
		LOGP(DL1C, LOGL_NOTICE, "Dropping frame with %u bit errors\n",
			dl->num_biterr);
		msgb_free(msg);
		return 0;
	}

	/* May not be initialized in some applications (e.g. ccch_scan) */
	if (ms->cellsel.si != NULL)
		bs_ag_blks_res = ms->cellsel.si->bs_ag_blks_res;
	else /* fall-back to 1 (this is what OsmoBTS does) */
		bs_ag_blks_res = 1;

	gsmtap_chan_type = chantype_rsl2gsmtap_ext(chan_type, dl->link_id, tm.fn, bs_ag_blks_res);
	/* don't log fill frames via GSMTAP; they serve no purpose other than
	 * to clog up your logs */
	if (!is_fill_frame(gsmtap_chan_type, ccch->data)) {
		/* send CCCH data via GSMTAP */
		gsmtap_send(l23_cfg.gsmtap.inst, ntohs(dl->band_arfcn), chan_ts,
			    gsmtap_chan_type, chan_ss, tm.fn, dl->rx_level-110,
			    dl->snr, ccch->data, sizeof(ccch->data));
	}

	/* Do not pass PDCH and CBCH frames to LAPDm */
	switch (chan_type) {
	case RSL_CHAN_OSMO_PDCH:
	case RSL_CHAN_OSMO_CBCH4:
	case RSL_CHAN_OSMO_CBCH8:
		/* TODO: pass directly to l23 application */
		msgb_free(msg);
		return 0;
	}

	/* determine LAPDm entity based on SACCH or not */
	if (dl->link_id & 0x40)
		le = &ms->lapdm_channel.lapdm_acch;
	else
		le = &ms->lapdm_channel.lapdm_dcch;

	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_DATA,
			PRIM_OP_INDICATION, msg);
	pp.u.data.chan_nr = dl->chan_nr;
	pp.u.data.link_id = dl->link_id;

	/* send it up into LAPDm */
	return lapdm_phsap_up(&pp.oph, le);
}

/* Receive L1CTL_DATA_CONF (Data Confirm from L1) */
static int rx_ph_data_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct osmo_phsap_prim pp;
	struct l1ctl_info_dl *dl = (struct l1ctl_info_dl *) msg->l1h;
	struct lapdm_entity *le;

	if (msgb_l1len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "DATA CONF MSG too short (len=%u), "
			"missing UL info header\n", msgb_l1len(msg));
		msgb_free(msg);
		return -1;
	}

	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_RTS,
			PRIM_OP_INDICATION, msg);

	/* determine LAPDm entity based on SACCH or not */
	if (dl->link_id & 0x40)
		le = &ms->lapdm_channel.lapdm_acch;
	else
		le = &ms->lapdm_channel.lapdm_dcch;

	/* send it up into LAPDm */
	return lapdm_phsap_up(&pp.oph, le);
}

/* Transmit L1CTL_DATA_REQ */
int l1ctl_tx_data_req(struct osmocom_ms *ms, struct msgb *msg,
                      uint8_t chan_nr, uint8_t link_id)
{
	struct l1ctl_hdr *l1h;
	struct l1ctl_info_ul *l1i_ul;
	uint8_t chan_type, chan_ts, chan_ss;

	DEBUGP(DL1C, "(%s)\n", osmo_hexdump(msg->l2h, msgb_l2len(msg)));

	if (msgb_l2len(msg) != 23) {
		LOGP(DL1C, LOGL_ERROR, "Wrong message length (len=%u), "
			"DATA REQ ignored, please fix!\n", msgb_l2len(msg));
		msgb_free(msg);
		return -EINVAL;
	}

	/* send copy via GSMTAP */
	if (rsl_dec_chan_nr(chan_nr, &chan_type, &chan_ss, &chan_ts) == 0) {
		uint8_t gsmtap_chan_type = chantype_rsl2gsmtap2(chan_type, link_id, false);
		gsmtap_send(l23_cfg.gsmtap.inst, ms->rrlayer.cd_now.arfcn | GSMTAP_ARFCN_F_UPLINK,
			    chan_ts, gsmtap_chan_type, chan_ss, 0, 127, 0,
			    msg->l2h, msgb_l2len(msg));
	} else {
		LOGP(DL1C, LOGL_ERROR,
		     "%s(): rsl_dec_chan_nr(chan_nr=0x%02x) failed\n",
		     __func__, chan_nr);
	}

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
		      uint8_t ccch_mode, uint8_t rxlev_exp)
{
	struct msgb *msg;
	struct l1ctl_fbsb_req *req;

	LOGP(DL1C, LOGL_INFO, "Sync Req\n");

	msg = osmo_l1_alloc(L1CTL_FBSB_REQ);
	if (!msg)
		return -1;

	req = (struct l1ctl_fbsb_req *) msgb_put(msg, sizeof(*req));
	req->band_arfcn = htons(osmo_make_band_arfcn(ms, arfcn));
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
	req->rxlev_exp = rxlev_exp;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_CCCH_MODE_REQ */
int l1ctl_tx_ccch_mode_req(struct osmocom_ms *ms, uint8_t ccch_mode)
{
	struct msgb *msg;
	struct l1ctl_ccch_mode_req *req;

	LOGP(DL1C, LOGL_INFO, "CCCH Mode Req\n");

	msg = osmo_l1_alloc(L1CTL_CCCH_MODE_REQ);
	if (!msg)
		return -1;

	req = (struct l1ctl_ccch_mode_req *) msgb_put(msg, sizeof(*req));
	req->ccch_mode = ccch_mode;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_TCH_MODE_REQ */
int l1ctl_tx_tch_mode_req(struct osmocom_ms *ms, uint8_t tch_mode,
			  uint8_t audio_mode, uint8_t tch_loop_mode)
{
	struct msgb *msg;
	struct l1ctl_tch_mode_req *req;

	LOGP(DL1C, LOGL_INFO, "TCH Mode Req\n");

	msg = osmo_l1_alloc(L1CTL_TCH_MODE_REQ);
	if (!msg)
		return -1;

	req = (struct l1ctl_tch_mode_req *) msgb_put(msg, sizeof(*req));
	req->tch_mode = tch_mode;
	req->audio_mode = audio_mode;
	req->tch_loop_mode = tch_loop_mode;
	/* TODO: Set AMR codec in req if req->tch_mode==GSM48_CMODE_SPEECH_AMR */

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_PARAM_REQ */
int l1ctl_tx_param_req(struct osmocom_ms *ms, int8_t ta, uint8_t tx_power)
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

/* Transmit L1CTL_CRYPTO_REQ */
int l1ctl_tx_crypto_req(struct osmocom_ms *ms, uint8_t chan_nr,
	uint8_t algo, uint8_t *key, uint8_t len)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_crypto_req *req;

	msg = osmo_l1_alloc(L1CTL_CRYPTO_REQ);
	if (!msg)
		return -1;

	DEBUGP(DL1C, "CRYPTO Req. algo=%d, len=%d\n", algo, len);
	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	req = (struct l1ctl_crypto_req *) msgb_put(msg, sizeof(*req) + len);

	ul->chan_nr = chan_nr;
	req->key_len = len;
	req->algo = algo;

	if (len)
		memcpy(req->key, key, len);

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_RACH_REQ */
int l1ctl_tx_rach_req(struct osmocom_ms *ms,
		      uint8_t chan_nr, uint8_t link_id,
		      uint8_t ra, uint16_t offset, uint8_t combined)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_rach_req *req;

	msg = osmo_l1_alloc(L1CTL_RACH_REQ);
	if (!msg)
		return -1;

	DEBUGP(DL1C, "RACH Req. offset=%d combined=%d\n", offset, combined);
	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	ul->chan_nr = chan_nr;
	ul->link_id = link_id;
	req = (struct l1ctl_rach_req *) msgb_put(msg, sizeof(*req));
	req->ra = ra;
	req->offset = htons(offset);
	req->combined = combined;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_DM_EST_REQ */
int l1ctl_tx_dm_est_req_h0(struct osmocom_ms *ms, uint16_t band_arfcn,
                           uint8_t chan_nr, uint8_t tsc, uint8_t tch_mode,
			   uint8_t audio_mode)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_dm_est_req *req;

	msg = osmo_l1_alloc(L1CTL_DM_EST_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "Tx Dedic.Mode Est Req (arfcn=%u, "
		"chan_nr=0x%02x)\n", band_arfcn, chan_nr);

	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	ul->chan_nr = chan_nr;
	ul->link_id = 0;

	req = (struct l1ctl_dm_est_req *) msgb_put(msg, sizeof(*req));
	req->tsc = tsc;
	req->h = 0;
	req->h0.band_arfcn = htons(band_arfcn);
	req->tch_mode = tch_mode;
	req->audio_mode = audio_mode;

	return osmo_send_l1(ms, msg);
}

int l1ctl_tx_dm_est_req_h1(struct osmocom_ms *ms, uint8_t maio, uint8_t hsn,
                           uint16_t *ma, uint8_t ma_len,
                           uint8_t chan_nr, uint8_t tsc, uint8_t tch_mode,
			   uint8_t audio_mode)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_dm_est_req *req;
	int i;

	msg = osmo_l1_alloc(L1CTL_DM_EST_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "Tx Dedic.Mode Est Req (maio=%u, hsn=%u, "
		"chan_nr=0x%02x)\n", maio, hsn, chan_nr);

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
	req->tch_mode = tch_mode;
	req->audio_mode = audio_mode;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_DM_FREQ_REQ */
int l1ctl_tx_dm_freq_req_h0(struct osmocom_ms *ms, uint16_t band_arfcn,
                            uint8_t tsc, uint16_t fn)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_dm_freq_req *req;

	msg = osmo_l1_alloc(L1CTL_DM_FREQ_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "Tx Dedic.Mode Freq Req (arfcn=%u, fn=%d)\n",
		band_arfcn, fn);

	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	ul->chan_nr = 0;
	ul->link_id = 0;

	req = (struct l1ctl_dm_freq_req *) msgb_put(msg, sizeof(*req));
	req->fn = htons(fn);
	req->tsc = tsc;
	req->h = 0;
	req->h0.band_arfcn = htons(band_arfcn);

	return osmo_send_l1(ms, msg);
}

int l1ctl_tx_dm_freq_req_h1(struct osmocom_ms *ms, uint8_t maio, uint8_t hsn,
                            uint16_t *ma, uint8_t ma_len,
                            uint8_t tsc, uint16_t fn)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_dm_freq_req *req;
	int i;

	msg = osmo_l1_alloc(L1CTL_DM_FREQ_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "Tx Dedic.Mode Freq Req (maio=%u, hsn=%u, "
		"fn=%d)\n", maio, hsn, fn);

	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
	ul->chan_nr = 0;
	ul->link_id = 0;

	req = (struct l1ctl_dm_freq_req *) msgb_put(msg, sizeof(*req));
	req->fn = htons(fn);
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
int l1ctl_tx_dm_rel_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;

	msg = osmo_l1_alloc(L1CTL_DM_REL_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "Tx Dedic.Mode Rel Req\n");

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

int l1ctl_tx_sim_req(struct osmocom_ms *ms, uint8_t *data, uint16_t length)
{
	struct msgb *msg;
	uint8_t *dat;

	msg = osmo_l1_alloc(L1CTL_SIM_REQ);
	if (!msg)
		return -1;

	dat = msgb_put(msg, length);
	memcpy(dat, data, length);

	return osmo_send_l1(ms, msg);
}

/* just forward the SIM response to the SIM handler */
static int rx_l1_sim_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	LOGP(DL1C, LOGL_INFO, "SIM %s\n", msgb_hexdump_l1(msg));

	sim_apdu_resp(ms, msg);

	return 0;
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

	LOGP(DL1C, LOGL_INFO, "Tx PM Req (%u-%u)\n", arfcn_from, arfcn_to);
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

	LOGP(DL1C, LOGL_INFO, "Tx Reset Req (%u)\n", type);
	res = (struct l1ctl_reset *) msgb_put(msg, sizeof(*res));
	res->type = type;

	return osmo_send_l1(ms, msg);
}

/* Receive L1CTL_RESET_IND */
static int rx_l1_reset(struct osmocom_ms *ms)
{
	LOGP(DL1C, LOGL_INFO, "Layer1 Reset indication\n");
	osmo_signal_dispatch(SS_L1CTL, S_L1CTL_RESET, ms);

	return 0;
}

/* Receive L1CTL_PM_CONF */
static int rx_l1_pm_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_pm_conf *pmr;

	if (msgb_l1len(msg) < sizeof(*pmr)) {
		LOGP(DL1C, LOGL_ERROR, "PM CONF MSG too short (len=%u), "
			"missing measurement results\n", msgb_l1len(msg));
		return -1;
	}

	for (pmr = (struct l1ctl_pm_conf *) msg->l1h;
	     (uint8_t *) pmr < msg->tail; pmr++) {
		struct osmobb_meas_res mr;
		DEBUGP(DL1C, "PM MEAS: ARFCN: %4u RxLev: %3d %3d\n",
			ntohs(pmr->band_arfcn), pmr->pm[0], pmr->pm[1]);
		mr.band_arfcn = ntohs(pmr->band_arfcn);
		mr.rx_lev = pmr->pm[0];
		mr.ms = ms;
		osmo_signal_dispatch(SS_L1CTL, S_L1CTL_PM_RES, &mr);
	}
	return 0;
}

/* Receive L1CTL_CCCH_MODE_CONF */
static int rx_l1_ccch_mode_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct osmobb_ccch_mode_conf mc;
	struct l1ctl_ccch_mode_conf *conf;

	if (msgb_l1len(msg) < sizeof(*conf)) {
		LOGP(DL1C, LOGL_ERROR, "CCCH MODE CONF: MSG too short "
			"(len=%u), missing CCCH mode info\n", msgb_l1len(msg));
		return -1;
	}

	conf = (struct l1ctl_ccch_mode_conf *) msg->l1h;

	LOGP(DL1C, LOGL_INFO, "CCCH MODE CONF: mode=%u\n", conf->ccch_mode);

	mc.ccch_mode = conf->ccch_mode;
	mc.ms = ms;
	osmo_signal_dispatch(SS_L1CTL, S_L1CTL_CCCH_MODE_CONF, &mc);

	return 0;
}

/* Receive L1CTL_TCH_MODE_CONF */
static int rx_l1_tch_mode_conf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct osmobb_tch_mode_conf mc;
	struct l1ctl_tch_mode_conf *conf;

	if (msgb_l1len(msg) < sizeof(*conf)) {
		LOGP(DL1C, LOGL_ERROR, "TCH MODE CONF: MSG too short "
			"(len=%u), missing TCH mode info\n", msgb_l1len(msg));
		return -1;
	}

	conf = (struct l1ctl_tch_mode_conf *) msg->l1h;

	LOGP(DL1C, LOGL_INFO, "TCH MODE CONF: mode=%u\n", conf->tch_mode);

	mc.tch_mode = conf->tch_mode;
	mc.audio_mode = conf->audio_mode;
	mc.ms = ms;
	osmo_signal_dispatch(SS_L1CTL, S_L1CTL_TCH_MODE_CONF, &mc);

	return 0;
}

/* Receive L1CTL_TRAFFIC_IND (Traffic Indication from L1) */
static int rx_l1_traffic_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_info_dl *dl;
	struct l1ctl_traffic_ind *ti;
	size_t frame_len;
	uint8_t *frame;

	if (msgb_l1len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "TRAFFIC IND MSG too short "
			"(len=%u), missing DL info header\n", msgb_l1len(msg));
		return -1;
	}

	/* Header handling */
	dl = (struct l1ctl_info_dl *) msg->l1h;
	ti = (struct l1ctl_traffic_ind *) dl->payload;
	frame = (uint8_t *) ti->data;

	msg->l2h = dl->payload;
	msg->l3h = frame;

	/* Calculate the frame length */
	frame_len = msgb_l3len(msg);

	DEBUGP(DL1C, "TRAFFIC IND len=%zu (%s)\n", frame_len,
		osmo_hexdump(frame, frame_len));

	/* distribute or drop */
	if (ms->l1_entity.l1_traffic_ind)
		return ms->l1_entity.l1_traffic_ind(ms, msg);

	msgb_free(msg);
	return 0;
}

/* Transmit L1CTL_TRAFFIC_REQ (Traffic Request to L1) */
int l1ctl_tx_traffic_req(struct osmocom_ms *ms, struct msgb *msg,
                       uint8_t chan_nr, uint8_t link_id)
{
	struct l1ctl_hdr *l1h;
	struct l1ctl_info_ul *l1i_ul;
	struct l1ctl_traffic_req *tr;
	size_t frame_len;
	uint8_t *frame;

	/* Header handling */
	tr = (struct l1ctl_traffic_req *) msg->l2h;
	frame = (uint8_t *) tr->data;
	msg->l3h = frame;

	/* Calculate the frame length */
	frame_len = msgb_l3len(msg);

	DEBUGP(DL1C, "TRAFFIC REQ len=%zu (%s)\n", frame_len,
		osmo_hexdump(frame, frame_len));

	/* prepend uplink info header */
	l1i_ul = (struct l1ctl_info_ul *) msgb_push(msg, sizeof(*l1i_ul));

	l1i_ul->chan_nr = chan_nr;
	l1i_ul->link_id = link_id;

	/* prepend l1 header */
	msg->l1h = msgb_push(msg, sizeof(*l1h));
	l1h = (struct l1ctl_hdr *) msg->l1h;
	l1h->msg_type = L1CTL_TRAFFIC_REQ;

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_NEIGH_PM_REQ */
int l1ctl_tx_neigh_pm_req(struct osmocom_ms *ms, int num, uint16_t *arfcn)
{
	struct msgb *msg;
	struct l1ctl_neigh_pm_req *pm_req;
	int i;

	msg = osmo_l1_alloc(L1CTL_NEIGH_PM_REQ);
	if (!msg)
		return -1;

	LOGP(DL1C, LOGL_INFO, "Tx NEIGH PM Req (num %u)\n", num);
	pm_req = (struct l1ctl_neigh_pm_req *) msgb_put(msg, sizeof(*pm_req));
	pm_req->n = num;
	for (i = 0; i < num; i++) {
		pm_req->band_arfcn[i] = htons(*arfcn++);
		pm_req->tn[i] = 0;
	}

	return osmo_send_l1(ms, msg);
}

/* Receive L1CTL_NEIGH_PM_IND */
static int rx_l1_neigh_pm_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct l1ctl_neigh_pm_ind *pm_ind;

	if (msgb_l1len(msg) < sizeof(*pm_ind)) {
		LOGP(DL1C, LOGL_ERROR, "NEIGH PH IND MSG too short "
			"(len=%u), missing measurement results\n", msgb_l1len(msg));
		return -1;
	}

	for (pm_ind = (struct l1ctl_neigh_pm_ind *) msg->l1h;
	     (uint8_t *) pm_ind < msg->tail; pm_ind++) {
		struct osmobb_neigh_pm_ind mi;
		DEBUGP(DL1C, "NEIGH_PM IND: ARFCN: %4u RxLev: %3d %3d\n",
			ntohs(pm_ind->band_arfcn), pm_ind->pm[0],
			pm_ind->pm[1]);
		mi.band_arfcn = ntohs(pm_ind->band_arfcn);
		mi.rx_lev = pm_ind->pm[0];
		mi.ms = ms;
		osmo_signal_dispatch(SS_L1CTL, S_L1CTL_NEIGH_PM_IND, &mi);
	}
	return 0;
}

/* Receive L1CTL_GPRS_DL_BLOCK_IND */
static int rx_l1_gprs_dl_block_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct l1ctl_gprs_dl_block_ind *ind = (void *)msg->l1h;

	if (msgb_l1len(msg) < sizeof(*ind)) {
		LOGP(DL1C, LOGL_ERROR,
		     "Rx malformed GPRS DL BLOCK.ind (len=%u < %zu)\n",
		     msgb_l1len(msg), sizeof(*ind));
		return -EINVAL;
	}
	if (OSMO_UNLIKELY(ind->hdr.tn >= 8)) {
		LOGP(DL1C, LOGL_ERROR,
		     "Rx malformed GPRS DL BLOCK.ind (tn=%u)\n",
		     ind->hdr.tn);
		return -EINVAL;
	}

	msg->l2h = (void *)&ind->data[0];

	DEBUGP(DL1C, "Rx GPRS DL block (fn=%u, tn=%u, len=%u): %s\n",
	       ntohl(ind->hdr.fn), ind->hdr.tn, msgb_l2len(msg), msgb_hexdump_l2(msg));

	/* distribute or drop */
	if (ms->l1_entity.l1_gprs_dl_block_ind)
		return ms->l1_entity.l1_gprs_dl_block_ind(ms, msg);

	msgb_free(msg);
	return 0;
}

/* Transmit L1CTL_GPRS_UL_BLOCK_REQ */
int l1ctl_tx_gprs_ul_block_req(struct osmocom_ms *ms, uint32_t fn, uint8_t tn,
			       const uint8_t *data, size_t data_len)
{
	struct l1ctl_gprs_ul_block_req *req;
	struct msgb *msg;

	msg = osmo_l1_alloc(L1CTL_GPRS_UL_BLOCK_REQ);
	if (!msg)
		return -ENOMEM;

	req = (void *)msgb_put(msg, sizeof(*req));
	req->hdr.fn = htonl(fn);
	req->hdr.tn = tn;
	if (data_len > 0)
		memcpy(msgb_put(msg, data_len), data, data_len);

	DEBUGP(DL1C, "Tx GPRS UL block (fn=%u, tn=%u, len=%zu): %s\n",
	       fn, tn, data_len, osmo_hexdump(data, data_len));

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_GPRS_UL_TBF_CFG_REQ */
int l1ctl_tx_gprs_ul_tbf_cfg_req(struct osmocom_ms *ms, uint8_t tbf_ref,
				 uint8_t slotmask)
{
	struct l1ctl_gprs_ul_tbf_cfg_req *req;
	struct msgb *msg;

	msg = osmo_l1_alloc(L1CTL_GPRS_UL_TBF_CFG_REQ);
	if (!msg)
		return -ENOMEM;

	req = (void *)msgb_put(msg, sizeof(*req));
	*req = (struct l1ctl_gprs_ul_tbf_cfg_req) {
		.tbf_ref = tbf_ref,
		.slotmask = slotmask,
	};

	DEBUGP(DL1C, "Tx GPRS UL TBF CFG (tbf_ref=%u, slotmask=0x%02x)\n",
	       tbf_ref, slotmask);

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_GPRS_DL_TBF_CFG_REQ */
int l1ctl_tx_gprs_dl_tbf_cfg_req(struct osmocom_ms *ms, uint8_t tbf_ref,
				 uint8_t slotmask, uint8_t dl_tfi)
{
	struct l1ctl_gprs_dl_tbf_cfg_req *req;
	struct msgb *msg;

	msg = osmo_l1_alloc(L1CTL_GPRS_DL_TBF_CFG_REQ);
	if (!msg)
		return -ENOMEM;

	req = (void *)msgb_put(msg, sizeof(*req));
	*req = (struct l1ctl_gprs_dl_tbf_cfg_req) {
		.tbf_ref = tbf_ref,
		.slotmask = slotmask,
		.dl_tfi = dl_tfi,
	};

	DEBUGP(DL1C, "Tx GPRS DL TBF CFG (tbf_ref=%u, slotmask=0x%02x, dl_tfi=%u)\n",
	       tbf_ref, slotmask, dl_tfi);

	return osmo_send_l1(ms, msg);
}

/* Receive incoming data from L1 using L1CTL format */
int l1ctl_recv(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc = 0;
	struct l1ctl_hdr *hdr;

	/* Make sure a message has L1CTL header (pointed by msg->l1h) */
	if (msgb_l1len(msg) < sizeof(*hdr)) {
		LOGP(DL1C, LOGL_ERROR, "Short L1CTL message, "
			"missing the header (len=%u)\n", msgb_l1len(msg));
		msgb_free(msg);
		return -1;
	}

	/* Pull the L1CTL header from the msgb */
	hdr = (struct l1ctl_hdr *) msg->l1h;
	msgb_pull(msg, sizeof(struct l1ctl_hdr));

	/* move the l1 header pointer to point _BEHIND_ l1ctl_hdr,
	   as the l1ctl header is of no interest to subsequent code */
	msg->l1h = hdr->data;

	switch (hdr->msg_type) {
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
		if (hdr->flags & L1CTL_F_DONE)
			osmo_signal_dispatch(SS_L1CTL, S_L1CTL_PM_DONE, ms);
		msgb_free(msg);
		break;
	case L1CTL_RACH_CONF:
		rc = rx_l1_rach_conf(ms, msg);
		break;
	case L1CTL_CCCH_MODE_CONF:
		rc = rx_l1_ccch_mode_conf(ms, msg);
		msgb_free(msg);
		break;
	case L1CTL_TCH_MODE_CONF:
		rc = rx_l1_tch_mode_conf(ms, msg);
		msgb_free(msg);
		break;
	case L1CTL_SIM_CONF:
		rc = rx_l1_sim_conf(ms, msg);
		break;
	case L1CTL_NEIGH_PM_IND:
		rc = rx_l1_neigh_pm_ind(ms, msg);
		msgb_free(msg);
		break;
	case L1CTL_TRAFFIC_IND:
		rc = rx_l1_traffic_ind(ms, msg);
		break;
	case L1CTL_TRAFFIC_CONF:
		msgb_free(msg);
		break;
	case L1CTL_GPRS_DL_BLOCK_IND:
		rc = rx_l1_gprs_dl_block_ind(ms, msg);
		break;
	default:
		LOGP(DL1C, LOGL_ERROR, "Unknown MSG: %u\n", hdr->msg_type);
		msgb_free(msg);
		break;
	}

	return rc;
}
