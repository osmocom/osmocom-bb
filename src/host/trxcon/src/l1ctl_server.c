/*
 * OsmocomBB <-> SDR connection bridge
 * UNIX socket server for L1CTL
 *
 * (C) 2013 by Sylvain Munaut <tnt@246tNt.com>
 * (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2022 by by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <sys/un.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/write_queue.h>

#include <osmocom/bb/trxcon/logging.h>
#include <osmocom/bb/trxcon/l1ctl_server.h>

#define LOGP_CLI(cli, cat, level, fmt, args...) \
	LOGP(cat, level, "%s" fmt, (cli)->log_prefix, ## args)

static int l1ctl_client_read_cb(struct osmo_fd *ofd)
{
	struct l1ctl_client *client = (struct l1ctl_client *)ofd->data;
	struct msgb *msg;
	uint16_t len;
	int rc;

	/* Attempt to read from socket */
	rc = read(ofd->fd, &len, L1CTL_MSG_LEN_FIELD);
	if (rc < L1CTL_MSG_LEN_FIELD) {
		LOGP_CLI(client, DL1D, LOGL_NOTICE,
			 "L1CTL server has lost connection (id=%u)\n",
			 client->id);
		if (rc >= 0)
			rc = -EIO;
		l1ctl_client_conn_close(client);
		return rc;
	}

	/* Check message length */
	len = ntohs(len);
	if (len > L1CTL_LENGTH) {
		LOGP_CLI(client, DL1D, LOGL_ERROR, "Length is too big: %u\n", len);
		return -EINVAL;
	}

	/* Allocate a new msg */
	msg = msgb_alloc_headroom(L1CTL_LENGTH + L1CTL_HEADROOM,
		L1CTL_HEADROOM, "l1ctl_rx_msg");
	if (!msg) {
		LOGP_CLI(client, DL1D, LOGL_ERROR, "Failed to allocate msg\n");
		return -ENOMEM;
	}

	msg->l1h = msgb_put(msg, len);
	rc = read(ofd->fd, msg->l1h, msgb_l1len(msg));
	if (rc != len) {
		LOGP_CLI(client, DL1D, LOGL_ERROR,
			 "Can not read data: len=%d < rc=%d: %s\n",
			 len, rc, strerror(errno));
		msgb_free(msg);
		return rc;
	}

	/* Debug print */
	LOGP_CLI(client, DL1D, LOGL_DEBUG, "RX: '%s'\n", osmo_hexdump(msg->data, msg->len));

	/* Call L1CTL handler */
	client->server->cfg->conn_read_cb(client, msg);

	return 0;
}

static int l1ctl_client_write_cb(struct osmo_fd *ofd, struct msgb *msg)
{
	struct l1ctl_client *client = (struct l1ctl_client *)ofd->data;
	int len;

	if (ofd->fd <= 0)
		return -EINVAL;

	len = write(ofd->fd, msg->data, msg->len);
	if (len != msg->len) {
		LOGP_CLI(client, DL1D, LOGL_ERROR,
			 "Failed to write data: written (%d) < msg_len (%d)\n",
			 len, msg->len);
		return -1;
	}

	return 0;
}

/* Connection handler */
static int l1ctl_server_conn_cb(struct osmo_fd *sfd, unsigned int flags)
{
	struct l1ctl_server *server = (struct l1ctl_server *)sfd->data;
	struct l1ctl_client *client;
	int rc, client_fd;

	client_fd = accept(sfd->fd, NULL, NULL);
	if (client_fd < 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to accept() a new connection: "
		     "%s\n", strerror(errno));
		return client_fd;
	}

	if (server->cfg->num_clients_max > 0 /* 0 means unlimited */ &&
	    server->num_clients >= server->cfg->num_clients_max) {
		LOGP(DL1C, LOGL_NOTICE, "L1CTL server cannot accept more "
		     "than %u connection(s)\n", server->cfg->num_clients_max);
		close(client_fd);
		return -ENOMEM;
	}

	client = talloc_zero(server, struct l1ctl_client);
	if (client == NULL) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate an L1CTL client\n");
		close(client_fd);
		return -ENOMEM;
	}

	/* Init the client's write queue */
	osmo_wqueue_init(&client->wq, 100);
	INIT_LLIST_HEAD(&client->wq.bfd.list);

	client->wq.write_cb = &l1ctl_client_write_cb;
	client->wq.read_cb = &l1ctl_client_read_cb;
	osmo_fd_setup(&client->wq.bfd, client_fd, OSMO_FD_READ, &osmo_wqueue_bfd_cb, client, 0);

	/* Register the client's write queue */
	rc = osmo_fd_register(&client->wq.bfd);
	if (rc != 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to register a new connection fd\n");
		close(client->wq.bfd.fd);
		talloc_free(client);
		return rc;
	}

	llist_add_tail(&client->list, &server->clients);
	client->id = server->next_client_id++;
	client->server = server;
	server->num_clients++;

	LOGP(DL1C, LOGL_NOTICE, "L1CTL server got a new connection (id=%u)\n", client->id);

	if (client->server->cfg->conn_accept_cb != NULL)
		client->server->cfg->conn_accept_cb(client);

	return 0;
}

