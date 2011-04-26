#ifndef _L1_SCHED_GSMTIME_H
#define _L1_SCHED_GSMTIME_H

#include <stdint.h>
#include <osmocom/core/linuxlist.h>

struct sched_gsmtime_event {
	struct llist_head list;
	const struct tdma_sched_item *si;
	uint32_t fn;
	uint16_t p3;	/* parameter for TDMA scheduler */
};

/* initialize the GSMTIME scheduler */
void sched_gsmtime_init(void);

/* Scheduling of a single event at a givnen GSM time */
int sched_gsmtime(const struct tdma_sched_item *si, uint32_t fn, uint16_t p3);

/* execute all GSMTIME one-shot events pending for 'current_fn' */
int sched_gsmtime_execute(uint32_t current_fn);

void sched_gsmtime_reset(void);
#endif
