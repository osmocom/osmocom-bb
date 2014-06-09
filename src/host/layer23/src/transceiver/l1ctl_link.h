/*
 * l1ctl_link.h
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

#ifndef __TRX_L1CTL_LINK_H__
#define __TRX_L1CTL_LINK_H__


#include <osmocom/core/socket.h>
#include <osmocom/core/write_queue.h>


/* Link */

typedef int (*l1ctl_cb_t)(void *data, struct msgb *msgb);

struct l1ctl_link
{
	struct osmo_wqueue wq;

	l1ctl_cb_t cb;
	void      *cb_data;

	int to_free;
};


int l1l_open(struct l1ctl_link *l1l,
             const char *path, l1ctl_cb_t cb, void *cb_data);

int l1l_close(struct l1ctl_link *l1l);

int l1l_send(struct l1ctl_link *l1l, struct msgb *msg);


/* Server */

typedef int (*l1ctl_server_cb_t)(void *data, struct l1ctl_link *l1l);

struct l1ctl_server
{
	struct osmo_fd bfd;

	l1ctl_server_cb_t cb;
	void             *cb_data;
};

int  l1l_start_server(struct l1ctl_server *l1s, const char *path,
                      l1ctl_server_cb_t cb, void *cb_data);
void l1l_stop_server (struct l1ctl_server *l1s);


#endif /* __TRX_L1CTL_LINK_H__ */
