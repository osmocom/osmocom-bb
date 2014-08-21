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

#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/gsm/lapdm.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/codec/codec.h>

extern struct gsmtap_inst *gsmtap_inst;

static int apdu_len = -1;
static uint8_t apdu_data[256 + 7];

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


static inline int msb_get_bit(uint8_t *buf, int bn)
{
	int pos_byte = bn >> 3;
	int pos_bit  = 7 - (bn & 7);

	return (buf[pos_byte] >> pos_bit) & 1;
}

static inline void msb_set_bit(uint8_t *buf, int bn, int bit)
{
	int pos_byte = bn >> 3;
	int pos_bit  = 7 - (bn & 7);

	buf[pos_byte] |=  (bit << pos_bit);
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

	if (msgb_l3len(msg) < sizeof(*dl) + sizeof(*sb)) {
		LOGP(DL1C, LOGL_ERROR, "FBSB RESP: MSG too short %u\n",
			msgb_l3len(msg));
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

	if (msgb_l2len(msg) < sizeof(*dl)) {
		LOGP(DL1C, LOGL_ERROR, "RACH CONF: MSG too short %u\n",
			msgb_l3len(msg));
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
			if ((dl->frame_nr % 51) != 6)
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
				printf("LOSS counter for CCCH %d\n", meas->dsc);
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
				printf("LOSS counter for ACCH %d\n", meas->s);
			if (meas->s > 0)
				break;
			meas->rl_fail = 0;
			osmo_signal_dispatch(SS_L1CTL, S_L1CTL_LOSS_IND, ms);
			break;
		}
	}

	if (dl->fire_crc >= 2) {
printf("Dropping frame with %u bit errors\n", dl->num_biterr);
		LOGP(DL1C, LOGL_NOTICE, "Dropping frame with %u bit errors\n",
			dl->num_biterr);
		msgb_free(msg);
		return 0;
	}

	/* send CCCH data via GSMTAP */
	gsmtap_chan_type = chantype_rsl2gsmtap(chan_type, dl->link_id);
	gsmtap_send(gsmtap_inst, ntohs(dl->band_arfcn), chan_ts,
		    gsmtap_chan_type, chan_ss, tm.fn, dl->rx_level-110,
		    dl->snr, ccch->data, sizeof(ccch->data));

	/* determine LAPDm entity based on SACCH or not */
	if (dl->link_id & 0x40)
		le = &ms->lapdm_channel.lapdm_acch;
	else
		le = &ms->lapdm_channel.lapdm_dcch;

	/* pull the L1 header from the msgb */
	msgb_pull(msg, msg->l2h - (msg->l1h-sizeof(struct l1ctl_hdr)));
	msg->l1h = NULL;

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
	uint8_t gsmtap_chan_type;

	DEBUGP(DL1C, "(%s)\n", osmo_hexdump(msg->l2h, msgb_l2len(msg)));

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
	gsmtap_send(gsmtap_inst, 0|0x4000, chan_ts, gsmtap_chan_type,
		    chan_ss, 0, 127, 255, msg->l2h, msgb_l2len(msg));

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
	uint8_t audio_mode)
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

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_PARAM_REQ */
int l1ctl_tx_param_req(struct osmocom_ms *ms, uint8_t ta, uint8_t tx_power)
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
int l1ctl_tx_crypto_req(struct osmocom_ms *ms, uint8_t algo, uint8_t *key,
	uint8_t len)
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
	req->algo = algo;
	if (len)
		memcpy(req->key, key, len);

	return osmo_send_l1(ms, msg);
}

