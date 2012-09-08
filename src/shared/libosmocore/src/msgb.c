/* (C) 2008 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
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

/*! \addtogroup msgb
 *  @{
 */

/*! \file msgb.c
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <osmocom/core/msgb.h>
//#include <openbsc/gsm_data.h>
#include <osmocom/core/talloc.h>
//#include <openbsc/debug.h>

void *tall_msgb_ctx;

/*! \brief Allocate a new message buffer
 * \param[in] size Length in octets, including headroom
 * \param[in] name Human-readable name to be associated with msgb
 *
 * This function allocates a 'struct msgb' as well as the underlying
 * memory buffer for the actual message data (size specified by \a size)
 * using the talloc memory context previously set by \ref msgb_set_talloc_ctx
 */
struct msgb *msgb_alloc(uint16_t size, const char *name)
{
	struct msgb *msg;

	msg = _talloc_zero(tall_msgb_ctx, sizeof(*msg) + size, name);

	if (!msg) {
		//LOGP(DRSL, LOGL_FATAL, "unable to allocate msgb\n");
		return NULL;
	}

	msg->data_len = size;
	msg->len = 0;
	msg->data = msg->_data;
	msg->head = msg->_data;
	msg->tail = msg->_data;

	return msg;
}

/*! \brief Release given message buffer
 * \param[in] m Message buffer to be free'd
 */
void msgb_free(struct msgb *m)
{
	talloc_free(m);
}

/*! \brief Enqueue message buffer to tail of a queue
 * \param[in] queue linked list header of queue
 * \param[in] msgb message buffer to be added to the queue
 *
 * The function will append the specified message buffer \a msg to the
 * queue implemented by \ref llist_head \a queue
 */
void msgb_enqueue(struct llist_head *queue, struct msgb *msg)
{
	llist_add_tail(&msg->list, queue);
}

/*! \brief Dequeue message buffer from head of queue
 * \param[in] queue linked list header of queue
 * \returns message buffer (if any) or NULL if queue empty
 *
 * The function will remove the first message buffer from the queue
 * implemented by 'ref llist_head \a queue.
 */
struct msgb *msgb_dequeue(struct llist_head *queue)
{
	struct llist_head *lh;

	if (llist_empty(queue))
		return NULL;

	lh = queue->next;
	llist_del(lh);
	
	return llist_entry(lh, struct msgb, list);
}

/*! \brief Re-set all message buffer pointers
 *  \param[in] m message buffer that is to be resetted
 *
 * This will re-set the various internal pointers into the underlying
 * message buffer, i.e. remvoe all headroom and treat the msgb as
 * completely empty.  It also initializes the control buffer to zero.
 */
void msgb_reset(struct msgb *msg)
{
	msg->len = 0;
	msg->data = msg->_data;
	msg->head = msg->_data;
	msg->tail = msg->_data;

	msg->trx = NULL;
	msg->lchan = NULL;
	msg->l2h = NULL;
	msg->l3h = NULL;
	msg->l4h = NULL;

	memset(&msg->cb, 0, sizeof(msg->cb));
}

/*! \brief get pointer to data section of message buffer
 *  \param[in] msg message buffer
 *  \returns pointer to data section of message buffer
 */
uint8_t *msgb_data(const struct msgb *msg)
{
	return msg->data;
}

/*! \brief get length of message buffer
 *  \param[in] msg message buffer
 *  \returns length of data section in message buffer
 */
uint16_t msgb_length(const struct msgb *msg)
{
	return msg->len;
}

/*! \brief Set the talloc context for \ref msgb_alloc
 *  \param[in] ctx talloc context to be used as root for msgb allocations
 */
void msgb_set_talloc_ctx(void *ctx)
{
	tall_msgb_ctx = ctx;
}

/*! @} */
