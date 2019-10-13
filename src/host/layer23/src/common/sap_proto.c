/*
 * SAP (SIM Access Profile) protocol definition
 * based on Bluetooth SAP specification
 *
 * (C) 2011 by Nico Golde <nico@ngolde.de>
 * (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>

#include <osmocom/bb/common/sap_proto.h>
#include <osmocom/bb/common/logging.h>

/* Table 5.1: Message Overview */
const struct value_string sap_msg_names[] = {
	{ SAP_CONNECT_REQ,			"CONNECT_REQ" },
	{ SAP_CONNECT_RESP,			"CONNECT_RESP" },
	{ SAP_DISCONNECT_REQ,			"DISCONNECT_REQ" },
	{ SAP_DISCONNECT_RESP,			"DISCONNECT_RESP" },
	{ SAP_DISCONNECT_IND,			"DISCONNECT_IND" },
	{ SAP_TRANSFER_APDU_REQ,		"TRANSFER_APDU_REQ" },
	{ SAP_TRANSFER_APDU_RESP,		"TRANSFER_APDU_RESP" },
	{ SAP_TRANSFER_ATR_REQ,			"TRANSFER_ATR_REQ" },
	{ SAP_TRANSFER_ATR_RESP,		"TRANSFER_ATR_RESP" },
	{ SAP_POWER_SIM_OFF_REQ,		"POWER_SIM_OFF_REQ" },
	{ SAP_POWER_SIM_OFF_RESP,		"POWER_SIM_OFF_RESP" },
	{ SAP_POWER_SIM_ON_REQ,			"POWER_SIM_ON_REQ" },
	{ SAP_POWER_SIM_ON_RESP,		"POWER_SIM_ON_RESP" },
	{ SAP_RESET_SIM_REQ,			"RESET_SIM_REQ" },
	{ SAP_RESET_SIM_RESP,			"RESET_SIM_RESP" },
	{ SAP_TRANSFER_CARD_READER_STATUS_REQ,	"TRANSFER_CARD_READER_STATUS_REQ" },
	{ SAP_TRANSFER_CARD_READER_STATUS_RESP,	"TRANSFER_CARD_READER_STATUS_RESP" },
	{ SAP_STATUS_IND,			"STATUS_IND" },
	{ SAP_ERROR_RESP,			"ERROR_RESP" },
	{ SAP_SET_TRANSPORT_PROTOCOL_REQ,	"SET_TRANSPORT_PROTOCOL_REQ" },
	{ SAP_SET_TRANSPORT_PROTOCOL_RESP,	"SET_TRANSPORT_PROTOCOL_RESP" },
	{ 0, NULL }
};

/* Table 5.15: List of Parameter IDs */
const struct value_string sap_param_names[] = {
	{ SAP_MAX_MSG_SIZE,		"MaxMsgSize" },
	{ SAP_CONNECTION_STATUS,	"ConnectionStatus" },
	{ SAP_RESULT_CODE,		"ResultCode" },
	{ SAP_DISCONNECTION_TYPE,	"DisconnectionType" },
	{ SAP_COMMAND_APDU,		"CommandAPDU" },
	{ SAP_COMMAND_APDU_7816,	"CommandAPDU7816" },
	{ SAP_RESPONSE_APDU,		"ResponseAPDU" },
	{ SAP_ATR,			"ATR" },
	{ SAP_CARD_READER_STATUS,	"CardReaderStatus" },
	{ SAP_STATUS_CHANGE,		"StatusChange" },
	{ SAP_TRANSPORT_PROTOCOL,	"TransportProtocol" },
	{ 0, NULL }
};

/* Table 5.18: Possible values for ResultCode */
const struct value_string sap_result_names[] = {
	{ SAP_RESULT_OK_REQ_PROC_CORR,		"OK, request processed correctly" },
	{ SAP_RESULT_ERROR_NO_REASON,		"Error, no reason defined" },
	{ SAP_RESULT_ERROR_CARD_NOT_ACC,	"Error, card not accessible" },
	{ SAP_RESULT_ERROR_CARD_POWERED_OFF,	"Error, card (already) powered off" },
	{ SAP_RESULT_ERROR_CARD_REMOVED,	"Error, card removed" },
	{ SAP_RESULT_ERROR_CARD_POWERED_ON,	"Error, card already powered on" },
	{ SAP_RESULT_ERROR_DATA_UNAVAIL,	"Error, data not available" },
	{ SAP_RESULT_ERROR_NOT_SUPPORTED,	"Error, not supported "},
	{ 0, NULL }
};

