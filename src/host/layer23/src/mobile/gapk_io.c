/*
 * GAPK (GSM Audio Pocket Knife) based audio I/O
 *
 * (C) 2017-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * Contributions by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/core/utils.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

#include <osmocom/gapk/procqueue.h>
#include <osmocom/gapk/formats.h>
#include <osmocom/gapk/codecs.h>
#include <osmocom/gapk/common.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/logging.h>

#include <osmocom/bb/mobile/tch.h>
#include <osmocom/bb/mobile/gapk_io.h>

/* The RAW PCM format is common for both audio source and sink */
static const struct osmo_gapk_format_desc *rawpcm_fmt;

static int pq_queue_tch_fb_recv(void *_state, uint8_t *out,
				const uint8_t *in, unsigned int in_len)
{
	struct gapk_io_state *state = (struct gapk_io_state *)_state;
	struct msgb *tch_msg;
	size_t frame_len;

	/* Obtain one TCH frame from the DL buffer */
	tch_msg = msgb_dequeue_count(&state->tch_dl_fb,
				     &state->tch_dl_fb_len);
	if (tch_msg == NULL)
		return -EIO;

	/* Calculate received frame length */
	frame_len = msgb_l3len(tch_msg);
	if (frame_len == 0) {
		msgb_free(tch_msg);
		return -EIO;
	}

	/* Copy the frame bytes from message */
	memcpy(out, tch_msg->l3h, frame_len);

	/* Release memory */
	msgb_free(tch_msg);

	return frame_len;
}

static int pq_queue_tch_fb_send(void *_state, uint8_t *out,
				const uint8_t *in, unsigned int in_len)
{
	struct gapk_io_state *state = (struct gapk_io_state *)_state;
	struct msgb *tch_msg;

	if (state->tch_ul_fb_len >= GAPK_ULDL_QUEUE_LIMIT) {
		LOGP(DGAPK, LOGL_ERROR, "UL TCH frame buffer overflow, dropping msg\n");
		return -EOVERFLOW;
	}

	/* Allocate a new message for the lower layers */
	tch_msg = msgb_alloc_headroom(in_len + 64, 64, "TCH frame");
	if (tch_msg == NULL)
		return -ENOMEM;

	/* Copy the frame bytes to a new message */
	tch_msg->l2h = msgb_put(tch_msg, in_len);
	memcpy(tch_msg->l2h, in, in_len);

	/* Put encoded TCH frame to the UL buffer */
	msgb_enqueue_count(&state->tch_ul_fb, tch_msg,
			   &state->tch_ul_fb_len);

	return 0;
}

/**
 * A custom TCH frame buffer block, which actually
 * handles incoming frames from DL buffer and puts
 * outgoing frames to UL buffer...
 */
static int pq_queue_tch_fb(struct osmo_gapk_pq *pq,
			   struct gapk_io_state *state,
			   bool is_src)
{
	struct osmo_gapk_pq_item *item;
	unsigned int frame_len;

	LOGP(DGAPK, LOGL_DEBUG, "PQ '%s': Adding TCH frame buffer %s\n",
	     pq->name, is_src ? "input" : "output");

	/* Allocate and add a new queue item */
	item = osmo_gapk_pq_add_item(pq);
	if (item == NULL)
		return -ENOMEM;

	/* General item type and description */
	item->type = is_src ? OSMO_GAPK_ITEM_TYPE_SOURCE : OSMO_GAPK_ITEM_TYPE_SINK;
	item->cat_name = is_src ? "source" : "sink";
	item->sub_name = "tch_fb";

	/* I/O length */
	frame_len = state->phy_fmt_desc->frame_len;
	item->len_in  = is_src ? 0 : frame_len;
	item->len_out = is_src ? frame_len : 0;

	/* Handler and it's state */
	item->proc = is_src ? &pq_queue_tch_fb_recv : &pq_queue_tch_fb_send;
	item->state = state;

	return 0;
}

/**
 * Auxiliary wrapper around format conversion block.
 * Is used to perform either a conversion from the format,
 * produced by encoder, to canonical, or a conversion
 * from canonical format to the format expected by decoder.
 */
static int pq_queue_codec_fmt_conv(struct osmo_gapk_pq *pq,
				   const struct osmo_gapk_codec_desc *codec,
				   bool is_src)
{
	const struct osmo_gapk_format_desc *codec_fmt_desc;

	/* Get format description */
	codec_fmt_desc = osmo_gapk_fmt_get_from_type(is_src ?
		codec->codec_enc_format_type : codec->codec_dec_format_type);
	if (codec_fmt_desc == NULL)
		return -ENOTSUP;

	/* Put format conversion block */
	return osmo_gapk_pq_queue_fmt_convert(pq, codec_fmt_desc, !is_src);
}

