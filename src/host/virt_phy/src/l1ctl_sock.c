/* Socket based Layer1 <-> Layer23 communication over L1CTL primitives. */

/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
 * (C) 2017 by Harald Welte <laforge@gnumonks.org>
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
#include <arpa/inet.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/select.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/socket.h>

#include <virtphy/l1ctl_sock.h>
#include <virtphy/logging.h>

#define L1CTL_SOCK_MSGB_SIZE	256

static void l1ctl_client_destroy(struct l1ctl_sock_client *lsc)
{
	struct l1ctl_sock_inst *lsi = lsc->l1ctl_sock;
	if (lsi->close_cb)
		lsi->close_cb(lsc);
	osmo_fd_close(&lsc->ofd);
	llist_del(&lsc->list);
	talloc_free(lsc);
}

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
	struct l1ctl_sock_client *lsc = ofd->data;
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
		lsc->l1ctl_sock->recv_cb(lsc, msg);
		return 0;
	}
err_close:
	LOGP(DL1C, LOGL_ERROR, "Failed to receive msg from l2. Connection will be closed.\n");
	l1ctl_client_destroy(lsc);

	return 0;

}

/* called for the master (listening) socket of the instance, allocates a new client */
static int l1ctl_sock_accept_cb(struct osmo_fd *ofd, unsigned int what)
{

	struct l1ctl_sock_inst *lsi = ofd->data;
	struct l1ctl_sock_client *lsc;
	int fd, rc;

	fd = accept(ofd->fd, NULL, NULL);
	if (fd < 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to accept connection to l2.\n");
		return -1;
	}

	lsc = talloc_zero(lsi, struct l1ctl_sock_client);
	if (!lsc) {
		close(fd);
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate L1CTL client\n");
		return -1;
	}

	lsc->l1ctl_sock = lsi;
	lsc->ofd.fd = fd;
	lsc->ofd.when = BSC_FD_READ;
	lsc->ofd.cb = l1ctl_sock_data_cb;
	lsc->ofd.data = lsc;
	if (lsi->accept_cb) {
		rc = lsi->accept_cb(lsc);
		if (rc < 0) {
			talloc_free(lsc);
			close(fd);
			return rc;
		}
	}

	LOGP(DL1C, LOGL_INFO, "Accepted client (fd=%u) from server (fd=%u)\n", fd, ofd->fd);
	if (osmo_fd_register(&lsc->ofd) != 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to register the l2 connection fd.\n");
		talloc_free(lsc);
		return -1;
	}
	llist_add_tail(&lsc->list, &lsi->clients);
	return 0;
}

struct l1ctl_sock_inst *l1ctl_sock_init(
                void *ctx,
                void (*recv_cb)(struct l1ctl_sock_client *lsc, struct msgb *msg),
                int (*accept_cb)(struct l1ctl_sock_client *lsc),
                void (*close_cb)(struct l1ctl_sock_client *lsc),
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
		LOGP(DL1C, LOGL_ERROR, "Error creating L1CTL listening socket\n");
		talloc_free(lsi);
		return NULL;
	}

	lsi->recv_cb = recv_cb;
	lsi->accept_cb = accept_cb;
	lsi->close_cb = close_cb;
	lsi->l1ctl_sock_path = path;
	INIT_LLIST_HEAD(&lsi->clients);

	return lsi;
}

void l1ctl_sock_destroy(struct l1ctl_sock_inst *lsi)
{
	struct l1ctl_sock_client *lsc, *lsc2;

	llist_for_each_entry_safe(lsc, lsc2, &lsi->clients, list)
		l1ctl_client_destroy(lsc);

	osmo_fd_close(&lsi->ofd);
	talloc_free(lsi);
}

int l1ctl_sock_write_msg(struct l1ctl_sock_client *lsc, struct msgb *msg)
{
	int rc;
	rc = write(lsc->ofd.fd, msgb_data(msg), msgb_length(msg));
	msgb_free(msg);
	return rc;
}
