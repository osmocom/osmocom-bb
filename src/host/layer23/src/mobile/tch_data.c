/*
 * (C) 2023-2024 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/isdn/v110_ta.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/transaction.h>
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

struct tch_csd_sock_state *tch_csd_sock_init(struct osmocom_ms *ms);
void tch_csd_sock_recv(struct tch_csd_sock_state *state, struct msgb *msg);
void tch_csd_sock_send(struct tch_csd_sock_state *state, struct msgb *msg);
void tch_csd_sock_exit(struct tch_csd_sock_state *state);

static void tch_soft_uart_rx_cb(void *priv, struct msgb *msg, unsigned int flags)
{
	struct tch_data_state *state = (struct tch_data_state *)priv;

	LOGP(DCSD, LOGL_DEBUG, "%s(): [flags=0x%08x] %s\n",
	     __func__, flags, msgb_hexdump(msg));

	if (state->sock != NULL && msgb_length(msg) > 0)
		tch_csd_sock_send(state->sock, msg);
	else
		msgb_free(msg);
}

static void tch_soft_uart_tx_cb(void *priv, struct msgb *msg)
{
	struct tch_data_state *state = (struct tch_data_state *)priv;

	tch_csd_sock_recv(state->sock, msg);

	LOGP(DCSD, LOGL_DEBUG, "%s(): [n_bytes=%u/%u] %s\n",
	     __func__, msg->len, msg->data_len, msgb_hexdump(msg));
}

struct osmo_soft_uart *tch_soft_uart_alloc(struct osmocom_ms *ms,
					   const struct gsm_mncc_bearer_cap *bcap)
{
	struct osmo_soft_uart *suart;

	struct osmo_soft_uart_cfg cfg = {
		.num_data_bits = bcap->data.nr_data_bits,
		.num_stop_bits = bcap->data.nr_stop_bits,
		/* .parity_mode is set below */
		.rx_buf_size = 1024, /* TODO: align with the current TCH mode */
		.rx_timeout_ms = 100, /* TODO: align with TCH framing interval */
		.priv = (void *)&ms->tch_state->data,
		.rx_cb = &tch_soft_uart_rx_cb,
		.tx_cb = &tch_soft_uart_tx_cb,
	};

	switch (bcap->data.parity) {
	case GSM48_BCAP_PAR_ODD:
		cfg.parity_mode = OSMO_SUART_PARITY_ODD;
		break;
	case GSM48_BCAP_PAR_EVEN:
		cfg.parity_mode = OSMO_SUART_PARITY_EVEN;
		break;
	case GSM48_BCAP_PAR_ZERO:
		cfg.parity_mode = OSMO_SUART_PARITY_SPACE;
		break;
	case GSM48_BCAP_PAR_ONE:
		cfg.parity_mode = OSMO_SUART_PARITY_MARK;
		break;
	case GSM48_BCAP_PAR_NONE:
	default:
		cfg.parity_mode = OSMO_SUART_PARITY_NONE;
		break;
	}

	suart = osmo_soft_uart_alloc(ms, "csd_soft_uart", &cfg);
	if (suart == NULL)
		return NULL;

	osmo_soft_uart_set_rx(suart, true);
	osmo_soft_uart_set_tx(suart, true);

	return suart;
}

/*************************************************************************************/

static void tch_v110_ta_rx_cb(void *priv, const ubit_t *buf, size_t buf_size)
{
	const struct tch_data_state *state = (struct tch_data_state *)priv;

	if (state->sock != NULL && buf_size > 0) {
		struct msgb *msg = msgb_alloc(buf_size, __func__);
		tch_csd_sock_send(state->sock, msg);
	}
}

static void tch_v110_ta_tx_cb(void *priv, ubit_t *buf, size_t buf_size)
{
	const struct tch_data_state *state = (struct tch_data_state *)priv;

	if (state->sock != NULL && buf_size > 0) {
		struct msgb *msg = msgb_alloc(buf_size, __func__);

		tch_csd_sock_recv(state->sock, msg);
		if (msgb_length(msg) < buf_size) {
			LOGP(DCSD, LOGL_NOTICE,
			     "%s(): not enough bytes for sync Tx (%u < %zu)\n",
			     __func__, msgb_length(msg), buf_size);
		}
	}
}

