#pragma once

#include <stdint.h>

#include <osmocom/core/msgb.h>

struct osmocom_ms;

typedef int (*sap_msg_cb_t)(struct osmocom_ms *ms, struct msgb *msg);
typedef int (*sap_rsp_cb_t)(struct osmocom_ms *ms, int res_code,
	uint8_t res_type, uint16_t param_len, const uint8_t *param_val);

void sap_init(struct osmocom_ms *ms);
int sap_open(struct osmocom_ms *ms);
int sap_close(struct osmocom_ms *ms);
int _sap_close_sock(struct osmocom_ms *ms);

int sap_send_reset_req(struct osmocom_ms *ms);
int sap_send_poweron_req(struct osmocom_ms *ms);
int sap_send_poweroff_req(struct osmocom_ms *ms);

int sap_send_atr_req(struct osmocom_ms *ms);
int sap_send_apdu(struct osmocom_ms *ms, uint8_t *apdu, uint16_t apdu_len);
