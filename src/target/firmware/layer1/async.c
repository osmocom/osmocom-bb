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
#include <asm/system.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

#include <layer1/sync.h>
#include <layer1/async.h>
#include <layer1/mframe_sched.h>
#include <layer1/sched_gsmtime.h>
#include <layer1/l23_api.h>
#include <calypso/l1_environment.h>

extern const struct tdma_sched_item rach_sched_set_ul[];

/* safely enable a message into the L1S TX queue */
void l1a_txq_msgb_enq(struct llist_head *queue, struct msgb *msg)
{
	unsigned long flags;

	local_firq_save(flags);
	msgb_enqueue(queue, msg);
	local_irq_restore(flags);
}

void l1a_meas_msgb_set(struct msgb *msg)
{
	unsigned long flags;

	local_firq_save(flags);
	if (l1s.tx_meas)
		msgb_free(l1s.tx_meas);
	l1s.tx_meas = msg;
	local_irq_restore(flags);
}

/* safely count messages in the L1S TX queue */
int l1a_txq_msgb_count(struct llist_head *queue)
{
	unsigned long flags;
	int num = 0;
	struct llist_head *le;

	local_firq_save(flags);
	llist_for_each(le, queue)
		num++;
	local_irq_restore(flags);

	return num;
}

/* safely flush all pending msgb */
void l1a_txq_msgb_flush(struct llist_head *queue)
{
	struct msgb *msg;
	unsigned long flags;

	local_firq_save(flags);
	while ((msg = msgb_dequeue(queue)))
		msgb_free(msg);
	local_irq_restore(flags);
}

/* Enable a repeating multiframe task */
void l1a_mftask_enable(enum mframe_task task)
{
	/* we don't need locking here as L1S only reads mframe.tasks */
	mframe_enable(task);
}

/* Disable a repeating multiframe task */
void l1a_mftask_disable(enum mframe_task task)
{
	/* we don't need locking here as L1S only reads mframe.tasks */
	mframe_disable(task);
}

/* Set the mask for repeating multiframe tasks */
void l1a_mftask_set(uint32_t tasks)
{
	/* we don't need locking here as L1S only reads mframe.tasks */
	mframe_set(tasks);
}

/* Set TCH mode */
uint8_t l1a_tch_mode_set(uint8_t mode)
{
	switch (mode) {
	case GSM48_CMODE_SPEECH_V1:
	case GSM48_CMODE_SPEECH_EFR:
		l1s.tch_mode = mode;
		break;
	default:
		l1s.tch_mode = GSM48_CMODE_SIGN;
	}

	return l1s.tch_mode;
}

/* Set Audio routing mode */
uint8_t l1a_audio_mode_set(uint8_t mode)
{
	l1s.audio_mode = mode;
	return mode;
}

/* Initialize asynchronous part of Layer1 */
void l1a_init(void)
{
	l1a_l23api_init();
}

/* Execute pending L1A completions */
void l1a_compl_execute(void)
{
	unsigned long flags;
	unsigned int scheduled;
	unsigned int i;

	/* get and reset the currently scheduled tasks */
	local_firq_save(flags);
	scheduled = l1s.scheduled_compl;
	l1s.scheduled_compl = 0;
	local_irq_restore(flags);

	/* Iterate over list of scheduled completions, call their
	 * respective completion handler */
	for (i = 0; i < 32; i++) {
		if (!(scheduled & (1 << i)))
			continue;
		/* call completion function */
		l1s.completion[i](i);
	}
}
