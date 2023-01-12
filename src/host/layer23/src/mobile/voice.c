/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 * (C) 2017-2018 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2022 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/codec/codec.h>

#include <osmocom/gsm/protocol/gsm_08_58.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/gapk_io.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_sock.h>
#include <osmocom/bb/mobile/voice.h>

/* Forward a Downlink voice frame to the external MNCC handler */
static int gsm_forward_mncc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_data_frame *mncc;

	/* Drop the l1ctl_info_dl header */
	msgb_pull_to_l2(msg);
	/* push mncc header in front of data */
	mncc = (struct gsm_data_frame *)
		msgb_push(msg, sizeof(struct gsm_data_frame));
	mncc->callref = ms->mncc_entity.ref;

	switch (ms->rrlayer.cd_now.mode) {
	case GSM48_CMODE_SPEECH_V1:
	{
		const uint8_t cbits = ms->rrlayer.cd_now.chan_nr >> 3;
		if (cbits == ABIS_RSL_CHAN_NR_CBITS_Bm_ACCHs)
			mncc->msg_type = GSM_TCHF_FRAME;
		else
			mncc->msg_type = GSM_TCHH_FRAME;
		break;
	}
	case GSM48_CMODE_SPEECH_EFR:
		mncc->msg_type = GSM_TCHF_FRAME_EFR;
		break;
	case GSM48_CMODE_SPEECH_AMR: /* TODO: no AMR support yet */
	default:
		/* TODO: print error message here */
		goto exit_free;
	}

	/* distribute and then free */
	if (ms->mncc_entity.sock_state && ms->mncc_entity.ref)
		return mncc_sock_from_cc(ms->mncc_entity.sock_state, msg);

exit_free:
	msgb_free(msg);
	return 0;
}

/* Receive a Downlink voice frame from the lower layers */
static int gsm_recv_voice(struct osmocom_ms *ms, struct msgb *msg)
{
	switch (ms->settings.audio.io_handler) {
	case AUDIO_IOH_LOOPBACK:
		/* Send voice frame back */
		return gsm_send_voice_msg(ms, msg);
	case AUDIO_IOH_MNCC_SOCK:
		return gsm_forward_mncc(ms, msg);
	case AUDIO_IOH_GAPK:
#ifdef WITH_GAPK_IO
		/* Enqueue a frame to the DL TCH buffer */
		if (ms->gapk_io != NULL)
			gapk_io_enqueue_dl(ms->gapk_io, msg);
		else
			msgb_free(msg);
		break;
#endif
	case AUDIO_IOH_L1PHY:
	case AUDIO_IOH_NONE:
		/* Drop voice frame */
		msgb_free(msg);
	}

	return 0;
}

/* Send an Uplink voice frame to the lower layers */
int gsm_send_voice_msg(struct osmocom_ms *ms, struct msgb *msg)
{
	/* Forward to RR */
	return gsm48_rr_tx_voice(ms, msg);
}

/* gsm_send_voice_msg() wrapper accepting an MNCC structure */
int gsm_send_voice_frame(struct osmocom_ms *ms, const struct gsm_data_frame *frame)
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

	return gsm_send_voice_msg(ms, nmsg);
}

/* Initialize voice router */
int gsm_voice_init(struct osmocom_ms *ms)
{
	ms->l1_entity.l1_traffic_ind = gsm_recv_voice;

	return 0;
}
