/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <stdlib.h>

#include <osmocom/core/msgb.h>
#include <osmocom/codec/codec.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/voice.h>


/*
 * receive voice
 */

static int gsm_recv_voice(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_data_frame *mncc;

	/* Drop the l1ctl_info_dl header */
	msgb_pull_to_l2(msg);
	/* push mncc header in front of data */
	mncc = (struct gsm_data_frame *)
		msgb_push(msg, sizeof(struct gsm_data_frame));
	mncc->callref = ms->mncc_entity.ref;

	/* FIXME: FR, EFR only! */
	switch (ms->rrlayer.cd_now.mode) {
	case GSM48_CMODE_SPEECH_V1:
		mncc->msg_type = GSM_TCHF_FRAME;
		break;
	case GSM48_CMODE_SPEECH_EFR:
		mncc->msg_type = GSM_TCHF_FRAME_EFR;
		break;
	default:
		/* TODO: print error message here */
		goto exit_free;
	}

	/* send voice frame back, if appropriate */
	if (ms->settings.audio.io_handler == AUDIO_IOH_LOOPBACK)
		gsm_send_voice_frame(ms, mncc);

	/* distribute and then free */
	if (ms->mncc_entity.mncc_recv && ms->mncc_entity.ref) {
		ms->mncc_entity.mncc_recv(ms, mncc->msg_type, mncc);
	}

exit_free:
	msgb_free(msg);
	return 0;
}

/*
 * send voice
 */
int gsm_send_voice_msg(struct osmocom_ms *ms, struct msgb *msg)
{
	/* Forward to RR */
	return gsm48_rr_tx_voice(ms, msg);
}

int gsm_send_voice_frame(struct osmocom_ms *ms, const struct gsm_data_frame *frame)
{
	struct msgb *nmsg;
	int len;

	switch (ms->rrlayer.cd_now.mode) {
	case GSM48_CMODE_SPEECH_V1:
		/* FIXME: FR only, check for TCH/F (FR) and TCH/H (HR) */
		len = GSM_FR_BYTES;
		break;
	case GSM48_CMODE_SPEECH_EFR:
		len = GSM_EFR_BYTES;
		break;
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

/*
 * init
 */

int gsm_voice_init(struct osmocom_ms *ms)
{
	ms->l1_entity.l1_traffic_ind = gsm_recv_voice;

	return 0;
}
