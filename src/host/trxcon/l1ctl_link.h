#pragma once

#include <osmocom/core/write_queue.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/fsm.h>

#define L1CTL_LENGTH 256
#define L1CTL_HEADROOM 32

/* Forward declaration to avoid mutual include */
struct trx_instance;

enum l1ctl_fsm_states {
	L1CTL_STATE_IDLE = 0,
	L1CTL_STATE_CONNECTED,
};

struct l1ctl_link {
	struct osmo_fsm_inst *fsm;
	struct osmo_fd listen_bfd;
	struct osmo_wqueue wq;

	/* Bind TRX instance */
	struct trx_instance *trx;

	/* L1CTL handlers specific */
	struct osmo_timer_list fbsb_timer;
	uint8_t fbsb_conf_sent;
};

int l1ctl_link_init(struct l1ctl_link **l1l, const char *sock_path);
void l1ctl_link_shutdown(struct l1ctl_link *l1l);

int l1ctl_link_send(struct l1ctl_link *l1l, struct msgb *msg);
int l1ctl_link_close_conn(struct l1ctl_link *l1l);
