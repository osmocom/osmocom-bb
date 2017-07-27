#pragma once

#include <stdint.h>
#include <time.h>

#include <osmocom/core/timer.h>

#define GSM_SUPERFRAME		(26 * 51)
#define GSM_HYPERFRAME		(2048 * GSM_SUPERFRAME)

enum tdma_sched_clck_state {
	SCH_CLCK_STATE_WAIT,
	SCH_CLCK_STATE_OK,
};

/* Forward structure declaration */
struct trx_sched;

/*! \brief One scheduler instance */
struct trx_sched {
	/*! \brief Clock state */
	uint8_t state;
	/*! \brief Local clock source */
	struct timeval clock;
	/*! \brief Count of processed frames */
	uint32_t fn_counter_proc;
	/*! \brief Frame counter */
	uint32_t fn_counter_lost;
	/*! \brief Frame callback timer */
	struct osmo_timer_list clock_timer;
	/*! \brief Frame callback */
	void (*clock_cb)(struct trx_sched *sched);
	/*! \brief Private data (e.g. pointer to trx instance) */
	void *data;
};

int sched_clck_handle(struct trx_sched *sched, uint32_t fn);
void sched_clck_reset(struct trx_sched *sched);
