/* minimalistic telnet/ms interface it might turn into a wire interface */
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

#include <osmocom/telnet_interface.h>
#include <osmocore/talloc.h>

#include <vty/buffer.h>

#define WRITE_CONNECTION(fd, msg...) \
	int ret; \
	char buf[4096]; \
	snprintf(buf, sizeof(buf), msg); \
	ret = write(fd, buf, strlen(buf));


/* per connection data */
LLIST_HEAD(active_connections);

void *l23_ctx;
static void *tall_telnet_ctx;

/* per ms data */
static int telnet_new_connection(struct bsc_fd *fd, unsigned int what);

static struct bsc_fd server_socket = {
	.when	    = BSC_FD_READ,
	.cb	    = telnet_new_connection,
	.priv_nr    = 0,
};

int telnet_init(struct osmocom_ms *ms, int port) {
	struct sockaddr_in sock_addr;
	int fd, on = 1;

	tall_telnet_ctx = talloc_named_const(l23_ctx, 1,
					     "telnet_connection");

	ms_vty_init(ms);

	fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (fd < 0) {
		fprintf(stderr, "Telnet interface socket creation failed\n");
		return -1;
	}

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	memset(&sock_addr, 0, sizeof(sock_addr));
	sock_addr.sin_family = AF_INET;
	sock_addr.sin_port = htons(port);
	sock_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) < 0) {
		fprintf(stderr, "Telnet interface failed to bind\n");
		return -1;
	}

	if (listen(fd, 0) < 0) {
		fprintf(stderr, "Telnet interface failed to listen\n");
		return -1;
	}

	server_socket.data = ms;
	server_socket.fd = fd;
	bsc_register_fd(&server_socket);

	return 0;
}

static void print_welcome(int fd) {
	int ret;
	static char *msg =
		"Welcome to the Osmocom Control interface\n"
		"License GPLv2+: GNU GPL version 2 or later "
		"<http://gnu.org/licenses/gpl.html>\n"
		"This is free software: you are free to change "
		"and redistribute it.\n"
		"There is NO WARRANTY, to the extent permitted "
		"by law.\nType \"help\" to get a short introduction.\n";

	ret = write(fd, msg, strlen(msg));
}

int telnet_close_client(struct bsc_fd *fd) {
	struct telnet_connection *conn = (struct telnet_connection*)fd->data;

	close(fd->fd);
	bsc_unregister_fd(fd);

	if (conn->dbg) {
//		debug_del_target(conn->dbg);
		talloc_free(conn->dbg);
	}

	llist_del(&conn->entry);
	talloc_free(conn);
	return 0;
}

static int client_data(struct bsc_fd *fd, unsigned int what)
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

static int telnet_new_connection(struct bsc_fd *fd, unsigned int what) {
	struct telnet_connection *connection;
	struct sockaddr_in sockaddr;
	socklen_t len = sizeof(sockaddr);
	int new_connection = accept(fd->fd, (struct sockaddr*)&sockaddr, &len);

	if (new_connection < 0) {
		fprintf(stderr, "telnet accept failed\n");
		return -1;
	}


	connection = talloc_zero(tall_telnet_ctx, struct telnet_connection);
	connection->ms = (struct osmocom_ms*)fd->data;
	connection->fd.data = connection;
	connection->fd.fd = new_connection;
	connection->fd.when = BSC_FD_READ;
	connection->fd.cb = client_data;
	bsc_register_fd(&connection->fd);
	llist_add_tail(&connection->entry, &active_connections);

	print_welcome(new_connection);

	connection->vty = vty_create(new_connection, connection);
	if (!connection->vty) {
		fprintf(stderr, "couldn't create VTY\n");
		return -1;
	}

	return 0;
}

/* callback from VTY code */
void vty_event(enum event event, int sock, struct vty *vty)
{
	struct telnet_connection *connection = vty->priv;
	struct bsc_fd *bfd = &connection->fd;

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

void vty_notify(struct osmocom_ms *ms, const char *fmt, ...)
{
	struct telnet_connection *connection;
	char buffer[1000];
	va_list args;
	struct vty *vty;

	if (fmt) {
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
		buffer[sizeof(buffer) - 1] = '\0';
		va_end(args);

		if (!buffer[0])
			return;
	}

	llist_for_each_entry(connection, &active_connections, entry) {
		vty = connection->vty;
		if (!vty)
			continue;
		if (!fmt) {
			vty_out(vty, "%s%% (MS %s)%s", VTY_NEWLINE, ms->name,
				VTY_NEWLINE);
			continue;
		}
		if (buffer[strlen(buffer) - 1] == '\n') {
			buffer[strlen(buffer) - 1] = '\0';
			vty_out(vty, "%% %s%s", buffer, VTY_NEWLINE);
			buffer[strlen(buffer)] = '\n';
		} else
			vty_out(vty, "%% %s", buffer);
	}
}

