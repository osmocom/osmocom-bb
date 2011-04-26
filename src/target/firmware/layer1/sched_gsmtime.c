/* GSM-Time One-shot Event Scheduler Implementation (on top of TDMA sched) */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdint.h>
#include <errno.h>

#include <debug.h>
#include <osmocom/core/linuxlist.h>

#include <layer1/tdma_sched.h>
#include <layer1/sched_gsmtime.h>

static struct sched_gsmtime_event sched_gsmtime_events[16];
static LLIST_HEAD(active_evts);
static LLIST_HEAD(inactive_evts);

/* Scheduling of a tdma_sched_item list one-shot at a given GSM time */
int sched_gsmtime(const struct tdma_sched_item *si, uint32_t fn, uint16_t p3)
{
	struct llist_head *lh;
	struct sched_gsmtime_event *evt, *cur;

	printd("sched_gsmtime(si=%p, fn=%u)\n", si, fn);

	/* obtain a free/inactive event structure */
	if (llist_empty(&inactive_evts))
		return -EBUSY;
	lh = inactive_evts.next;
	llist_del(lh);
	evt = llist_entry(lh, struct sched_gsmtime_event, list);

	evt->fn = fn;
	evt->si = si;
	evt->p3 = p3;

	/* do a sorted insert into the list, i.e. insert the new
	 * event _before_ the first entry that has a higher fn */
	llist_for_each_entry(cur, &active_evts, list) {
		if (cur->fn > evt->fn) {
			llist_add_tail(lh, &cur->list);
			return 0;
		}
	}

	/* if we reach here, active_evts is empty _OR_ new event
	 * is after all the other events: append at end of list */
	llist_add_tail(lh, &active_evts);

	return 0;
}

/* how many TDMA frame ticks should we schedule events ahead? */
#define SCHEDULE_AHEAD	2

/* how long do we need to tell the DSP in advance what we want to do? */
#define SCHEDULE_LATENCY	1

/* execute all GSMTIME one-shot events pending for 'fn' */
int sched_gsmtime_execute(uint32_t fn)
{
	struct sched_gsmtime_event *evt, *evt2;
	int num = 0;

	llist_for_each_entry_safe(evt, evt2, &active_evts, list) {
		if (evt->fn == fn + SCHEDULE_AHEAD) {
			printd("sched_gsmtime_execute(time=%u): fn=%u si=%p\n", fn, evt->fn, evt->si);
			tdma_schedule_set(SCHEDULE_AHEAD-SCHEDULE_LATENCY,
					  evt->si, evt->p3);
			llist_del(&evt->list);
			/* put event back in list of inactive (free) events */
			llist_add(&evt->list, &inactive_evts);
			num++;
		} if (evt->fn > fn + SCHEDULE_AHEAD) {
			/* break the loop as our list is ordered */
			break;
		}
	}
	return num;
}

void sched_gsmtime_init(void)
{
	unsigned int i;

	printd("sched_gsmtime_init()\n");

	for (i = 0; i < ARRAY_SIZE(sched_gsmtime_events); i++)
		llist_add(&sched_gsmtime_events[i].list, &inactive_evts);
}

void sched_gsmtime_reset(void)
{
	struct sched_gsmtime_event *evt, *evt2;

	llist_for_each_entry_safe(evt, evt2, &active_evts, list) {
		llist_del(&evt->list);
		/* put event back in list of inactive (free) events */
		llist_add(&evt->list, &inactive_evts);
	}
}
