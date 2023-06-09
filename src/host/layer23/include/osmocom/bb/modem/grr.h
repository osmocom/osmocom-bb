#pragma once

#include <stdbool.h>
#include <stdint.h>

struct msgb;
struct osmocom_ms;
struct lapdm_entity;
struct osmo_fsm;

enum grr_fsm_state {
	GRR_ST_PACKET_NOT_READY,
	GRR_ST_PACKET_IDLE,
	GRR_ST_PACKET_TRANSFER,
};

enum grr_fsm_event {
	GRR_EV_BCCH_BLOCK_IND,
	GRR_EV_PCH_AGCH_BLOCK_IND,
	GRR_EV_RACH_REQ,
	GRR_EV_RACH_CNF,
	GRR_EV_PDCH_ESTABLISH_REQ,
	GRR_EV_PDCH_RELEASE_REQ,
	GRR_EV_PDCH_UL_TBF_CFG_REQ,
	GRR_EV_PDCH_DL_TBF_CFG_REQ,
	GRR_EV_PDCH_BLOCK_REQ,
	GRR_EV_PDCH_BLOCK_IND,
};

extern struct osmo_fsm grr_fsm_def;

int modem_grr_rslms_cb(struct msgb *msg, struct lapdm_entity *le, void *ctx);
uint8_t modem_grr_gen_chan_req(bool single_block);
