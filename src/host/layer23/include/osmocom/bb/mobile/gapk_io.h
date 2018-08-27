#pragma once

#include <osmocom/gapk/procqueue.h>
#include <osmocom/gapk/codecs.h>

/* Forward declarations */
struct osmocom_ms;

struct gapk_io_state {
	/* src/alsa -> proc/codec -> sink/tch_fb */
	struct osmo_gapk_pq *pq_source;
	/* src/tch_fb -> proc/codec -> sink/alsa */
	struct osmo_gapk_pq *pq_sink;

	/* Description of currently used codec / format */
	const struct osmo_gapk_format_desc *phy_fmt_desc;
	const struct osmo_gapk_codec_desc *codec_desc;

	/* Buffer for to be played TCH frames (from DL) */
	struct llist_head tch_fb_dl;
	/* Buffer for encoded TCH frames (for UL) */
	struct llist_head tch_fb_ul;
};

void gapk_io_init(void);
int gapk_io_dequeue(struct osmocom_ms *ms);

int gapk_io_init_ms_chan(struct osmocom_ms *ms,
	uint8_t ch_type, uint8_t ch_mode);
int gapk_io_init_ms(struct osmocom_ms *ms,
	enum osmo_gapk_codec_type codec);
int gapk_io_clean_up_ms(struct osmocom_ms *ms);
