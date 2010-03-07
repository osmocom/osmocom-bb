/* Asynchronous part of GSM Layer 1 */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <debug.h>
#include <arm.h>

#include <osmocore/msgb.h>

#include <layer1/sync.h>
#include <layer1/mframe_sched.h>
#include <layer1/sched_gsmtime.h>
#include <layer1/l23_api.h>

extern const struct tdma_sched_item rach_sched_set_ul[];

/* When altering data structures used by L1 Sync part, we need to
 * make sure to temporarily disable IRQ/FIQ to keep data consistent */
static inline void l1a_lock_sync(void)
{
	arm_disable_interrupts();
}

static inline void l1a_unlock_sync(void)
{
	arm_enable_interrupts();
}

/* safely enable a message into the L1S TX queue */
void l1a_txq_msgb_enq(struct llist_head *queue, struct msgb *msg)
{
	l1a_lock_sync();
	msgb_enqueue(queue, msg);
	l1a_unlock_sync();
}

/* request a RACH request at the next multiframe T3 = fn51 */
void l1a_rach_req(uint8_t fn51, uint8_t ra)
{
	uint32_t fn_sched;

	l1a_lock_sync();
	l1s.rach.ra = ra;
	/* TODO: can we wrap here? I don't think so */
	fn_sched = l1s.current_time.fn - l1s.current_time.t3;
	fn_sched += fn51;
	sched_gsmtime(rach_sched_set_ul, fn_sched, 0);
	l1a_unlock_sync();
}

/* Enable a repeating multiframe task */
void l1a_mftask_enable(enum mframe_task task)
{
	/* we don't need locking here as L1S only reads mf_tasks */
	l1s.mf_tasks |= (1 << task);
}

/* Disable a repeating multiframe task */
void l1a_mftask_disable(enum mframe_task task)
{
	/* we don't need locking here as L1S only reads mf_tasks */
	l1s.mf_tasks &= ~(1 << task);
}

/* Initialize asynchronous part of Layer1 */
void l1a_init(void)
{
	l1a_l23api_init();
}
