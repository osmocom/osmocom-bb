#pragma once

#include <stdint.h>
#include <time.h>

#include <osmocom/core/timer.h>

#define FRAME_DURATION_uS	4615

#define GSM_SUPERFRAME		(26 * 51)
#define GSM_HYPERFRAME		(2048 * GSM_SUPERFRAME)

/* TDMA frame number arithmetics */
#define TDMA_FN_SUM(a, b) \
	((a + b) % GSM_HYPERFRAME)
#define TDMA_FN_SUB(a, b) \
	((a + GSM_HYPERFRAME - b) % GSM_HYPERFRAME)
#define TDMA_FN_INC(fn) \
	(*fn = TDMA_FN_SUM(*fn, 1))
#define TDMA_FN_MIN(a, b) \
	(a < b ? a : b)
#define TDMA_FN_DIFF(a, b) \
	TDMA_FN_MIN(TDMA_FN_SUB(a, b), TDMA_FN_SUB(b, a))

enum tdma_sched_clck_state {
	SCH_CLCK_STATE_WAIT,
	SCH_CLCK_STATE_OK,
};

/* Forward structure declaration */
struct trx_sched;

/*! \brief One scheduler instance */
struct trx_sched {
	/*! \brief Clock state */
	enum tdma_sched_clck_state state;
	/*! \brief Local clock source */
	struct timespec clock;
	/*! \brief Count of processed frames */
	uint32_t fn_counter_proc;
	/*! \brief Local frame counter advance */
	uint32_t fn_counter_advance;
	/*! \brief Count of lost frames */
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
