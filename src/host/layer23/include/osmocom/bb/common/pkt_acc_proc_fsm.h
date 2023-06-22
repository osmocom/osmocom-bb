/* Packet Access Procedure FSM */
#pragma once

#include <osmocom/core/fsm.h>

struct osmocom_ms;

enum apn_fsm_states {
	PKT_ACC_PROC_ST_IDLE,
	PKT_ACC_PROC_ST_ACTIVE,
};

enum pkt_acc_proc_fsm_events {
	APN_EV_GPRS_ALLOWED,	/* data: bool *allowed */
	APN_EV_GMM_ATTACHED,
	APN_EV_GMM_DETACHED,
	APN_EV_RX_SM_ACT_PDP_CTX_REJ, /* data: enum gsm48_gsm_cause *cause */
	APN_EV_RX_SM_ACT_PDP_CTX_ACC,
	APN_EV_RX_SM_DEACT_PDP_CTX_ACC,
};

struct pkt_acc_proc_fsm_ctx {
	struct osmo_fsm_inst *fi;
	struct osmocom_ms *ms;
	uint8_t chan_req;
};

int pkt_acc_proc_fsm_ctx_init(struct pkt_acc_proc_fsm_ctx *ctx, struct osmocom_ms *ms);
void pkt_acc_proc_fsm_ctx_release(struct pkt_acc_proc_fsm_ctx *ctx);

int pkt_acc_proc_start(struct pkt_acc_proc_fsm_ctx *ctx, uint8_t chan_req);
