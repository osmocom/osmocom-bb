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

#include <osmocom/core/write_queue.h>

int write_queue_bfd_cb(struct osmo_fd *fd, unsigned int what)
{
	struct write_queue *queue;

	queue = container_of(fd, struct write_queue, bfd);

	if (what & BSC_FD_READ)
		queue->read_cb(fd);

	if (what & BSC_FD_EXCEPT)
		queue->except_cb(fd);

	if (what & BSC_FD_WRITE) {
		struct msgb *msg;

		fd->when &= ~BSC_FD_WRITE;

		/* the queue might have been emptied */
		if (!llist_empty(&queue->msg_queue)) {
			--queue->current_length;

			msg = msgb_dequeue(&queue->msg_queue);
			queue->write_cb(fd, msg);
			msgb_free(msg);

			if (!llist_empty(&queue->msg_queue))
				fd->when |= BSC_FD_WRITE;
		}
	}

	return 0;
}

void write_queue_init(struct write_queue *queue, int max_length)
{
	queue->max_length = max_length;
	queue->current_length = 0;
	queue->read_cb = NULL;
	queue->write_cb = NULL;
	queue->bfd.cb = write_queue_bfd_cb;
	INIT_LLIST_HEAD(&queue->msg_queue);
}

int write_queue_enqueue(struct write_queue *queue, struct msgb *data)
{
//	if (queue->current_length + 1 >= queue->max_length)
//		LOGP(DMSC, LOGL_ERROR, "The queue is full. Dropping not yet implemented.\n");

	++queue->current_length;
	msgb_enqueue(&queue->msg_queue, data);
	queue->bfd.when |= BSC_FD_WRITE;

	return 0;
}

void write_queue_clear(struct write_queue *queue)
{
	while (!llist_empty(&queue->msg_queue)) {
		struct msgb *msg = msgb_dequeue(&queue->msg_queue);
		msgb_free(msg);
	}

	queue->current_length = 0;
	queue->bfd.when &= ~BSC_FD_WRITE;
}
