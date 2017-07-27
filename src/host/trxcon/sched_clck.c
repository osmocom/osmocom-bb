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
#include <string.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/bits.h>
#include <osmocom/core/fsm.h>
#include <osmocom/gsm/a5.h>

#include "scheduler.h"
#include "logging.h"
#include "trx_if.h"
#include "trxcon.h"

#define FRAME_DURATION_uS	4615
#define MAX_FN_SKEW		50
#define TRX_LOSS_FRAMES	400

static void sched_clck_tick(void *data)
{
	struct trx_sched *sched = (struct trx_sched *) data;
	struct timeval tv_now, *tv_clock;
	int32_t elapsed;

	/* Check if transceiver is still alive */
	if (sched->fn_counter_lost++ == TRX_LOSS_FRAMES) {
		LOGP(DSCH, LOGL_NOTICE, "No more clock from transceiver\n");
		sched->state = SCH_CLCK_STATE_WAIT;

		return;
	}

	/* Get actual / previous frame time */
	gettimeofday(&tv_now, NULL);
	tv_clock = &sched->clock;

	elapsed = (tv_now.tv_sec - tv_clock->tv_sec) * 1000000
		+ (tv_now.tv_usec - tv_clock->tv_usec);

	/* If someone played with clock, or if the process stalled */
	if (elapsed > FRAME_DURATION_uS * MAX_FN_SKEW || elapsed < 0) {
		LOGP(DSCH, LOGL_NOTICE, "PC clock skew: "
			"elapsed uS %d\n", elapsed);

		sched->state = SCH_CLCK_STATE_WAIT;

		return;
	}

	/* Schedule next FN clock */
	while (elapsed > FRAME_DURATION_uS / 2) {
		tv_clock->tv_usec += FRAME_DURATION_uS;
		elapsed -= FRAME_DURATION_uS;

		if (tv_clock->tv_usec >= 1000000) {
			tv_clock->tv_sec++;
			tv_clock->tv_usec -= 1000000;
		}

		sched->fn_counter_proc = (sched->fn_counter_proc + 1)
			% GSM_HYPERFRAME;

		/* Call frame callback */
		if (sched->clock_cb)
			sched->clock_cb(sched);
	}

	osmo_timer_schedule(&sched->clock_timer, 0,
		FRAME_DURATION_uS - elapsed);
}

static void sched_clck_correct(struct trx_sched *sched,
	struct timeval *tv_now, uint32_t fn)
{
	sched->fn_counter_proc = fn;

	/* Call frame callback */
	if (sched->clock_cb)
		sched->clock_cb(sched);

	/* Schedule first FN clock */
	memcpy(&sched->clock, tv_now, sizeof(struct timeval));
	memset(&sched->clock_timer, 0, sizeof(sched->clock_timer));

	sched->clock_timer.cb = sched_clck_tick;
	sched->clock_timer.data = sched;
	osmo_timer_schedule(&sched->clock_timer, 0, FRAME_DURATION_uS);
}

int sched_clck_handle(struct trx_sched *sched, uint32_t fn)
{
	struct timeval tv_now, *tv_clock;
	int32_t elapsed, elapsed_fn;

	/* Reset lost counter */
	sched->fn_counter_lost = 0;

	/* Get actual / previous frame time */
	gettimeofday(&tv_now, NULL);
	tv_clock = &sched->clock;

	/* If this is the first CLCK IND */
	if (sched->state == SCH_CLCK_STATE_WAIT) {
		sched_clck_correct(sched, &tv_now, fn);

		LOGP(DSCH, LOGL_NOTICE, "Initial clock received: fn=%u\n", fn);
		sched->state = SCH_CLCK_STATE_OK;

		return 0;
	}

	osmo_timer_del(&sched->clock_timer);

	/* Calculate elapsed time / frames since last processed fn */
	elapsed = (tv_now.tv_sec - tv_clock->tv_sec) * 1000000
		+ (tv_now.tv_usec - tv_clock->tv_usec);
	elapsed_fn = (fn + GSM_HYPERFRAME - sched->fn_counter_proc)
		% GSM_HYPERFRAME;

	if (elapsed_fn >= 135774)
		elapsed_fn -= GSM_HYPERFRAME;

	/* Check for max clock skew */
	if (elapsed_fn > MAX_FN_SKEW || elapsed_fn < -MAX_FN_SKEW) {
		LOGP(DSCH, LOGL_NOTICE, "GSM clock skew: old fn=%u, "
			"new fn=%u\n", sched->fn_counter_proc, fn);

		sched_clck_correct(sched, &tv_now, fn);
		return 0;
	}

	LOGP(DSCH, LOGL_INFO, "GSM clock jitter: %d\n",
		elapsed_fn * FRAME_DURATION_uS - elapsed);

	/* Too many frames have been processed already */
	if (elapsed_fn < 0) {
		/**
		 * Set clock to the time or last FN should
		 * have been transmitted
		 */
		tv_clock->tv_sec = tv_now.tv_sec;
		tv_clock->tv_usec = tv_now.tv_usec +
			(0 - elapsed_fn) * FRAME_DURATION_uS;

		if (tv_clock->tv_usec >= 1000000) {
			tv_clock->tv_sec++;
			tv_clock->tv_usec -= 1000000;
		}

		/* Set time to the time our next FN has to be transmitted */
		osmo_timer_schedule(&sched->clock_timer, 0,
			FRAME_DURATION_uS * (1 - elapsed_fn));

		return 0;
	}

	/* Transmit what we still need to transmit */
	while (fn != sched->fn_counter_proc) {
		sched->fn_counter_proc = (sched->fn_counter_proc + 1)
			% GSM_HYPERFRAME;

		/* Call frame callback */
		if (sched->clock_cb)
			sched->clock_cb(sched);
	}

	/* Schedule next FN to be transmitted */
	memcpy(tv_clock, &tv_now, sizeof(struct timeval));
	osmo_timer_schedule(&sched->clock_timer, 0, FRAME_DURATION_uS);

	return 0;
}