/**
 * Prepares the following queue (source is mic):
 *
 * source/alsa -> proc/codec -> proc/format ->
 *   -> proc/format -> sink/tch_fb
 *
 * The two format conversion blocks are aimed to
 * convert an encoder specific format
 * to a PHY specific format.
 */
static int prepare_audio_source(struct gapk_io_state *state,
				const char *alsa_input_dev)
{
	struct osmo_gapk_pq *pq;
	char *pq_desc;
	int rc;

	LOGP(DGAPK, LOGL_DEBUG, "Prepare audio input (capture) chain\n");

	/* Allocate a processing queue */
	pq = osmo_gapk_pq_create("pq_audio_source");
	if (pq == NULL)
		return -ENOMEM;

	/* ALSA audio source */
	rc = osmo_gapk_pq_queue_alsa_input(pq, alsa_input_dev, rawpcm_fmt->frame_len);
	if (rc)
		goto error;

	/* Frame encoder */
	rc = osmo_gapk_pq_queue_codec(pq, state->codec_desc, 1);
	if (rc)
		goto error;

	/* Encoder specific format -> canonical */
	rc = pq_queue_codec_fmt_conv(pq, state->codec_desc, true);
	if (rc)
		goto error;

	/* Canonical -> PHY specific format */
	rc = osmo_gapk_pq_queue_fmt_convert(pq, state->phy_fmt_desc, 1);
	if (rc)
		goto error;

	/* TCH frame buffer sink */
	rc = pq_queue_tch_fb(pq, state, false);
	if (rc)
		goto error;

	/* Check composed queue in strict mode */
	rc = osmo_gapk_pq_check(pq, 1);
	if (rc)
		goto error;

	/* Prepare queue (allocate buffers, etc.) */
	rc = osmo_gapk_pq_prepare(pq);
	if (rc)
		goto error;

	/* Save pointer within MS GAPK state */
	state->pq_source = pq;

	/* Describe prepared chain */
	pq_desc = osmo_gapk_pq_describe(pq);
	LOGP(DGAPK, LOGL_DEBUG, "PQ '%s': chain '%s' prepared\n", pq->name, pq_desc);
	talloc_free(pq_desc);

	return 0;

error:
	talloc_free(pq);
	return rc;
}

/**
 * Prepares the following queue (sink is speaker):
 *
 * src/tch_fb -> proc/format -> [proc/ecu] ->
 *   proc/format -> proc/codec -> sink/alsa
 *
 * The two format conversion blocks (proc/format)
 * are aimed to convert a PHY specific format
 * to an encoder specific format.
 *
 * A ECU (Error Concealment Unit) block is optionally
 * added if implemented for a given codec.
 */
static int prepare_audio_sink(struct gapk_io_state *state,
			      const char *alsa_output_dev)
{
	struct osmo_gapk_pq *pq;
	char *pq_desc;
	int rc;

	LOGP(DGAPK, LOGL_DEBUG, "Prepare audio output (playback) chain\n");

	/* Allocate a processing queue */
	pq = osmo_gapk_pq_create("pq_audio_sink");
	if (pq == NULL)
		return -ENOMEM;

	/* TCH frame buffer source */
	rc = pq_queue_tch_fb(pq, state, true);
	if (rc)
		goto error;

	/* PHY specific format -> canonical */
	rc = osmo_gapk_pq_queue_fmt_convert(pq, state->phy_fmt_desc, 0);
	if (rc)
		goto error;

	/* Optional ECU (Error Concealment Unit) */
	osmo_gapk_pq_queue_ecu(pq, state->codec_desc);

	/* Canonical -> decoder specific format */
	rc = pq_queue_codec_fmt_conv(pq, state->codec_desc, false);
	if (rc)
		goto error;

	/* Frame decoder */
	rc = osmo_gapk_pq_queue_codec(pq, state->codec_desc, 0);
	if (rc)
		goto error;

	/* ALSA audio sink */
	rc = osmo_gapk_pq_queue_alsa_output(pq, alsa_output_dev, rawpcm_fmt->frame_len);
	if (rc)
		goto error;

	/* Check composed queue in strict mode */
	rc = osmo_gapk_pq_check(pq, 1);
	if (rc)
		goto error;

	/* Prepare queue (allocate buffers, etc.) */
	rc = osmo_gapk_pq_prepare(pq);
	if (rc)
		goto error;

	/* Save pointer within MS GAPK state */
	state->pq_sink = pq;

	/* Describe prepared chain */
	pq_desc = osmo_gapk_pq_describe(pq);
	LOGP(DGAPK, LOGL_DEBUG, "PQ '%s': chain '%s' prepared\n", pq->name, pq_desc);
	talloc_free(pq_desc);

	return 0;

error:
	talloc_free(pq);
	return rc;
}

