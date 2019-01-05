#pragma once

#include <stdint.h>

#include <osmocom/core/utils.h>

/* Table 5.1: Message Overview */
enum sap_msg_type {
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

/* Table 5.15: List of Parameter IDs */
enum sap_param_type {
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

/* Table 5.18: Possible values for ResultCode */
enum sap_result_type {
	SAP_RESULT_OK_REQ_PROC_CORR = 0x00,
	SAP_RESULT_ERROR_NO_REASON = 0x01,
	SAP_RESULT_ERROR_CARD_NOT_ACC = 0x02,
	SAP_RESULT_ERROR_CARD_POWERED_OFF = 0x03,
	SAP_RESULT_ERROR_CARD_REMOVED = 0x04,
	SAP_RESULT_ERROR_CARD_POWERED_ON = 0x05,
	SAP_RESULT_ERROR_DATA_UNAVAIL = 0x06,
	SAP_RESULT_ERROR_NOT_SUPPORTED = 0x07,
};

/* Table 5.19: Possible values for StatusChange */
enum sap_card_status_type {
	SAP_CARD_STATUS_UNKNOWN_ERROR = 0x00,
	SAP_CARD_STATUS_RESET = 0x01,
	SAP_CARD_STATUS_NOT_ACC = 0x02,
	SAP_CARD_STATUS_REMOVED = 0x03,
	SAP_CARD_STATUS_INSERTED = 0x04,
	SAP_CARD_STATUS_RECOVERED = 0x05,
};

/* Table 5.16: Possible values for ConnectionStatus */
enum sap_conn_status_type {
	SAP_CONN_STATUS_OK_READY = 0x00,
	SAP_CONN_STATUS_ERROR_CONN = 0x01,
	SAP_CONN_STATUS_ERROR_MAX_MSG_SIZE = 0x02,
	SAP_CONN_STATUS_ERROR_SMALL_MSG_SIZE = 0x03,
	SAP_CONN_STATUS_OK_CALL = 0x04,
};

extern const struct value_string sap_msg_names[];
extern const struct value_string sap_param_names[];
extern const struct value_string sap_result_names[];
extern const struct value_string sap_card_status_names[];
extern const struct value_string sap_conn_status_names[];

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
