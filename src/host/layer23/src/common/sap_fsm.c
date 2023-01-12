/*
 * SAP (SIM Access Profile) FSM definition
 * based on Bluetooth SAP specification
 *
 * (C) 2018-2019 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>

#include <osmocom/core/socket.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/fsm.h>
#include <osmocom/core/write_queue.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/logging.h>

#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/common/sap_proto.h>
#include <osmocom/bb/common/sap_fsm.h>

/*! Send encoded SAP message to the Server.
 * \param[in] ms MS instance with active SAP connection
 * \param[in] msg encoded SAP message buffer
 * \returns 0 in case of success, negative in case of error
 */
static int sap_send_msgb(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc;

	rc = osmo_wqueue_enqueue(&ms->sap_wq, msg);
	if (rc) {
		LOGP(DSAP, LOGL_ERROR, "Failed to enqueue SAP message\n");
		msgb_free(msg);
		return rc;
	}

	return 0;
}

static void sap_fsm_disconnect(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	osmo_fsm_inst_term(fi, OSMO_FSM_TERM_REGULAR, NULL);
}

static void sap_fsm_connect(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) fi->priv;
	struct msgb *msg;
	uint16_t size;

	/* Section 5.1.1, CONNECT_REQ */
	msg = sap_msgb_alloc(SAP_CONNECT_REQ);
	if (!msg)
		return;

	/* Section 4.1.1, start MaxMsgSize negotiation */
	size = htons(ms->sap_entity.max_msg_size);
	sap_msgb_add_param(msg, SAP_MAX_MSG_SIZE,
		sizeof(size), (uint8_t *) &size);

	sap_send_msgb(ms, msg);
}

static void sap_negotiate_msg_size(struct osmosap_entity *sap,
	const struct sap_message *sap_msg)
{
	uint16_t size, param_len;
	const char *cause = NULL;
	struct sap_param *param;

	param = sap_get_param(sap_msg, SAP_MAX_MSG_SIZE, &param_len);
	if (!param) {
		cause = "missing expected MaxMsgSize parameter";
		goto error;
	}
	if (param_len != sizeof(size)) {
		cause = "MaxMsgSize parameter has wrong length";
		goto error;
	}

	/* Parse MaxMsgSize suggested by server */
	size = osmo_load16be(param->value);
	if (size > SAP_MAX_MSG_SIZE) {
		cause = "suggested MaxMsgSize is too big for us";
		goto error;
	}

	/* Attempt to initiate connection again */
	sap->max_msg_size = size;
	sap_fsm_connect(sap->fi, sap->fi->state);
	return;

error:
	LOGP(DSAP, LOGL_ERROR, "MaxMsgSize negotiation failed: %s\n", cause);
	osmo_fsm_inst_state_chg(sap->fi, SAP_STATE_NOT_CONNECTED, 0, 0);
}

static void sap_fsm_conn_handler(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	struct sap_message *sap_msg = (struct sap_message *) data;
	struct osmocom_ms *ms = (struct osmocom_ms *) fi->priv;
	struct sap_param *param;
	uint16_t param_len;
	uint8_t status;

	/* Section 5.1.2, CONNECT_RESP */
	param = sap_get_param(sap_msg, SAP_CONNECTION_STATUS, &param_len);
	if (!param || param_len != sizeof(status)) {
		LOGP(DSAP, LOGL_ERROR, "Missing mandatory connection status\n");
		osmo_fsm_inst_state_chg(fi, SAP_STATE_NOT_CONNECTED, 0, 0);
		return;
	}

	/* Parse connection status */
	status = param->value[0];

	LOGP(DSAP, LOGL_INFO, "SAP connection status (0x%02x): %s\n",
		status, get_value_string(sap_conn_status_names, status));

	switch ((enum sap_conn_status_type) status) {
	case SAP_CONN_STATUS_OK_CALL:
		ms->sap_entity.card_status = SAP_CARD_STATUS_NOT_ACC;
		/* fall-through */
	case SAP_CONN_STATUS_OK_READY:
		osmo_fsm_inst_state_chg(fi, SAP_STATE_WAIT_FOR_CARD, 0, 0);
		break;

	case SAP_CONN_STATUS_ERROR_SMALL_MSG_SIZE:
	case SAP_CONN_STATUS_ERROR_CONN:
		osmo_fsm_inst_state_chg(fi, SAP_STATE_NOT_CONNECTED, 0, 0);
		break;

	/* Section 4.1.1, MaxMsgSize negotiation */
	case SAP_CONN_STATUS_ERROR_MAX_MSG_SIZE:
		sap_negotiate_msg_size(&ms->sap_entity, sap_msg);
		break;
	}
}

