/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 * (C) 2017-2018 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2022-2024 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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

#include <string.h>
#include <errno.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/signal.h>
#include <osmocom/codec/codec.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_sock.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/tch.h>

int tch_voice_state_init(struct gsm_trans *trans,
			 struct tch_voice_state *state);
int tch_data_state_init(struct gsm_trans *trans,
			struct tch_data_state *state);

void tch_voice_state_free(struct tch_voice_state *state);
void tch_data_state_free(struct tch_data_state *state);

int tch_voice_recv(struct osmocom_ms *ms, struct msgb *msg);
int tch_data_recv(struct osmocom_ms *ms, struct msgb *msg);
int tch_voice_serve_ms(struct osmocom_ms *ms);

/* Receive a Downlink traffic (voice/data) frame from the lower layers */
static int tch_recv_cb(struct osmocom_ms *ms, struct msgb *msg)
{
	struct tch_state *state = ms->tch_state;

	if (state == NULL) {
		msgb_free(msg);
		return 0;
	}

	if (state->is_voice) {
		return tch_voice_recv(ms, msg);
	} else {
		/* Rx only mode makes no sense for data calls, so we discard
		 * received DL frames and thus do not trigger sending UL frames. */
		if (!state->rx_only)
			return tch_data_recv(ms, msg);
		msgb_free(msg);
		return 0;
	}
}

/* Send an Uplink voice frame to the lower layers */
int tch_send_msg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct tch_state *state = ms->tch_state;

	if (state == NULL || state->rx_only) {
		/* TODO: fix callers and print a warning here */
		msgb_free(msg);
		return -EIO;
	}

	return gsm48_rr_tx_traffic(ms, msg);
}

/* tch_send_msg() wrapper accepting an MNCC structure */
int tch_send_mncc_frame(struct osmocom_ms *ms, const struct gsm_data_frame *frame)
{
	struct msgb *nmsg;
	int len;

	switch (frame->msg_type) {
	case GSM_TCHF_FRAME:
		len = GSM_FR_BYTES;
		break;
	case GSM_TCHF_FRAME_EFR:
		len = GSM_EFR_BYTES;
		break;
	case GSM_TCHH_FRAME:
		len = GSM_HR_BYTES;
		break;
	/* TODO: case GSM_TCH_FRAME_AMR (variable length) */
	/* TODO: case GSM_BAD_FRAME (empty?) */
	default:
		LOGP(DL1C, LOGL_ERROR, "%s(): msg_type=0x%02x: not implemented\n",
		     __func__, frame->msg_type);
		return -EINVAL;
	}

	nmsg = msgb_alloc_headroom(len + 64, 64, "TCH/F");
	if (!nmsg)
		return -ENOMEM;
	nmsg->l2h = msgb_put(nmsg, len);
	memcpy(nmsg->l2h, frame->data, len);

	return tch_send_msg(ms, nmsg);
}

int tch_serve_ms(struct osmocom_ms *ms)
{
	struct tch_state *state = ms->tch_state;
	int rc = 0;

	if (state == NULL)
		return 0;
	if (state->is_voice)
		rc = tch_voice_serve_ms(ms);
	/* TODO: else tch_data_serve_ms() */

	return rc;
}

static void tch_trans_cstate_active_cb(struct gsm_trans *trans, bool rx_only)
{
	struct osmocom_ms *ms = trans->ms;
	struct tch_state *state;
	enum gsm48_chan_mode ch_mode;

	if (ms->tch_state != NULL) {
		ms->tch_state->rx_only = rx_only;
		return; /* TODO: handle modify? */
	}

	state = talloc_zero(ms, struct tch_state);
	OSMO_ASSERT(state != NULL);
	ms->tch_state = state;
	ms->tch_state->rx_only = rx_only;

	ch_mode = ms->rrlayer.cd_now.mode;
	switch (ch_mode) {
	case GSM48_CMODE_SPEECH_V1:
	case GSM48_CMODE_SPEECH_EFR:
	case GSM48_CMODE_SPEECH_AMR:
		state->is_voice = true;
		state->voice.handler = ms->settings.tch_voice.io_handler;
		if (tch_voice_state_init(trans, &state->voice) != 0)
			goto exit_free;
		break;
	case GSM48_CMODE_DATA_14k5:
	case GSM48_CMODE_DATA_12k0:
	case GSM48_CMODE_DATA_6k0:
	case GSM48_CMODE_DATA_3k6:
		state->is_voice = false;
		state->data.handler = ms->settings.tch_data.io_handler;
		if (tch_data_state_init(trans, &state->data) != 0)
			goto exit_free;
		break;
	case GSM48_CMODE_SIGN:
	default:
		LOGP(DL1C, LOGL_ERROR, "Unhandled channel mode %s\n",
		     get_value_string(gsm48_chan_mode_names, ch_mode));
exit_free:
		talloc_free(state);
		ms->tch_state = NULL;
		return;
	}
}

static void tch_trans_free_cb(struct gsm_trans *trans)
{
	struct osmocom_ms *ms = trans->ms;
	struct tch_state *state = ms->tch_state;

	if (state == NULL)
		return;
	if (state->is_voice)
		tch_voice_state_free(&state->voice);
	else
		tch_data_state_free(&state->data);

	talloc_free(state);
	ms->tch_state = NULL;
}

static void tch_trans_state_chg_cb(struct gsm_trans *trans)
{
	switch (trans->cc.state) {
	case GSM_CSTATE_CALL_DELIVERED: /* MO call: Rx CC ALERTING */
		tch_trans_cstate_active_cb(trans, true);
		break;
	case GSM_CSTATE_ACTIVE: /* MO/MT call: Rx CC CONNECT */
		tch_trans_cstate_active_cb(trans, false);
		break;
	case GSM_CSTATE_NULL:
		tch_trans_free_cb(trans);
		break;
	}
}

/* a call-back for CC (Call Control) transaction related events */
static int tch_trans_signal_cb(unsigned int subsys, unsigned int signal,
			       void *handler_data, void *signal_data)
{
	struct gsm_trans *trans = signal_data;

	OSMO_ASSERT(subsys == SS_L23_TRANS);
	OSMO_ASSERT(trans != NULL);

	/* we only care about CC transactions here */
	if (trans->protocol != GSM48_PDISC_CC)
		return 0;

	switch ((enum osmobb_l23_trans_sig)signal) {
	case S_L23_CC_TRANS_ALLOC:
		break;
	case S_L23_CC_TRANS_FREE:
		tch_trans_free_cb(trans);
		break;
	case S_L23_CC_TRANS_STATE_CHG:
		tch_trans_state_chg_cb(trans);
		break;
	}

	return 0;
}

/* Initialize the TCH router */
int tch_init(struct osmocom_ms *ms)
{
	ms->l1_entity.l1_traffic_ind = &tch_recv_cb;

	osmo_signal_register_handler(SS_L23_TRANS, &tch_trans_signal_cb, ms);

	return 0;
}
