#pragma once

struct osmocom_ms;
struct gsm_data_frame;
struct msgb;

struct tch_state {
	bool rx_only; /* Rx TCH frames, but Tx nothing */
	bool is_voice; /* voice (true) or data (false) */
	union {
		struct tch_voice_state {
			enum tch_voice_io_handler handler;
			struct gapk_io_state *gapk_io;
		} voice;
		struct tch_data_state {
			enum tch_data_io_handler handler;
			struct tch_csd_sock_state *sock;
			struct osmo_v110_ta *v110_ta;
			struct osmo_soft_uart *suart;
			unsigned int num_tx;
			uint8_t e1e2e3[3];
		} data;
	};
};

int tch_init(struct osmocom_ms *ms);
int tch_serve_ms(struct osmocom_ms *ms);
int tch_send_msg(struct osmocom_ms *ms, struct msgb *msg);
int tch_send_mncc_frame(struct osmocom_ms *ms, const struct gsm_data_frame *frame);
