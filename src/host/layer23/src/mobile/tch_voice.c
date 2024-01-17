/*
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

#include <stdint.h>
#include <errno.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/mobile/gapk_io.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_sock.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/tch.h>

/* Forward a Downlink voice frame to the external MNCC handler */
static int tch_forward_mncc(struct osmocom_ms *ms, struct msgb *msg)
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

int tch_voice_recv(struct osmocom_ms *ms, struct msgb *msg)
{
	struct tch_voice_state *state = &ms->tch_state->voice;

	switch (state->handler) {
	case TCH_VOICE_IOH_LOOPBACK:
		/* Remove the DL info header */
		msgb_pull_to_l2(msg);
		/* Send voice frame back */
		return tch_send_msg(ms, msg);
	case TCH_VOICE_IOH_MNCC_SOCK:
		return tch_forward_mncc(ms, msg);
	case TCH_VOICE_IOH_GAPK:
#ifdef WITH_GAPK_IO
		/* Enqueue a frame to the DL TCH buffer */
		if (state->gapk_io != NULL)
			gapk_io_enqueue_dl(state->gapk_io, msg);
		else
			msgb_free(msg);
		break;
#endif
	case TCH_VOICE_IOH_L1PHY:
	case TCH_VOICE_IOH_NONE:
		/* Drop voice frame */
		msgb_free(msg);
		break;
	}

	return 0;
}

int tch_voice_serve_ms(struct osmocom_ms *ms)
{
#ifdef WITH_GAPK_IO
	struct tch_voice_state *state = &ms->tch_state->voice;

	if (state->handler == TCH_VOICE_IOH_GAPK)
		return gapk_io_serve_ms(ms, state->gapk_io);
#endif

	return 0;
}

int tch_voice_state_init(struct gsm_trans *trans, struct tch_voice_state *state)
{
#ifdef WITH_GAPK_IO
	struct osmocom_ms *ms = trans->ms;
	const struct gsm48_rr_cd *cd = &ms->rrlayer.cd_now;

	switch (state->handler) {
	case TCH_VOICE_IOH_GAPK:
		if ((cd->chan_nr & RSL_CHAN_NR_MASK) == RSL_CHAN_Bm_ACCHs)
			state->gapk_io = gapk_io_state_alloc_mode_rate(ms, cd->mode, true);
		else /* RSL_CHAN_Lm_ACCHs */
			state->gapk_io = gapk_io_state_alloc_mode_rate(ms, cd->mode, false);
		if (state->gapk_io == NULL)
			return -1;
		break;
	default:
		break;
	}
#endif

	return 0;
}

void tch_voice_state_free(struct tch_voice_state *state)
{
	switch (state->handler) {
#ifdef WITH_GAPK_IO
	case TCH_VOICE_IOH_GAPK:
		gapk_io_state_free(state->gapk_io);
		break;
#endif
	default:
		break;
	}
}