/* Table 5.19: Possible values for StatusChange */
const struct value_string sap_card_status_names[] = {
	{ SAP_CARD_STATUS_UNKNOWN_ERROR,	"Unknown Error" },
	{ SAP_CARD_STATUS_RESET,		"Card reset" },
	{ SAP_CARD_STATUS_NOT_ACC,		"Card not accessible" },
	{ SAP_CARD_STATUS_REMOVED,		"Card removed" },
	{ SAP_CARD_STATUS_INSERTED,		"Card inserted" },
	{ SAP_CARD_STATUS_RECOVERED,		"Card recovered" },
	{ 0, NULL }
};

/* Table 5.16: Possible values for ConnectionStatus */
const struct value_string sap_conn_status_names[] = {
	{ SAP_CONN_STATUS_OK_READY,		"OK, Server can fulfill requirements" },
	{ SAP_CONN_STATUS_ERROR_CONN,		"Error, Server unable to establish connection" },
	{ SAP_CONN_STATUS_ERROR_MAX_MSG_SIZE,	"Error, Server does not support maximum message size" },
	{ SAP_CONN_STATUS_ERROR_SMALL_MSG_SIZE,	"Error, maximum message size by Client is too small" },
	{ SAP_CONN_STATUS_OK_CALL,		"OK, ongoing call" },
	{ 0, NULL }
};

/*! Allocate a new message buffer with SAP message header.
 * \param[in] msg_id SAP message identifier
 * \returns message buffer in case of success, NULL otherwise
 */
struct msgb *sap_msgb_alloc(uint8_t msg_id)
{
	struct sap_message *sap_msg;
	struct msgb *msg;

	msg = msgb_alloc(GSM_SAP_LENGTH, "sap_msg");
	if (!msg) {
		LOGP(DSAP, LOGL_ERROR, "Failed to allocate SAP message\n");
		return NULL;
	}

	sap_msg = (struct sap_message *) msgb_put(msg, sizeof(*sap_msg));
	sap_msg->msg_id = msg_id;

	return msg;
}

/*! Add a new parameter to a given SAP message buffer.
 *  Padding is added automatically, SAP message header
 *  (number of parameters) is also updated automatically.
 * \param[in] msg SAP message buffer
 * \param[in] param_type parameter type (see sap_param_type enum)
 * \param[in] param_len parameter length
 * \param[in] param_value pointer to parameter value
 */
void sap_msgb_add_param(struct msgb *msg,
	enum sap_param_type param_type,
	uint16_t param_len, const uint8_t *param_value)
{
	struct sap_message *sap_msg;
	struct sap_param *param;
	uint8_t padding;
	uint8_t *buf;

	/* Update number of parameters */
	sap_msg = (struct sap_message *) msg->data;
	sap_msg->num_params++;

	/* Allocate a new parameter */
	param = (struct sap_param *) msgb_put(msg, sizeof(*param));
	param->param_id = param_type;
	param->reserved[0] = 0x00;

	/* Encode parameter value and length */
	param->length = htons(param_len);
	buf = msgb_put(msg, param_len);
	memcpy(buf, param_value, param_len);

	/* Optional padding */
	padding = 4 - (param_len % 4);
	if (padding) {
		buf = msgb_put(msg, padding);
		memset(buf, 0x00, padding);
	}
}

/*! Attempt to find a given parameter in a given SAP message.
 * \param[in] sap_msg pointer to SAP message header
 * \param[in] param_type parameter type (see sap_param_type enum)
 * \param[out] param_len parameter length (if found)
 * \returns pointer to a given parameter within the message, NULL otherwise
 */
struct sap_param *sap_get_param(const struct sap_message *sap_msg,
	enum sap_param_type param_type, uint16_t *param_len)
{
	const uint8_t *ptr = sap_msg->payload;
	struct sap_param *param = NULL;
	uint16_t plen;
	int i;

