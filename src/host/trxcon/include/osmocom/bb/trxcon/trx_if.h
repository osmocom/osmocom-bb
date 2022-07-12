#pragma once

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/fsm.h>

#include <osmocom/gsm/gsm_utils.h>

#define TRXC_BUF_SIZE	1024
#define TRXD_BUF_SIZE	512

/* Forward declaration to avoid mutual include */
struct l1sched_burst_req;
struct trxcon_inst;

enum trx_fsm_states {
	TRX_STATE_OFFLINE = 0,
	TRX_STATE_IDLE,
	TRX_STATE_ACTIVE,
	TRX_STATE_RSP_WAIT,
};

struct trx_instance {
	/* trxcon instance we belong to */
	struct trxcon_inst *trxcon;

	struct osmo_fd trx_ofd_ctrl;
	struct osmo_fd trx_ofd_data;

	struct osmo_timer_list trx_ctrl_timer;
	struct llist_head trx_ctrl_list;
	struct osmo_fsm_inst *fsm;

	/* HACK: we need proper state machines */
	uint32_t prev_state;
	bool powered_up;

	/* GSM L1 specific */
	uint16_t pm_band_arfcn_start;
	uint16_t pm_band_arfcn_stop;
	uint16_t band_arfcn;
	uint8_t tx_power;
	int8_t ta;
};

struct trx_ctrl_msg {
	struct llist_head list;
	char cmd[TRXC_BUF_SIZE];
	int retry_cnt;
	int critical;
	int cmd_len;
};

struct trx_instance *trx_if_open(struct trxcon_inst *trxcon,
	const char *local_host, const char *remote_host, uint16_t port);
void trx_if_flush_ctrl(struct trx_instance *trx);
void trx_if_close(struct trx_instance *trx);

int trx_if_cmd_poweron(struct trx_instance *trx);
int trx_if_cmd_poweroff(struct trx_instance *trx);
int trx_if_cmd_echo(struct trx_instance *trx);

int trx_if_cmd_setta(struct trx_instance *trx, int8_t ta);

int trx_if_cmd_rxtune(struct trx_instance *trx, uint16_t band_arfcn);
int trx_if_cmd_txtune(struct trx_instance *trx, uint16_t band_arfcn);

int trx_if_cmd_setslot(struct trx_instance *trx, uint8_t tn,
		       enum gsm_phys_chan_config pchan);
int trx_if_cmd_setfh(struct trx_instance *trx, uint8_t hsn,
	uint8_t maio, uint16_t *ma, size_t ma_len);

int trx_if_cmd_measure(struct trx_instance *trx,
	uint16_t band_arfcn_start, uint16_t band_arfcn_stop);

int trx_if_tx_burst(struct trx_instance *trx,
		    const struct l1sched_burst_req *br);
