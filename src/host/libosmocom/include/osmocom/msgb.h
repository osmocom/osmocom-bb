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

#include <sys/types.h>
#include "linuxlist.h"

struct bts_link;

struct msgb {
	struct llist_head list;

	/* ptr to the physical E1 link to the BTS(s) */
	struct gsm_bts_link *bts_link;

	/* Part of which TRX logical channel we were received / transmitted */
	struct gsm_bts_trx *trx;
	struct gsm_lchan *lchan;

	unsigned char *l2h;
	unsigned char *l3h;
	unsigned char *smsh;

	u_int16_t data_len;
	u_int16_t len;

	unsigned char *head;
	unsigned char *tail;
	unsigned char *data;
	unsigned char _data[0];
};

extern struct msgb *msgb_alloc(u_int16_t size, const char *name);
extern void msgb_free(struct msgb *m);
extern void msgb_enqueue(struct llist_head *queue, struct msgb *msg);
extern struct msgb *msgb_dequeue(struct llist_head *queue);
extern void msgb_reset(struct msgb *m);

#define msgb_l2(m)	((void *)(m->l2h))
#define msgb_l3(m)	((void *)(m->l3h))
#define msgb_sms(m)	((void *)(m->smsh))

static inline unsigned int msgb_l2len(const struct msgb *msgb)
{
	return msgb->tail - (u_int8_t *)msgb_l2(msgb);
}

static inline unsigned int msgb_l3len(const struct msgb *msgb)
{
	return msgb->tail - (u_int8_t *)msgb_l3(msgb);
}

static inline unsigned int msgb_headlen(const struct msgb *msgb)
{
	return msgb->len - msgb->data_len;
}
static inline unsigned char *msgb_put(struct msgb *msgb, unsigned int len)
{
	unsigned char *tmp = msgb->tail;
	msgb->tail += len;
	msgb->len += len;
	return tmp;
}
static inline unsigned char *msgb_push(struct msgb *msgb, unsigned int len)
{
	msgb->data -= len;
	msgb->len += len;
	return msgb->data;
}
static inline unsigned char *msgb_pull(struct msgb *msgb, unsigned int len)
{
	msgb->len -= len;
	return msgb->data += len;
}
static inline int msgb_tailroom(const struct msgb *msgb)
{
	return (msgb->data + msgb->data_len) - msgb->tail;
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

#endif /* _MSGB_H */
