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

#include <osmocom/core/logging.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/timer_compat.h>

#include <osmocom/bb/l1sched/l1sched.h>
#include <osmocom/bb/l1sched/logging.h>

#define MAX_FN_SKEW		50
#define TRX_LOSS_FRAMES	400

static void l1sched_clck_tick(void *data)
{
	struct l1sched_state *sched = (struct l1sched_state *) data;
	struct timespec tv_now, *tv_clock, elapsed;
	int64_t elapsed_us;
	const struct timespec frame_duration = { .tv_sec = 0, .tv_nsec = GSM_TDMA_FN_DURATION_nS };

	/* Check if transceiver is still alive */
	if (sched->fn_counter_lost++ == TRX_LOSS_FRAMES) {
		LOGP_SCHEDC(sched, LOGL_NOTICE, "No more clock from transceiver\n");
		sched->clck_state = L1SCHED_CLCK_ST_WAIT;

		return;
	}

	/* Get actual / previous frame time */
	osmo_clock_gettime(CLOCK_MONOTONIC, &tv_now);
	tv_clock = &sched->clock;

	timespecsub(&tv_now, tv_clock, &elapsed);
	elapsed_us = (elapsed.tv_sec * 1000000) + (elapsed.tv_nsec / 1000);

	/* If someone played with clock, or if the process stalled */
	if (elapsed_us > GSM_TDMA_FN_DURATION_uS * MAX_FN_SKEW || elapsed_us < 0) {
		LOGP_SCHEDC(sched, LOGL_NOTICE, "PC clock skew: "
			    "elapsed uS %" PRId64 "\n", elapsed_us);

		sched->clck_state = L1SCHED_CLCK_ST_WAIT;

		return;
	}

	/* Schedule next FN clock */
	while (elapsed_us > GSM_TDMA_FN_DURATION_uS / 2) {
		timespecadd(tv_clock, &frame_duration, tv_clock);
		elapsed_us -= GSM_TDMA_FN_DURATION_uS;

		GSM_TDMA_FN_INC(sched->fn_counter_proc);

		/* Call frame callback */
		if (sched->clock_cb)
			sched->clock_cb(sched);
	}

	osmo_timer_schedule(&sched->clock_timer, 0,
		GSM_TDMA_FN_DURATION_uS - elapsed_us);
}

static void l1sched_clck_correct(struct l1sched_state *sched,
	struct timespec *tv_now, uint32_t fn)
{
	sched->fn_counter_proc = fn;

	/* Call frame callback */
	if (sched->clock_cb)
		sched->clock_cb(sched);

	/* Schedule first FN clock */
	sched->clock = *tv_now;
	memset(&sched->clock_timer, 0, sizeof(sched->clock_timer));

	sched->clock_timer.cb = l1sched_clck_tick;
	sched->clock_timer.data = sched;
	osmo_timer_schedule(&sched->clock_timer, 0, GSM_TDMA_FN_DURATION_uS);
}

int l1sched_clck_handle(struct l1sched_state *sched, uint32_t fn)
{
	struct timespec tv_now, *tv_clock, elapsed;
	int64_t elapsed_us, elapsed_fn;

	/* Reset lost counter */
	sched->fn_counter_lost = 0;

	/* Get actual / previous frame time */
	osmo_clock_gettime(CLOCK_MONOTONIC, &tv_now);
	tv_clock = &sched->clock;

	/* If this is the first CLCK IND */
	if (sched->clck_state == L1SCHED_CLCK_ST_WAIT) {
		l1sched_clck_correct(sched, &tv_now, fn);

		LOGP_SCHEDC(sched, LOGL_NOTICE, "Initial clock received: fn=%u\n", fn);
		sched->clck_state = L1SCHED_CLCK_ST_OK;

		return 0;
	}

	LOGP_SCHEDC(sched, LOGL_DEBUG, "Clock indication: fn=%u\n", fn);

	osmo_timer_del(&sched->clock_timer);

	/* Calculate elapsed time / frames since last processed fn */
	timespecsub(&tv_now, tv_clock, &elapsed);
	elapsed_us = (elapsed.tv_sec * 1000000) + (elapsed.tv_nsec / 1000);
	elapsed_fn = GSM_TDMA_FN_SUB(fn, sched->fn_counter_proc);

	if (elapsed_fn >= 135774)
		elapsed_fn -= GSM_TDMA_HYPERFRAME;

	/* Check for max clock skew */
	if (elapsed_fn > MAX_FN_SKEW || elapsed_fn < -MAX_FN_SKEW) {
		LOGP_SCHEDC(sched, LOGL_NOTICE, "GSM clock skew: old fn=%u, "
			    "new fn=%u\n", sched->fn_counter_proc, fn);

		l1sched_clck_correct(sched, &tv_now, fn);
		return 0;
	}

	LOGP_SCHEDC(sched, LOGL_DEBUG, "GSM clock jitter: %" PRId64 "\n",
		    elapsed_fn * GSM_TDMA_FN_DURATION_uS - elapsed_us);

	/* Too many frames have been processed already */
	if (elapsed_fn < 0) {
		struct timespec duration;
		/**
		 * Set clock to the time or last FN should
		 * have been transmitted
		 */
		duration.tv_nsec = (0 - elapsed_fn) * GSM_TDMA_FN_DURATION_nS;
		duration.tv_sec = duration.tv_nsec / 1000000000;
		duration.tv_nsec = duration.tv_nsec % 1000000000;
		timespecadd(&tv_now, &duration, tv_clock);

		/* Set time to the time our next FN has to be transmitted */
		osmo_timer_schedule(&sched->clock_timer, 0,
			GSM_TDMA_FN_DURATION_uS * (1 - elapsed_fn));

		return 0;
	}

	/* Transmit what we still need to transmit */
	while (fn != sched->fn_counter_proc) {
		GSM_TDMA_FN_INC(sched->fn_counter_proc);

		/* Call frame callback */
		if (sched->clock_cb)
			sched->clock_cb(sched);
	}

	/* Schedule next FN to be transmitted */
	*tv_clock = tv_now;
	osmo_timer_schedule(&sched->clock_timer, 0, GSM_TDMA_FN_DURATION_uS);

	return 0;
}

void l1sched_clck_reset(struct l1sched_state *sched)
{
	/* Reset internal state */
	sched->clck_state = L1SCHED_CLCK_ST_WAIT;

	/* Stop clock timer */
	osmo_timer_del(&sched->clock_timer);

	/* Flush counters */
	sched->fn_counter_proc = 0;
	sched->fn_counter_lost = 0;
}
