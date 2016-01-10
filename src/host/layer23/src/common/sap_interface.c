/* BTSAP socket interface of layer2/3 stack */

/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 * (C) 2011 by Nico Golde <nico@ngolde.de>
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

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/sap_interface.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/talloc.h>

#include <sys/socket.h>
#include <sys/un.h>

#include <arpa/inet.h>

#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define GSM_SAP_LENGTH 300
#define GSM_SAP_HEADROOM 32

static void sap_connect(struct osmocom_ms *ms);

static const struct value_string sap_param_names[] = {
	{SAP_MAX_MSG_SIZE,			"MaxMsgSize"},
	{SAP_CONNECTION_STATUS, 	"ConnectionStatus"},
	{SAP_RESULT_CODE,			"ResultCode"},
	{SAP_DISCONNECTION_TYPE,	"DisconnectionType"},
	{SAP_COMMAND_APDU,			"CommandAPDU"},
	{SAP_COMMAND_APDU_7816,		"CommandAPDU7816"},
	{SAP_RESPONSE_APDU,			"ResponseAPDU"},
	{SAP_ATR,					"ATR"},
	{SAP_CARD_READER_STATUS,	"CardReaderStatus"},
	{SAP_STATUS_CHANGE,			"StatusChange"},
	{SAP_TRANSPORT_PROTOCOL,	"TransportProtocol"}
};

static const struct value_string sap_msg_names[] = {
	{SAP_CONNECT_REQ,						"CONNECT_REQ"},
	{SAP_CONNECT_RESP,						"CONNECT_RESP"},
	{SAP_DISCONNECT_REQ,					"DISCONNECT_REQ"},
	{SAP_DISCONNECT_RESP,					"DISCONNECT_RESP"},
	{SAP_DISCONNECT_IND,					"DISCONNECT_IND"},
	{SAP_TRANSFER_APDU_REQ,					"TRANSFER_APDU_REQ"},
	{SAP_TRANSFER_APDU_RESP,				"TRANSFER_APDU_RESP"},
	{SAP_TRANSFER_ATR_REQ,					"TRANSFER_ATR_REQ"},
	{SAP_TRANSFER_ATR_RESP,					"TRANSFER_ATR_RESP"},
	{SAP_POWER_SIM_OFF_REQ,					"POWER_SIM_OFF_REQ"},
	{SAP_POWER_SIM_OFF_RESP,				"POWER_SIM_OFF_RESP"},
	{SAP_POWER_SIM_ON_REQ,					"POWER_SIM_ON_REQ"},
	{SAP_POWER_SIM_ON_RESP,					"POWER_SIM_ON_RESP"},
	{SAP_RESET_SIM_REQ,						"RESET_SIM_REQ"},
	{SAP_RESET_SIM_RESP,					"RESET_SIM_RESP"},
	{SAP_TRANSFER_CARD_READER_STATUS_REQ,	"TRANSFER_CARD_READER_STATUS_REQ"},
	{SAP_TRANSFER_CARD_READER_STATUS_RESP,	"TRANSFER_CARD_READER_STATUS_RESP"},
	{SAP_STATUS_IND,						"STATUS_IND"},
	{SAP_ERROR_RESP,						"ERROR_RESP"},
	{SAP_SET_TRANSPORT_PROTOCOL_REQ,		"SET_TRANSPORT_PROTOCOL_REQ"},
	{SAP_SET_TRANSPORT_PROTOCOL_RESP,		"SET_TRANSPORT_PROTOCOL_RESP"}
};

/* BTSAP table 5.18 */
static const struct value_string sap_result_names[] = {
	{0, "OK, request processed correctly"},
	{1, "Error, no reason defined"},
	{2, "Error, card not accessible"},
	{3, "Error, card (already) powered off"},
	{4, "Error, card removed"},
	{5, "Error, card already powered on"},
	{6, "Error, data not available"},
	{7, "Error, not supported"}
};

static const struct value_string sap_status_change_names[] = {
	{0, "Unknown Error"},
	{1, "Card reset"},
	{2, "Card not accessible"},
	{3, "Card removed"},
	{4, "Card inserted"},
	{5, "Card recovered"},
};

