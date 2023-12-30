#pragma once

struct osmocom_ms;
struct gsm_data_frame;
struct msgb;

int tch_init(struct osmocom_ms *ms);
int tch_send_voice_msg(struct osmocom_ms *ms, struct msgb *msg);
int tch_send_voice_frame(struct osmocom_ms *ms, const struct gsm_data_frame *frame);

int tch_soft_uart_alloc(struct osmocom_ms *ms);
int tch_v110_ta_alloc(struct osmocom_ms *ms);

int tch_csd_rx_from_l1(struct osmocom_ms *ms, struct msgb *msg);
int tch_csd_tx_to_l1(struct osmocom_ms *ms);

struct tch_csd_sock_state;

struct tch_csd_sock_state *tch_csd_sock_init(void *ctx, const char *sock_path);
void tch_csd_sock_exit(struct tch_csd_sock_state *state);
void tch_csd_sock_conn_cb(struct tch_csd_sock_state *state, bool connected);
int tch_csd_sock_send(struct tch_csd_sock_state *state, struct msgb *msg);
int tch_csd_sock_recv(struct tch_csd_sock_state *state, struct msgb *msg);
