/*
 * l1ctl_link.c
 *
 * L1CTL link handling
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
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
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <osmocom/core/socket.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/write_queue.h>

#include <osmocom/bb/common/logging.h>

#include "l1ctl_link.h"


#define L1CTL_LENGTH 256
#define L1CTL_HEADROOM 32

static int
_l1l_read(struct osmo_fd *fd)
{
	struct l1ctl_link *l1l = fd->data;
	struct msgb *msg;
	uint16_t len;
	int rc;

	msg = msgb_alloc_headroom(L1CTL_LENGTH+L1CTL_HEADROOM, L1CTL_HEADROOM, "L1CTL");
	if (!msg) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate msg.\n");
		return -ENOMEM;
	}

	rc = read(fd->fd, &len, sizeof(len));
	if (rc < sizeof(len)) {
		LOGP(DL1C, LOGL_ERROR, "l1ctl socket failed\n");
		msgb_free(msg);
		if (rc >= 0)
			rc = -EIO;
		l1l_close(l1l);
		return rc;
	}

	len = ntohs(len);
	if (len > L1CTL_LENGTH) {
		LOGP(DL1C, LOGL_ERROR, "Length is too big: %u\n", len);
		msgb_free(msg);
		return -EINVAL;
	}


	msg->l1h = msgb_put(msg, len);
	rc = read(fd->fd, msg->l1h, msgb_l1len(msg));
	if (rc != msgb_l1len(msg)) {
		LOGP(DL1C, LOGL_ERROR, "Can not read data: len=%d rc=%d "
		     "errno=%d\n", len, rc, errno);
		msgb_free(msg);
		return rc;
	}

	return l1l->cb(l1l->cb_data, msg);
}

static int
_l1l_write(struct osmo_fd *fd, struct msgb *msg)
{
	int rc;

	if (fd->fd <= 0)
		return -EINVAL;

	rc = write(fd->fd, msg->data, msg->len);
	if (rc != msg->len) {
		LOGP(DL1C, LOGL_ERROR, "Failed to write data: rc: %d\n", rc);
		return rc;
	}

	return 0;
}


int
l1l_open(struct l1ctl_link *l1l,
         const char *path, l1ctl_cb_t cb, void *cb_data)
{
	int rc;

	memset(l1l, 0x00, sizeof(struct l1ctl_link));

	l1l->cb = cb;
	l1l->cb_data = cb_data;

	osmo_wqueue_init(&l1l->wq, 100);
	l1l->wq.bfd.data = l1l;
	l1l->wq.read_cb  = _l1l_read;
	l1l->wq.write_cb = _l1l_write;

	rc = osmo_sock_unix_init_ofd(
		&l1l->wq.bfd,
		SOCK_STREAM, 0,
		path,
		OSMO_SOCK_F_CONNECT
	);

	if (rc < 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to create and init unix domain socket.\n");
		return rc;
	}

	return 0;
}

int
l1l_close(struct l1ctl_link *l1l)
{
	if (l1l->wq.bfd.fd <= 0)
		return -EINVAL;

	osmo_fd_unregister(&l1l->wq.bfd);

	close(l1l->wq.bfd.fd);
	l1l->wq.bfd.fd = -1;

	osmo_wqueue_clear(&l1l->wq);

	if (l1l->to_free)
		talloc_free(l1l);

	return 0;
}

int
l1l_send(struct l1ctl_link *l1l, struct msgb *msg)
{
	uint16_t *len;

	DEBUGP(DL1C, "Sending: '%s'\n", osmo_hexdump(msg->data, msg->len));

	if (msg->l1h != msg->data)
		LOGP(DL1C, LOGL_NOTICE, "Message L1 header != Message Data\n");

	/* prepend 16bit length before sending */
	len = (uint16_t *) msgb_push(msg, sizeof(*len));
	*len = htons(msg->len - sizeof(*len));

	if (osmo_wqueue_enqueue(&l1l->wq, msg) != 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to enqueue msg.\n");
		msgb_free(msg);
		return -EIO;
	}

	return 0;
}


static int
_l1l_accept(struct osmo_fd *fd, unsigned int flags)
{
	struct l1ctl_server *l1s = fd->data;
	struct l1ctl_link *l1l;
	struct sockaddr_un un_addr;
	socklen_t len;
	int cfd, rc;

	len = sizeof(un_addr);
	cfd = accept(fd->fd, (struct sockaddr *) &un_addr, &len);
	if (cfd < 0) {
		LOGP(DL1C, LOGL_ERROR, "Failed to accept a new L1CTL connection.\n");
		return cfd;
	}

	l1l = talloc_zero(NULL, struct l1ctl_link);
	if (!l1l) {
		LOGP(DL1C, LOGL_ERROR, "Failed to allocate a new L1CTL connection.\n");
		return -ENOMEM;
	}

	l1l->to_free = 1;

	osmo_wqueue_init(&l1l->wq, 100);
	l1l->wq.bfd.fd   = cfd;
	l1l->wq.bfd.data = l1l;
	l1l->wq.bfd.when = BSC_FD_READ;
	l1l->wq.read_cb  = _l1l_read;
	l1l->wq.write_cb = _l1l_write;

	INIT_LLIST_HEAD(&l1l->wq.bfd.list);

	rc = l1s->cb(l1s->cb_data, l1l);
	if (rc)
		goto error;

	if (!l1l->cb || !l1l->cb_data) {
		LOGP(DL1C, LOGL_NOTICE, "New L1CTL callback didn't set message callback !\n");
	}

	rc = osmo_fd_register(&l1l->wq.bfd);
	if (rc) {
		LOGP(DL1C, LOGL_ERROR, "Failed to register fd of new L1CTL connection.\n");
		goto error;
	}

	return 0;

error:
	if (l1l)
		l1l_close(l1l);

	return rc;
}

int
l1l_start_server(struct l1ctl_server *l1s, const char *path,
                 l1ctl_server_cb_t cb, void *cb_data)
{
	int rc;

	memset(l1s, 0x00, sizeof(struct l1ctl_server));

	l1s->cb = cb;
	l1s->cb_data = cb_data;

	l1s->bfd.cb   = _l1l_accept;
	l1s->bfd.data = l1s;

	rc = osmo_sock_unix_init_ofd(
		&l1s->bfd,
		SOCK_STREAM, 0,
		path,
		OSMO_SOCK_F_BIND
	);
	if (rc < 0)
		return rc;

	return 0;
}

void
l1l_stop_server(struct l1ctl_server *l1s)
{
	/* FIXME */

}
