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
#ifndef write_queue_h
#define write_queue_h

#include "select.h"
#include "msgb.h"

struct write_queue {
	struct bsc_fd bfd;
	unsigned int max_length;
	unsigned int current_length;

	struct llist_head msg_queue;

	int (*read_cb)(struct bsc_fd *fd);
	int (*write_cb)(struct bsc_fd *fd, struct msgb *msg);
	int (*except_cb)(struct bsc_fd *fd);
};

void write_queue_init(struct write_queue *queue, int max_length);
void write_queue_clear(struct write_queue *queue);
int write_queue_enqueue(struct write_queue *queue, struct msgb *data);
int write_queue_bfd_cb(struct bsc_fd *fd, unsigned int what);

#endif
