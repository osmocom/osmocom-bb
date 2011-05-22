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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>

#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/command.h>

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

int telnet_init(void *tall_ctx, void *priv, int port)
{
	struct sockaddr_in sock_addr;
	int fd, rc, on = 1;

	tall_telnet_ctx = talloc_named_const(tall_ctx, 1,
					     "telnet_connection");

	fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (fd < 0) {
		LOGP(0, LOGL_ERROR, "Telnet interface socket creation failed\n");
		return fd;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);
	sock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	rc = bind(fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
	if (rc < 0) {
		LOGP(0, LOGL_ERROR, "Telnet interface failed to bind\n");
		close(fd);
		return rc;
	}

	rc = listen(fd, 0);
	if (rc < 0) {
		LOGP(0, LOGL_ERROR, "Telnet interface failed to listen\n");
		close(fd);
		return rc;
	}

	server_socket.data = priv;
	server_socket.fd = fd;
	osmo_fd_register(&server_socket);

	return 0;
}

extern struct host host;

static void print_welcome(int fd)
{
	int ret;
	static const char *msg1 = "Welcome to the ";
	static const char *msg2 = " control interface\r\n";
	char *app_name = "<unnamed>";

	if (host.app_info->name)
		app_name = host.app_info->name;

	ret = write(fd, msg1, strlen(msg1));
	ret = write(fd, app_name, strlen(app_name));
	ret = write(fd, msg2, strlen(msg2));

	if (host.app_info->copyright)
		ret = write(fd, host.app_info->copyright, strlen(host.app_info->copyright));
}

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
	if (!conn->vty)
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

	print_welcome(new_connection);

	connection->vty = vty_create(new_connection, connection);
	if (!connection->vty) {
		LOGP(0, LOGL_ERROR, "couldn't create VTY\n");
		close(new_connection);
		talloc_free(connection);
		return -1;
	}

	return 0;
}

/* callback from VTY code */
void vty_event(enum event event, int sock, struct vty *vty)
{
	struct telnet_connection *connection = vty->priv;
	struct osmo_fd *bfd = &connection->fd;

	if (vty->type != VTY_TERM)
		return;

	switch (event) {
	case VTY_READ:
		bfd->when |= BSC_FD_READ;
		break;
	case VTY_WRITE:
		bfd->when |= BSC_FD_WRITE;
		break;
	case VTY_CLOSED:
		/* vty layer is about to free() vty */
		connection->vty = NULL;
		telnet_close_client(bfd);
		break;
	default:
		break;
	}
}

