#pragma once

#include <osmocom/core/msgb.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>

#define L1CTL_SOCK_PATH	"/tmp/osmocom_l2"

struct l1ctl_sock_inst;

struct l1ctl_sock_client {
	/* list head in l1ctl_sock_inst.clients */
	struct llist_head list;
	/* pointer back to the server socket that accepted us */
	struct l1ctl_sock_inst *l1ctl_sock;
	/* Osmo FD for the client socket */
	struct osmo_fd ofd;
	/* private data, can be set in accept_cb */
	void *priv;
};

/* L1CTL socket instance contains socket data. */
struct l1ctl_sock_inst {
	void *priv; /* Will be appended after osmo-fd's data pointer. */
	struct llist_head clients;
	char* l1ctl_sock_path; /* Socket path used to connect to l23 */
	struct osmo_fd ofd; /* Osmocom file descriptor to accept L1CTL connections. */
	void (*recv_cb)(struct l1ctl_sock_client *lsc, struct msgb *msg); /* Callback function called for incoming data from l2 app. */
	/* Callback function called for new client after accept() */
	int (*accept_cb)(struct l1ctl_sock_client *lsc);
	/* Callback function called when client disappeared */
	void (*close_cb)(struct l1ctl_sock_client *lsc);
};

/**
 * @brief Initialise the l1ctl socket for communication with l2 apps.
 */
struct l1ctl_sock_inst *l1ctl_sock_init(
                void *ctx,
                void (*recv_cb)(struct l1ctl_sock_client *lsc, struct msgb *msg),
                int (*accept_cb)(struct l1ctl_sock_client *lsc),
                void (*close_cb)(struct l1ctl_sock_client *lsc),
                char *path);

/**
 * @brief Transmit message to l2.
 */
int l1ctl_sock_write_msg(struct l1ctl_sock_client *lsc, struct msgb *msg);

/**
 * @brief Destroy instance.
 */
void l1ctl_sock_destroy(struct l1ctl_sock_inst *lsi);