static void sap_fsm_conn_release(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct msgb *msg;

	LOGP(DSAP, LOGL_DEBUG, "Initiating connection release\n");

	/* We don't care about possible allocating / sending errors */
	msg = sap_msgb_alloc(SAP_DISCONNECT_REQ);
	if (msg != NULL)
		sap_send_msgb((struct osmocom_ms *) fi->priv, msg);
}

static void sap_fsm_release_handler(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	LOGP(DSAP, LOGL_DEBUG, "Connection release complete\n");
	osmo_fsm_inst_state_chg(fi, SAP_STATE_NOT_CONNECTED, 0, 0);
}

static void sap_fsm_idle_enter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) fi->priv;

	switch ((enum sap_fsm_state) prev_state) {
	case SAP_STATE_CONNECTING:
	case SAP_STATE_WAIT_FOR_CARD:
		/* According to 4.1, if a subscription module is inserted
		 * in the Server and powered on (i.e. STATUS_IND message
		 * indicates "Card reset" state), the Client shall request
		 * the ATR of the subscription module. */
		if (ms->sap_entity.card_status == SAP_CARD_STATUS_RESET)
			sap_send_atr_req(ms);
		break;
	default:
		/* Do nothing, suppress compiler warning */
		break;
	}
}

static void sap_fsm_idle_handler(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) fi->priv;
	struct msgb *msg = (struct msgb *) data;
	enum sap_fsm_state state;
	int rc;

	/* Map event to the corresponding state */
	switch ((enum sap_msg_type) event) {
	case SAP_TRANSFER_ATR_REQ:
		state = SAP_STATE_PROC_ATR_REQ;
		break;
	case SAP_TRANSFER_APDU_REQ:
		state = SAP_STATE_PROC_APDU_REQ;
		break;
	case SAP_RESET_SIM_REQ:
		state = SAP_STATE_PROC_RESET_REQ;
		break;
	case SAP_TRANSFER_CARD_READER_STATUS_REQ:
		state = SAP_STATE_PROC_STATUS_REQ;
		break;
	case SAP_SET_TRANSPORT_PROTOCOL_REQ:
		state = SAP_STATE_PROC_SET_TP_REQ;
		break;
	case SAP_POWER_SIM_ON_REQ:
		state = SAP_STATE_PROC_POWERON_REQ;
		break;
	case SAP_POWER_SIM_OFF_REQ:
		state = SAP_STATE_PROC_POWEROFF_REQ;
		break;
	default:
		/* Shall not happen */
		OSMO_ASSERT(0);
	}

	rc = sap_send_msgb(ms, msg);
	if (rc)
		return;

	osmo_fsm_inst_state_chg(fi, state,
		SAP_FSM_PROC_REQ_TIMEOUT, SAP_FSM_PROC_REQ_T);
}

