/* Synchronous part of GSM Layer 1: API to Layer2+ */

/* (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#include <stdint.h>
#include <stdio.h>

#include <comm/msgb.h>
#include <comm/sercomm.h>

#include <layer1/sync.h>
#include <l1a_l23_interface.h>

/* the size we will allocate struct msgb* for HDLC */
#define L3_MSG_SIZE (sizeof(struct l1_ccch_info_ind) + 4)
#define L3_MSG_HEAD 4

void l1_queue_for_l2(struct msgb *msg)
{
	/* forward via serial for now */
	sercomm_sendmsg(SC_DLCI_L1A_L23, msg);
}

struct msgb *l1_create_l2_msg(int msg_type, uint32_t fn, uint16_t snr)
{
	struct l1_info_dl *dl;
	struct msgb *msg;

	msg = msgb_alloc_headroom(L3_MSG_SIZE, L3_MSG_HEAD, "l1_burst");
	if (!msg) {
		while (1) {
			puts("OOPS. Out of buffers...\n");
		}

		return NULL;
	}

	dl = (struct l1_info_dl *) msgb_put(msg, sizeof(*dl));
	dl->msg_type = msg_type;
	/* FIXME: we may want to compute T1/T2/T3 in L23 */
	gsm_fn2gsmtime(&dl->time, fn);
	dl->snr[0] = snr;

	return msg;
}
