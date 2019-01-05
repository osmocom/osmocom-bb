#pragma once

typedef int (*sap_cb_t)(struct msgb *msg, struct osmocom_ms *ms);

int sap_open(struct osmocom_ms *ms);
int sap_close(struct osmocom_ms *ms);
int sap_send_apdu(struct osmocom_ms *ms, uint8_t *data, uint16_t length);
int sap_register_handler(struct osmocom_ms *ms, sap_cb_t cb);
int sap_init(struct osmocom_ms *ms);

enum sap_state {
	SAP_SOCKET_ERROR,
	SAP_NOT_CONNECTED,
	SAP_IDLE,
	SAP_CONNECTION_UNDER_NEGOTIATION,
	SAP_PROCESSING_ATR_REQUEST,
	SAP_PROCESSING_APDU_REQUEST
};
