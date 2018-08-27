/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 * (C) 2017-2018 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <string.h>
#include <errno.h>

#include <osmocom/core/msgb.h>
#include <osmocom/codec/codec.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/settings.h>
#include <osmocom/bb/mobile/gapk_io.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/voice.h>

/*
 * TCH frame (voice) router
 */
static int gsm_recv_voice(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_data_frame *frame;
	size_t frame_len;

	/* Make sure that a MNCC handler is set */
	if (!ms->mncc_entity.mncc_recv) {
		msgb_free(msg);
		return -ENOTSUP;
	}

	/* TODO: Make sure there is an active call */

	/* Route a frame according to settings */
	switch (ms->settings.audio.io_target) {
	/* External MNCC application (e.g. LCR) */
	case AUDIO_IO_SOCKET:
		/* Push MNCC header in front of data */
		frame = (struct gsm_data_frame *)
			msgb_push(msg, sizeof(struct gsm_data_frame));

		/* Determine a frame type */
		frame_len = msgb_l3len(msg);
		switch (frame_len) {
		case GSM_FR_BYTES:
			frame->msg_type = GSM_TCHF_FRAME;
			break;
		case GSM_EFR_BYTES:
			frame->msg_type = GSM_TCHF_FRAME_EFR;
			break;
		case (GSM_HR_BYTES + 1):
			frame->msg_type = GSM_TCHH_FRAME;
			break;
		case GSM_TCH_FRAME_AMR: /* FIXME! */
		default:
			/* TODO: add some logging here */
			msgb_free(msg);
			return -ENOTSUP;
		}

		frame->callref = ms->mncc_entity.ref;

		/* Forward to an MNCC-handler */
		ms->mncc_entity.mncc_recv(ms, frame->msg_type, frame);

		/* Release memory */
		msgb_free(msg);
		break;

	/* Build-in GAPK-based audio back-end */
	case AUDIO_IO_GAPK:
		/* Prevent null pointer dereference */
		if (!ms->gapk_io) {
			msgb_free(msg);
			break;
		}

		/* Push a frame to the DL frame buffer */
		msgb_enqueue(&ms->gapk_io->tch_fb_dl, msg);
		break;

	/* Drop frame and release memory */
	case AUDIO_IO_HARDWARE:
	case AUDIO_IO_NONE:
	default:
		msgb_free(msg);
	}

	return 0;
}

/*
 * Send voice to the lower layers
 */
int gsm_send_voice(struct osmocom_ms *ms, struct msgb *msg)
{
	/**
	 * Nothing to do for now...
	 * TODO: compose l1ctl_traffic_ind header here
	 */

	/* Forward to RR */
	return gsm48_rr_tx_voice(ms, msg);
}

int gsm_send_voice_mncc(struct osmocom_ms *ms, struct gsm_data_frame *frame)
{
	unsigned int frame_len;
	struct msgb *nmsg;

	/* Determine frame length */
	switch (frame->msg_type) {
	case GSM_TCHF_FRAME:
		frame_len = GSM_FR_BYTES;
		break;
	case GSM_TCHF_FRAME_EFR:
		frame_len = GSM_EFR_BYTES;
		break;
	case GSM_TCHH_FRAME:
		frame_len = (GSM_HR_BYTES + 1);
		break;
	case GSM_TCH_FRAME_AMR:
	default:
		/* TODO: add some logging here */
		return -ENOTSUP;
	}

	/* Allocate a message for the lower layers */
	nmsg = msgb_alloc_headroom(frame_len + 64, 64, "TCH frame");
	if (!nmsg)
		return -ENOMEM;

	/* Copy payload from a frame */
	nmsg->l2h = msgb_put(nmsg, frame_len);
	memcpy(nmsg->l2h, frame->data, frame_len);

	return gsm_send_voice(ms, nmsg);
}

/*
 * Init TCH frame (voice) router
 */
int gsm_voice_init(struct osmocom_ms *ms)
{
	ms->l1_entity.l1_traffic_ind = gsm_recv_voice;
	return 0;
}
