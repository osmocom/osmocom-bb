#ifndef _L1_SCHED_GSMTIME_H
#define _L1_SCHED_GSMTIME_H

#include <stdint.h>
#include <linuxlist.h>

struct sched_gsmtime_event {
	struct llist_head list;
	struct tdma_sched_item *si;
	uint32_t fn;
};

/* initialize the GSMTIME scheduler */
void sched_gsmtime_init(void);

/* Scheduling of a single event at a givnen GSM time */
int sched_gsmtime(struct tdma_sched_item *si, uint32_t fn);

/* execute all GSMTIME one-shot events pending for 'current_fn' */
int sched_gsmtime_execute(uint32_t current_fn);

#endif