static void tch_v110_ta_async_rx_cb(void *priv, const ubit_t *buf, size_t buf_size)
{
	const struct tch_data_state *state = (struct tch_data_state *)priv;

	osmo_soft_uart_rx_ubits(state->suart, buf, buf_size);
}

static void tch_v110_ta_async_tx_cb(void *priv, ubit_t *buf, size_t buf_size)
{
	const struct tch_data_state *state = (struct tch_data_state *)priv;

	osmo_soft_uart_tx_ubits(state->suart, buf, buf_size);
}

static const struct {
	enum osmo_v110_ta_circuit c;
	enum osmo_soft_uart_status s;
} tch_v110_circuit_map[] = {
	{ OSMO_V110_TA_C_106, OSMO_SUART_STATUS_F_CTS },
	{ OSMO_V110_TA_C_107, OSMO_SUART_STATUS_F_DSR },
	{ OSMO_V110_TA_C_109, OSMO_SUART_STATUS_F_DCD },
};

static void tch_v110_ta_status_update_cb(void *priv, unsigned int status)
{
	const struct tch_data_state *state = (struct tch_data_state *)priv;

	LOGP(DCSD, LOGL_DEBUG, "V.110 TA status mask=0x%08x\n", status);

	for (unsigned int i = 0; i < ARRAY_SIZE(tch_v110_circuit_map); i++) {
		enum osmo_v110_ta_circuit c = tch_v110_circuit_map[i].c;
		enum osmo_soft_uart_status s = tch_v110_circuit_map[i].s;
		bool is_on = (status & (1 << c)) != 0;

		LOGP(DCSD, LOGL_DEBUG, "V.110 TA circuit %s (%s) is %s\n",
		     osmo_v110_ta_circuit_name(c),
		     osmo_v110_ta_circuit_desc(c),
		     is_on ? "ON" : "OFF");

		/* update status lines of the soft-UART */
		if (state->suart != NULL)
			osmo_soft_uart_set_status_line(state->suart, s, is_on);
	}
}

struct osmo_v110_ta *tch_v110_ta_alloc(struct osmocom_ms *ms,
				       const struct gsm_mncc_bearer_cap *bcap)
{
	struct tch_data_state *state = &ms->tch_state->data;

	struct osmo_v110_ta_cfg cfg = {
		/* .rate is set below */
		.priv = (void *)state,
		.rx_cb = &tch_v110_ta_rx_cb,
		.tx_cb = &tch_v110_ta_tx_cb,
		.status_update_cb = &tch_v110_ta_status_update_cb,
	};

	if (bcap->data.async) {
		OSMO_ASSERT(state->suart != NULL);
		cfg.rx_cb = &tch_v110_ta_async_rx_cb;
		cfg.tx_cb = &tch_v110_ta_async_tx_cb;
	}

#define BCAP_RATE(interm_rate, user_rate) \
	((interm_rate << 8) | (user_rate << 0))

	switch (BCAP_RATE(bcap->data.interm_rate, bcap->data.user_rate)) {
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_1200):
		cfg.rate = OSMO_V110_SYNC_RA1_1200;
		break;
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_2400):
		cfg.rate = OSMO_V110_SYNC_RA1_2400;
		break;
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_4800):
		cfg.rate = OSMO_V110_SYNC_RA1_4800;
		break;
	case BCAP_RATE(GSM48_BCAP_IR_16k, GSM48_BCAP_UR_9600):
		cfg.rate = OSMO_V110_SYNC_RA1_9600;
		break;
	/* TODO: according to 3GPP TS 44.021, section 4.1, the 300 bit/s user data
	 * signalling rate shall be adapted to a synchronous 600 bit/s stream. */
	case BCAP_RATE(GSM48_BCAP_IR_8k, GSM48_BCAP_UR_300):
	default:
		LOGP(DCSD, LOGL_ERROR,
		     "%s(): IR 0x%02x / UR 0x%02x combination is not supported\n",
		     __func__, bcap->data.interm_rate, bcap->data.user_rate);
		return NULL;
	}

