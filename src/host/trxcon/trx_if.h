#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/fsm.h>

#include "scheduler.h"
#include "sched_trx.h"

enum trx_fsm_states {
	TRX_STATE_OFFLINE = 0,
	TRX_STATE_IDLE,
	TRX_STATE_ACTIVE,
	TRX_STATE_RSP_WAIT,
};

struct trx_instance {
	struct osmo_fd trx_ofd_clck;
	struct osmo_fd trx_ofd_ctrl;
	struct osmo_fd trx_ofd_data;

	struct osmo_timer_list trx_ctrl_timer;
	struct llist_head trx_ctrl_list;
	struct osmo_fsm_inst *fsm;
	uint32_t prev_state;

	/* Scheduler stuff */
	struct trx_sched sched;
	struct llist_head ts_list;
};

struct trx_ctrl_msg {
	struct llist_head list;
	char cmd[128];
	int retry_cnt;
	int critical;
	int cmd_len;
};

int trx_if_open(struct trx_instance **trx, const char *host, uint16_t port);
void trx_if_flush_ctrl(struct trx_instance *trx);
void trx_if_close(struct trx_instance *trx);

int trx_if_cmd_poweron(struct trx_instance *trx);
int trx_if_cmd_poweroff(struct trx_instance *trx);
int trx_if_cmd_echo(struct trx_instance *trx);

int trx_if_cmd_setpower(struct trx_instance *trx, int db);
int trx_if_cmd_adjpower(struct trx_instance *trx, int db);

int trx_if_cmd_setrxgain(struct trx_instance *trx, int db);
int trx_if_cmd_setmaxdly(struct trx_instance *trx, int dly);

int trx_if_cmd_rxtune(struct trx_instance *trx, uint16_t arfcn);
int trx_if_cmd_txtune(struct trx_instance *trx, uint16_t arfcn);

int trx_if_cmd_setslot(struct trx_instance *trx, uint8_t tn, uint8_t type);
