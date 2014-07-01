/* minimalistic telnet/network interface it might turn into a wire interface */
/* (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/signal.h>

#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/command.h>

/*! \addtogroup telnet_interface
 *  @{
 */
/*! \file telnet_interface.c */

/* per connection data */
LLIST_HEAD(active_connections);

static void *tall_telnet_ctx;

/* per network data */
static int telnet_new_connection(struct osmo_fd *fd, unsigned int what);

static struct osmo_fd server_socket = {
	.when	    = BSC_FD_READ,
	.cb	    = telnet_new_connection,
	.priv_nr    = 0,
};

/*! \brief Initialize telnet based VTY interface listening to 127.0.0.1
 *  \param[in] tall_ctx \ref talloc context
 *  \param[in] priv private data to be passed to callback
 *  \param[in] port UDP port number
 */
int telnet_init(void *tall_ctx, void *priv, int port)
{
	return telnet_init_dynif(tall_ctx, priv, "127.0.0.1", port);
}

/*! \brief Initialize telnet based VTY interface
 *  \param[in] tall_ctx \ref talloc context
 *  \param[in] priv private data to be passed to callback
 *  \param[in] ip IP to listen to ('::1' for localhost, '::0' for all, ...)
 *  \param[in] port UDP port number
 */
int telnet_init_dynif(void *tall_ctx, void *priv, const char *ip, int port)
{
	int rc;

	tall_telnet_ctx = talloc_named_const(tall_ctx, 1,
			"telnet_connection");

	rc = osmo_sock_init_ofd(
			&server_socket,
			AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP,
			ip, port, OSMO_SOCK_F_BIND
			);

	server_socket.data = priv;

	return (rc < 0) ? -1 : 0;
}

extern struct host host;

/*! \brief close a telnet connection */
int telnet_close_client(struct osmo_fd *fd)
{
	struct telnet_connection *conn = (struct telnet_connection*)fd->data;

	close(fd->fd);
	osmo_fd_unregister(fd);

	if (conn->dbg) {
		log_del_target(conn->dbg);
		talloc_free(conn->dbg);
	}

	llist_del(&conn->entry);
	talloc_free(conn);
	return 0;
}

static int client_data(struct osmo_fd *fd, unsigned int what)
{
	struct telnet_connection *conn = fd->data;
	int rc = 0;

	if (what & BSC_FD_READ) {
		conn->fd.when &= ~BSC_FD_READ;
		rc = vty_read(conn->vty);
	}

	/* vty might have been closed from vithin vty_read() */
	if (rc == -EBADF)
		return rc;

	if (what & BSC_FD_WRITE) {
		rc = buffer_flush_all(conn->vty->obuf, fd->fd);
		if (rc == BUFFER_EMPTY)
			conn->fd.when &= ~BSC_FD_WRITE;
	}

	return rc;
}

static int telnet_new_connection(struct osmo_fd *fd, unsigned int what)
{
	struct telnet_connection *connection;
	struct sockaddr_in sockaddr;
	socklen_t len = sizeof(sockaddr);
	int new_connection = accept(fd->fd, (struct sockaddr*)&sockaddr, &len);

	if (new_connection < 0) {
		LOGP(0, LOGL_ERROR, "telnet accept failed\n");
		return new_connection;
	}

	connection = talloc_zero(tall_telnet_ctx, struct telnet_connection);
	connection->priv = fd->data;
	connection->fd.data = connection;
	connection->fd.fd = new_connection;
	connection->fd.when = BSC_FD_READ;
	connection->fd.cb = client_data;
	osmo_fd_register(&connection->fd);
	llist_add_tail(&connection->entry, &active_connections);

	connection->vty = vty_create(new_connection, connection);
	if (!connection->vty) {
		LOGP(0, LOGL_ERROR, "couldn't create VTY\n");
		close(new_connection);
		talloc_free(connection);
		return -1;
	}

	return 0;
}

/*! \brief callback from core VTY code about VTY related events */
void vty_event(enum event event, int sock, struct vty *vty)
{
	struct vty_signal_data sig_data = { 0, };
	struct telnet_connection *connection = vty->priv;
	struct osmo_fd *bfd;

	if (vty->type != VTY_TERM)
		return;

	sig_data.event = event;
	sig_data.sock = sock;
	sig_data.vty = vty;
	osmo_signal_dispatch(SS_L_VTY, S_VTY_EVENT, &sig_data);

	if (!connection)
		return;

	bfd = &connection->fd;

	switch (event) {
	case VTY_READ:
		bfd->when |= BSC_FD_READ;
		break;
	case VTY_WRITE:
		bfd->when |= BSC_FD_WRITE;
		break;
	case VTY_CLOSED:
		/* vty layer is about to free() vty */
		telnet_close_client(bfd);
		break;
	default:
		break;
	}
}

void telnet_exit(void) 
{
	struct telnet_connection *tc, *tc2;

	llist_for_each_entry_safe(tc, tc2, &active_connections, entry)
		telnet_close_client(&tc->fd);

	osmo_fd_unregister(&server_socket);
	close(server_socket.fd);
	talloc_free(tall_telnet_ctx);
}

/*! @} */
