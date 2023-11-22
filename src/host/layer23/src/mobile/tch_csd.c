/*
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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
#include <string.h>
#include <errno.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/soft_uart.h>

#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/gsm/gsm44021.h>
#include <osmocom/isdn/v110.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/mobile/tch.h>

struct csd_v110_frame_desc {
	uint16_t num_blocks;
	uint16_t num_bits;
};

struct csd_v110_lchan_desc {
	struct csd_v110_frame_desc fr;
	struct csd_v110_frame_desc hr;
};

/* key is enum gsm48_chan_mode, so assuming a value in range 0..255 */
const struct csd_v110_lchan_desc csd_v110_lchan_desc[256] = {
#if 0
	[GSM48_CMODE_DATA_14k5] = {
		/* TCH/F14.4: 290 bits every 20 ms (14.5 kbit/s) */
		.fr = { .num_blocks = 1, .num_bits = 290 },
	},
#endif
	[GSM48_CMODE_DATA_12k0] = {
		/* TCH/F9.6: 4 * 60 bits every 20 ms (12.0 kbit/s) */
		.fr = { .num_blocks = 4, .num_bits = 60 },
	},
	[GSM48_CMODE_DATA_6k0] = {
		/* TCH/F4.8: 2 * 60 bits every 20 ms (6.0 kbit/s) */
		.fr = { .num_blocks = 2, .num_bits = 60 },
		/* TCH/H4.8: 4 * 60 bits every 40 ms (6.0 kbit/s) */
		.hr = { .num_blocks = 4, .num_bits = 60 },
	},
	[GSM48_CMODE_DATA_3k6] = {
		/* TCH/F2.4: 2 * 36 bits every 20 ms (3.6 kbit/s) */
		.fr = { .num_blocks = 2, .num_bits = 36 },
		/* TCH/H2.4: 4 * 36 bits every 40 ms (3.6 kbit/s) */
		.hr = { .num_blocks = 4, .num_bits = 36 },
	},
};

/* FIXME: store this in struct osmocom_ms */
static struct tch_csd_sock_state *g_sock_state;
static struct osmo_soft_uart *g_suart;

static void tch_soft_uart_rx_cb(void *priv, struct msgb *msg, unsigned int flags)
{
	LOGP(DL1C, LOGL_DEBUG, "%s(): [flags=0x%08x] %s\n",
	     __func__, flags, msgb_hexdump(msg));
	if (g_sock_state != NULL && msgb_length(msg) > 0)
		tch_csd_sock_send(g_sock_state, msg);
	else
		msgb_free(msg);
}

static void tch_soft_uart_tx_cb(void *priv, struct msgb *msg)
{
	int n_bytes = tch_csd_sock_recv(g_sock_state, msg);
	LOGP(DL1C, LOGL_DEBUG, "%s(): [n_bytes=%u/%u] %s\n",
	     __func__, n_bytes, msg->data_len, msgb_hexdump(msg));
}

int tch_soft_uart_alloc(struct osmocom_ms *ms)
{
	/* FIXME: take the exact config from BCap */
	const struct osmo_soft_uart_cfg cfg = {
		.num_data_bits = 8,
		.num_stop_bits = 1,
		.parity_mode = OSMO_SUART_PARITY_NONE,
		.rx_buf_size = 1024, /* TODO: align with the current TCH mode */
		.rx_timeout_ms = 100, /* TODO: align with TCH framing interval */
		.priv = (void *)ms,
		.rx_cb = &tch_soft_uart_rx_cb,
		.tx_cb = &tch_soft_uart_tx_cb,
	};

	g_suart = osmo_soft_uart_alloc(ms, "csd_soft_uart", &cfg);
	if (g_suart == NULL)
		return -ENOMEM;

	/* FIXME: enable when a data call gets connected */
	osmo_soft_uart_set_rx(g_suart, true);
	osmo_soft_uart_set_tx(g_suart, true);

	/* FIXME: this should not be here */
	g_sock_state = tch_csd_sock_init(ms, ms->settings.tch_data.unix_socket_path);
	OSMO_ASSERT(g_sock_state != NULL);

	return 0;
}

static void swap_words(uint8_t *data, size_t data_len)
{
	/* swap bytes in words */
	while (data_len >= 2) {
		uint8_t tmp = data[0];
		data[0] = data[1];
		data[1] = tmp;
		data_len -= 2;
		data += 2;
	}
}