	/* We assume that message is parsed already,
	 * so we don't check for buffer overflows */
	for (i = 0; i < sap_msg->num_params; i++) {
		/* Parse one parameter */
		param = (struct sap_param *) ptr;
		plen = ntohs(param->length);

		/* Match against a given ID */
		if (param->param_id == param_type) {
			if (param_len != NULL)
				*param_len = plen;
			return param;
		}

		/* Shift pointer to the next parameter */
		ptr += sizeof(*param) + plen;
		/* Optional padding */
		ptr += 4 - (plen % 4);
	}

	return NULL;
}

/*! Parse SAP message from a given buffer into a new message buffer.
 * \param[in] buf pointer to a buffer with to be parsed message
 * \param[in] buf_len length of the buffer
 * \param[in] max_msg_size max (negotiated) message size
 * \returns new message buffer with parsed message, NULL otherwise
 */
struct msgb *sap_msg_parse(const uint8_t *buf, size_t buf_len, int max_msg_size)
{
	const struct sap_message *sap_msg;
	const uint8_t *ptr;
	struct msgb *msg;
	size_t len;
	int i;

	/* Message header is mandatory */
	if (buf_len < sizeof(*sap_msg)) {
		LOGP(DSAP, LOGL_ERROR, "Missing SAP message header\n");
		return NULL;
	}

	/* MaxMsgSize limitation (optional) */
	if (max_msg_size > 0 && buf_len > max_msg_size) {
		LOGP(DSAP, LOGL_ERROR, "Buffer (len=%zu) is bigger than "
			"given MaxMsgSize=%d\n", buf_len, max_msg_size);
		return NULL;
	}

	sap_msg = (const struct sap_message *) buf;
	len = buf_len - sizeof(*sap_msg);
	ptr = sap_msg->payload;

	LOGP(DSAP, LOGL_DEBUG, "SAP message '%s' has %u parameter(s)\n",
		get_value_string(sap_msg_names, sap_msg->msg_id),
		sap_msg->num_params);

	for (i = 0; i < sap_msg->num_params; i++) {
		struct sap_param *param;
		uint16_t param_len;
		uint16_t offset;

		/* Prevent buffer overflow */
		if (len < sizeof(*param))
			goto malformed;

		/* Parse one parameter */
		param = (struct sap_param *) ptr;
		param_len = ntohs(param->length);

		LOGP(DSAP, LOGL_DEBUG, "SAP parameter '%s' (len=%u): %s\n",
			get_value_string(sap_param_names, param->param_id),
			param_len, osmo_hexdump(param->value, param_len));

		/* Calculate relative offset */
		offset  = sizeof(*param) + param_len;
		offset += 4 - (param_len % 4); /* Optional padding */

		/* Prevent buffer overflow */
		if (offset > len)
			goto malformed;

		len -= offset;
		ptr += offset;
	}

	/* Allocate a new message buffer */
	msg = msgb_alloc(GSM_SAP_LENGTH, "sap_msg");
	if (!msg) {
		LOGP(DSAP, LOGL_ERROR, "Failed to allocate SAP message\n");
		return NULL;
	}

	msg->data = msgb_put(msg, buf_len);
	memcpy(msg->data, buf, buf_len);

	return msg;

malformed:
	LOGP(DSAP, LOGL_ERROR, "Malformed SAP message "
		"(parameter %i/%u)\n", i + 1, sap_msg->num_params);
	return NULL;
}

/*! Parse ResultCode from a given SAP message.
 * \param[in] sap_msg pointer to SAP message header
 * \returns parsed ResultCode (if found), negative otherwise
 */
int sap_check_result_code(const struct sap_message *sap_msg)
{
	struct sap_param *param;
	uint16_t param_len;
	uint8_t res_code;

	param = sap_get_param(sap_msg, SAP_RESULT_CODE, &param_len);
	if (!param || param_len != sizeof(res_code)) {
		LOGP(DSAP, LOGL_ERROR, "Missing mandatory '%s' parameter\n",
			get_value_string(sap_param_names, SAP_RESULT_CODE));
		return -EINVAL;
	}

	res_code = param->value[0];
	if (res_code >= ARRAY_SIZE(sap_result_names)) {
		LOGP(DSAP, LOGL_ERROR, "Unknown SAP ResultCode=0x%02x\n", res_code);
		return -EINVAL;
	}

	LOGP(DSAP, LOGL_DEBUG, "SAP ResultCode is '%s'\n",
		get_value_string(sap_result_names, res_code));

	return res_code;
}
