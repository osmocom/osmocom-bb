#pragma once

/* (C) 2008 by Harald Welte <laforge@gnumonks.org>
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

#include <stdint.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/bits.h>

/*! \defgroup msgb Message buffers
 *  @{
 */

/*! \file msgb.h
 *  \brief Osmocom message buffers
 * The Osmocom message buffers are modelled after the 'struct skb'
 * inside the Linux kernel network stack.  As they exist in userspace,
 * they are much simplified.  However, terminology such as headroom,
 * tailroom, push/pull/put etc. remains the same.
 */

#define MSGB_DEBUG

/*! \brief Osmocom message buffer */
struct msgb {
	struct llist_head list; /*!< \brief linked list header */


	/* Part of which TRX logical channel we were received / transmitted */
	/* FIXME: move them into the control buffer */
	union {
		void *dst; /*!< \brief reference of origin/destination */
		struct gsm_bts_trx *trx;
	};
	struct gsm_lchan *lchan; /*!< \brief logical channel */

	unsigned char *l1h; /*!< \brief pointer to Layer1 header (if any) */
	unsigned char *l2h; /*!< \brief pointer to A-bis layer 2 header: OML, RSL(RLL), NS */
	unsigned char *l3h; /*!< \brief pointer to Layer 3 header. For OML: FOM; RSL: 04.08; GPRS: BSSGP */
	unsigned char *l4h; /*!< \brief pointer to layer 4 header */

	unsigned long cb[5]; /*!< \brief control buffer */

	uint16_t data_len;   /*!< \brief length of underlying data array */
	uint16_t len;	     /*!< \brief length of bytes used in msgb */

	unsigned char *head;	/*!< \brief start of underlying memory buffer */
	unsigned char *tail;	/*!< \brief end of message in buffer */
	unsigned char *data;	/*!< \brief start of message in buffer */
	unsigned char _data[0]; /*!< \brief optional immediate data array */
};

extern struct msgb *msgb_alloc(uint16_t size, const char *name);
extern void msgb_free(struct msgb *m);
extern void msgb_enqueue(struct llist_head *queue, struct msgb *msg);
extern struct msgb *msgb_dequeue(struct llist_head *queue);
extern void msgb_reset(struct msgb *m);
uint16_t msgb_length(const struct msgb *msg);
extern const char *msgb_hexdump(const struct msgb *msg);
extern int msgb_resize_area(struct msgb *msg, uint8_t *area,
	int old_size, int new_size);
extern struct msgb *msgb_copy(const struct msgb *msg, const char *name);
static int msgb_test_invariant(const struct msgb *msg) __attribute__((pure));

#ifdef MSGB_DEBUG
#include <osmocom/core/panic.h>
#define MSGB_ABORT(msg, fmt, args ...) do {		\
	osmo_panic("msgb(%p): " fmt, msg, ## args);	\
	} while(0)
#else
#define MSGB_ABORT(msg, fmt, args ...)
#endif

/*! \brief obtain L1 header of msgb */
#define msgb_l1(m)	((void *)(m->l1h))
/*! \brief obtain L2 header of msgb */
#define msgb_l2(m)	((void *)(m->l2h))
/*! \brief obtain L3 header of msgb */
#define msgb_l3(m)	((void *)(m->l3h))
/*! \brief obtain SMS header of msgb */
#define msgb_sms(m)	((void *)(m->l4h))

/*! \brief determine length of L1 message
 *  \param[in] msgb message buffer
 *  \returns size of L1 message in bytes
 *
 * This function computes the number of bytes between the tail of the
 * message and the layer 1 header.
 */
static inline unsigned int msgb_l1len(const struct msgb *msgb)
{
	return msgb->tail - (uint8_t *)msgb_l1(msgb);
}

/*! \brief determine length of L2 message
 *  \param[in] msgb message buffer
 *  \returns size of L2 message in bytes
 *
 * This function computes the number of bytes between the tail of the
 * message and the layer 2 header.
 */
static inline unsigned int msgb_l2len(const struct msgb *msgb)
{
	return msgb->tail - (uint8_t *)msgb_l2(msgb);
}

/*! \brief determine length of L3 message
 *  \param[in] msgb message buffer
 *  \returns size of L3 message in bytes
 *
 * This function computes the number of bytes between the tail of the
 * message and the layer 3 header.
 */
static inline unsigned int msgb_l3len(const struct msgb *msgb)
{
	return msgb->tail - (uint8_t *)msgb_l3(msgb);
}

/*! \brief determine the length of the header
 *  \param[in] msgb message buffer
 *  \returns number of bytes between start of buffer and start of msg
 *
 * This function computes the length difference between the underlying
 * data buffer and the used section of the \a msgb.
 */
static inline unsigned int msgb_headlen(const struct msgb *msgb)
{
	return msgb->len - msgb->data_len;
}

/*! \brief determine how much tail room is left in msgb
 *  \param[in] msgb message buffer
 *  \returns number of bytes remaining at end of msgb
 *
 * This function computes the amount of octets left in the underlying
 * data buffer after the end of the message.
 */
