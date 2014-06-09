/*
 * trx.c
 *
 * OpenBTS TRX interface handling
 *
 * Copyright (C) 2014  Sylvain Munaut <tnt@246tNt.com>
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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <osmocom/core/select.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/talloc.h>

#include <osmocom/bb/common/logging.h>

#include "trx.h"


static int _trx_clk_read_cb(struct osmo_fd *ofd, unsigned int what);
static int _trx_ctrl_read_cb(struct osmo_fd *ofd, unsigned int what);
static int _trx_data_read_cb(struct osmo_fd *ofd, unsigned int what);


/* ------------------------------------------------------------------------ */
/* Init / Cleanup                                                           */
/* ------------------------------------------------------------------------ */

static int
_trx_udp_init(struct trx *trx,
              struct osmo_fd *ofd, const char *addr, uint16_t port,
              int (*cb)(struct osmo_fd *fd, unsigned int what))
{
	struct sockaddr_storage _sas;
	struct sockaddr *sa = (struct sockaddr *)&_sas;
	socklen_t sa_len;
	int rv;

	/* Init */
	ofd->fd = -1;
	ofd->cb = cb;
	ofd->data = trx;

	/* Listen / Binds */
	rv = osmo_sock_init_ofd(
		ofd,
		AF_UNSPEC, SOCK_DGRAM, 0, addr, port + 100,
		OSMO_SOCK_F_BIND);
	if (rv < 0)
		goto err;

	/* Connect */
	sa_len = sizeof(struct sockaddr_storage);
	rv = getsockname(ofd->fd, sa, &sa_len);
	if (rv)
		goto err;

	if (sa->sa_family == AF_INET) {
	        struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		sin->sin_port = htons(ntohs(sin->sin_port)-100);
	} else if (sa->sa_family == AF_INET6) {
	        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
		sin6->sin6_port = htons(ntohs(sin6->sin6_port)-100);
	} else {
		rv = -EINVAL;
		goto err;
	}

	rv = connect(ofd->fd, sa, sa_len);
	if (rv)
		goto err;

	return 0;

err:
	if (ofd->fd >= 0) {
		osmo_fd_unregister(ofd);
		close(ofd->fd);
	}

	return rv;
}


struct trx *
trx_alloc(const char *addr, uint16_t base_port)
{
	struct trx *trx;
	int rv;

	/* Alloc */
	trx = talloc_zero(NULL, struct trx);
	if (!trx)
		return NULL;

	/* Clock */
	rv = _trx_udp_init(trx, &trx->ofd_clk, addr, base_port, _trx_clk_read_cb);
	if (rv)
		goto err;

	/* Control */
	rv = _trx_udp_init(trx, &trx->ofd_ctrl, addr, base_port+1, _trx_ctrl_read_cb);
	if (rv)
		goto err;

	/* Data */
	rv = _trx_udp_init(trx, &trx->ofd_data, addr, base_port+2, _trx_data_read_cb);
	if (rv)
		goto err;

	/* Done */
	return trx;

	/* Error path */
err:
	trx_free(trx);

	return NULL;
}

void
trx_free(struct trx *trx)
{
	if (trx->ofd_data.fd >= 0) {
		osmo_fd_unregister(&trx->ofd_data);
		close(trx->ofd_data.fd);
	}

	if (trx->ofd_ctrl.fd >= 0) {
		osmo_fd_unregister(&trx->ofd_ctrl);
		close(trx->ofd_ctrl.fd);
	}

	if (trx->ofd_clk.fd >= 0) {
		osmo_fd_unregister(&trx->ofd_clk);
		close(trx->ofd_clk.fd);
	}

	talloc_free(trx);
}


/* ------------------------------------------------------------------------ */
/* Clock interface                                                          */
/* ------------------------------------------------------------------------ */

#define CLK_BUF_LEN	128

static int
_trx_clk_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	char buf[CLK_BUF_LEN];
	uint32_t fn;
	int l;

	l = recv(ofd->fd, buf, sizeof(buf), MSG_TRUNC);
	if (l <= 0)
		return -EIO;
	if (l >= sizeof(buf)) {
		LOGP(DTRX, LOGL_ERROR,
		     "Received large message on CLK interface (%d)\n", l);
		return -EINVAL;
	}

	if (memcmp("IND CLOCK ", buf, 10) || buf[l-1]) {
		LOGP(DTRX, LOGL_ERROR,
		     "Received invalid message on CLK interface\n");
		return -EINVAL;
	}

	fn = atoi(&buf[11]);

	LOGP(DTRX, LOGL_DEBUG, "Clock IND: fn=%d\n", (int)fn);

	/* FIXME call the clk ind callback */

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Control interface                                                        */
/* ------------------------------------------------------------------------ */

#define CMD_BUF_LEN	128

static int
_trx_ctrl_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	char buf[CMD_BUF_LEN];
	int l;

	l = recv(ofd->fd, buf, sizeof(buf), MSG_TRUNC);
	if (l <= 0)
		return -EIO;

	/* FIXME should not happen ... */

	printf("Here %s\n", buf);

	return 0;
}

#include <fcntl.h>

int
trx_ctrl_send_cmd(struct trx *trx, const char *cmd, const char *fmt, ...)
{
	va_list ap;
	char buf[CMD_BUF_LEN], cmd_match[32];
	int l;

	/* Send the commands */
	l = snprintf(buf, sizeof(buf)-1, "CMD %s%s", cmd, fmt ? " " : "");

	if (fmt) {
		va_start(ap, fmt);
		l += vsnprintf(buf+l, sizeof(buf)-l-1, fmt, ap);
		va_end(ap);
	}

	buf[l] = '\0';

	LOGP(DTRX, LOGL_DEBUG, "TRX Control send: |%s|\n", buf);

	send(trx->ofd_ctrl.fd, buf, strlen(buf)+1, 0);

	/* Wait for response */
	{
		int fd = trx->ofd_ctrl.fd;
		int flags;

		/* make FD nonblocking */
		flags = fcntl(fd, F_GETFL);
		if (flags < 0)
			return flags;
		flags &= ~O_NONBLOCK;
		flags = fcntl(fd, F_SETFL, flags);
		if (flags < 0)
			return flags;
	}

	/* Get a response */
	l = recv(trx->ofd_ctrl.fd, buf, sizeof(buf), MSG_TRUNC);
	if (l <= 0)
		return -EIO;

	if (memcmp(buf, "RSP ", 4) || buf[l-1] != '\0') {
		LOGP(DTRX, LOGL_ERROR, "Invalid response on TRX Control socket\n");
		return -EIO;
	}

	LOGP(DTRX, LOGL_DEBUG, "TRX Control read: |%s|\n", buf);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Data interface                                                           */
/* ------------------------------------------------------------------------ */

static int
_trx_data_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	char buf[128];
	int l;

	l = recv(ofd->fd, buf, sizeof(buf), MSG_TRUNC);
	if (l <= 0)
		return -EIO;

	return 0;
}