static const struct value_string sap_status_names[] = {
	{0, "OK, Server can fulfill requirements"},
	{1, "Error, Server unable to establish connection"},
	{2, "Error, Server does not support maximum message size"},
	{3, "Error, maximum message size by Client is too small"},
	{4, "OK, ongoing call"}
};

static struct msgb *sap_create_msg(uint8_t id, uint8_t num_params, struct sap_param *params)
{
	struct msgb *msg;
	uint8_t *msgp;
	uint8_t i, plen, padding = 0;

	msg = msgb_alloc(GSM_SAP_LENGTH, "osmosap");
	if (!msg) {
		LOGP(DSAP, LOGL_ERROR, "Failed to allocate msg.\n");
		return NULL;
	}

	/* BTSAP 5.1 */
	msgb_put_u8(msg, id);
	msgb_put_u8(msg, num_params);
	msgb_put_u16(msg, 0);

	for(i=0; i<num_params; i++){
		plen = params[i].len;
		msgb_put_u8(msg, params[i].id);
		msgb_put_u8(msg, 0);
		msgb_put_u16(msg, plen);
		if(plen % 4){
			padding = 4 - (plen % 4);
		}
		msgp = msgb_put(msg, plen + padding);
		memcpy(msgp, params[i].value, plen);

		if(padding){
			memset(msgp + plen, 0, padding);
		}
	}

	return msg;
}

static int osmosap_send(struct osmocom_ms *ms, struct msgb *msg)
{
	if(ms->sap_entity.sap_state == SAP_NOT_CONNECTED && !ms->sap_entity.sap_state == SAP_CONNECTION_UNDER_NEGOTIATION)
		sap_connect(ms);

	if (ms->sap_wq.bfd.fd <= 0)
		return -EINVAL;

	if (osmo_wqueue_enqueue(&ms->sap_wq, msg) != 0) {
		LOGP(DSAP, LOGL_ERROR, "Failed to enqueue msg.\n");
		msgb_free(msg);
		return -1;
	}

	return 0;
}

static int sap_parse_result(struct sap_param *param)
{
	if(param->id != SAP_RESULT_CODE){
		LOGP(DSAP, LOGL_INFO, "> Parameter id: %u no valid result type\n", param->id);
		return -1;
	} else {
		LOGP(DSAP, LOGL_INFO, "> RESULT CODE: %s\n",
				get_value_string(sap_result_names, param->value[0]));
	}

	if(param->value[0] > sizeof(sap_result_names)/sizeof(struct value_string)){
		return -1;
	}

	return 0;
}

static uint8_t *sap_get_param(uint8_t *data, struct sap_param *param)
{
	uint8_t *dptr = data;
	uint8_t padlen;

	param->id = *dptr++;
	/* skip reserved byte */
	dptr++;
	param->len = *dptr << 8;
	dptr++;
	param->len |= *dptr++;
	param->value = talloc_zero_size(NULL, param->len);
	memcpy(param->value, dptr, param->len);

	/* skip parameter and padding and return pointer to next parameter */
	dptr += param->len;
	if(param->len % 4){
		padlen = (4 - param->len % 4);
	} else {
		padlen = 0;
	}
	dptr += padlen;

	return dptr;
}

static void sap_msg_free(struct sap_msg *msg)
{
	uint8_t i;
	for(i=0; i<msg->num_params; i++){
		talloc_free(msg->params[i].value);
		talloc_free(msg->params);
	}
	talloc_free(msg);
}

static struct sap_msg *sap_parse_msg(uint8_t *data)
{
	struct sap_msg *msg = talloc_zero(NULL, struct sap_msg);
	uint8_t *ptr = data;
	uint8_t i;

	if(!msg){
		return NULL;
	}

	msg->id = *ptr++;
	LOGP(DSAP, LOGL_INFO, "> %s \n", get_value_string(sap_msg_names, msg->id));

	msg->num_params = *ptr++;
	/* skip two reserved null bytes, BTSAP 5.1 */
	ptr += 2;

	msg->params = talloc_zero_size(NULL, sizeof(struct sap_param) * msg->num_params);

