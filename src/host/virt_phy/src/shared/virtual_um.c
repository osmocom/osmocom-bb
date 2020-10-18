/* Routines for a Virtual Um interface over GSMTAP/UDP */

/* (C) 2015 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <osmocom/core/select.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <virtphy/osmo_mcast_sock.h>
#include <virtphy/virtual_um.h>

#include <unistd.h>
#include <errno.h>

/**
 * Virtual UM interface file descriptor callback.
 * Should be called by select.c when the fd is ready for reading.
 */
static int virt_um_fd_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct virt_um_inst *vui = ofd->data;

	if (what & OSMO_FD_READ) {
		struct msgb *msg = msgb_alloc(VIRT_UM_MSGB_SIZE, "Virtual UM Rx");
		int rc;

		/* read message from fd into message buffer */
		rc = mcast_bidir_sock_rx(vui->mcast_sock, msgb_data(msg), msgb_tailroom(msg));
		if (rc > 0) {
			msgb_put(msg, rc);
			msg->l1h = msgb_data(msg);
			/* call the l1 callback function for a received msg */
			vui->recv_cb(vui, msg);
		} else if (rc == 0) {
			vui->recv_cb(vui, NULL);
			osmo_fd_close(ofd);
		} else
			perror("Read from multicast socket");

	}

	return 0;
}

struct virt_um_inst *virt_um_init(void *ctx, char *tx_mcast_group, uint16_t tx_mcast_port,
				  char *rx_mcast_group, uint16_t rx_mcast_port, int ttl, const char *dev_name,
				  void (*recv_cb)(struct virt_um_inst *vui, struct msgb *msg))
{
	struct virt_um_inst *vui = talloc_zero(ctx, struct virt_um_inst);
	int rc;

	vui->mcast_sock = mcast_bidir_sock_setup(ctx, tx_mcast_group, tx_mcast_port,
						 rx_mcast_group, rx_mcast_port, 1, virt_um_fd_cb, vui);
	if (!vui->mcast_sock) {
		perror("Unable to create VirtualUm multicast socket");
		talloc_free(vui);
		return NULL;
	}
	vui->recv_cb = recv_cb;

	if (ttl >= 0) {
		rc = osmo_sock_mcast_ttl_set(vui->mcast_sock->tx_ofd.fd, ttl);
		if (rc < 0) {
			perror("Cannot set TTL of Virtual Um transmit socket");
			goto out_close;
		}
	}

	if (dev_name) {
		rc = osmo_sock_mcast_iface_set(vui->mcast_sock->tx_ofd.fd, dev_name);
		if (rc < 0) {
			perror("Cannot bind multicast tx to given device");
			goto out_close;
		}
		rc = osmo_sock_mcast_iface_set(vui->mcast_sock->rx_ofd.fd, dev_name);
		if (rc < 0) {
			perror("Cannot bind multicast rx to given device");
			goto out_close;
		}
	}

	return vui;

out_close:
	mcast_bidir_sock_close(vui->mcast_sock);
	talloc_free(vui);
	return NULL;
}

void virt_um_destroy(struct virt_um_inst *vui)
{
	mcast_bidir_sock_close(vui->mcast_sock);
	talloc_free(vui);
}

/**
 * Write msg to to multicast socket and free msg afterwards
 */
int virt_um_write_msg(struct virt_um_inst *vui, struct msgb *msg)
{
	int rc;

	rc = mcast_bidir_sock_tx(vui->mcast_sock, msgb_data(msg),
	                msgb_length(msg));
	if (rc < 0)
		rc = -errno;
	msgb_free(msg);

	return rc;
}
