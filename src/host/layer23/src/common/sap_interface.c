/* BTSAP socket interface of layer2/3 stack */

/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010,2018 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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
 */

#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>

#include <osmocom/core/write_queue.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/fsm.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/common/sap_proto.h>
#include <osmocom/bb/common/sap_fsm.h>

/*! Send ATR request to the Server.
 * \param[in] ms MS instance with active SAP connection
 * \returns 0 in case of success, negative in case of error
 */
int sap_send_atr_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	int rc;

	if (!ms->sap_entity.fi) {
		LOGP(DSAP, LOGL_ERROR, "SAP interface is not connected\n");
		return -EAGAIN;
	}

	msg = sap_msgb_alloc(SAP_TRANSFER_ATR_REQ);
	if (!msg)
		return -ENOMEM;

	rc = osmo_fsm_inst_dispatch(ms->sap_entity.fi,
		SAP_TRANSFER_ATR_REQ, msg);
	if (rc) {
		msgb_free(msg);
		return rc;
	}

	return 0;
}

/*! Send APDU request to the Server.
 * \param[in] ms MS instance with active SAP connection
 * \param[in] apdu APDU to be send
 * \param[in] apdu_len length of APDU
 * \returns 0 in case of success, negative in case of error
 */
int sap_send_apdu(struct osmocom_ms *ms, uint8_t *apdu, uint16_t apdu_len)
{
	struct msgb *msg;
	int rc;

	if (!ms->sap_entity.fi) {
		LOGP(DSAP, LOGL_ERROR, "SAP interface is not connected\n");
		return -EAGAIN;
	}

	msg = sap_msgb_alloc(SAP_TRANSFER_APDU_REQ);
	if (!msg)
		return -ENOMEM;

	sap_msgb_add_param(msg, SAP_COMMAND_APDU, apdu_len, apdu);

	rc = osmo_fsm_inst_dispatch(ms->sap_entity.fi,
		SAP_TRANSFER_APDU_REQ, msg);
	if (rc) {
		msgb_free(msg);
		return rc;
	}

	return 0;
}

/*! Send (SIM) reset request to the Server.
 * \param[in] ms MS instance with active SAP connection
 * \returns 0 in case of success, negative in case of error
 */
int sap_send_reset_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	int rc;

	if (!ms->sap_entity.fi) {
		LOGP(DSAP, LOGL_ERROR, "SAP interface is not connected\n");
		return -EAGAIN;
	}

	msg = sap_msgb_alloc(SAP_RESET_SIM_REQ);
	if (!msg)
		return -ENOMEM;

	rc = osmo_fsm_inst_dispatch(ms->sap_entity.fi,
		SAP_RESET_SIM_REQ, msg);
	if (rc) {
		msgb_free(msg);
		return rc;
	}

	return 0;
}

/*! Send (SIM) power on request to the Server.
 * \param[in] ms MS instance with active SAP connection
 * \returns 0 in case of success, negative in case of error
 */
int sap_send_poweron_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	int rc;

	if (!ms->sap_entity.fi) {
		LOGP(DSAP, LOGL_ERROR, "SAP interface is not connected\n");
		return -EAGAIN;
	}

	msg = sap_msgb_alloc(SAP_POWER_SIM_ON_REQ);
	if (!msg)
		return -ENOMEM;

	rc = osmo_fsm_inst_dispatch(ms->sap_entity.fi,
		SAP_POWER_SIM_ON_REQ, msg);
	if (rc) {
		msgb_free(msg);
		return rc;
	}

	return 0;
}

/*! Send (SIM) power off request to the Server.
 * \param[in] ms MS instance with active SAP connection
 * \returns 0 in case of success, negative in case of error
 */
int sap_send_poweroff_req(struct osmocom_ms *ms)
{
	struct msgb *msg;
	int rc;

	if (!ms->sap_entity.fi) {
		LOGP(DSAP, LOGL_ERROR, "SAP interface is not connected\n");
		return -EAGAIN;
	}

	msg = sap_msgb_alloc(SAP_POWER_SIM_OFF_REQ);
	if (!msg)
		return -ENOMEM;

	rc = osmo_fsm_inst_dispatch(ms->sap_entity.fi,
		SAP_POWER_SIM_OFF_REQ, msg);
	if (rc) {
		msgb_free(msg);
		return rc;
	}

	return 0;
}

