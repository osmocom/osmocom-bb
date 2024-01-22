/*
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/socket.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/mobile/tch.h>

struct tch_csd_sock_state {
	struct osmocom_ms *ms;		/* the MS instance we belong to */
	struct osmo_fd listen_bfd;	/* fd for listen socket */
	struct osmo_fd conn_bfd;	/* fd for a client connection */
	struct llist_head rxqueue;
	struct llist_head txqueue;
};

void tch_csd_sock_state_cb(struct osmocom_ms *ms, bool connected);

static void tch_csd_sock_close(struct tch_csd_sock_state *state)
{
	struct osmo_fd *bfd = &state->conn_bfd;

	LOGP(DCSD, LOGL_NOTICE, "TCH CSD sock has closed connection\n");

	tch_csd_sock_state_cb(state->ms, false);

	osmo_fd_unregister(bfd);
	close(bfd->fd);
	bfd->fd = -1;

	/* re-enable the generation of ACCEPT for new connections */
	osmo_fd_read_enable(&state->listen_bfd);

	/* flush the queues */
	while (!llist_empty(&state->rxqueue))
		msgb_free(msgb_dequeue(&state->rxqueue));
	while (!llist_empty(&state->txqueue))
		msgb_free(msgb_dequeue(&state->txqueue));
}

static int tch_csd_sock_read(struct osmo_fd *bfd)
{
	struct tch_csd_sock_state *state = (struct tch_csd_sock_state *)bfd->data;
	struct msgb *msg;
	int rc;

	msg = msgb_alloc(256, "tch_csd_sock_rx");
	if (!msg)
		return -ENOMEM;

	rc = recv(bfd->fd, msg->tail, msgb_tailroom(msg), 0);
	if (rc == 0)
		goto close;
	if (rc < 0) {
		if (errno == EAGAIN)
			return 0;
		goto close;
	}

	msgb_put(msg, rc);
	msgb_enqueue(&state->rxqueue, msg);
	return rc;

close:
	msgb_free(msg);
	tch_csd_sock_close(state);
	return -1;
}

static int tch_csd_sock_write(struct osmo_fd *bfd)
{
	struct tch_csd_sock_state *state = bfd->data;

	while (!llist_empty(&state->txqueue)) {
		struct msgb *msg;
		int rc;

		/* dequeue a msgb */
		msg = msgb_dequeue(&state->txqueue);

		/* try to send it over the socket */
		rc = write(bfd->fd, msgb_data(msg), msgb_length(msg));
		if (rc < 0 && errno == EAGAIN) {
			llist_add(&msg->list, &state->txqueue);
			return 0;
		}
		msgb_free(msg);
		if (rc <= 0)
			goto close;
	}

	osmo_fd_write_disable(bfd);
	return 0;

close:
	tch_csd_sock_close(state);
	return -1;
}

static int tch_csd_sock_cb(struct osmo_fd *bfd, unsigned int flags)
{
	int rc = 0;

	if (flags & OSMO_FD_READ) {
		rc = tch_csd_sock_read(bfd);
		if (rc < 0)
			return rc;
	}

	if (flags & OSMO_FD_WRITE)
		rc = tch_csd_sock_write(bfd);

	return rc;
}

static int tch_csd_sock_accept(struct osmo_fd *bfd, unsigned int flags)
{
	struct tch_csd_sock_state *state = (struct tch_csd_sock_state *)bfd->data;
	struct osmo_fd *conn_bfd = &state->conn_bfd;
	struct sockaddr_un un_addr;
	socklen_t len;
	int rc;

	len = sizeof(un_addr);
	rc = accept(bfd->fd, (struct sockaddr *)&un_addr, &len);
	if (rc < 0) {
		LOGP(DCSD, LOGL_ERROR, "Failed to accept() a new connection\n");
		return -1;
	}

	if (conn_bfd->fd >= 0) {
		LOGP(DCSD, LOGL_NOTICE, "TCH CSD sock already has an active connection\n");
		close(rc); /* reject this connection request */
		return 0;
	}

	osmo_fd_setup(conn_bfd, rc, OSMO_FD_READ, &tch_csd_sock_cb, state, 0);
	if (osmo_fd_register(conn_bfd) != 0) {
		LOGP(DCSD, LOGL_ERROR, "osmo_fd_register() failed\n");
		close(conn_bfd->fd);
		conn_bfd->fd = -1;
		return -1;
	}

	LOGP(DCSD, LOGL_NOTICE, "TCH CSD sock got a connection\n");

	tch_csd_sock_state_cb(state->ms, true);

	return 0;
}

struct tch_csd_sock_state *tch_csd_sock_init(struct osmocom_ms *ms)
{
	const char *sock_path = ms->settings.tch_data.unix_socket_path;
	struct tch_csd_sock_state *state;
	struct osmo_fd *bfd;
	int rc;

	state = talloc_zero(ms, struct tch_csd_sock_state);
	if (state == NULL)
		return NULL;

	INIT_LLIST_HEAD(&state->rxqueue);
	INIT_LLIST_HEAD(&state->txqueue);
	state->conn_bfd.fd = -1;
	state->ms = ms;

	bfd = &state->listen_bfd;

	rc = osmo_sock_unix_init_ofd(bfd, SOCK_STREAM, 0, sock_path, OSMO_SOCK_F_BIND);
	if (rc < 0) {
		LOGP(DCSD, LOGL_ERROR, "Could not create unix socket: %s\n", strerror(errno));
		talloc_free(state);
		return NULL;
	}

	bfd->cb = &tch_csd_sock_accept;
	bfd->data = state;

	return state;
}

void tch_csd_sock_exit(struct tch_csd_sock_state *state)
{
	if (state->conn_bfd.fd > -1)
		tch_csd_sock_close(state);
	osmo_fd_unregister(&state->listen_bfd);
	close(state->listen_bfd.fd);
	talloc_free(state);
}

void tch_csd_sock_recv(struct tch_csd_sock_state *state, struct msgb *msg)
{
	while (msgb_tailroom(msg) > 0) {
		struct msgb *rmsg = msgb_dequeue(&state->rxqueue);
		if (rmsg == NULL)
			break;
		size_t len = OSMO_MIN(msgb_tailroom(msg), msgb_length(rmsg));
		memcpy(msgb_put(msg, len), msgb_data(rmsg), len);
		msgb_pull(rmsg, len);
		if (msgb_length(rmsg) > 0)
			llist_add(&rmsg->list, &state->rxqueue);
		else
			msgb_free(rmsg);
	}
}

void tch_csd_sock_send(struct tch_csd_sock_state *state, struct msgb *msg)
{
	msgb_enqueue(&state->txqueue, msg);
	osmo_fd_write_enable(&state->conn_bfd);
}
