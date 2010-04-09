#ifndef _L1_ASYNC_H
#define _L1_ASYNC_H

#include <osmocore/msgb.h>

#include <layer1/mframe_sched.h>

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
void l1a_txq_msgb_enq(struct llist_head *queue, struct msgb *msg);

/* request a RACH request at the next multiframe T3 = fn51 */
void l1a_rach_req(uint8_t fn51, uint8_t ra);

/* Enable a repeating multiframe task */
void l1a_mftask_enable(enum mframe_task task);

/* Disable a repeating multiframe task */
void l1a_mftask_disable(enum mframe_task task);

/* Initialize asynchronous part of Layer1 */
void l1a_init(void);

#endif
