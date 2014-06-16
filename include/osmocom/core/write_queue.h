/* Generic write queue implementation */
/*
 * (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by On-Waves
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#pragma once

/*! \defgroup write_queue Osmocom msgb write queues
 *  @{
 */

/*! \file write_queue.h
 */

#include <osmocom/core/select.h>
#include <osmocom/core/msgb.h>

/*! write queue instance */
struct osmo_wqueue {
	/*! \brief osmocom file descriptor */
	struct osmo_fd bfd;
	/*! \brief maximum length of write queue */
	unsigned int max_length;
	/*! \brief current length of write queue */
	unsigned int current_length;

	/*! \brief actual linked list implementing the queue */
	struct llist_head msg_queue;

	/*! \brief call-back in case qeueue is readable */
	int (*read_cb)(struct osmo_fd *fd);
	/*! \brief call-back in case qeueue is writable */
	int (*write_cb)(struct osmo_fd *fd, struct msgb *msg);
	/*! \brief call-back in case qeueue has exceptions */
	int (*except_cb)(struct osmo_fd *fd);
};

void osmo_wqueue_init(struct osmo_wqueue *queue, int max_length);
void osmo_wqueue_clear(struct osmo_wqueue *queue);
int osmo_wqueue_enqueue(struct osmo_wqueue *queue, struct msgb *data);
int osmo_wqueue_bfd_cb(struct osmo_fd *fd, unsigned int what);

/*! @} */
