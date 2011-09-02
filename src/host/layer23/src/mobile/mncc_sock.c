/* mncc_sock.c: Tie the MNCC interface to a unix domain socket */

/* (C) 2008-2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2009,2011 by Andreas Eversberg <andreas@eversberg.eu>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <osmocom/gsm/protocol/gsm_04_08.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_sock.h>
#include <osmocom/bb/mobile/gsm48_cc.h>

/* input from CC code into mncc_sock */
int mncc_sock_from_cc(struct mncc_sock_state *state, struct msgb *msg)
{
	struct gsm_mncc *mncc_in = (struct gsm_mncc *) msgb_data(msg);
	int msg_type = mncc_in->msg_type;

	/* Check if we currently have a MNCC handler connected */
	if (state->conn_bfd.fd < 0) {
		LOGP(DMNCC, LOGL_ERROR, "mncc_sock receives %s for external CC app "
			"but socket is gone\n", get_mncc_name(msg_type));
		if (msg_type != GSM_TCHF_FRAME
		 && msg_type != GSM_TCHF_FRAME_EFR
		 && msg_type != MNCC_REL_IND) {
			/* release the request */
			struct gsm_mncc mncc_out;
			memset(&mncc_out, 0, sizeof(mncc_out));
			mncc_out.callref = mncc_in->callref;
			mncc_set_cause(&mncc_out, GSM48_CAUSE_LOC_PRN_S_LU,
					GSM48_CC_CAUSE_TEMP_FAILURE);
			mncc_tx_to_cc(state->inst, MNCC_REL_REQ, &mncc_out);
		}
		/* free the original message */
		msgb_free(msg);
		return -1;
	}

	/* FIXME: check for some maximum queue depth? */

	/* Actually enqueue the message and mark socket write need */
	msgb_enqueue(&state->upqueue, msg);
	state->conn_bfd.when |= BSC_FD_WRITE;
	return 0;
}

void mncc_sock_write_pending(struct mncc_sock_state *state)
{
	state->conn_bfd.when |= BSC_FD_WRITE;
}

/* FIXME: move this to libosmocore */
int osmo_unixsock_listen(struct osmo_fd *bfd, int type, const char *path);

static void mncc_sock_close(struct mncc_sock_state *state)
{
	struct osmo_fd *bfd = &state->conn_bfd;

	LOGP(DMNCC, LOGL_NOTICE, "MNCC Socket has closed connection\n");

	close(bfd->fd);
	bfd->fd = -1;
	osmo_fd_unregister(bfd);

	/* re-enable the generation of ACCEPT for new connections */
	state->listen_bfd.when |= BSC_FD_READ;

	/* FIXME: make sure we don't enqueue anymore */

	/* release all exisitng calls */
	mncc_clear_trans(state->inst, GSM48_PDISC_CC);

	/* flush the queue */
	while (!llist_empty(&state->upqueue)) {
		struct msgb *msg = msgb_dequeue(&state->upqueue);
		msgb_free(msg);
	}
}

static int mncc_sock_read(struct osmo_fd *bfd)
{
	struct mncc_sock_state *state = (struct mncc_sock_state *)bfd->data;
	struct gsm_mncc *mncc_prim;
	struct msgb *msg;
	int rc;

	msg = msgb_alloc(sizeof(*mncc_prim)+256, "mncc_sock_rx");
	if (!msg)
		return -ENOMEM;

	mncc_prim = (struct gsm_mncc *) msg->tail;

	rc = recv(bfd->fd, msg->tail, msgb_tailroom(msg), 0);
	if (rc == 0)
		goto close;

	if (rc < 0) {
		if (errno == EAGAIN)
			return 0;
		goto close;
	}

	rc = mncc_tx_to_cc(state->inst, mncc_prim->msg_type, mncc_prim);

	/* as we always synchronously process the message in mncc_send() and
	 * its callbacks, we can free the message here. */
	msgb_free(msg);

	return rc;

close:
	msgb_free(msg);
	mncc_sock_close(state);
	return -1;
}

static int mncc_sock_write(struct osmo_fd *bfd)
{
	struct mncc_sock_state *state = bfd->data;
	int rc;

	while (!llist_empty(&state->upqueue)) {
		struct msgb *msg, *msg2;
		struct gsm_mncc *mncc_prim;

		/* peek at the beginning of the queue */
		msg = llist_entry(state->upqueue.next, struct msgb, list);
		mncc_prim = (struct gsm_mncc *)msg->data;

		bfd->when &= ~BSC_FD_WRITE;

		/* bug hunter 8-): maybe someone forgot msgb_put(...) ? */
		if (!msgb_length(msg)) {
			LOGP(DMNCC, LOGL_ERROR, "message type (%d) with ZERO "
				"bytes!\n", mncc_prim->msg_type);
			goto dontsend;
		}

		/* try to send it over the socket */
		rc = write(bfd->fd, msgb_data(msg), msgb_length(msg));
		if (rc == 0)
			goto close;
		if (rc < 0) {
			if (errno == EAGAIN) {
				bfd->when |= BSC_FD_WRITE;
				break;
			}
			goto close;
		}

dontsend:
		/* _after_ we send it, we can deueue */
		msg2 = msgb_dequeue(&state->upqueue);
		assert(msg == msg2);
		msgb_free(msg);
	}
	return 0;

close:
	mncc_sock_close(state);

	return -1;
}