	for(i=0; i<msg->num_params; i++){
		ptr = sap_get_param(ptr, &msg->params[i]);
		LOGP(DSAP, LOGL_INFO, "> %s %s\n",
				get_value_string(sap_param_names, msg->params[i].id),
				osmo_hexdump(msg->params[i].value, msg->params[i].len));
	}

	return msg;
}

static void sap_apdu_resp(struct osmocom_ms *ms, uint8_t *data, uint16_t len)
{
	struct msgb *msg;
	uint8_t *apdu;
	msg = msgb_alloc(GSM_SAP_LENGTH, "osmosap");
	if(!msg){
		LOGP(DSAP, LOGL_ERROR, "Failed to allocate memory.\n");
		return;
	}

	apdu = msgb_put(msg, len);
	memcpy(apdu, data, len);

	LOGP(DSAP, LOGL_DEBUG, "Forwarding APDU to SIM handler.\n");
	sim_apdu_resp(ms, msg);
}

static int sap_adapt_msg_size(struct osmocom_ms *ms, struct sap_param *param)
{
	uint16_t size;
	size = (param->value[0] << 8) | param->value[1];
	if(size != ms->sap_entity.max_msg_size && size > 0){
		LOGP(DSAP, LOGL_NOTICE, "Server can not handle max_msg_size, adapting.\n");
		ms->sap_entity.max_msg_size = size;
		return -1;
	}
	return 0;
}

static void sap_atr(struct osmocom_ms *ms)
{
	struct msgb *msg;
	if(ms->sap_entity.sap_state != SAP_IDLE){
		LOGP(DSAP, LOGL_ERROR, "Attempting to send ATR request while not being idle.\n");
		return;
	}

	msg = sap_create_msg(SAP_TRANSFER_ATR_REQ, 0, NULL);
	if(!msg)
		return;

	osmosap_send(ms, msg);
	ms->sap_entity.sap_state = SAP_PROCESSING_ATR_REQUEST;
}

static void sap_parse_resp(struct osmocom_ms *ms, uint8_t *data, uint16_t len)
{
	struct sap_msg *msg = NULL;
	if(len > ms->sap_entity.max_msg_size){
		LOGP(DSAP, LOGL_ERROR, "Read more data than allowed by max_msg_size, ignoring.\n");
		return;
	}

	msg = sap_parse_msg(data);
	if(!msg){
		sap_msg_free(msg);
		return;
	}

	switch(msg->id){
	case SAP_CONNECT_RESP:
		LOGP(DSAP, LOGL_INFO, "Status: %s\n", get_value_string(sap_status_names, msg->params[0].value[0]));
		if(msg->params[0].value[0] == 0){
			ms->sap_entity.sap_state = SAP_IDLE;
		}
		if(msg->num_params == 2 && msg->params[1].len == 2){
			if(sap_adapt_msg_size(ms, &msg->params[1]) < 0) {
				ms->sap_entity.sap_state = SAP_NOT_CONNECTED;
			} else {
				sap_atr(ms);
			}
		}
		break;
	case SAP_DISCONNECT_RESP:
		ms->sap_entity.sap_state = SAP_NOT_CONNECTED;
		break;
	case SAP_STATUS_IND:
		LOGP(DSAP, LOGL_INFO, "New card state: %s\n", get_value_string(sap_status_change_names,
					msg->params[0].value[0]));
		if(msg->params[0].value[0] != 1){
			/* TODO: handle case in which the card is not ready yet */
		}
		break;
	case SAP_TRANSFER_ATR_RESP:
		if(ms->sap_entity.sap_state != SAP_PROCESSING_ATR_REQUEST){
			LOGP(DSAP, LOGL_ERROR, "got ATR resp in state: %u\n", ms->sap_entity.sap_state);
			return;
		}
		if(msg->num_params >= 2){
			LOGP(DSAP, LOGL_INFO, "ATR: %s\n", osmo_hexdump(msg->params[1].value, msg->params[1].len));
		}
		ms->sap_entity.sap_state = SAP_IDLE;
		break;
	case SAP_TRANSFER_APDU_RESP:
		if(ms->sap_entity.sap_state != SAP_PROCESSING_APDU_REQUEST){
			LOGP(DSAP, LOGL_ERROR, "got APDU resp in state: %u\n", ms->sap_entity.sap_state);
			return;
		}
		if(msg->num_params != 2){
			LOGP(DSAP, LOGL_ERROR, "wrong number of parameters %u in APDU response\n", msg->num_params);
			return;
		}
		ms->sap_entity.sap_state = SAP_IDLE;
		if(sap_parse_result(&msg->params[0]) == 0){
			/* back apdu resp to layer23 */
			sap_apdu_resp(ms, msg->params[1].value, msg->params[1].len);
			LOGP(DSAP, LOGL_INFO, "sap_apdu_resp called, sending data back to layer23\n");
		}
		break;
	case SAP_ERROR_RESP:
		if(ms->sap_entity.sap_state == SAP_CONNECTION_UNDER_NEGOTIATION){
			ms->sap_entity.sap_state = SAP_NOT_CONNECTED;
		} else {
			ms->sap_entity.sap_state = SAP_IDLE;
		}
		break;
	default:
		LOGP(DSAP, LOGL_ERROR, "got unknown or not implemented SAP msgid: %u\n", msg->id);
		break;
	}
}

