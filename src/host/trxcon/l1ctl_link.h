#pragma once

#include <osmocom/core/write_queue.h>
#include <osmocom/core/select.h>
#include <osmocom/core/msgb.h>

#define L1CTL_LENGTH 256
#define L1CTL_HEADROOM 32

struct l1ctl_link {
	struct osmo_fd listen_bfd;
	struct osmo_wqueue wq;
};

int l1ctl_link_init(struct l1ctl_link **l1l, const char *sock_path);
void l1ctl_link_shutdown(struct l1ctl_link *l1l);

int l1ctl_link_send(struct l1ctl_link *l1l, struct msgb *msg);
int l1ctl_link_close_conn(struct l1ctl_link *l1l);