int tch_soft_uart_rx_from_l1(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct gsm48_rr_cd *cd = &ms->rrlayer.cd_now;
	const struct csd_v110_frame_desc *desc;
	ubit_t data[4 * 60];

	if (msgb_l3len(msg) < 30)
		return -EINVAL;

	if ((cd->chan_nr & RSL_CHAN_NR_MASK) == RSL_CHAN_Bm_ACCHs)
		desc = &csd_v110_lchan_desc[cd->mode].fr;
	else /* RSL_CHAN_Lm_ACCHs */
		desc = &csd_v110_lchan_desc[cd->mode].hr;
	if (OSMO_UNLIKELY(desc->num_blocks == 0))
		return -ENOTSUP;

	switch (ms->settings.tch_data.io_format) {
	case TCH_DATA_IOF_OSMO:
		break;
	case TCH_DATA_IOF_TI:
		/* the layer1 firmware emits frames with swapped words (LE ordering) */
		swap_words(msgb_l3(msg), msgb_l3len(msg));
		break;
	}

	/* unpack packed bits (MSB goes first) */
	osmo_pbit2ubit_ext(data, 0, msgb_l3(msg), 0, sizeof(data), 1);

	for (unsigned int i = 0; i < desc->num_blocks; i++) {
		struct osmo_v110_decoded_frame df;

		if (desc->num_bits == 60) {
			osmo_csd_12k_6k_decode_frame(&df, &data[i * 60], 60);
			/* feed D-bits (D1..D48) into the soft-UART */
			osmo_soft_uart_rx_ubits(g_suart, &df.d_bits[0], 48);
		} else { /* desc->num_bits == 36 */
			osmo_csd_3k6_decode_frame(&df, &data[i * 36], 36);
			/* feed D-bits (D1..D24) into the soft-UART */
			osmo_soft_uart_rx_ubits(g_suart, &df.d_bits[0], 24);
		}

		/* XXX: what do we do with S-/X-/E-bits? */
	}

	osmo_soft_uart_flush_rx(g_suart);

	return 0;
}

int tch_soft_uart_tx_to_l1(struct osmocom_ms *ms)
{
	const struct gsm48_rr_cd *cd = &ms->rrlayer.cd_now;
	const struct csd_v110_frame_desc *desc;
	ubit_t data[60 * 4];
	struct msgb *nmsg;

	if ((cd->chan_nr & RSL_CHAN_NR_MASK) == RSL_CHAN_Bm_ACCHs)
		desc = &csd_v110_lchan_desc[cd->mode].fr;
	else /* RSL_CHAN_Lm_ACCHs */
		desc = &csd_v110_lchan_desc[cd->mode].hr;
	if (OSMO_UNLIKELY(desc->num_blocks == 0))
		return -ENOTSUP;

	for (unsigned int i = 0; i < desc->num_blocks; i++) {
		struct osmo_v110_decoded_frame df;

		/* init all bits to 1 */
		memset(&df, 0x01, sizeof(df));

		if (desc->num_bits == 60) {
			/* pull D-bits (D1..D48) out of the soft-UART */
			osmo_soft_uart_tx_ubits(g_suart, &df.d_bits[0], 48);
			osmo_csd_12k_6k_encode_frame(&data[i * 60], 60, &df);
		} else { /* desc->num_bits == 36 */
			/* pull D-bits (D1..D24) out of the soft-UART */
			osmo_soft_uart_tx_ubits(g_suart, &df.d_bits[0], 24);
			osmo_csd_3k6_encode_frame(&data[i * 36], 36, &df);
		}
	}

	nmsg = msgb_alloc_headroom(33 + 64, 64, __func__);
	OSMO_ASSERT(nmsg != NULL);

	nmsg->l2h = msgb_put(nmsg, 33); /* XXX: proper size */

	/* pack unpacked bits (MSB goes first) */
	osmo_ubit2pbit_ext(msgb_l2(nmsg), 0, &data[0], 0, sizeof(data), 1);

	switch (ms->settings.tch_data.io_format) {
	case TCH_DATA_IOF_OSMO:
		break;
	case TCH_DATA_IOF_TI:
		/* the layer1 firmware expects frames with swapped words (LE ordering) */
		swap_words(msgb_l2(nmsg), msgb_l2len(nmsg));
		break;
	}

	return gsm48_rr_tx_traffic(ms, nmsg);
}
