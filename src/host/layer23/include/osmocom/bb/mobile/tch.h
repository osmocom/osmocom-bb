#pragma once

struct osmocom_ms;
struct gsm_data_frame;
struct msgb;

int tch_init(struct osmocom_ms *ms);
int tch_send_voice_msg(struct osmocom_ms *ms, struct msgb *msg);
int tch_send_voice_frame(struct osmocom_ms *ms, const struct gsm_data_frame *frame);

int tch_soft_uart_alloc(struct osmocom_ms *ms);
int tch_soft_uart_rx_from_l1(struct osmocom_ms *ms, struct msgb *msg);
int tch_soft_uart_tx_to_l1(struct osmocom_ms *ms);