/**
 * Cleans up both TCH frame I/O buffers, destroys both
 * processing queues (chains), and deallocates the memory.
 * Should be called when a voice call is finished...
 */
void gapk_io_state_free(struct gapk_io_state *state)
{
	struct msgb *msg;

	if (state == NULL)
		return;

	/* Flush TCH frame I/O buffers */
	while ((msg = msgb_dequeue(&state->tch_dl_fb)))
		msgb_free(msg);
	while ((msg = msgb_dequeue(&state->tch_ul_fb)))
		msgb_free(msg);

	/* Destroy both audio I/O chains */
	if (state->pq_source != NULL)
		osmo_gapk_pq_destroy(state->pq_source);
	if (state->pq_sink != NULL)
		osmo_gapk_pq_destroy(state->pq_sink);

	talloc_free(state);
}

/**
 * Picks the corresponding PHY's frame format for a given codec.
 * To be used with PHYs that produce audio frames in RTP format,
 * such as trxcon (GSM 05.03 libosmocoding API).
 */
static enum osmo_gapk_format_type phy_fmt_pick_rtp(enum osmo_gapk_codec_type codec)
{
	switch (codec) {
	case CODEC_HR:
		return FMT_RTP_HR_IETF;
	case CODEC_FR:
		return FMT_GSM;
	case CODEC_EFR:
		return FMT_RTP_EFR;
	case CODEC_AMR:
		return FMT_RTP_AMR;
	default:
		return FMT_INVALID;
	}
}

/**
 * Picks the corresponding PHY's frame format for a given codec.
 * To be used with PHYs that produce audio in TI Calypso format.
 */
static enum osmo_gapk_format_type phy_fmt_pick_ti(enum osmo_gapk_codec_type codec)
{
	switch (codec) {
	case CODEC_HR:
		return FMT_TI_HR;
	case CODEC_FR:
		return FMT_TI_FR;
	case CODEC_EFR:
		return FMT_TI_EFR;
	case CODEC_AMR: /* not supported */
	default:
		return FMT_INVALID;
	}
}

/**
 * Allocates both TCH frame I/O buffers
 * and prepares both processing queues (chains).
 * Should be called when a voice call is initiated...
 */
struct gapk_io_state *
gapk_io_state_alloc(struct osmocom_ms *ms,
		    enum osmo_gapk_codec_type codec)
{
	const struct osmo_gapk_format_desc *phy_fmt_desc;
	const struct osmo_gapk_codec_desc *codec_desc;
	const struct gsm_settings *set = &ms->settings;
	enum osmo_gapk_format_type phy_fmt;
	struct gapk_io_state *state;
	int rc = 0;

	LOGP(DGAPK, LOGL_NOTICE, "Initialize GAPK I/O\n");

	/* Make sure that the chosen codec has description */
	codec_desc = osmo_gapk_codec_get_from_type(codec);
	if (codec_desc == NULL) {
		LOGP(DGAPK, LOGL_ERROR, "Invalid codec type 0x%02x\n", codec);
		return NULL;
	}

	/* Make sure that the chosen codec is supported */
	if (codec_desc->codec_encode == NULL || codec_desc->codec_decode == NULL) {
		LOGP(DGAPK, LOGL_ERROR,
		     "Codec '%s' is not supported by GAPK\n", codec_desc->name);
		return NULL;
	}

	switch (set->tch_voice.io_format) {
	case TCH_VOICE_IOF_RTP:
		phy_fmt = phy_fmt_pick_rtp(codec);
		break;
	case TCH_VOICE_IOF_TI:
		phy_fmt = phy_fmt_pick_ti(codec);
		break;
	default:
		LOGP(DGAPK, LOGL_ERROR, "Unhandled I/O format %s\n",
		     tch_voice_io_format_name(set->tch_voice.io_format));
		return NULL;
	}

