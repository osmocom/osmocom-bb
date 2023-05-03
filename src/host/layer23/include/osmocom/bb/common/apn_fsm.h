#pragma once

#include <osmocom/core/fsm.h>

struct osmobb_apn;

enum apn_fsm_states {
	APN_ST_DISABLED,
	APN_ST_INACTIVE,
	APN_ST_ACTIVATING,
	APN_ST_ACTIVE,
};

enum apn_fsm_events {
	APN_EV_GPRS_ALLOWED,	/* data: bool *allowed */
	APN_EV_GMM_ATTACHED,
	APN_EV_GMM_DETACHED,
	APN_EV_RX_SM_ACT_PDP_CTX_REJ, /* data: enum gsm48_gsm_cause *cause */
	APN_EV_RX_SM_ACT_PDP_CTX_ACC,
	APN_EV_RX_SM_DEACT_PDP_CTX_ACC,
};

struct apn_fsm_ctx {
	struct osmo_fsm_inst *fi;
	struct osmobb_apn *apn;
};

int apn_fsm_ctx_init(struct apn_fsm_ctx *ctx, struct osmobb_apn *apn);
void apn_fsm_ctx_release(struct apn_fsm_ctx *ctx);
