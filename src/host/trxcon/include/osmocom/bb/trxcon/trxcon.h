#pragma once

struct l1sched_state;
struct trx_instance;
struct l1ctl_client;

enum trxcon_fsm_states {
	TRXCON_STATE_IDLE = 0,
	TRXCON_STATE_MANAGED,
};

enum trxcon_fsm_events {
	/* L1CTL specific events */
	L1CTL_EVENT_CONNECT,
	L1CTL_EVENT_DISCONNECT,

	/* TRX specific events */
	TRX_EVENT_RSP_ERROR,
	TRX_EVENT_OFFLINE,
};

struct trxcon_inst {
	struct osmo_fsm_inst *fi;

	/* The L1 scheduler */
	struct l1sched_state *sched;
	/* L1/L2 interfaces */
	struct trx_instance *trx;
	struct l1ctl_client *l1c;

	/* TODO: implement this as an FSM state with timeout */
	struct osmo_timer_list fbsb_timer;
	bool fbsb_conf_sent;
};

struct trxcon_inst *trxcon_inst_alloc(void *ctx);
void trxcon_inst_free(struct trxcon_inst *trxcon);
