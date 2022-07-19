#pragma once

#include <stdint.h>

#include <osmocom/core/write_queue.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>

#define L1CTL_LENGTH 256
#define L1CTL_HEADROOM 32

/**
 * Each L1CTL message gets its own length pushed
 * as two bytes in front before sending.
 */
#define L1CTL_MSG_LEN_FIELD 2

struct l1ctl_client;

typedef int l1ctl_conn_data_func(struct l1ctl_client *, struct msgb *);
typedef void l1ctl_conn_state_func(struct l1ctl_client *);

struct l1ctl_server_cfg {
	/* UNIX socket path to listen on */
	const char *sock_path;
	/* talloc context to be used for new clients */
	void *talloc_ctx;
	/* maximum number of connected clients */
	unsigned int num_clients_max;
	/* functions to be called on various events */
	l1ctl_conn_data_func *conn_read_cb;	/* mandatory */
	l1ctl_conn_state_func *conn_accept_cb;	/* optional */
	l1ctl_conn_state_func *conn_close_cb;	/* optional */
};

struct l1ctl_server {
	/* list of connected clients */
	struct llist_head clients;
	/* number of connected clients */
	unsigned int num_clients;
	/* socket on which we listen for connections */
	struct osmo_fd ofd;
	/* server configuration */
	const struct l1ctl_server_cfg *cfg;
};

struct l1ctl_client {
	/* list head in l1ctl_server.clients */
	struct llist_head list;
	/* struct l1ctl_server we belong to */
	struct l1ctl_server *server;
	/* client's write queue */
	struct osmo_wqueue wq;
	/* some private data */
	void *priv;
};

int l1ctl_server_start(struct l1ctl_server *server,
		       const struct l1ctl_server_cfg *cfg);
void l1ctl_server_shutdown(struct l1ctl_server *server);

int l1ctl_client_send(struct l1ctl_client *client, struct msgb *msg);
void l1ctl_client_conn_close(struct l1ctl_client *client);
