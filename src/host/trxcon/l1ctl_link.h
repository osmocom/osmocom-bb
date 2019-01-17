#pragma once

#include <stdint.h>

#include <osmocom/core/write_queue.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/fsm.h>

#define L1CTL_LENGTH 256
#define L1CTL_HEADROOM 32

/**
 * Each L1CTL message gets its own length pushed
 * as two bytes in front before sending.
 */
#define L1CTL_MSG_LEN_FIELD 2

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

	/* Shutdown callback */
	void (*shutdown_cb)(struct l1ctl_link *l1l);
};

struct l1ctl_link *l1ctl_link_init(void *tall_ctx, const char *sock_path);
void l1ctl_link_shutdown(struct l1ctl_link *l1l);

int l1ctl_link_send(struct l1ctl_link *l1l, struct msgb *msg);
int l1ctl_link_close_conn(struct l1ctl_link *l1l);