static void sap_fsm_response_handler(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	struct sap_message *sap_msg = (struct sap_message *) data;
	struct osmocom_ms *ms = (struct osmocom_ms *) fi->priv;
	struct sap_param *param = NULL;
	uint16_t param_len = 0;
	int param_id, rc;

	switch ((enum sap_msg_type) event) {
	/* Both POWER_SIM_OFF_REQ and RESET_SIM_REQ can be sent in nearly
	 * any state, in order to allow the Client to reactivate
	 * a not accessible subscription module card. */
	case SAP_POWER_SIM_OFF_REQ:
	case SAP_RESET_SIM_REQ:
		OSMO_ASSERT(data != NULL);
		goto request;

	/* Messages without parameters */
	case SAP_SET_TRANSPORT_PROTOCOL_RESP:
	case SAP_POWER_SIM_OFF_RESP:
	case SAP_POWER_SIM_ON_RESP:
	case SAP_RESET_SIM_RESP:
		param_id = -1;
		break;

	case SAP_TRANSFER_CARD_READER_STATUS_RESP:
		param_id = SAP_CARD_READER_STATUS;
		break;
	case SAP_TRANSFER_APDU_RESP:
		param_id = SAP_RESPONSE_APDU;
		break;
	case SAP_TRANSFER_ATR_RESP:
		param_id = SAP_ATR;
		break;

	default:
		/* Shall not happen */
		OSMO_ASSERT(0);
	}

	/* We're done with request now */
	osmo_fsm_inst_state_chg(fi, SAP_STATE_IDLE, 0, 0);

	/* Check the ResultCode */
	rc = sap_check_result_code(sap_msg);
	if (rc != SAP_RESULT_OK_REQ_PROC_CORR) {
		LOGP(DSAP, LOGL_NOTICE, "Bad ResultCode: '%s'\n",
			get_value_string(sap_result_names, rc));
		goto response;
	}

	if (param_id < 0)
		goto response;

	param = sap_get_param(sap_msg, param_id, &param_len);
	if (!param) {
		LOGP(DSAP, LOGL_ERROR, "Message '%s' missing "
			"mandatory parameter '%s'\n",
			get_value_string(sap_msg_names, sap_msg->msg_id),
			get_value_string(sap_param_names, param_id));
		rc = -EINVAL;
		goto response;
	}

response:
	/* Poke optional response handler */
	if (ms->sap_entity.sap_rsp_cb != NULL) {
		if (param != NULL) {
			ms->sap_entity.sap_rsp_cb(ms, rc,
				sap_msg->msg_id, param_len, param->value);
		} else {
			ms->sap_entity.sap_rsp_cb(ms, rc,
				sap_msg->msg_id, 0, NULL);
		}
	}

	return;

request:
	rc = sap_send_msgb(ms, (struct msgb *) data);
	if (rc)
		return;

	osmo_fsm_inst_state_chg(fi, event == SAP_RESET_SIM_REQ ?
		SAP_STATE_PROC_RESET_REQ : SAP_STATE_PROC_POWEROFF_REQ,
		SAP_FSM_PROC_REQ_TIMEOUT, SAP_FSM_PROC_REQ_T);
}

/* Generates mask for a single state or event */
#define S(x) (1 << x)

