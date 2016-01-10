#ifndef _SAP_INTERFACE_H
#define _SAP_INTERFACE_H

typedef int (*osmosap_cb_t)(struct msgb *msg, struct osmocom_ms *ms);

int sap_open(struct osmocom_ms *ms, const char *socket_path);
int sap_close(struct osmocom_ms *ms);
int osmosap_send_apdu(struct osmocom_ms *ms, uint8_t *data, uint16_t length);
int osmosap_register_handler(struct osmocom_ms *ms, osmosap_cb_t cb);
int osmosap_sapsocket(struct osmocom_ms *ms, const char *path);
int osmosap_init(struct osmocom_ms *ms);

enum osmosap_state {
	SAP_SOCKET_ERROR,
	SAP_NOT_CONNECTED,
	SAP_IDLE,
	SAP_CONNECTION_UNDER_NEGOTIATION,
	SAP_PROCESSING_ATR_REQUEST,
	SAP_PROCESSING_APDU_REQUEST
};

/* BTSAP 1.13 */
enum osmosap_msg_type {
	SAP_CONNECT_REQ = 0x00,
	SAP_CONNECT_RESP = 0x01,
	SAP_DISCONNECT_REQ = 0x02,
	SAP_DISCONNECT_RESP = 0x03,
	SAP_DISCONNECT_IND = 0x04,
	SAP_TRANSFER_APDU_REQ = 0x05,
	SAP_TRANSFER_APDU_RESP = 0x06,
	SAP_TRANSFER_ATR_REQ = 0x07,
	SAP_TRANSFER_ATR_RESP = 0x08,
	SAP_POWER_SIM_OFF_REQ = 0x09,
	SAP_POWER_SIM_OFF_RESP = 0x0A,
	SAP_POWER_SIM_ON_REQ = 0x0B,
	SAP_POWER_SIM_ON_RESP = 0x0C,
	SAP_RESET_SIM_REQ = 0x0D,
	SAP_RESET_SIM_RESP = 0x0E,
	SAP_TRANSFER_CARD_READER_STATUS_REQ = 0x0F,
	SAP_TRANSFER_CARD_READER_STATUS_RESP = 0x10,
	SAP_STATUS_IND = 0x11,
	SAP_ERROR_RESP = 0x12,
	SAP_SET_TRANSPORT_PROTOCOL_REQ = 0x13,
	SAP_SET_TRANSPORT_PROTOCOL_RESP = 0x14
};

/* BTSAP 5.2 */
enum osmosap_param_type {
	SAP_MAX_MSG_SIZE = 0x00,
	SAP_CONNECTION_STATUS = 0x01,
	SAP_RESULT_CODE = 0x02,
	SAP_DISCONNECTION_TYPE = 0x03,
	SAP_COMMAND_APDU = 0x04,
	SAP_COMMAND_APDU_7816 = 0x10,
	SAP_RESPONSE_APDU = 0x05,
	SAP_ATR = 0x06,
	SAP_CARD_READER_STATUS = 0x07,
	SAP_STATUS_CHANGE = 0x08,
	SAP_TRANSPORT_PROTOCOL = 0x09
};

struct sap_param {
	uint8_t id;
	uint16_t len;
	uint8_t *value;
};

struct sap_msg {
	uint8_t id;
	uint8_t num_params;
	struct sap_param *params;
};


#endif /* _SAP_INTERFACE_H */
