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
#include <inttypes.h>

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
 * \param[in] msg message buffer to be added to the queue
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
 * implemented by \ref llist_head \a queue.
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
 *  \param[in] msg message buffer that is to be resetted
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

/*! \brief Copy an msgb.
 *
 *  This function allocates a new msgb, copies the data buffer of msg,
 *  and adjusts the pointers (incl l1h-l4h) accordingly. The cb part
 *  is not copied.
 *  \param[in] msg  The old msgb object
 *  \param[in] name Human-readable name to be associated with msgb
 */
struct msgb *msgb_copy(const struct msgb *msg, const char *name)
{
	struct msgb *new_msg;

	new_msg = msgb_alloc(msg->data_len, name);
	if (!new_msg)
		return NULL;

	/* copy data */
	memcpy(new_msg->_data, msg->_data, new_msg->data_len);

	/* copy header */
	new_msg->len = msg->len;
	new_msg->data += msg->data - msg->_data;
	new_msg->head += msg->head - msg->_data;
	new_msg->tail += msg->tail - msg->_data;

	if (msg->l1h)
		new_msg->l1h = new_msg->_data + (msg->l1h - msg->_data);
	if (msg->l2h)
		new_msg->l2h = new_msg->_data + (msg->l2h - msg->_data);
	if (msg->l3h)
		new_msg->l3h = new_msg->_data + (msg->l3h - msg->_data);
	if (msg->l4h)
		new_msg->l4h = new_msg->_data + (msg->l4h - msg->_data);

	return new_msg;
}

/*! \brief Resize an area within an msgb
 *
 *  This resizes a sub area of the msgb data and adjusts the pointers (incl
 *  l1h-l4h) accordingly. The cb part is not updated. If the area is extended,
 *  the contents of the extension is undefined. The complete sub area must be a
 *  part of [data,tail].
 *
 *  \param[inout] msg       The msgb object
 *  \param[in]    area      A pointer to the sub-area
 *  \param[in]    old_size  The old size of the sub-area
 *  \param[in]    new_size  The new size of the sub-area
 *  \returns 0 on success, -1 if there is not enough space to extend the area
 */
int msgb_resize_area(struct msgb *msg, uint8_t *area,
			    int old_size, int new_size)
{
	int rc;
	uint8_t *post_start = area + old_size;
	int pre_len = area - msg->data;
	int post_len = msg->len - old_size - pre_len;
	int delta_size = new_size - old_size;

	if (old_size < 0 || new_size < 0)
		MSGB_ABORT(msg, "Negative sizes are not allowed\n");
	if (area < msg->data || post_start > msg->tail)
		MSGB_ABORT(msg, "Sub area is not fully contained in the msg data\n");

	if (delta_size == 0)
		return 0;

	if (delta_size > 0) {
		rc = msgb_trim(msg, msg->len + delta_size);
		if (rc < 0)
			return rc;
	}

	memmove(area + new_size, area + old_size, post_len);

	if (msg->l1h >= post_start)
		msg->l1h += delta_size;
	if (msg->l2h >= post_start)
		msg->l2h += delta_size;
	if (msg->l3h >= post_start)
		msg->l3h += delta_size;
	if (msg->l4h >= post_start)
		msg->l4h += delta_size;

	if (delta_size < 0)
		msgb_trim(msg, msg->len + delta_size);

	return 0;
}


/*! \brief Return a (static) buffer containing a hexdump of the msg
 * \param[in] msg message buffer
 * \returns a pointer to a static char array
 */
const char *msgb_hexdump(const struct msgb *msg)
{
	static char buf[4100];
	int buf_offs = 0;
	int nchars;
	const unsigned char *start = msg->data;
	const unsigned char *lxhs[4];
	int i;

	lxhs[0] = msg->l1h;
	lxhs[1] = msg->l2h;
	lxhs[2] = msg->l3h;
	lxhs[3] = msg->l4h;

	for (i = 0; i < ARRAY_SIZE(lxhs); i++) {
		if (!lxhs[i])
			continue;

		if (lxhs[i] < msg->head)
			continue;
		if (lxhs[i] > msg->head + msg->data_len)
			continue;
		if (lxhs[i] > msg->tail)
			continue;
		if (lxhs[i] < msg->data || lxhs[i] > msg->tail) {
			nchars = snprintf(buf + buf_offs, sizeof(buf) - buf_offs,
					  "(L%d=data%+" PRIdPTR ") ",
					  i+1, lxhs[i] - msg->data);
			buf_offs += nchars;
			continue;
		}
		if (lxhs[i] < start) {
			nchars = snprintf(buf + buf_offs, sizeof(buf) - buf_offs,
					  "(L%d%+" PRIdPTR ") ", i+1,
					  start - lxhs[i]);
			buf_offs += nchars;
			continue;
		}
		nchars = snprintf(buf + buf_offs, sizeof(buf) - buf_offs,
				  "%s[L%d]> ",
				  osmo_hexdump(start, lxhs[i] - start),
				  i+1);
		if (nchars < 0 || nchars + buf_offs >= sizeof(buf))
			return "ERROR";

		buf_offs += nchars;
		start = lxhs[i];
	}
	nchars = snprintf(buf + buf_offs, sizeof(buf) - buf_offs,
			  "%s", osmo_hexdump(start, msg->tail - start));
	if (nchars < 0 || nchars + buf_offs >= sizeof(buf))
		return "ERROR";

	buf_offs += nchars;

	for (i = 0; i < ARRAY_SIZE(lxhs); i++) {
		if (!lxhs[i])
			continue;

		if (lxhs[i] < msg->head || lxhs[i] > msg->head + msg->data_len) {
			nchars = snprintf(buf + buf_offs, sizeof(buf) - buf_offs,
					  "(L%d out of range) ", i+1);
		} else if (lxhs[i] <= msg->data + msg->data_len &&
			   lxhs[i] > msg->tail) {
			nchars = snprintf(buf + buf_offs, sizeof(buf) - buf_offs,
					  "(L%d=tail%+" PRIdPTR ") ",
					  i+1, lxhs[i] - msg->tail);
		} else
			continue;

		if (nchars < 0 || nchars + buf_offs >= sizeof(buf))
			return "ERROR";
		buf_offs += nchars;
	}

	return buf;
}

/*! @} */
