/* Socket based Layer1 <-> Layer23 communication over L1CTL primitives. */

/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/serial.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/socket.h>

#include <arpa/inet.h>

#include <l1ctl_proto.h>

#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/logging.h>

#define L1CTL_SOCK_MSGB_SIZE	256

/**
 * @brief L1CTL socket file descriptor callback function.
 *
 * @param ofd The osmocom file descriptor.
 * @param what Indicates if the fd has a read, write or exception request. See select.h.
 *
 * Will be called by osmo_select_main() if data on fd is pending.
 */
static int l1ctl_sock_data_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct l1ctl_sock_inst *lsi = ofd->data;
	struct l1ctl_hdr *l1h;
	struct msgb *msg;
	uint16_t len;
	int rc;

	/* Check if request is really read request */
	if (!(what & BSC_FD_READ))
		return 0;

	msg = msgb_alloc(L1CTL_SOCK_MSGB_SIZE, "L1CTL sock rx");

	/* read length of the message first and convert to host byte order */
	rc = read(ofd->fd, &len, sizeof(len));
	if (rc < sizeof(len))
		goto err_close;

	/* convert to host byte order */
	len = ntohs(len);
	if (len <= 0 || len > L1CTL_SOCK_MSGB_SIZE)
		goto err_close;

	rc = read(ofd->fd, msgb_data(msg), len);
	if (rc == len) {
		msgb_put(msg, rc);
		l1h = (void *) msgb_data(msg);
		msg->l1h = (void *) l1h;
		lsi->recv_cb(lsi, msg);
		return 0;
	}
err_close:
	perror("Failed to receive msg from l2. Connection will be closed.\n");
	l1ctl_sock_disconnect(lsi);

	return 0;

}

static int l1ctl_sock_accept_cb(struct osmo_fd *ofd, unsigned int what)
{

	struct l1ctl_sock_inst *lsi = ofd->data;
	int fd;

	fd = accept(ofd->fd, NULL, NULL);
	if (fd < 0) {
		fprintf(stderr, "Failed to accept connection to l2.\n");
		return -1;
	}

	lsi->connection.fd = fd;
	lsi->connection.when = BSC_FD_READ;
	lsi->connection.cb = l1ctl_sock_data_cb;
	lsi->connection.data = lsi;

	if (osmo_fd_register(&lsi->connection) != 0) {
		fprintf(stderr, "Failed to register the l2 connection fd.\n");
		return -1;
	}
	return 0;
}

struct l1ctl_sock_inst *l1ctl_sock_init(
                void *ctx,
                void (*recv_cb)(struct l1ctl_sock_inst *lsi, struct msgb *msg),
                char *path)
{
	struct l1ctl_sock_inst *lsi;
	int rc;

	if (!path)
		path = L1CTL_SOCK_PATH;

	lsi = talloc_zero(ctx, struct l1ctl_sock_inst);
	lsi->priv = NULL;
	lsi->ofd.data = lsi;
	lsi->ofd.when = BSC_FD_READ;
	lsi->ofd.cb = l1ctl_sock_accept_cb;

	rc = osmo_sock_unix_init_ofd(&lsi->ofd, SOCK_STREAM, 0, path, OSMO_SOCK_F_BIND);
	if (rc < 0) {
		talloc_free(lsi);
		return NULL;
	}

	lsi->recv_cb = recv_cb;
	/* no connection -> invalid filedescriptor and not 0 (==std_in) */
	lsi->connection.fd = -1;
	lsi->l1ctl_sock_path = path;

	osmo_fd_register(&lsi->ofd);

	return lsi;
}

void l1ctl_sock_destroy(struct l1ctl_sock_inst *lsi)
{
	struct osmo_fd *ofd = &lsi->ofd;

	osmo_fd_unregister(ofd);
	close(ofd->fd);
	ofd->fd = -1;
	ofd->when = 0;

	talloc_free(lsi);
}

void l1ctl_sock_disconnect(struct l1ctl_sock_inst *lsi)
{
	struct osmo_fd *ofd = &lsi->connection;
	osmo_fd_unregister(ofd);
	close(ofd->fd);
	ofd->fd = -1;
	ofd->when = 0;
}

int l1ctl_sock_write_msg(struct l1ctl_sock_inst *lsi, struct msgb *msg)
{
	int rc;
	rc = write(lsi->connection.fd, msgb_data(msg), msgb_length(msg));
	msgb_free(msg);
	return rc;
}
