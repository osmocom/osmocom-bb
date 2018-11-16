#pragma once

int mncc_call(struct osmocom_ms *ms, char *number);
int mncc_hangup(struct osmocom_ms *ms);
int mncc_answer(struct osmocom_ms *ms);
int mncc_hold(struct osmocom_ms *ms);
int mncc_retrieve(struct osmocom_ms *ms, int number);
int mncc_dtmf(struct osmocom_ms *ms, char *dtmf);

