#pragma once

#include <osmocom/bb/common/osmocom_data.h>

/* How long should we wait for connection establishment */
#define SAP_FSM_CONN_EST_TIMEOUT 5
#define SAP_FSM_CONN_EST_T 0

/* How long should we wait for connection release */
#define SAP_FSM_CONN_REL_TIMEOUT 3
#define SAP_FSM_CONN_REL_T 1

/* How long should we wait for request to complete */
#define SAP_FSM_PROC_REQ_TIMEOUT 5
#define SAP_FSM_PROC_REQ_T 2

#define SAP_STATE_IS_ACTIVE(state) \
	(state >= SAP_STATE_WAIT_FOR_CARD)

enum sap_fsm_state {
	SAP_STATE_NOT_CONNECTED = 0,
	SAP_STATE_CONNECTING,
	SAP_STATE_DISCONNECTING, /* Auxiliary state (not from specs) */
	SAP_STATE_WAIT_FOR_CARD, /* Auxiliary state (not from specs) */
	SAP_STATE_IDLE,
	SAP_STATE_PROC_ATR_REQ,
	SAP_STATE_PROC_APDU_REQ,
	SAP_STATE_PROC_RESET_REQ,
	SAP_STATE_PROC_STATUS_REQ,
	SAP_STATE_PROC_SET_TP_REQ,
	SAP_STATE_PROC_POWERON_REQ,
	SAP_STATE_PROC_POWEROFF_REQ,
};

int sap_fsm_alloc(struct osmocom_ms *ms);
