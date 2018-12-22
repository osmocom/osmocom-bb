#pragma once

#include <stdint.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>

/* Table 5.1: Message Overview
 * NOTE: messages are used as events for SAP FSM */
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

/* Figure 5.2: Payload Coding */
struct sap_param {
	/* Parameter ID, see sap_param_type enum */
	uint8_t param_id;
	/* Reserved for further use (shall be set to 0x00) */
	uint8_t reserved[1];
	/* Parameter length */
	uint16_t length;
	/* Parameter value (and optional padding) */
	uint8_t value[0];
} __attribute__((packed));

/* Figure 5.1 Message Format */
struct sap_message {
	/* Message ID, see sap_msg_type enum */
	uint8_t msg_id;
	/* Number of parameters */
	uint8_t num_params;
	/* Reserved for further use (shall be set to 0x00) */
	uint8_t reserved[2];
	/* Payload, see sap_param struct */
	uint8_t payload[0];
} __attribute__((packed));

#define GSM_SAP_LENGTH 300
#define GSM_SAP_HEADROOM 32

struct msgb *sap_msgb_alloc(uint8_t msg_id);
struct msgb *sap_msg_parse(const uint8_t *buf, size_t buf_len, int max_msg_size);
int sap_check_result_code(const struct sap_message *sap_msg);

void sap_msgb_add_param(struct msgb *msg,
	enum sap_param_type param_type,
	uint16_t param_len, const uint8_t *param_value);
struct sap_param *sap_get_param(const struct sap_message *sap_msg,
	enum sap_param_type param_type, uint16_t *param_len);