#undef BCAP_RATE

	osmo_v110_e1e2e3_set(state->e1e2e3, cfg.rate);

	return osmo_v110_ta_alloc(ms, "csd_v110_ta", &cfg);
}

/*************************************************************************************/

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

static int tch_csd_rx_from_l1(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct tch_data_state *state = &ms->tch_state->data;
	const struct gsm48_rr_cd *cd = &ms->rrlayer.cd_now;
	const struct csd_v110_frame_desc *desc;
	ubit_t data[4 * 60];
	size_t data_len;

	if ((cd->chan_nr & RSL_CHAN_NR_MASK) == RSL_CHAN_Bm_ACCHs)
		desc = &csd_v110_lchan_desc[cd->mode].fr;
	else /* RSL_CHAN_Lm_ACCHs */
		desc = &csd_v110_lchan_desc[cd->mode].hr;
	if (OSMO_UNLIKELY(desc->num_blocks == 0))
		return -ENOTSUP;

	data_len = desc->num_blocks * desc->num_bits;
	OSMO_ASSERT(sizeof(data) >= data_len);

	switch (ms->settings.tch_data.io_format) {
	case TCH_DATA_IOF_OSMO:
		/* trxcon emits raw bits from the convolutional decoder */
		if (OSMO_UNLIKELY(msgb_l3len(msg) != data_len))
			return -EINVAL;
		memcpy(&data[0], msgb_l3(msg), msgb_l3len(msg));
		break;
	case TCH_DATA_IOF_TI:
		/* the layer1 firmware emits packed bits (LE ordering) */
		if (OSMO_UNLIKELY(msgb_l3len(msg) < data_len / 8))
			return -EINVAL;
		/* ... with swapped words (LE ordering) */
		swap_words(msgb_l3(msg), msgb_l3len(msg));
		osmo_pbit2ubit_ext(data, 0, msgb_l3(msg), 0, data_len, 1);
		break;
	default:
		LOGP(DCSD, LOGL_FATAL,
		     "%s(): unhandled data I/O format\n", __func__);
		OSMO_ASSERT(0);
	}

	for (unsigned int i = 0; i < desc->num_blocks; i++) {
		struct osmo_v110_decoded_frame df;

		if (desc->num_bits == 60)
			osmo_csd_12k_6k_decode_frame(&df, &data[i * 60], 60);
		else /* desc->num_bits == 36 */
			osmo_csd_3k6_decode_frame(&df, &data[i * 36], 36);

		/* E1/E2/E3 is out-of-band knowledge in GSM/CSD */
		memcpy(df.e_bits, state->e1e2e3, sizeof(state->e1e2e3));

		osmo_v110_ta_frame_in(state->v110_ta, &df);
	}

	if (state->suart != NULL)
		osmo_soft_uart_flush_rx(state->suart);

	return 0;
}