static int sap_read(struct osmo_fd *fd)
{
	struct msgb *msg = NULL;
	struct osmocom_ms *ms = (struct osmocom_ms *) fd->data;
	uint8_t *sap_buffer;
	ssize_t rc;

	sap_buffer = talloc_zero_size(NULL, ms->sap_entity.max_msg_size);
	if(!sap_buffer){
		fprintf(stderr, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	rc = read(fd->fd, sap_buffer, ms->sap_entity.max_msg_size - 1);
	if (rc < 0) {
		fprintf(stderr, "SAP socket failed\n");
		msgb_free(msg);
		sap_close(ms);
		return rc;
	}
	if(rc == 0) {
		fprintf(stderr, "SAP socket closed by server\n");
		msgb_free(msg);
		sap_close(ms);
		return -ECONNREFUSED;
	}

	sap_buffer[rc] = 0;
	LOGP(DSAP, LOGL_INFO, "Received %zd bytes: %s\n", rc, osmo_hexdump(sap_buffer, rc));

	sap_parse_resp(ms, sap_buffer, rc);

	talloc_free(sap_buffer);

	if (ms->sap_entity.msg_handler){
		ms->sap_entity.msg_handler(msg, ms);
	}
	return 0;
}

static int sap_write(struct osmo_fd *fd, struct msgb *msg)
{
	ssize_t rc;

	if (fd->fd <= 0)
		return -EINVAL;

	LOGP(DSAP, LOGL_INFO, "< %s\n", osmo_hexdump(msg->data, msg->len));
	rc = write(fd->fd, msg->data, msg->len);
	if (rc != msg->len) {
		LOGP(DSAP, LOGL_ERROR, "Failed to write data: rc: %zd\n", rc);
		return rc;
	}

	return 0;
}

static void sap_connect(struct osmocom_ms *ms)
{
	uint8_t buffer[3];
	struct msgb *msg;
	uint16_t size = ms->sap_entity.max_msg_size;
	struct sap_param params[1];

	params[0].id = SAP_MAX_MSG_SIZE;
	params[0].len = 2;

	if(ms->sap_entity.sap_state != SAP_NOT_CONNECTED) {
		LOGP(DSAP, LOGL_ERROR, "Attempting to connect while there is an active connection.\n");
		return;
	}

	buffer[0] = (size >> 8) & 0xFF;
	buffer[1] = size & 0xFF;
	buffer[2] = 0;
	params[0].value = buffer;

	msg = sap_create_msg(SAP_CONNECT_REQ, 1, params);
	if(!msg)
		return;

	osmosap_send(ms, msg);

	ms->sap_entity.sap_state = SAP_CONNECTION_UNDER_NEGOTIATION;
}

static void sap_disconnect(struct osmocom_ms *ms)
{
	struct msgb *msg;
	if(ms->sap_entity.sap_state != SAP_NOT_CONNECTED && ms->sap_entity.sap_state != SAP_CONNECTION_UNDER_NEGOTIATION){
		LOGP(DSAP, LOGL_ERROR, "Attempting to disconnect while no active connection.\n");
		return;
	}

	msg = sap_create_msg(SAP_DISCONNECT_REQ, 0, NULL);
	if(!msg)
		return;

	osmosap_send(ms, msg);

	ms->sap_entity.sap_state = SAP_NOT_CONNECTED;
}

static void sap_apdu(struct osmocom_ms *ms, uint8_t *data, uint16_t len)
{
	struct msgb *msg;
	struct sap_param params[1];

	params[0].id = SAP_COMMAND_APDU;
	params[0].len = len;
	params[0].value = data;

	if(ms->sap_entity.sap_state != SAP_IDLE){
		LOGP(DSAP, LOGL_ERROR, "Attempting to send APDU request while not being idle.\n");
		return;
	}

	msg = sap_create_msg(SAP_TRANSFER_APDU_REQ, 1, params);
	if(!msg)
		return;

	osmosap_send(ms, msg);

	ms->sap_entity.sap_state = SAP_PROCESSING_APDU_REQUEST;
}

int sap_open(struct osmocom_ms *ms, const char *socket_path)
{
	ssize_t rc;
	struct sockaddr_un local;

	ms->sap_wq.bfd.fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ms->sap_wq.bfd.fd < 0) {
		fprintf(stderr, "Failed to create unix domain socket.\n");
		return ms->sap_wq.bfd.fd;
	}

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, socket_path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';

	rc = connect(ms->sap_wq.bfd.fd, (struct sockaddr *) &local, sizeof(local));
	if (rc < 0) {
		fprintf(stderr, "Failed to connect to '%s'\n", local.sun_path);
		ms->sap_entity.sap_state = SAP_SOCKET_ERROR;
		close(ms->sap_wq.bfd.fd);
		return rc;
	}

	osmo_wqueue_init(&ms->sap_wq, 100);
	ms->sap_wq.bfd.data = ms;
	ms->sap_wq.bfd.when = BSC_FD_READ;
	ms->sap_wq.read_cb = sap_read;
	ms->sap_wq.write_cb = sap_write;

	rc = osmo_fd_register(&ms->sap_wq.bfd);
	if (rc != 0) {
		fprintf(stderr, "Failed to register fd.\n");
		return rc;
	}

	sap_connect(ms);

	return 0;
}

int sap_close(struct osmocom_ms *ms)
{
	if (ms->sap_wq.bfd.fd <= 0)
		return -EINVAL;

	sap_disconnect(ms);
	close(ms->sap_wq.bfd.fd);
	ms->sap_wq.bfd.fd = -1;
	osmo_fd_unregister(&ms->sap_wq.bfd);
	osmo_wqueue_clear(&ms->sap_wq);

	return 0;
}

/* same signature as in L1CTL, so it can be called from sim.c */
int osmosap_send_apdu(struct osmocom_ms *ms, uint8_t *data, uint16_t length)
{
	//LOGP(DSAP, LOGL_ERROR, "Received the following APDU from sim.c: %s\n" ,
	//     osmo_hexdump(data, length));
	sap_apdu(ms, data, length);

	return 0;
}

/* register message handler for messages that are sent from L2->L3 */
int osmosap_register_handler(struct osmocom_ms *ms, osmosap_cb_t cb)
{
	ms->sap_entity.msg_handler = cb;

	return 0;
}

int osmosap_sapsocket(struct osmocom_ms *ms, const char *path)
{
	struct gsm_settings *set = &ms->settings;
	memset(set->sap_socket_path, 0, sizeof(set->sap_socket_path));
	strncpy(set->sap_socket_path, path, sizeof(set->sap_socket_path) - 1);

	return 0;
}

/* init */
int osmosap_init(struct osmocom_ms *ms)
{
	struct osmosap_entity *sap = &ms->sap_entity;

	LOGP(DSAP, LOGL_INFO, "init SAP client\n");
	sap->sap_state = SAP_NOT_CONNECTED;
	sap->max_msg_size = GSM_SAP_LENGTH;

	return 0;
}

