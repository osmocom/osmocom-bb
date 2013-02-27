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


#include <osmocom/core/write_queue.h>


typedef int (*l1ctl_cb_t)(void *data, struct msgb *msgb);

struct l1ctl_link
{
	struct osmo_wqueue wq;

	l1ctl_cb_t cb;
	void      *cb_data;

	struct app_state *as;

	struct trx *trx;

	uint8_t tx_mask, rx_mask;
};


int l1l_open(struct l1ctl_link *l1l,
             const char *path, l1ctl_cb_t cb, void *cb_data);

int l1l_close(struct l1ctl_link *l1l);

int l1l_send(struct l1ctl_link *l1l, struct msgb *msg);


#endif /* __TRX_L1CTL_LINK_H__ */