static inline int msgb_tailroom(const struct msgb *msgb)
{
	return (msgb->head + msgb->data_len) - msgb->tail;
}

/*! \brief determine the amount of headroom in msgb
 *  \param[in] msgb message buffer
 *  \returns number of bytes left ahead of message start in msgb
 *
 * This function computes the amount of bytes left in the underlying
 * data buffer before the start of the actual message.
 */
static inline int msgb_headroom(const struct msgb *msgb)
{
	return (msgb->data - msgb->head);
}

/*! \brief append data to end of message buffer
 *  \param[in] msgb message buffer
 *  \param[in] len number of bytes to append to message
 *  \returns pointer to start of newly-appended data
 *
 * This function will move the \a tail pointer of the message buffer \a
 * len bytes further, thus enlarging the message by \a len bytes.
 *
 * The return value is a pointer to start of the newly added section at
 * the end of the message and can be used for actually filling/copying
 * data into it.
 */
static inline unsigned char *msgb_put(struct msgb *msgb, unsigned int len)
{
	unsigned char *tmp = msgb->tail;
	if (msgb_tailroom(msgb) < (int) len)
		MSGB_ABORT(msgb, "Not enough tailroom msgb_push (%u < %u)\n",
			   msgb_tailroom(msgb), len);
	msgb->tail += len;
	msgb->len += len;
	return tmp;
}

/*! \brief append a uint8 value to the end of the message
 *  \param[in] msgb message buffer
 *  \param[in] word unsigned 8bit byte to be appended
 */
static inline void msgb_put_u8(struct msgb *msgb, uint8_t word)
{
	uint8_t *space = msgb_put(msgb, 1);
	space[0] = word & 0xFF;
}

/*! \brief append a uint16 value to the end of the message
 *  \param[in] msgb message buffer
 *  \param[in] word unsigned 16bit byte to be appended
 */
static inline void msgb_put_u16(struct msgb *msgb, uint16_t word)
{
	uint8_t *space = msgb_put(msgb, 2);
	osmo_store16be(word, space);
}

/*! \brief append a uint32 value to the end of the message
 *  \param[in] msgb message buffer
 *  \param[in] word unsigned 32bit byte to be appended
 */
static inline void msgb_put_u32(struct msgb *msgb, uint32_t word)
{
	uint8_t *space = msgb_put(msgb, 4);
	osmo_store32be(word, space);
}

/*! \brief remove data from end of message
 *  \param[in] msgb message buffer
 *  \param[in] len number of bytes to remove from end
 */
static inline unsigned char *msgb_get(struct msgb *msgb, unsigned int len)
{
	unsigned char *tmp = msgb->tail - len;
	if (msgb_length(msgb) < len)
		MSGB_ABORT(msgb, "msgb too small to get %u (len %u)\n",
			   len, msgb_length(msgb));
	msgb->tail -= len;
	msgb->len -= len;
	return tmp;
}

/*! \brief remove uint8 from end of message
 *  \param[in] msgb message buffer
 *  \returns 8bit value taken from end of msgb
 */
static inline uint8_t msgb_get_u8(struct msgb *msgb)
{
	uint8_t *space = msgb_get(msgb, 1);
	return space[0];
}

/*! \brief remove uint16 from end of message
 *  \param[in] msgb message buffer
 *  \returns 16bit value taken from end of msgb
 */
static inline uint16_t msgb_get_u16(struct msgb *msgb)
{
	uint8_t *space = msgb_get(msgb, 2);
	return osmo_load16be(space);
}

/*! \brief remove uint32 from end of message
 *  \param[in] msgb message buffer
 *  \returns 32bit value taken from end of msgb
 */
static inline uint32_t msgb_get_u32(struct msgb *msgb)
{
	uint8_t *space = msgb_get(msgb, 4);
	return osmo_load32be(space);
}

/*! \brief prepend (push) some data to start of message
 *  \param[in] msgb message buffer
 *  \param[in] len number of bytes to pre-pend
 *  \returns pointer to newly added portion at start of \a msgb
 *
 * This function moves the \a data pointer of the \ref msgb further
 * to the front (by \a len bytes), thereby enlarging the message by \a
 * len bytes.
 *
 * The return value is a pointer to the newly added section in the
 * beginning of the message.  It can be used to fill/copy data into it.
 */
static inline unsigned char *msgb_push(struct msgb *msgb, unsigned int len)
{
	if (msgb_headroom(msgb) < (int) len)
		MSGB_ABORT(msgb, "Not enough headroom msgb_push (%u < %u)\n",
			   msgb_headroom(msgb), len);
	msgb->data -= len;
	msgb->len += len;
	return msgb->data;
}

/*! \brief remove (pull) a header from the front of the message buffer
 *  \param[in] msgb message buffer
 *  \param[in] len number of octets to be pulled
 *  \returns pointer to new start of msgb
 *
 * This function moves the \a data pointer of the \ref msgb further back
 * in the message, thereby shrinking the size of the message by \a len
 * bytes.
 */