static int mncc_sock_cb(struct osmo_fd *bfd, unsigned int flags)
{
	int rc = 0;

	if (flags & BSC_FD_READ)
		rc = mncc_sock_read(bfd);
	if (rc < 0)
		return rc;

	if (flags & BSC_FD_WRITE)
		rc = mncc_sock_write(bfd);

	return rc;
}

/* accept a new connection */
static int mncc_sock_accept(struct osmo_fd *bfd, unsigned int flags)
{
	struct mncc_sock_state *state = (struct mncc_sock_state *)bfd->data;
	struct osmo_fd *conn_bfd = &state->conn_bfd;
	struct sockaddr_un un_addr;
	socklen_t len;
	int rc;

	len = sizeof(un_addr);
	rc = accept(bfd->fd, (struct sockaddr *) &un_addr, &len);
	if (rc < 0) {
		LOGP(DMNCC, LOGL_ERROR, "Failed to accept a new connection\n");
		return -1;
	}

	if (conn_bfd->fd >= 0) {
		LOGP(DMNCC, LOGL_NOTICE, "MNCC app connects but we already have "
			"another active connection ?!?\n");
		/* We already have one MNCC app connected, this is all we support */
		state->listen_bfd.when &= ~BSC_FD_READ;
		close(rc);
		return 0;
	}

	conn_bfd->fd = rc;
	conn_bfd->when = BSC_FD_READ;
	conn_bfd->cb = mncc_sock_cb;
	conn_bfd->data = state;

	if (osmo_fd_register(conn_bfd) != 0) {
		LOGP(DMNCC, LOGL_ERROR, "Failed to register new connection fd\n");
		close(conn_bfd->fd);
		conn_bfd->fd = -1;
		return -1;
	}

	LOGP(DMNCC, LOGL_NOTICE, "MNCC Socket has connection with external "
		"call control application\n");

	return 0;
}


struct mncc_sock_state *mncc_sock_init(void *inst, const char *name, void *tall_ctx)
{
	struct mncc_sock_state *state;
	struct osmo_fd *bfd;
	int rc;

	state = talloc_zero(tall_ctx, struct mncc_sock_state);
	if (!state)
		return NULL;

	state->inst = inst;
	INIT_LLIST_HEAD(&state->upqueue);
	state->conn_bfd.fd = -1;

	bfd = &state->listen_bfd;

	rc = osmo_unixsock_listen(bfd, SOCK_SEQPACKET, name);
	if (rc < 0) {
		LOGP(DMNCC, LOGL_ERROR, "Could not create unix socket: %s\n",
			strerror(errno));
		talloc_free(state);
		return NULL;
	}

	bfd->when = BSC_FD_READ;
	bfd->cb = mncc_sock_accept;
	bfd->data = state;

	rc = osmo_fd_register(bfd);
	if (rc < 0) {
		LOGP(DMNCC, LOGL_ERROR, "Could not register listen fd: %d\n", rc);
		close(bfd->fd);
		talloc_free(state);
		return NULL;
	}

	return state;
}

void mncc_sock_exit(struct mncc_sock_state *state)
{
	if (state->conn_bfd.fd > -1)
		mncc_sock_close(state);
	osmo_fd_unregister(&state->listen_bfd);
	close(state->listen_bfd.fd);
	talloc_free(state);
}

/* FIXME: move this to libosmocore */
int osmo_unixsock_listen(struct osmo_fd *bfd, int type, const char *path)
{
	struct sockaddr_un local;
	unsigned int namelen;
	int rc;

	bfd->fd = socket(AF_UNIX, type, 0);

	if (bfd->fd < 0) {
		fprintf(stderr, "Failed to create Unix Domain Socket.\n");
		return -1;
	}

	local.sun_family = AF_UNIX;
	strncpy(local.sun_path, path, sizeof(local.sun_path));
	local.sun_path[sizeof(local.sun_path) - 1] = '\0';
	unlink(local.sun_path);

	/* we use the same magic that X11 uses in Xtranssock.c for
	 * calculating the proper length of the sockaddr */
#if defined(BSD44SOCKETS) || defined(__UNIXWARE__)
	local.sun_len = strlen(local.sun_path);
#endif
#if defined(BSD44SOCKETS) || defined(SUN_LEN)
	namelen = SUN_LEN(&local);
#else
	namelen = strlen(local.sun_path) +
		  offsetof(struct sockaddr_un, sun_path);
#endif

	rc = bind(bfd->fd, (struct sockaddr *) &local, namelen);
	if (rc != 0) {
		fprintf(stderr, "Failed to bind the unix domain socket. '%s'\n",
			local.sun_path);
		return -1;
	}

	if (listen(bfd->fd, 0) != 0) {
		fprintf(stderr, "Failed to listen.\n");
		return -1;
	}

	return 0;
}
