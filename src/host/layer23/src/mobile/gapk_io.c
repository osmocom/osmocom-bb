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
#include <osmocom/bb/common/logging.h>

#include <osmocom/bb/mobile/voice.h>
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
			   struct gapk_io_state *io_state,
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
	frame_len = io_state->phy_fmt_desc->frame_len;
	item->len_in  = is_src ? 0 : frame_len;
	item->len_out = is_src ? frame_len : 0;

	/* Handler and it's state */
	item->proc = is_src ? &pq_queue_tch_fb_recv : &pq_queue_tch_fb_send;
	item->state = io_state;

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
static int prepare_audio_source(struct gapk_io_state *gapk_io,
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
	rc = osmo_gapk_pq_queue_codec(pq, gapk_io->codec_desc, 1);
	if (rc)
		goto error;

	/* Encoder specific format -> canonical */
	rc = pq_queue_codec_fmt_conv(pq, gapk_io->codec_desc, true);
	if (rc)
		goto error;

	/* Canonical -> PHY specific format */
	rc = osmo_gapk_pq_queue_fmt_convert(pq, gapk_io->phy_fmt_desc, 1);
	if (rc)
		goto error;

	/* TCH frame buffer sink */
	rc = pq_queue_tch_fb(pq, gapk_io, false);
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
	gapk_io->pq_source = pq;

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
static int prepare_audio_sink(struct gapk_io_state *gapk_io,
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
	rc = pq_queue_tch_fb(pq, gapk_io, true);
	if (rc)
		goto error;

#if 0
	/* TODO: PHY specific format -> canonical */
	rc = osmo_gapk_pq_queue_fmt_convert(pq, gapk_io->phy_fmt_desc, 0);
	if (rc)
		goto error;
#endif

	/* Optional ECU (Error Concealment Unit) */
	osmo_gapk_pq_queue_ecu(pq, gapk_io->codec_desc);

#if 0
	/* TODO: canonical -> decoder specific format */
	rc = pq_queue_codec_fmt_conv(pq, gapk_io->codec_desc, false);
	if (rc)
		goto error;
#endif

	/* Frame decoder */
	rc = osmo_gapk_pq_queue_codec(pq, gapk_io->codec_desc, 0);
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
	gapk_io->pq_sink = pq;

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
int gapk_io_clean_up_ms(struct osmocom_ms *ms)
{
	struct msgb *msg;

	if (ms->gapk_io == NULL)
		return 0;

	/* Flush TCH frame I/O buffers */
	while ((msg = msgb_dequeue(&ms->gapk_io->tch_dl_fb)))
		msgb_free(msg);
	while ((msg = msgb_dequeue(&ms->gapk_io->tch_ul_fb)))
		msgb_free(msg);

	/* Destroy both audio I/O chains */
	if (ms->gapk_io->pq_source)
		osmo_gapk_pq_destroy(ms->gapk_io->pq_source);
	if (ms->gapk_io->pq_sink)
		osmo_gapk_pq_destroy(ms->gapk_io->pq_sink);

	talloc_free(ms->gapk_io);
	ms->gapk_io = NULL;

	return 0;
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
 * Allocates both TCH frame I/O buffers
 * and prepares both processing queues (chains).
 * Should be called when a voice call is initiated...
 */
int gapk_io_init_ms(struct osmocom_ms *ms, enum osmo_gapk_codec_type codec)
{
	const struct osmo_gapk_format_desc *phy_fmt_desc;
	const struct osmo_gapk_codec_desc *codec_desc;
	struct gsm_settings *set = &ms->settings;
	enum osmo_gapk_format_type phy_fmt;
	struct gapk_io_state *gapk_io;
	int rc = 0;

	LOGP(DGAPK, LOGL_NOTICE, "Initialize GAPK I/O\n");

	OSMO_ASSERT(ms->gapk_io == NULL);

	/* Make sure that the chosen codec has description */
	codec_desc = osmo_gapk_codec_get_from_type(codec);
	if (codec_desc == NULL) {
		LOGP(DGAPK, LOGL_ERROR, "Invalid codec type 0x%02x\n", codec);
		return -EINVAL;
	}

	/* Make sure that the chosen codec is supported */
	if (codec_desc->codec_encode == NULL || codec_desc->codec_decode == NULL) {
		LOGP(DGAPK, LOGL_ERROR,
		     "Codec '%s' is not supported by GAPK\n", codec_desc->name);
		return -ENOTSUP;
	}

	/**
	 * Pick the corresponding PHY's frame format
	 * TODO: ask PHY, which format is supported?
	 * FIXME: RTP (valid for trxcon) is used for now
	 */
	phy_fmt = phy_fmt_pick_rtp(codec);
	phy_fmt_desc = osmo_gapk_fmt_get_from_type(phy_fmt);
	if (phy_fmt_desc == NULL) {
		LOGP(DGAPK, LOGL_ERROR, "Failed to pick the PHY specific "
		     "frame format for codec '%s'\n", codec_desc->name);
		return -EINVAL;
	}

	gapk_io = talloc_zero(ms, struct gapk_io_state);
	if (gapk_io == NULL) {
		LOGP(DGAPK, LOGL_ERROR, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	/* Init TCH frame I/O buffers */
	INIT_LLIST_HEAD(&gapk_io->tch_dl_fb);
	INIT_LLIST_HEAD(&gapk_io->tch_ul_fb);

	/* Store the codec / format description */
	gapk_io->codec_desc = codec_desc;
	gapk_io->phy_fmt_desc = phy_fmt_desc;

	/* Use gapk_io_state as talloc context for both chains */
	osmo_gapk_set_talloc_ctx(gapk_io);

	/* Prepare both source and sink chains */
	rc |= prepare_audio_source(gapk_io, set->audio.alsa_input_dev);
	rc |= prepare_audio_sink(gapk_io, set->audio.alsa_output_dev);

	/* Fall back to ms instance */
	osmo_gapk_set_talloc_ctx(ms);

	/* If at lease one chain constructor failed */
	if (rc) {
		/* Destroy both audio I/O chains */
		if (gapk_io->pq_source)
			osmo_gapk_pq_destroy(gapk_io->pq_source);
		if (gapk_io->pq_sink)
			osmo_gapk_pq_destroy(gapk_io->pq_sink);

		/* Release the memory and return */
		talloc_free(gapk_io);

		LOGP(DGAPK, LOGL_ERROR, "Failed to initialize GAPK I/O\n");
		return rc;
	}

	/* Init pointers */
	ms->gapk_io = gapk_io;

	LOGP(DGAPK, LOGL_NOTICE,
	     "GAPK I/O initialized for MS '%s', codec '%s'\n",
	     ms->name, codec_desc->name);

	return 0;
}

/**
 * Wrapper around gapk_io_init_ms(), that maps both
 * given GSM 04.08 channel type (HR/FR) and channel
 * mode to a codec from 'osmo_gapk_codec_type' enum,
 * checks if a mapped codec is supported by GAPK,
 * and finally calls the wrapped function.
 */
int gapk_io_init_ms_chan(struct osmocom_ms *ms,
			 uint8_t ch_type, uint8_t ch_mode)
{
	enum osmo_gapk_codec_type codec;

	/* Map GSM 04.08 channel mode to GAPK codec type */
	switch (ch_mode) {
	case GSM48_CMODE_SPEECH_V1: /* FR or HR */
		if (ch_type == RSL_CHAN_Bm_ACCHs)
			codec = CODEC_FR;
		else
			codec = CODEC_HR;
		break;

	case GSM48_CMODE_SPEECH_EFR:
		codec = CODEC_EFR;
		break;

	case GSM48_CMODE_SPEECH_AMR:
		codec = CODEC_AMR;
		break;

	/* Signalling or CSD, do nothing */
	case GSM48_CMODE_DATA_14k5:
	case GSM48_CMODE_DATA_12k0:
	case GSM48_CMODE_DATA_6k0:
	case GSM48_CMODE_DATA_3k6:
	case GSM48_CMODE_SIGN:
		return 0;
	default:
		LOGP(DGAPK, LOGL_ERROR, "Unhandled channel mode 0x%02x (%s)\n",
		     ch_mode, get_value_string(gsm48_chan_mode_names, ch_mode));
		return -EINVAL;
	}

	return gapk_io_init_ms(ms, codec);
}

/**
 * Performs basic initialization of GAPK library,
 * setting the talloc root context and a logging category.
 * Should be called during the application initialization...
 */
void gapk_io_init(void)
{
	/* Init logging subsystem */
	osmo_gapk_log_init(DGAPK);

	/* Make RAWPCM format info easy to access */
	rawpcm_fmt = osmo_gapk_fmt_get_from_type(FMT_RAWPCM_S16LE);
}

void gapk_io_enqueue_dl(struct gapk_io_state *state, struct msgb *msg)
{
	if (state->tch_dl_fb_len >= GAPK_ULDL_QUEUE_LIMIT) {
		LOGP(DGAPK, LOGL_ERROR, "DL TCH frame buffer overflow, dropping msg\n");
		msgb_free(msg);
		return;
	}

	msgb_enqueue_count(&state->tch_dl_fb, msg,
			   &state->tch_dl_fb_len);
}

/* Serves both UL/DL TCH frame I/O buffers */
int gapk_io_serve_ms(struct osmocom_ms *ms)
{
	struct gapk_io_state *gapk_io = ms->gapk_io;
	int work = 0;

	/**
	 * Make sure we have at least two DL frames
	 * to prevent discontinuous playback.
	 */
	if (gapk_io->tch_dl_fb_len < 2)
		return 0;

	/**
	 * TODO: if there is an active call, but no TCH frames
	 * in DL buffer, put silence frames using the upcoming
	 * ECU (Error Concealment Unit) of libosmocodec.
	 */
	while (!llist_empty(&gapk_io->tch_dl_fb)) {
		/* Decode and play a received DL TCH frame */
		osmo_gapk_pq_execute(gapk_io->pq_sink);

		/* Record and encode an UL TCH frame back */
		osmo_gapk_pq_execute(gapk_io->pq_source);

		work |= 1;
	}

	while (!llist_empty(&gapk_io->tch_ul_fb)) {
		struct msgb *tch_msg;

		/* Obtain one TCH frame from the UL buffer */
		tch_msg = msgb_dequeue_count(&gapk_io->tch_ul_fb,
					     &gapk_io->tch_ul_fb_len);

		/* Push a voice frame to the lower layers */
		gsm_send_voice_msg(ms, tch_msg);

		work |= 1;
	}

	return work;
}