static inline unsigned char *msgb_pull(struct msgb *msgb, unsigned int len)
{
	msgb->len -= len;
	return msgb->data += len;
}

/*! \brief remove (pull) all headers in front of l3h from the message buffer.
 *  \param[in] msgb message buffer with a valid l3h
 *  \returns pointer to new start of msgb (l3h)
 *
 * This function moves the \a data pointer of the \ref msgb further back
 * in the message, thereby shrinking the size of the message.
 * l1h and l2h will be cleared.
 */
static inline unsigned char *msgb_pull_to_l3(struct msgb *msg)
{
	unsigned char *ret = msgb_pull(msg, msg->l3h - msg->data);
	msg->l1h = msg->l2h = NULL;
	return ret;
}

/*! \brief remove uint8 from front of message
 *  \param[in] msgb message buffer
 *  \returns 8bit value taken from end of msgb
 */
static inline uint8_t msgb_pull_u8(struct msgb *msgb)
{
	uint8_t *space = msgb_pull(msgb, 1) - 1;
	return space[0];
}

/*! \brief remove uint16 from front of message
 *  \param[in] msgb message buffer
 *  \returns 16bit value taken from end of msgb
 */
static inline uint16_t msgb_pull_u16(struct msgb *msgb)
{
	uint8_t *space = msgb_pull(msgb, 2) - 2;
	return osmo_load16be(space);
}

/*! \brief remove uint32 from front of message
 *  \param[in] msgb message buffer
 *  \returns 32bit value taken from end of msgb
 */
static inline uint32_t msgb_pull_u32(struct msgb *msgb)
{
	uint8_t *space = msgb_pull(msgb, 4) - 4;
	return osmo_load32be(space);
}

/*! \brief Increase headroom of empty msgb, reducing the tailroom
 *  \param[in] msg message buffer
 *  \param[in] len amount of extra octets to be reserved as headroom
 *
 * This function reserves some memory at the beginning of the underlying
 * data buffer.  The idea is to reserve space in case further headers
 * have to be pushed to the \ref msgb during further processing.
 *
 * Calling this function leads to undefined reusults if it is called on
 * a non-empty \ref msgb.
 */
static inline void msgb_reserve(struct msgb *msg, int len)
{
	msg->data += len;
	msg->tail += len;
}

/*! \brief Trim the msgb to a given absolute length
 *  \param[in] msg message buffer
 *  \param[in] len new total length of buffer
 *  \returns 0 in case of success, negative in case of error
 */
static inline int msgb_trim(struct msgb *msg, int len)
{
	if (len < 0)
		MSGB_ABORT(msg, "Negative length is not allowed\n");
	if (len > msg->data_len)
		return -1;

	msg->len = len;
	msg->tail = msg->data + len;

	return 0;
}

/*! \brief Trim the msgb to a given layer3 length
 *  \param[in] msg message buffer
 *  \param[in] l3len new layer3 length
 *  \returns 0 in case of success, negative in case of error
 */
static inline int msgb_l3trim(struct msgb *msg, int l3len)
{
	return msgb_trim(msg, (msg->l3h - msg->data) + l3len);
}

/*! \brief Allocate message buffer with specified headroom
 *  \param[in] size size in bytes, including headroom
 *  \param[in] headroom headroom in bytes
 *  \param[in] name human-readable name
 *  \returns allocated message buffer with specified headroom
 *
 * This function is a convenience wrapper around \ref msgb_alloc
 * followed by \ref msgb_reserve in order to create a new \ref msgb with
 * user-specified amount of headroom.
 */
static inline struct msgb *msgb_alloc_headroom(int size, int headroom,
						const char *name)
{
	osmo_static_assert(size > headroom, headroom_bigger);

	struct msgb *msg = msgb_alloc(size, name);
	if (msg)
		msgb_reserve(msg, headroom);
	return msg;
}

/*! \brief Check a message buffer for consistency
 *  \param[in] msg message buffer
 *  \returns 0 (false) if inconsistent, != 0 (true) otherwise
 */
static inline int msgb_test_invariant(const struct msgb *msg)
{
	const unsigned char *lbound;
	if (!msg || !msg->data || !msg->tail ||
	    (msg->data + msg->len != msg->tail) ||
	    (msg->data < msg->head) ||
	    (msg->tail > msg->head + msg->data_len))
		return 0;

	lbound = msg->head;

	if (msg->l1h) {
		if (msg->l1h < lbound)
			return 0;
		lbound = msg->l1h;
	}
	if (msg->l2h) {
		if (msg->l2h < lbound)
			return 0;
		lbound = msg->l2h;
	}
	if (msg->l3h) {
		if (msg->l3h < lbound)
			return 0;
		lbound = msg->l3h;
	}
	if (msg->l4h) {
		if (msg->l4h < lbound)
			return 0;
		lbound = msg->l4h;
	}

	return lbound <= msg->head +  msg->data_len;
}

/* non inline functions to ease binding */

uint8_t *msgb_data(const struct msgb *msg);
void msgb_set_talloc_ctx(void *ctx);

/*! @} */
