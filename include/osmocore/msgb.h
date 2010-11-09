#ifndef _MSGB_H
#define _MSGB_H

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
#include "linuxlist.h"

#define MSGB_DEBUG

struct msgb {
	struct llist_head list;

	/* Part of which TRX logical channel we were received / transmitted */
	/* FIXME: move them into the control buffer */
	struct gsm_bts_trx *trx;
	struct gsm_lchan *lchan;

	/* the Layer1 header (if any) */
	unsigned char *l1h;
	/* the A-bis layer 2 header: OML, RSL(RLL), NS */
	unsigned char *l2h;
	/* the layer 3 header. For OML: FOM; RSL: 04.08; GPRS: BSSGP */
	unsigned char *l3h;
	/* the layer 4 header */
	unsigned char *l4h;

	/* the 'control buffer', large enough to contain 5 pointers */
	unsigned long cb[5];

	uint16_t data_len;
	uint16_t len;

	unsigned char *head;
	unsigned char *tail;
	unsigned char *data;
	unsigned char _data[0];
};

extern struct msgb *msgb_alloc(uint16_t size, const char *name);
extern void msgb_free(struct msgb *m);
extern void msgb_enqueue(struct llist_head *queue, struct msgb *msg);
extern struct msgb *msgb_dequeue(struct llist_head *queue);
extern void msgb_reset(struct msgb *m);

#ifdef MSGB_DEBUG
#include <osmocore/panic.h>
#include <osmocore/gsm_utils.h>
#define MSGB_ABORT(msg, fmt, args ...) do {		\
	generate_backtrace();				\
	osmo_panic("msgb(%p): " fmt, msg, ## args);	\
	} while(0)
#else
#define MSGB_ABORT(msg, fmt, args ...)
#endif

#define msgb_l1(m)	((void *)(m->l1h))
#define msgb_l2(m)	((void *)(m->l2h))
#define msgb_l3(m)	((void *)(m->l3h))
#define msgb_sms(m)	((void *)(m->l4h))

static inline unsigned int msgb_l1len(const struct msgb *msgb)
{
	return msgb->tail - (uint8_t *)msgb_l1(msgb);
}

static inline unsigned int msgb_l2len(const struct msgb *msgb)
{
	return msgb->tail - (uint8_t *)msgb_l2(msgb);
}

static inline unsigned int msgb_l3len(const struct msgb *msgb)
{
	return msgb->tail - (uint8_t *)msgb_l3(msgb);
}

static inline unsigned int msgb_headlen(const struct msgb *msgb)
{
	return msgb->len - msgb->data_len;
}

static inline int msgb_tailroom(const struct msgb *msgb)
{
	return (msgb->head + msgb->data_len) - msgb->tail;
}

static inline int msgb_headroom(const struct msgb *msgb)
{
	return (msgb->data - msgb->head);
}

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
static inline void msgb_put_u8(struct msgb *msgb, uint8_t word)
{
	uint8_t *space = msgb_put(msgb, 1);
	space[0] = word & 0xFF;
}
static inline void msgb_put_u16(struct msgb *msgb, uint16_t word)
{
	uint8_t *space = msgb_put(msgb, 2);
	space[0] = word >> 8 & 0xFF;
	space[1] = word & 0xFF;
}
static inline void msgb_put_u32(struct msgb *msgb, uint32_t word)
{
	uint8_t *space = msgb_put(msgb, 4);
	space[0] = word >> 24 & 0xFF;
	space[1] = word >> 16 & 0xFF;
	space[2] = word >> 8 & 0xFF;
	space[3] = word & 0xFF;
}
static inline unsigned char *msgb_get(struct msgb *msgb, unsigned int len)
{
	unsigned char *tmp = msgb->data;
	msgb->data += len;
	msgb->len -= len;
	return tmp;
}
static inline uint8_t msgb_get_u8(struct msgb *msgb)
{
	uint8_t *space = msgb_get(msgb, 1);
	return space[0];
}
static inline uint16_t msgb_get_u16(struct msgb *msgb)
{
	uint8_t *space = msgb_get(msgb, 2);
	return space[0] << 8 | space[1];
}
static inline uint32_t msgb_get_u32(struct msgb *msgb)
{
	uint8_t *space = msgb_get(msgb, 4);
	return space[0] << 24 | space[1] << 16 | space[2] << 8 | space[3];
}
static inline unsigned char *msgb_push(struct msgb *msgb, unsigned int len)
{
	if (msgb_headroom(msgb) < (int) len)
		MSGB_ABORT(msgb, "Not enough headroom msgb_push (%u < %u)\n",
			   msgb_headroom(msgb), len);
	msgb->data -= len;
	msgb->len += len;
	return msgb->data;
}
static inline unsigned char *msgb_pull(struct msgb *msgb, unsigned int len)
{
	msgb->len -= len;
	return msgb->data += len;
}

/* increase the headroom of an empty msgb, reducing the tailroom */
static inline void msgb_reserve(struct msgb *msg, int len)
{
	msg->data += len;
	msg->tail += len;
}

static inline struct msgb *msgb_alloc_headroom(int size, int headroom,
						const char *name)
{
	struct msgb *msg = msgb_alloc(size, name);
	if (msg)
		msgb_reserve(msg, headroom);
	return msg;
}

/* non inline functions to ease binding */
uint8_t *msgb_data(const struct msgb *msg);
uint16_t msgb_length(const struct msgb *msg);


#endif /* _MSGB_H */
