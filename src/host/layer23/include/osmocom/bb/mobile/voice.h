#pragma once

#include <osmocom/core/msgb.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/mncc.h>

int gsm_voice_init(struct osmocom_ms *ms);
int gsm_send_voice(struct osmocom_ms *ms, struct msgb *msg);
int gsm_send_voice_mncc(struct osmocom_ms *ms, struct gsm_data_frame *frame);