/* Transmit L1CTL_RACH_REQ */
int l1ctl_tx_rach_req(struct osmocom_ms *ms, uint8_t ra, uint16_t offset,
	uint8_t combined)
{
	struct msgb *msg;
	struct l1ctl_info_ul *ul;
	struct l1ctl_rach_req *req;

	msg = osmo_l1_alloc(L1CTL_RACH_REQ);
	if (!msg)
		return -1;

	DEBUGP(DL1C, "RACH Req. offset=%d combined=%d\n", offset, combined);
	ul = (struct l1ctl_info_ul *) msgb_put(msg, sizeof(*ul));
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

	if (length <= sizeof(apdu_data)) {
		memcpy(apdu_data, data, length);
		apdu_len = length;
	} else
		apdu_len = -1;

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
	uint16_t len = msg->len - sizeof(struct l1ctl_hdr);
	uint8_t *data = msg->data + sizeof(struct l1ctl_hdr);

	if (apdu_len > -1 && apdu_len + len <= sizeof(apdu_data)) {
		memcpy(apdu_data + apdu_len, data, len);
		apdu_len += len;
		gsmtap_send_ex(gsmtap_inst, GSMTAP_TYPE_SIM, 0, 0, 0, 0, 0, 0,
			0, apdu_data, apdu_len);
	}

	LOGP(DL1C, LOGL_INFO, "SIM %s\n", osmo_hexdump(data, len));
	
	/* pull the L1 header from the msgb */
	msgb_pull(msg, sizeof(struct l1ctl_hdr));
	msg->l1h = NULL;

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

	if (msgb_l3len(msg) < sizeof(*conf)) {
		LOGP(DL1C, LOGL_ERROR, "CCCH MODE CONF: MSG too short %u\n",
			msgb_l3len(msg));
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

	if (msgb_l3len(msg) < sizeof(*conf)) {
		LOGP(DL1C, LOGL_ERROR, "TCH MODE CONF: MSG too short %u\n",
			msgb_l3len(msg));
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
	uint8_t fr[33];
	int i, di, si;

	/* Header handling */
	dl = (struct l1ctl_info_dl *) msg->l1h;
	msg->l2h = dl->payload;
	ti = (struct l1ctl_traffic_ind *) msg->l2h;

	memset(fr, 0x00, 33);
	fr[0] = 0xd0;
	for (i = 0; i < 260; i++) {
		di = gsm610_bitorder[i];
		si = (i > 181) ? i + 4 : i;
		msb_set_bit(fr, 4 + di, msb_get_bit(ti->data, si));
        }
	memcpy(ti->data, fr, 33);

	DEBUGP(DL1C, "TRAFFIC IND (%s)\n", osmo_hexdump(ti->data, 33));

	/* distribute or drop */
	if (ms->l1_entity.l1_traffic_ind) {
		/* pull the L1 header from the msgb */
		msgb_pull(msg, msg->l2h - (msg->l1h-sizeof(struct l1ctl_hdr)));
		msg->l1h = NULL;

		return ms->l1_entity.l1_traffic_ind(ms, msg);
	}

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
	uint8_t fr[33];
	int i, di, si;

	/* Header handling */
	tr = (struct l1ctl_traffic_req *) msg->l2h;

	DEBUGP(DL1C, "TRAFFIC REQ (%s)\n",
		osmo_hexdump(msg->l2h, msgb_l2len(msg)));

	if (msgb_l2len(msg) != 33) {
		LOGP(DL1C, LOGL_ERROR, "Traffic Request has incorrect length "
			"(%u != 33)\n", msgb_l2len(msg));
		msgb_free(msg);
		return -EINVAL;
	}

	if ((tr->data[0] >> 4) != 0xd) {
		LOGP(DL1C, LOGL_ERROR, "Traffic Request has incorrect magic "
			"(%u != 0xd)\n", tr->data[0] >> 4);
		msgb_free(msg);
		return -EINVAL;
	}

	memset(fr, 0x00, 33);
	for (i = 0; i < 260; i++) {
		si = gsm610_bitorder[i];
		di = (i > 181) ? i + 4 : i;
		msb_set_bit(fr, di, msb_get_bit(tr->data, 4 + si));
        }
	memcpy(tr->data, fr, 33);
//	printf("TX %s\n", osmo_hexdump(tr->data, 33));

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
		if (l1h->flags & L1CTL_F_DONE)
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
	default:
		LOGP(DL1C, LOGL_ERROR, "Unknown MSG: %u\n", l1h->msg_type);
		msgb_free(msg);
		break;
	}

	return rc;
}