	phy_fmt_desc = osmo_gapk_fmt_get_from_type(phy_fmt);
	if (phy_fmt_desc == NULL) {
		LOGP(DGAPK, LOGL_ERROR, "Failed to pick the PHY specific "
		     "frame format for codec '%s'\n", codec_desc->name);
		return NULL;
	}

	state = talloc_zero(ms, struct gapk_io_state);
	if (state == NULL) {
		LOGP(DGAPK, LOGL_ERROR, "Failed to allocate memory\n");
		return NULL;
	}

	/* Init TCH frame I/O buffers */
	INIT_LLIST_HEAD(&state->tch_dl_fb);
	INIT_LLIST_HEAD(&state->tch_ul_fb);

	/* Store the codec / format description */
	state->codec_desc = codec_desc;
	state->phy_fmt_desc = phy_fmt_desc;

	/* Use gapk_io_state as talloc context for both chains */
	osmo_gapk_set_talloc_ctx(state);

	/* Prepare both source and sink chains */
	rc |= prepare_audio_source(state, set->tch_voice.alsa_input_dev);
	rc |= prepare_audio_sink(state, set->tch_voice.alsa_output_dev);

	/* Fall back to ms instance */
	osmo_gapk_set_talloc_ctx(ms);

	/* If at lease one chain constructor failed */
	if (rc) {
		/* Destroy both audio I/O chains */
		if (state->pq_source)
			osmo_gapk_pq_destroy(state->pq_source);
		if (state->pq_sink)
			osmo_gapk_pq_destroy(state->pq_sink);

		/* Release the memory and return */
		talloc_free(state);

		LOGP(DGAPK, LOGL_ERROR, "Failed to initialize GAPK I/O\n");
		return NULL;
	}

	LOGP(DGAPK, LOGL_NOTICE,
	     "GAPK I/O initialized for MS '%s', codec '%s'\n",
	     ms->name, codec_desc->name);

	return state;
}

/* gapk_io_init_ms() wrapper, selecting a codec based on channel mode and rate */
struct gapk_io_state *
gapk_io_state_alloc_mode_rate(struct osmocom_ms *ms,
			      enum gsm48_chan_mode ch_mode,
			      bool full_rate)
{
	enum osmo_gapk_codec_type codec;

	switch (ch_mode) {
	case GSM48_CMODE_SPEECH_V1: /* FR or HR */
		codec = full_rate ? CODEC_FR : CODEC_HR;
		break;
	case GSM48_CMODE_SPEECH_EFR:
		codec = CODEC_EFR;
		break;
	case GSM48_CMODE_SPEECH_AMR:
		codec = CODEC_AMR;
		break;
	default:
		LOGP(DGAPK, LOGL_ERROR, "Unhandled channel mode 0x%02x (%s)\n",
		     ch_mode, get_value_string(gsm48_chan_mode_names, ch_mode));
		return NULL;
	}

	return gapk_io_state_alloc(ms, codec);
}

/* Enqueue a Downlink TCH frame */
void gapk_io_enqueue_dl(struct gapk_io_state *state, struct msgb *msg)
{
	if (state->tch_dl_fb_len >= GAPK_ULDL_QUEUE_LIMIT) {
		LOGP(DGAPK, LOGL_ERROR, "DL TCH frame buffer overflow, dropping msg\n");
		msgb_free(msg);
		return;
	}

	msgb_enqueue_count(&state->tch_dl_fb, msg,
			   &state->tch_dl_fb_len);

	/* Decode and play a received DL TCH frame */
	osmo_gapk_pq_execute(state->pq_sink);
}

/* Dequeue an Uplink TCH frame */
void gapk_io_dequeue_ul(struct osmocom_ms *ms, struct gapk_io_state *state)
{
	struct msgb *msg;

	/* Record and encode an UL TCH frame */
	osmo_gapk_pq_execute(state->pq_source);

	/* Obtain one TCH frame from the UL buffer */
	msg = msgb_dequeue_count(&state->tch_ul_fb, &state->tch_ul_fb_len);
	if (msg != NULL)
		tch_send_msg(ms, msg);
}

/**
 * Performs basic initialization of GAPK library,
 * setting the talloc root context and a logging category.
 */
static __attribute__((constructor)) void gapk_io_init(void)
{
	/* Init logging subsystem */
	osmo_gapk_log_init(DGAPK);

	/* Make RAWPCM format info easy to access */
	rawpcm_fmt = osmo_gapk_fmt_get_from_type(FMT_RAWPCM_S16LE);
}
