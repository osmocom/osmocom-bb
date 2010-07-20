/* Protocol constants and helpers for Bluetooth SIM Access Profile */

/* (C) 2010 by Ingo Albrecht <prom@berlin.ccc.de>
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

/* This intends to cover SAP 1.1 */

#ifndef btsap_h
#define btsap_h

#include <stdint.h>

/* XXX put this somewhere serious and clean up uses */
#define HACK_MAX_MSG 128


/* Message type ids */
enum {
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
	SAP_POWER_SIM_OFF_RESP = 0x0a,
	SAP_POWER_SIM_ON_REQ = 0x0b,
	SAP_POWER_SIM_ON_RESP = 0x0c,
	SAP_RESET_SIM_REQ = 0x0d,
	SAP_RESET_SIM_RESP = 0x0e,
	SAP_TRANSFER_CARD_READER_STATUS_REQ = 0x0f,
	SAP_TRANSFER_CARD_READER_STATUS_RESP = 0x10,
	SAP_STATUS_IND = 0x11,
	SAP_ERROR_RESP = 0x12,
	SAP_SET_TRANSPORT_PROTOCOL_REQ = 0x13,
	SAP_SET_TRANSPORT_PROTOCOL_RESP = 0x14
};

/* Parameter type ids */
enum {
	SAP_Parameter_MaxMsgSize = 0x00,
	SAP_Parameter_ConnectionStatus = 0x01,
	SAP_Parameter_ResultCode = 0x02,
	SAP_Parameter_DisconnectionType = 0x03,
	SAP_Parameter_CommandAPDU = 0x04,
	SAP_Parameter_ResponseAPDU = 0x05,
	SAP_Parameter_ATR = 0x06,
	SAP_Parameter_CardReaderStatus = 0x07,
	SAP_Parameter_StatusChange = 0x08,
	SAP_Parameter_TransportProtocol = 0x09,
	SAP_Parameter_CommandAPDU7816 = 0x10
};

/* Parameter enums */

enum {
	SAP_ConnectionStatus_OK = 0x00,
	SAP_ConnectionStatus_SERVER_UNABLE_CONN = 0x01,
	SAP_ConnectionStatus_SERVER_MSGSIZE_NOT_SUPP = 0x02,
	SAP_ConnectionStatus_MSGSIZE_TOO_SMALL = 0x03,
	SAP_ConnectionStatus_OK_ONGOING_CALL = 0x04
};

enum {
	SAP_ResultCode_OK = 0x00,
	SAP_ResultCode_FAULT = 0x01,
	SAP_ResultCode_CARD_NOT_ACCESSIBLE = 0x02,
	SAP_ResultCode_CARD_POWERED_OFF = 0x03,
	SAP_ResultCode_CARD_REMOVED = 0x04,
	SAP_ResultCode_CARD_ALREADY_POWERED_ON = 0x05,
	SAP_ResultCode_DATA_NOT_AVAILABLE = 0x06,
	SAP_ResultCode_NOT_SUPPORTED = 0x07
};

enum {
	SAP_DisconnectionType_GRACEFUL = 0x00,
	SAP_DisconnectionType_IMMEDIATE = 0x01
};

/* XXX SAP_CardReaderStatus from GSM 11.14 and TS31.111 */

enum {
	SAP_StatusChange_UNKNOWN_ERROR = 0x00,
	SAP_StatusChange_CARD_RESET = 0x01,
	SAP_StatusChange_CARD_NOT_ACCESSIBLE = 0x02,
	SAP_StatusChange_CARD_REMOVED = 0x03,
	SAP_StatusChange_CARD_INSERTED = 0x04,
	SAP_StatusChange_CARD_RECOVERED = 0x05
};

enum {
	SAP_TransportProtocol_T0 = 0x00,
	SAP_TransportProtocol_T1 = 0x01
};


/* client and server state machine */

/* The following enum is not from the spec
 * but has been cobbled together from a simplified
 * state chart found in the spec.
 */
enum {
	SAP_State_NOT_CONNECTED,
	SAP_State_NEGOTIATING,
	SAP_State_IDLE,
	SAP_State_PROCESSING_APDU_REQ,
	SAP_State_PROCESSING_ATR_REQ,
	SAP_State_PROCESSING_SIM_RESET_REQ,
	SAP_State_PROCESSING_SIM_OFF_REQ,
	SAP_State_PROCESSING_SIM_ON_REQ,
	SAP_State_PROCESSING_CARD_READER_STATUS_REQ,
	SAP_State_PROCESSING_SET_TRANSPORT_PROTOCOL_REQ,
};

/* parsing and formatting helpers */

struct msgb *sap_alloc_msg(uint8_t id, uint8_t nparams);
void sap_parse_msg(struct msgb *m, uint8_t *id, uint8_t *nparams);

void sap_put_param(struct msgb *m, uint8_t id, uint16_t length, const uint8_t *value);
void sap_get_param(struct msgb *m, uint8_t *id, uint16_t *length, const uint8_t **value);

#endif /* !btsap_h */
