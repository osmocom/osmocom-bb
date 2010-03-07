#ifndef _L1_ASYNC_H
#define _L1_ASYNC_H

#include <layer1/mframe_sched.h>

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