/* Figure 4.13: Simplified State Machine */
static const struct osmo_fsm_state sap_fsm_states[] = {
	[SAP_STATE_NOT_CONNECTED] = {
		.name = "NOT_CONNECTED",
		.out_state_mask = S(SAP_STATE_CONNECTING),
		.onenter = &sap_fsm_disconnect,
	},
	[SAP_STATE_CONNECTING] = {
		.name = "CONNECTING",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_WAIT_FOR_CARD),
		.in_event_mask  = S(SAP_CONNECT_RESP),
		.onenter = &sap_fsm_connect,
		.action = &sap_fsm_conn_handler,
	},
	/* NOTE: this is a custom state (i.e. not defined by the specs).
	 * We need it in order to do release procedure correctly. */
	[SAP_STATE_DISCONNECTING] = {
		.name = "DISCONNECTING",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED),
		.in_event_mask  = S(SAP_DISCONNECT_RESP),
		.onenter = &sap_fsm_conn_release,
		.action = &sap_fsm_release_handler,
	},
	/* NOTE: this is a custom state (i.e. not defined by the specs).
	 * We need it in order to wait until SIM card becomes available.
	 * SAP_STATUS_IND event is handled by sap_fsm_allstate_action(). */
	[SAP_STATE_WAIT_FOR_CARD] = {
		.name = "WAIT_FOR_CARD",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_IDLE),
	},
	[SAP_STATE_IDLE] = {
		.name = "IDLE",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_PROC_APDU_REQ)
				| S(SAP_STATE_PROC_ATR_REQ)
				| S(SAP_STATE_PROC_RESET_REQ)
				| S(SAP_STATE_PROC_STATUS_REQ)
				| S(SAP_STATE_PROC_SET_TP_REQ)
				| S(SAP_STATE_PROC_POWERON_REQ)
				| S(SAP_STATE_PROC_POWEROFF_REQ),
		.in_event_mask  = S(SAP_TRANSFER_ATR_REQ)
				| S(SAP_TRANSFER_APDU_REQ)
				| S(SAP_RESET_SIM_REQ)
				| S(SAP_TRANSFER_CARD_READER_STATUS_REQ)
				| S(SAP_SET_TRANSPORT_PROTOCOL_REQ)
				| S(SAP_POWER_SIM_ON_REQ)
				| S(SAP_POWER_SIM_OFF_REQ),
		.onenter = &sap_fsm_idle_enter,
		.action = &sap_fsm_idle_handler,
	},
	[SAP_STATE_PROC_ATR_REQ] = {
		.name = "PROC_ATR_REQ",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_IDLE)
				| S(SAP_STATE_PROC_RESET_REQ)
				| S(SAP_STATE_PROC_POWEROFF_REQ),
		.in_event_mask  = S(SAP_TRANSFER_ATR_RESP)
				| S(SAP_RESET_SIM_REQ)
				| S(SAP_POWER_SIM_OFF_REQ),
		.action = &sap_fsm_response_handler,
	},
	[SAP_STATE_PROC_APDU_REQ] = {
		.name = "PROC_APDU_REQ",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_IDLE)
				| S(SAP_STATE_PROC_RESET_REQ)
				| S(SAP_STATE_PROC_POWEROFF_REQ),
		.in_event_mask  = S(SAP_TRANSFER_APDU_RESP)
				| S(SAP_RESET_SIM_REQ)
				| S(SAP_POWER_SIM_OFF_REQ),
		.action = &sap_fsm_response_handler,
	},
	[SAP_STATE_PROC_RESET_REQ] = {
		.name = "PROC_RESET_REQ",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_IDLE)
				| S(SAP_STATE_PROC_POWEROFF_REQ),
		.in_event_mask  = S(SAP_RESET_SIM_RESP)
				| S(SAP_POWER_SIM_OFF_REQ),
		.action = &sap_fsm_response_handler,
	},
	[SAP_STATE_PROC_STATUS_REQ] = {
		.name = "PROC_STATUS_REQ",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_IDLE)
				| S(SAP_STATE_PROC_RESET_REQ)
				| S(SAP_STATE_PROC_POWEROFF_REQ),
		.in_event_mask  = S(SAP_TRANSFER_CARD_READER_STATUS_RESP)
				| S(SAP_RESET_SIM_REQ)
				| S(SAP_POWER_SIM_OFF_REQ),
		.action = &sap_fsm_response_handler,
	},
	[SAP_STATE_PROC_SET_TP_REQ] = {
		.name = "PROC_SET_TP_REQ",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_IDLE),
		.in_event_mask  = S(SAP_SET_TRANSPORT_PROTOCOL_RESP),
		.action = &sap_fsm_response_handler,
	},
	[SAP_STATE_PROC_POWERON_REQ] = {
		.name = "PROC_POWERON_REQ",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_IDLE)
				| S(SAP_STATE_PROC_POWEROFF_REQ),
		.in_event_mask  = S(SAP_POWER_SIM_ON_RESP)
				| S(SAP_POWER_SIM_OFF_REQ),
		.action = &sap_fsm_response_handler,
	},
	[SAP_STATE_PROC_POWEROFF_REQ] = {
		.name = "PROC_POWEROFF_REQ",
		.out_state_mask = S(SAP_STATE_NOT_CONNECTED)
				| S(SAP_STATE_DISCONNECTING)
				| S(SAP_STATE_WAIT_FOR_CARD)
				| S(SAP_STATE_IDLE),
		.in_event_mask  = S(SAP_POWER_SIM_OFF_RESP),
		.action = &sap_fsm_response_handler,
	},
};