static int tch_csd_tx_to_l1(struct osmocom_ms *ms)
{
	struct tch_data_state *state = &ms->tch_state->data;
	const struct gsm48_rr_cd *cd = &ms->rrlayer.cd_now;
	const struct csd_v110_frame_desc *desc;
	ubit_t data[60 * 4];
	struct msgb *nmsg;
	size_t data_len;

	if ((cd->chan_nr & RSL_CHAN_NR_MASK) == RSL_CHAN_Bm_ACCHs)
		desc = &csd_v110_lchan_desc[cd->mode].fr;
	else /* RSL_CHAN_Lm_ACCHs */
		desc = &csd_v110_lchan_desc[cd->mode].hr;
	if (OSMO_UNLIKELY(desc->num_blocks == 0))
		return -ENOTSUP;

	data_len = desc->num_blocks * desc->num_bits;
	OSMO_ASSERT(sizeof(data) >= data_len);

	for (unsigned int i = 0; i < desc->num_blocks; i++) {
		struct osmo_v110_decoded_frame df;

		if (osmo_v110_ta_frame_out(state->v110_ta, &df) != 0)
			memset(&df, 0x01, sizeof(df));

		/* If E1/E2/E3 bits indicate a meaningful user data rate (see Table 5/V.110),
		 * set E7 to binary 0 in every 4-th frame (as per 3GPP TS 44.021, subclause 10.2.1).
		 * ITU-T V.110 requires this only for 600 bps, but 3GPP TS 44.021 clearly states
		 * that "such a multiframe structure exists for all user data rates". */
		if ((df.e_bits[0] + df.e_bits[1] + df.e_bits[2]) == 2)
			df.e_bits[6] = (state->num_tx != 0);
		state->num_tx = (state->num_tx + 1) & 0x03;

		if (desc->num_bits == 60)
			osmo_csd_12k_6k_encode_frame(&data[i * 60], 60, &df);
		else /* desc->num_bits == 36 */
			osmo_csd_3k6_encode_frame(&data[i * 36], 36, &df);
	}

	switch (ms->settings.tch_data.io_format) {
	case TCH_DATA_IOF_OSMO:
		/* trxcon operates on unpacked bits */
		nmsg = msgb_alloc_headroom(data_len + 64, 64, __func__);
		if (nmsg == NULL)
			return -ENOMEM;
		memcpy(msgb_put(nmsg, data_len), &data[0], data_len);
		break;
	case TCH_DATA_IOF_TI:
		/* XXX: the layer1 firmware expects TRAFFIC.req with len=33 bytes */
		nmsg = msgb_alloc_headroom(33 + 64, 64, __func__);
		if (nmsg == NULL)
			return -ENOMEM;
		nmsg->l2h = msgb_put(nmsg, 33);
		/* the layer1 firmware expects packed bits (LE ordering) */
		osmo_ubit2pbit_ext(msgb_l2(nmsg), 0, &data[0], 0, sizeof(data), 1);
		/* ... with swapped words (LE ordering) */
		swap_words(msgb_l2(nmsg), msgb_l2len(nmsg));
		break;
	default:
		LOGP(DCSD, LOGL_FATAL,
		     "%s(): unhandled data I/O format\n", __func__);
		OSMO_ASSERT(0);
	}

	return tch_send_msg(ms, nmsg);
}

static int tch_data_check_bcap(const struct gsm_mncc_bearer_cap *bcap)
{
	if (bcap == NULL) {
		LOGP(DCSD, LOGL_ERROR,
		     "%s(): CC transaction without BCap\n",
		     __func__);
		return -ENODEV;
	}

	if (bcap->mode != GSM48_BCAP_TMOD_CIRCUIT) {
		LOGP(DCSD, LOGL_ERROR,
		     "%s(): Transfer mode 0x%02x is not supported\n",
		     __func__, bcap->mode);
		return -ENOTSUP;
	}
	if (bcap->coding != GSM48_BCAP_CODING_GSM_STD) {
		LOGP(DCSD, LOGL_ERROR,
		     "%s(): Coding standard 0x%02x is not supported\n",
		     __func__, bcap->coding);
		return -ENOTSUP;
	}

	switch (bcap->transfer) {
	case GSM48_BCAP_ITCAP_UNR_DIG_INF:
		if (bcap->data.rate_adaption != GSM48_BCAP_RA_V110_X30) {
			LOGP(DCSD, LOGL_ERROR,
			     "%s(): Rate adaption (octet 5) 0x%02x is not supported\n",
			     __func__, bcap->data.rate_adaption);
			return -ENOTSUP;
		}
		break;
	case GSM48_BCAP_ITCAP_3k1_AUDIO:
	case GSM48_BCAP_ITCAP_FAX_G3:
		if (bcap->data.rate_adaption != GSM48_BCAP_RA_NONE) {
			LOGP(DCSD, LOGL_ERROR,
			     "%s(): Rate adaption (octet 5) 0x%02x was expected to be NONE\n",
			     __func__, bcap->data.rate_adaption);
			return -ENOTSUP;
		}
		break;
	default:
		LOGP(DCSD, LOGL_ERROR,
		     "%s(): Information transfer capability 0x%02x is not supported\n",
		     __func__, bcap->transfer);
		return -ENOTSUP;
	}

	if (bcap->data.sig_access != GSM48_BCAP_SA_I440_I450) {
		LOGP(DCSD, LOGL_ERROR,
		     "%s(): Signalling access protocol (octet 5) 0x%02x is not supported\n",
		     __func__, bcap->data.sig_access);
		return -ENOTSUP;
	}
	if (bcap->data.transp != GSM48_BCAP_TR_TRANSP) {
		LOGP(DCSD, LOGL_ERROR,
		     "%s(): only transparent calls are supported so far\n",
		     __func__);
		return -ENOTSUP;
	}

	return 0;
}

