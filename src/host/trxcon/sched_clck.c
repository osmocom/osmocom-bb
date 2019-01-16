/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: clock synchronization
 *
 * (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
 * (C) 2015 by Alexander Chemeris <Alexander.Chemeris@fairwaves.co>
 * (C) 2015 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/fsm.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/timer_compat.h>
#include <osmocom/gsm/a5.h>

#include "scheduler.h"
#include "logging.h"
#include "trx_if.h"

#define MAX_FN_SKEW		50
#define TRX_LOSS_FRAMES	400

static void sched_clck_tick(void *data)
{
	struct trx_sched *sched = (struct trx_sched *) data;
	struct timespec tv_now, *tv_clock, elapsed;
	int64_t elapsed_us;
	const struct timespec frame_duration = { .tv_sec = 0, .tv_nsec = FRAME_DURATION_uS * 1000 };

	/* Check if transceiver is still alive */
	if (sched->fn_counter_lost++ == TRX_LOSS_FRAMES) {
		LOGP(DSCH, LOGL_DEBUG, "No more clock from transceiver\n");
		sched->state = SCH_CLCK_STATE_WAIT;

		return;
	}

	/* Get actual / previous frame time */
	osmo_clock_gettime(CLOCK_MONOTONIC, &tv_now);
	tv_clock = &sched->clock;

	timespecsub(&tv_now, tv_clock, &elapsed);
	elapsed_us = (elapsed.tv_sec * 1000000) + (elapsed.tv_nsec / 1000);

	/* If someone played with clock, or if the process stalled */
	if (elapsed_us > FRAME_DURATION_uS * MAX_FN_SKEW || elapsed_us < 0) {
		LOGP(DSCH, LOGL_NOTICE, "PC clock skew: "
			"elapsed uS %" PRId64 "\n", elapsed_us);

		sched->state = SCH_CLCK_STATE_WAIT;

		return;
	}

	/* Schedule next FN clock */
	while (elapsed_us > FRAME_DURATION_uS / 2) {
		timespecadd(tv_clock, &frame_duration, tv_clock);
		elapsed_us -= FRAME_DURATION_uS;

		sched->fn_counter_proc = TDMA_FN_INC(sched->fn_counter_proc);

		/* Call frame callback */
		if (sched->clock_cb)
			sched->clock_cb(sched);
	}

	osmo_timer_schedule(&sched->clock_timer, 0,
		FRAME_DURATION_uS - elapsed_us);
}

static void sched_clck_correct(struct trx_sched *sched,
	struct timespec *tv_now, uint32_t fn)
{
	sched->fn_counter_proc = fn;

	/* Call frame callback */
	if (sched->clock_cb)
		sched->clock_cb(sched);

	/* Schedule first FN clock */
	sched->clock = *tv_now;
	memset(&sched->clock_timer, 0, sizeof(sched->clock_timer));

	sched->clock_timer.cb = sched_clck_tick;
	sched->clock_timer.data = sched;
	osmo_timer_schedule(&sched->clock_timer, 0, FRAME_DURATION_uS);
}

int sched_clck_handle(struct trx_sched *sched, uint32_t fn)
{
	struct timespec tv_now, *tv_clock, elapsed;
	int64_t elapsed_us, elapsed_fn;

	/* Reset lost counter */
	sched->fn_counter_lost = 0;

	/* Get actual / previous frame time */
	osmo_clock_gettime(CLOCK_MONOTONIC, &tv_now);
	tv_clock = &sched->clock;

	/* If this is the first CLCK IND */
	if (sched->state == SCH_CLCK_STATE_WAIT) {
		sched_clck_correct(sched, &tv_now, fn);

		LOGP(DSCH, LOGL_DEBUG, "Initial clock received: fn=%u\n", fn);
		sched->state = SCH_CLCK_STATE_OK;

		return 0;
	}

	LOGP(DSCH, LOGL_NOTICE, "Clock indication: fn=%u\n", fn);

	osmo_timer_del(&sched->clock_timer);

	/* Calculate elapsed time / frames since last processed fn */
	timespecsub(&tv_now, tv_clock, &elapsed);
	elapsed_us = (elapsed.tv_sec * 1000000) + (elapsed.tv_nsec / 1000);
	elapsed_fn = TDMA_FN_SUB(fn, sched->fn_counter_proc);

	if (elapsed_fn >= 135774)
		elapsed_fn -= GSM_HYPERFRAME;

	/* Check for max clock skew */
	if (elapsed_fn > MAX_FN_SKEW || elapsed_fn < -MAX_FN_SKEW) {
		LOGP(DSCH, LOGL_NOTICE, "GSM clock skew: old fn=%u, "
			"new fn=%u\n", sched->fn_counter_proc, fn);

		sched_clck_correct(sched, &tv_now, fn);
		return 0;
	}

	LOGP(DSCH, LOGL_INFO, "GSM clock jitter: %" PRId64 "\n",
		elapsed_fn * FRAME_DURATION_uS - elapsed_us);

	/* Too many frames have been processed already */
	if (elapsed_fn < 0) {
		struct timespec duration;
		/**
		 * Set clock to the time or last FN should
		 * have been transmitted
		 */
		duration.tv_nsec = (0 - elapsed_fn) * FRAME_DURATION_uS * 1000;
		duration.tv_sec = duration.tv_nsec / 1000000000;
		duration.tv_nsec = duration.tv_nsec % 1000000000;
		timespecadd(&tv_now, &duration, tv_clock);

		/* Set time to the time our next FN has to be transmitted */
		osmo_timer_schedule(&sched->clock_timer, 0,
			FRAME_DURATION_uS * (1 - elapsed_fn));

		return 0;
	}

	/* Transmit what we still need to transmit */
	while (fn != sched->fn_counter_proc) {
		sched->fn_counter_proc = TDMA_FN_INC(sched->fn_counter_proc);

		/* Call frame callback */
		if (sched->clock_cb)
			sched->clock_cb(sched);
	}

	/* Schedule next FN to be transmitted */
	*tv_clock = tv_now;
	osmo_timer_schedule(&sched->clock_timer, 0, FRAME_DURATION_uS);

	return 0;
}

void sched_clck_reset(struct trx_sched *sched)
{
	/* Reset internal state */
	sched->state = SCH_CLCK_STATE_WAIT;

	/* Stop clock timer */
	osmo_timer_del(&sched->clock_timer);

	/* Flush counters */
	sched->fn_counter_proc = 0;
	sched->fn_counter_lost = 0;
}