static void sap_fsm_tear_down(struct osmo_fsm_inst *fi,
	enum osmo_fsm_term_cause cause)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) fi->priv;

	/* Flush buffers, close socket */
	_sap_close_sock(ms);

	/* Reset SAP state */
	ms->sap_entity.card_status = SAP_CARD_STATUS_NOT_ACC;
	ms->sap_entity.max_msg_size = GSM_SAP_LENGTH;
	ms->sap_entity.fi = NULL;
}

static void sap_fsm_handle_card_status_ind(struct osmo_fsm_inst *fi,
	const struct sap_message *sap_msg)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) fi->priv;
	struct sap_param *param;
	uint16_t param_len;
	uint8_t status;

	param = sap_get_param(sap_msg, SAP_STATUS_CHANGE, &param_len);
	if (!param || param_len != sizeof(status)) {
		LOGP(DSAP, LOGL_ERROR, "Missing mandatory '%s' parameter\n",
			get_value_string(sap_param_names, SAP_STATUS_CHANGE));
		return;
	}

	status = param->value[0];

	if (ms->sap_entity.card_status != status) {
		LOGP(DSAP, LOGL_NOTICE, "(SIM) card status change '%s' -> '%s'\n",
			get_value_string(sap_card_status_names, ms->sap_entity.card_status),
			get_value_string(sap_card_status_names, status));
		ms->sap_entity.card_status = status;
	}

	switch ((enum sap_card_status_type) status) {
	/* SIM card is ready */
	case SAP_CARD_STATUS_RESET:
		if (fi->state != SAP_STATE_IDLE)
			osmo_fsm_inst_state_chg(fi, SAP_STATE_IDLE, 0, 0);
		break;

	/* SIM card has recovered after unaccessful state */
	case SAP_CARD_STATUS_RECOVERED:
		if (fi->state != SAP_STATE_IDLE)
			osmo_fsm_inst_state_chg(fi, SAP_STATE_IDLE, 0, 0);
		break;

	/* SIM card inserted, we need to power it on */
	case SAP_CARD_STATUS_INSERTED:
		if (fi->state != SAP_STATE_IDLE)
			osmo_fsm_inst_state_chg(fi, SAP_STATE_IDLE, 0, 0);
		sap_send_poweron_req(ms);
		break;

	case SAP_CARD_STATUS_UNKNOWN_ERROR:
	case SAP_CARD_STATUS_NOT_ACC:
	case SAP_CARD_STATUS_REMOVED:
	default: /* Unknown card status */
		if (fi->state != SAP_STATE_WAIT_FOR_CARD)
			osmo_fsm_inst_state_chg(fi, SAP_STATE_WAIT_FOR_CARD, 0, 0);
		break;
	}
}

static void sap_fsm_allstate_action(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	struct sap_message *sap_msg = (struct sap_message *) data;

	switch ((enum sap_msg_type) event) {
	/* Disconnect indication initiated by the Server.
	 * FIXME: at the moment, immediate release is always assumed,
	 * but ideally we should check type of release (using *data) */
	case SAP_DISCONNECT_IND:
		/* This message may arrive in any of the sub-states of
		 * the "Connected" state (i.e. connection shall exist) */
		if (!SAP_STATE_IS_ACTIVE(fi->state))
			goto not_peritted;

		sap_msg = NULL;
		/* fall-through */

	/* Disconnect initiated by the Client */
	case SAP_DISCONNECT_REQ:
		/* DISCONNECT_REQ has no parameters, so the caller
		 * shall not allocate the message manually. */
		OSMO_ASSERT(sap_msg == NULL);

		/* If we have no active connection, tear-down immediately */
		if (!SAP_STATE_IS_ACTIVE(fi->state)) {
			osmo_fsm_inst_state_chg(fi,
				SAP_STATE_NOT_CONNECTED, 0, 0);
			break;
		}

		/* Trigger Client-initiated connection release */
		osmo_fsm_inst_state_chg(fi, SAP_STATE_DISCONNECTING,
			SAP_FSM_CONN_REL_TIMEOUT, SAP_FSM_CONN_REL_T);
		break;

	/* SIM status indication (inserted or ejected) */
	case SAP_STATUS_IND:
		/* This message may arrive in any of the sub-states of
		 * the "Connected" state (i.e. connection shall exist) */
		if (!SAP_STATE_IS_ACTIVE(fi->state))
			goto not_peritted;

		sap_fsm_handle_card_status_ind(fi, sap_msg);
		break;

	case SAP_ERROR_RESP:
		LOGP(DSAP, LOGL_NOTICE, "RX Error Response from Server\n");

		if (fi->state == SAP_STATE_CONNECTING) {
			/* Connection establishment error */
			osmo_fsm_inst_state_chg(fi,
				SAP_STATE_NOT_CONNECTED, 0, 0);
		} else if (fi->state > SAP_STATE_IDLE) {
			/* Error replaces any Request message */
			osmo_fsm_inst_state_chg(fi,
				SAP_STATE_IDLE, 0, 0);
		} else {
			/* Should not happen in general */
			goto not_peritted;
		}
		break;

	default:
		/* Shall not happen */
		OSMO_ASSERT(0);
	}

	return;

not_peritted:
	LOGPFSML(fi, LOGL_NOTICE, "Event '%s' is not "
		"permitted in state '%s', please fix!\n",
		osmo_fsm_event_name(fi->fsm, event),
		osmo_fsm_state_name(fi->fsm, fi->state));
}