/*************************************************************************************/

int tch_data_recv(struct osmocom_ms *ms, struct msgb *msg)
{
	struct tch_data_state *state = &ms->tch_state->data;

	switch (state->handler) {
	case TCH_DATA_IOH_LOOPBACK:
		/* Remove the DL info header */
		msgb_pull_to_l2(msg);
		/* Send data frame back */
		return tch_send_msg(ms, msg);
	case TCH_DATA_IOH_UNIX_SOCK:
		tch_csd_rx_from_l1(ms, msg);
		tch_csd_tx_to_l1(ms);
		msgb_free(msg);
		break;
	case TCH_DATA_IOH_NONE:
		/* Drop voice frame */
		msgb_free(msg);
		break;
	}

	return 0;
}

int tch_data_state_init(struct gsm_trans *trans,
			struct tch_data_state *state)
{
	struct osmocom_ms *ms = trans->ms;
	const struct gsm_mncc_bearer_cap *bcap = trans->cc.bcap;
	int rc;

	if ((rc = tch_data_check_bcap(bcap)) != 0)
		return rc;

	switch (state->handler) {
	case TCH_DATA_IOH_UNIX_SOCK:
		state->sock = tch_csd_sock_init(ms);
		if (state->sock == NULL)
			return -ENOMEM;
		break;
	case TCH_DATA_IOH_LOOPBACK:
	case TCH_DATA_IOH_NONE:
		/* we don't need V.110 TA / soft-UART */
		return 0;
	default:
		break;
	}

	if (bcap->data.async) {
		state->suart = tch_soft_uart_alloc(ms, bcap);
		if (state->suart == NULL)
			goto exit_free;
	}

	state->v110_ta = tch_v110_ta_alloc(ms, bcap);
	if (state->v110_ta == NULL)
		goto exit_free;

	return 0;

exit_free:
	if (state->sock != NULL)
		tch_csd_sock_exit(state->sock);
	if (state->suart != NULL)
		osmo_soft_uart_free(state->suart);
	if (state->v110_ta != NULL)
		osmo_v110_ta_free(state->v110_ta);
	return -1;
}

void tch_data_state_free(struct tch_data_state *state)
{
	switch (state->handler) {
	case TCH_DATA_IOH_UNIX_SOCK:
		if (state->sock != NULL)
			tch_csd_sock_exit(state->sock);
		break;
	default:
		break;
	}

	if (state->suart != NULL)
		osmo_soft_uart_free(state->suart);
	if (state->v110_ta != NULL)
		osmo_v110_ta_free(state->v110_ta);
}

void tch_csd_sock_state_cb(struct osmocom_ms *ms, bool connected)
{
	struct tch_data_state *state = NULL;

	if (ms->tch_state == NULL || ms->tch_state->is_voice) {
		LOGP(DCSD, LOGL_INFO, "No data call is ongoing, "
		     "ignoring [dis]connection event for CSD socket\n");
		return;
	}

	state = &ms->tch_state->data;
	osmo_v110_ta_set_circuit(state->v110_ta, OSMO_V110_TA_C_108, connected);

	/* GSM/CSD employs the modified 60-bit V.110 frame format, which is basically
	 * a stripped down version of the nurmal 80-bit V.110 frame without E1/E2/E3
	 * bits and without the sync pattern.  These 60-bit V.110 frames are perfectly
	 * aligned with the radio interface block boundaries, so we're always in sync. */
	if (connected)
		osmo_v110_ta_sync_ind(state->v110_ta);
}
