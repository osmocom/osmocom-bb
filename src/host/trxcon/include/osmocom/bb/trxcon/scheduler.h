#pragma once

#include <stdint.h>
#include <time.h>

#include <osmocom/core/timer.h>
#include <osmocom/gsm/gsm0502.h>

enum l1sched_clck_state {
	L1SCHED_CLCK_ST_WAIT,
	L1SCHED_CLCK_ST_OK,
};

/* Forward structure declaration */
struct l1sched_state;

/*! One scheduler instance */
struct l1sched_state {
	/*! Clock state */
	enum l1sched_clck_state state;
	/*! Local clock source */
	struct timespec clock;
	/*! Count of processed frames */
	uint32_t fn_counter_proc;
	/*! Local frame counter advance */
	uint32_t fn_counter_advance;
	/*! Count of lost frames */
	uint32_t fn_counter_lost;
	/*! Frame callback timer */
	struct osmo_timer_list clock_timer;
	/*! Frame callback */
	void (*clock_cb)(struct l1sched_state *sched);
	/*! Private data (e.g. pointer to trx instance) */
	void *data;
};

int l1sched_clck_handle(struct l1sched_state *sched, uint32_t fn);
void l1sched_clck_reset(struct l1sched_state *sched);