static int sap_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	switch ((enum sap_fsm_state) fi->state) {
	/* Connection establishment / release timeout */
	case SAP_STATE_DISCONNECTING:
	case SAP_STATE_CONNECTING:
		LOGP(DSAP, LOGL_NOTICE, "Connection timeout\n");
		osmo_fsm_inst_state_chg(fi, SAP_STATE_NOT_CONNECTED, 0, 0);
		break;

	/* Request processing timeout */
	case SAP_STATE_PROC_ATR_REQ:
	case SAP_STATE_PROC_APDU_REQ:
	case SAP_STATE_PROC_RESET_REQ:
	case SAP_STATE_PROC_STATUS_REQ:
	case SAP_STATE_PROC_SET_TP_REQ:
	case SAP_STATE_PROC_POWERON_REQ:
	case SAP_STATE_PROC_POWEROFF_REQ:
		LOGP(DSAP, LOGL_NOTICE, "Timeout waiting for '%s' to complete, "
			"going back to IDLE\n", osmo_fsm_inst_state_name(fi));
		osmo_fsm_inst_state_chg(fi, SAP_STATE_IDLE, 0, 0);
		break;

	default:
		LOGP(DSAP, LOGL_ERROR, "Timeout for unhandled state '%s'\n",
			osmo_fsm_inst_state_name(fi));
	}

	/* Do not tear-down FSM */
	return 0;
}

static struct osmo_fsm sap_fsm_def = {
	.name = "sap_fsm",
	.log_subsys = DSAP,
	.states = sap_fsm_states,
	.num_states = ARRAY_SIZE(sap_fsm_states),
	.event_names = sap_msg_names,
	.cleanup = &sap_fsm_tear_down,
	.timer_cb = &sap_fsm_timer_cb,
	.allstate_action = &sap_fsm_allstate_action,
	.allstate_event_mask = 0
		| S(SAP_DISCONNECT_REQ)
		| S(SAP_DISCONNECT_IND)
		| S(SAP_STATUS_IND)
		| S(SAP_ERROR_RESP),
};

/*! Allocate a new SAP state machine for a given ms.
 * \param[in] ms MS instance associated with SAP FSM
 * \returns 0 in case of success, negative in case of error
 */
int sap_fsm_alloc(struct osmocom_ms *ms)
{
	struct osmosap_entity *sap;

	sap = &ms->sap_entity;
	OSMO_ASSERT(sap->fi == NULL);

	/* Allocate an instance using ms as talloc context */
	sap->fi = osmo_fsm_inst_alloc(&sap_fsm_def, ms,
		ms, LOGL_DEBUG, ms->name);
	if (!sap->fi) {
		LOGP(DSAP, LOGL_ERROR, "Failed to allocate SAP FSM\n");
		return -ENOMEM;
	}

	return 0;
}

static __attribute__((constructor)) void on_dso_load(void)
{
	OSMO_ASSERT(osmo_fsm_register(&sap_fsm_def) == 0);
}
