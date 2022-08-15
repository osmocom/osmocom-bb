#pragma once

#include <stdint.h>

struct osmo_fsm_inst;
struct l1sched_state;
struct l1gprs_state;
struct msgb;

struct trxcon_inst {
	struct osmo_fsm_inst *fi;
	unsigned int id;

	/* Logging context for sched and l1c */
	const char *log_prefix;

	/* GSMTAP instance (optional) */
	struct gsmtap_inst *gsmtap;

	/* The L1 scheduler */
	struct l1sched_state *sched;
	/* GPRS state (MAC layer) */
	struct l1gprs_state *gprs;

	/* PHY interface (e.g. TRXC/TRXD) */
	void *phyif;
	/* L2 interface (e.g. L1CTL) */
	void *l2if;

	/* State specific data of trxcon_fsm */
	void *fi_data;

	/* L1 parameters */
	struct {
		uint16_t band_arfcn;
		uint8_t tx_power;
		int8_t ta;
	} l1p;

	/* PHY specific quirks */
	struct {
		/* FBSB timeout extension (in TDMA FNs) */
		unsigned int fbsb_extend_fns;
	} phy_quirks;
};

enum trxcon_log_cat {
	TRXCON_LOGC_FSM,	/* trxcon_fsm */
	TRXCON_LOGC_L1C,	/* L1CTL control */
	TRXCON_LOGC_L1D,	/* L1CTL data */
	TRXCON_LOGC_SCHC,	/* l1sched control */
	TRXCON_LOGC_SCHD,	/* l1sched data */
	TRXCON_LOGC_GPRS,	/* l1gprs logging */
};

void trxcon_set_log_cfg(const int *logc, unsigned int logc_num);

struct trxcon_inst *trxcon_inst_alloc(void *ctx, unsigned int id, uint32_t fn_advance);
void trxcon_inst_free(struct trxcon_inst *trxcon);

int trxcon_l1ctl_receive(struct trxcon_inst *trxcon, struct msgb *msg);
int trxcon_l1ctl_send(struct trxcon_inst *trxcon, struct msgb *msg);
void trxcon_l1ctl_close(struct trxcon_inst *trxcon);
