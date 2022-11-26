#pragma once

struct osmocom_ms;
struct gsm_data_frame;

int gsm_voice_init(struct osmocom_ms *ms);
int gsm_send_voice(struct osmocom_ms *ms, struct gsm_data_frame *data);
