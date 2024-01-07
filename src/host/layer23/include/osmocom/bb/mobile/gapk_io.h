#pragma once

#ifdef WITH_GAPK_IO

#include <stdint.h>
#include <stdbool.h>

#include <osmocom/gapk/procqueue.h>
#include <osmocom/gapk/codecs.h>

#define GAPK_ULDL_QUEUE_LIMIT	8

/* Forward declarations */
struct osmocom_ms;
struct msgb;

struct gapk_io_state {
	/* src/alsa -> proc/codec -> sink/tch_fb */
	struct osmo_gapk_pq *pq_source;
	/* src/tch_fb -> proc/codec -> sink/alsa */
	struct osmo_gapk_pq *pq_sink;

	/* Description of currently used codec / format */
	const struct osmo_gapk_format_desc *phy_fmt_desc;
	const struct osmo_gapk_codec_desc *codec_desc;

	/* DL TCH frame buffer (received, to be played) */
	struct llist_head tch_dl_fb;
	unsigned int tch_dl_fb_len;
	/* UL TCH frame buffer (captured, to be sent) */
	struct llist_head tch_ul_fb;
	unsigned int tch_ul_fb_len;
};

struct gapk_io_state *
gapk_io_state_alloc(struct osmocom_ms *ms,
		    enum osmo_gapk_codec_type codec);
struct gapk_io_state *
gapk_io_state_alloc_mode_rate(struct osmocom_ms *ms,
			      enum gsm48_chan_mode ch_mode,
			      bool full_rate);
void gapk_io_state_free(struct gapk_io_state *state);

void gapk_io_enqueue_dl(struct gapk_io_state *state, struct msgb *msg);
int gapk_io_serve_ms(struct osmocom_ms *ms, struct gapk_io_state *state);

#endif /* WITH_GAPK_IO */