static int sap_read_cb(struct osmo_fd *fd)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) fd->data;
	struct osmosap_entity *sap = &ms->sap_entity;
	uint8_t buf[GSM_SAP_LENGTH];
	struct msgb *msg;
	ssize_t rc;

	/* Prevent buffer overflow */
	OSMO_ASSERT(sap->max_msg_size <= GSM_SAP_LENGTH);

	rc = read(fd->fd, buf, sap->max_msg_size);
	if (rc < 0) {
		LOGP(DSAP, LOGL_ERROR, "SAP socket failed\n");
		rc = -EIO;
		goto conn_error;
	}
	if (rc == 0) {
		LOGP(DSAP, LOGL_NOTICE, "SAP socket closed by server\n");
		rc = -ECONNREFUSED;
		goto conn_error;
	}

	LOGP(DSAP, LOGL_DEBUG, "RX SAP message '%s' (len=%zd): %s\n",
		get_value_string(sap_msg_names, buf[0]),
		rc, osmo_hexdump(buf, rc));

	/* Parse received SAP message and allocate a new msgb */
	msg = sap_msg_parse(buf, rc, sap->max_msg_size);
	if (!msg) {
		LOGP(DSAP, LOGL_ERROR, "Failed to parse SAP message\n");
		return -EINVAL;
	}

	/* Pass parsed message to our FSM using message ID as event */
	rc = osmo_fsm_inst_dispatch(sap->fi, msg->data[0], msg->data);
	if (rc) {
		msgb_free(msg);
		return rc;
	}

	/* Pass to (optional) SAP message handler */
	if (sap->sap_msg_cb)
		sap->sap_msg_cb(ms, msg);
	else
		msgb_free(msg);

	return 0;

conn_error:
	/* Immediately tear-down FSM */
	osmo_fsm_inst_state_chg(sap->fi, SAP_STATE_NOT_CONNECTED, 0, 0);
	return rc;
}

static int sap_write_cb(struct osmo_fd *fd, struct msgb *msg)
{
	ssize_t rc;

	if (fd->fd <= 0)
		return -EINVAL;

	rc = write(fd->fd, msg->data, msg->len);
	if (rc != msg->len) {
		LOGP(DSAP, LOGL_ERROR, "Failed to write data\n");
		return rc;
	}

	LOGP(DSAP, LOGL_DEBUG, "TX SAP message '%s' (len=%u): %s\n",
		get_value_string(sap_msg_names, msg->data[0]),
		msg->len, osmo_hexdump(msg->data, msg->len));

	return 0;
}

/*! Establishes SAP connection to the Server,
 *  allocates SAP FSM, and triggers connection procedure.
 * \param[in] ms MS instance with configured SAP socket path
 * \returns 0 in case of success, negative in case of error
 */
int sap_open(struct osmocom_ms *ms)
{
	int rc;

	LOGP(DSAP, LOGL_INFO, "Establishing SAP connection "
		"(using socket '%s')\n", ms->settings.sap_socket_path);

	rc = osmo_sock_unix_init_ofd(&ms->sap_wq.bfd, SOCK_STREAM, 0,
		ms->settings.sap_socket_path, OSMO_SOCK_F_CONNECT);
	if (rc < 0) {
		LOGP(DSAP, LOGL_ERROR, "Failed to create unix domain socket %s: %s\n",
		     ms->settings.sap_socket_path, strerror(-rc));
		return rc;
	}

	osmo_wqueue_init(&ms->sap_wq, 100);
	ms->sap_wq.bfd.data = ms;
	ms->sap_wq.read_cb = &sap_read_cb;
	ms->sap_wq.write_cb = &sap_write_cb;

	/* Allocate a SAP FSM for a given ms */
	rc = sap_fsm_alloc(ms);
	if (rc) {
		_sap_close_sock(ms);
		return rc;
	}

	/* Initiate SAP connection with Server */
	LOGP(DSAP, LOGL_DEBUG, "Connecting to the Server...\n");
	return osmo_fsm_inst_state_chg(ms->sap_entity.fi, SAP_STATE_CONNECTING,
		SAP_FSM_CONN_EST_TIMEOUT, SAP_FSM_CONN_EST_T);
}

/*! Closes SAP connection with the Server.
 * \param[in] ms MS instance with active SAP connection
 * \returns 0 in case of success, negative in case of error
 */
int sap_close(struct osmocom_ms *ms)
{
	if (ms->sap_entity.fi == NULL) {
		LOGP(DSAP, LOGL_NOTICE, "No active SAP connection (no FSM)\n");
		return -EINVAL;
	}

	LOGP(DSAP, LOGL_INFO, "Closing SAP connection\n");
	return osmo_fsm_inst_dispatch(ms->sap_entity.fi,
		SAP_DISCONNECT_REQ, NULL);
}

/*! Low-level function for closing SAP (socket) connection.
 * \param[in] ms MS instance with active SAP connection
 * \returns 0 in case of success, negative in case of error
 */
int _sap_close_sock(struct osmocom_ms *ms)
{
	if (ms->sap_wq.bfd.fd <= 0)
		return -EINVAL;

	osmo_fd_unregister(&ms->sap_wq.bfd);
	close(ms->sap_wq.bfd.fd);
	ms->sap_wq.bfd.fd = -1;
	osmo_wqueue_clear(&ms->sap_wq);

	return 0;
}

/*! Init SAP client state for a given MS. */
void sap_init(struct osmocom_ms *ms)
{
	struct osmosap_entity *sap = &ms->sap_entity;

	LOGP(DSAP, LOGL_INFO, "init SAP client\n");

	/* Default MaxMsgSize (to be negotiated) */
	sap->max_msg_size = GSM_SAP_LENGTH;
	/* SIM card status is not known yet */
	sap->card_status = SAP_CARD_STATUS_NOT_ACC;
}
