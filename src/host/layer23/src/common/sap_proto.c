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

#include <osmocom/core/utils.h>

#include <osmocom/bb/common/sap_proto.h>

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