int l1ctl_client_send(struct l1ctl_client *client, struct msgb *msg)
{
	uint8_t *len;

	/* Debug print */
	LOGP_CLI(client, DL1D, LOGL_DEBUG, "TX: '%s'\n", osmo_hexdump(msg->data, msg->len));

	if (msg->l1h != msg->data)
		LOGP_CLI(client, DL1D, LOGL_INFO, "Message L1 header != Message Data\n");

	/* Prepend 16-bit length before sending */
	len = msgb_push(msg, L1CTL_MSG_LEN_FIELD);
	osmo_store16be(msg->len - L1CTL_MSG_LEN_FIELD, len);

	if (osmo_wqueue_enqueue(&client->wq, msg) != 0) {
		LOGP_CLI(client, DL1D, LOGL_ERROR, "Failed to enqueue msg!\n");
		msgb_free(msg);
		return -EIO;
	}

	return 0;
}

void l1ctl_client_conn_close(struct l1ctl_client *client)
{
	struct l1ctl_server *server = client->server;

	if (server->cfg->conn_close_cb != NULL)
		server->cfg->conn_close_cb(client);

	/* Close connection socket */
	osmo_fd_unregister(&client->wq.bfd);
	close(client->wq.bfd.fd);
	client->wq.bfd.fd = -1;

	/* Clear pending messages */
	osmo_wqueue_clear(&client->wq);

	client->server->num_clients--;
	llist_del(&client->list);
	talloc_free(client);

	/* If this was the last client, reset the client IDs generator to 0.
	 * This way avoid assigning huge unreadable client IDs like 26545. */
	if (llist_empty(&server->clients))
		server->next_client_id = 0;
}

struct l1ctl_server *l1ctl_server_alloc(void *ctx, const struct l1ctl_server_cfg *cfg)
{
	struct l1ctl_server *server;
	int rc;

	LOGP(DL1C, LOGL_NOTICE, "Init L1CTL server (sock_path=%s)\n", cfg->sock_path);

	server = talloc(ctx, struct l1ctl_server);
	OSMO_ASSERT(server != NULL);

	*server = (struct l1ctl_server) {
		.clients = LLIST_HEAD_INIT(server->clients),
		.cfg = cfg,
	};

	/* conn_read_cb shall not be NULL */
	OSMO_ASSERT(cfg->conn_read_cb != NULL);

	/* Bind connection handler */
	osmo_fd_setup(&server->ofd, -1, OSMO_FD_READ, &l1ctl_server_conn_cb, server, 0);

	rc = osmo_sock_unix_init_ofd(&server->ofd, SOCK_STREAM, 0,
				     cfg->sock_path, OSMO_SOCK_F_BIND);
	if (rc < 0) {
		LOGP(DL1C, LOGL_ERROR, "Could not create UNIX socket: %s\n",
			strerror(errno));
		talloc_free(server);
		return NULL;
	}

	return server;
}

void l1ctl_server_free(struct l1ctl_server *server)
{
	LOGP(DL1C, LOGL_NOTICE, "Shutdown L1CTL server\n");

	/* Close all client connections */
	while (!llist_empty(&server->clients)) {
		struct l1ctl_client *client = llist_entry(server->clients.next,
							  struct l1ctl_client,
							  list);
		l1ctl_client_conn_close(client);
	}

	/* Unbind listening socket */
	if (server->ofd.fd != -1) {
		osmo_fd_unregister(&server->ofd);
		close(server->ofd.fd);
		server->ofd.fd = -1;
	}

	talloc_free(server);
}
