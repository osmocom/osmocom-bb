#pragma once

#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>

#define L1CTL_SOCK_PATH	"/tmp/osmocom_l2"

/* L1CTL socket instance contains socket data. */
struct l1ctl_sock_inst {
	void *priv; /* Will be appended after osmo-fd's data pointer. */
	struct osmo_fd connection; /* L1CTL connection to l2 app */
	struct osmo_fd ofd; /* Osmocom file descriptor to accept L1CTL connections. */
	void (*recv_cb)(struct l1ctl_sock_inst *vui, struct msgb *msg); /* Callback function called for incoming data from l2 app. */
};

/**
 * @brief Initialise the l1ctl socket for communication with l2 apps.
 */
struct l1ctl_sock_inst *l1ctl_sock_init(
                void *ctx,
                void (*recv_cb)(struct l1ctl_sock_inst *lsi, struct msgb *msg),
                char *path);

/**
 * @brief Transmit message to l2.
 */
int l1ctl_sock_write_msg(struct l1ctl_sock_inst *lsi, struct msgb *msg);

/**
 * @brief Destroy instance.
 */
void l1ctl_sock_destroy();

/**
 * @brief Disconnect current connection.
 */
void l1ctl_sock_disconnect(struct l1ctl_sock_inst *lsi);
