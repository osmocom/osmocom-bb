/* (C) 2008 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <debug.h>
#include <osmocom/core/linuxlist.h>

#include <comm/timer.h>

#include <calypso/timer.h>
#include <calypso/irq.h>

#include <keypad.h>

static LLIST_HEAD(timer_list);

unsigned long volatile jiffies;

#define time_after(a,b)         \
	(typecheck(unsigned long, a) && \
	 typecheck(unsigned long, b) && \
	 ((long)(b) - (long)(a) < 0))
#define time_before(a,b)        time_after(b,a)

void osmo_timer_add(struct osmo_timer_list *timer)
{
	struct osmo_timer_list *list_timer;

	/* TODO: Optimize and remember the closest item... */
	timer->active = 1;

	/* this might be called from within osmo_timers_update */
	llist_for_each_entry(list_timer, &timer_list, entry)
		if (timer == list_timer)
			return;

	timer->in_list = 1;
	llist_add(&timer->entry, &timer_list);
}

void osmo_timer_schedule(struct osmo_timer_list *timer, int milliseconds)
{
	timer->expires = jiffies + ((milliseconds * HZ) / 1000);
	osmo_timer_add(timer);
}

void osmo_timer_del(struct osmo_timer_list *timer)
{
	if (timer->in_list) {
		timer->active = 0;
		timer->in_list = 0;
		llist_del(&timer->entry);
	}
}

int osmo_timer_pending(struct osmo_timer_list *timer)
{
	return timer->active;
}

#if 0
/*
 * if we have a nearest time return the delta between the current
 * time and the time of the nearest timer.
 * If the nearest timer timed out return NULL and then we will
 * dispatch everything after the select
 */
struct timeval *nearest_timer()
{
	struct timeval current_time;

	if (s_nearest_time.tv_sec == 0 && s_nearest_time.tv_usec == 0)
		return NULL;

	if (gettimeofday(&current_time, NULL) == -1)
		return NULL;

	unsigned long long nearestTime = s_nearest_time.tv_sec * MICRO_SECONDS + s_nearest_time.tv_usec;
	unsigned long long currentTime = current_time.tv_sec * MICRO_SECONDS + current_time.tv_usec;

	if (nearestTime < currentTime) {
		s_select_time.tv_sec = 0;
		s_select_time.tv_usec = 0;
	} else {
		s_select_time.tv_sec = (nearestTime - currentTime) / MICRO_SECONDS;
		s_select_time.tv_usec = (nearestTime - currentTime) % MICRO_SECONDS;
	}

	return &s_select_time;
}

/*
 * Find the nearest time and update s_nearest_time
 */
void prepare_timers()
{
	struct osmo_timer_list *timer, *nearest_timer = NULL;
	llist_for_each_entry(timer, &timer_list, entry) {
		if (!nearest_timer || time_before(timer->expires, nearest_timer->expires)) {
			nearest_timer = timer;
		}
	}

	if (nearest_timer) {
		s_nearest_time = nearest_timer->timeout;
	} else {
		memset(&s_nearest_time, 0, sizeof(struct timeval));
	}
}
#endif

/*
 * fire all timers... and remove them
 */
int osmo_timers_update(void)
{
	struct osmo_timer_list *timer, *tmp;
	int work = 0;

	/*
	 * The callbacks might mess with our list and in this case
	 * even llist_for_each_entry_safe is not safe to use. To allow
	 * osmo_timer_del, osmo_timer_add, osmo_timer_schedule to be called from within
	 * the callback we jump through some loops.
	 *
	 * First we set the handled flag of each active timer to zero,
	 * then we iterate over the list and execute the callbacks. As the
	 * list might have been changed (specially the next) from within
	 * the callback we have to start over again. Once every callback
	 * is dispatched we will remove the non-active from the list.
	 *
	 * TODO: If this is a performance issue we can poison a global
	 * variable in osmo_timer_add and osmo_timer_del and only then restart.
	 */
	llist_for_each_entry(timer, &timer_list, entry) {
		timer->handled = 0;
	}

restart:
	llist_for_each_entry(timer, &timer_list, entry) {
		if (!timer->handled && time_before(timer->expires, jiffies)) {
			timer->handled = 1;
			timer->active = 0;
			(*timer->cb)(timer->data);
			work = 1;
			goto restart;
		}
	}

	llist_for_each_entry_safe(timer, tmp, &timer_list, entry) {
		timer->handled = 0;
		if (!timer->active) {
			osmo_timer_del(timer);
		}
	}

	return work;
}

int osmo_timers_check(void)
{
	struct osmo_timer_list *timer;
	int i = 0;

	llist_for_each_entry(timer, &timer_list, entry) {
		i++;
	}
	return i;
}

static void timer_irq(enum irq_nr irq)
{
	/* we only increment jiffies here.  FIXME: does this need to be atomic? */
	jiffies++;

	keypad_poll();
}

void timer_init(void)
{
	/* configure TIMER2 for our purpose */
	hwtimer_enable(2, 1);
	/* The timer runs at 13MHz / 32, i.e. 406.25kHz */
#if (HZ == 100)
	hwtimer_load(2, 4062);
	hwtimer_config(2, 0, 1);
#elif (HZ == 10)
	/* prescaler 4, 1015 ticks until expiry */
	hwtimer_load(2, 1015);
	hwtimer_config(2, 4, 1);
#endif
	hwtimer_enable(2, 1);

	/* register interrupt handler with default priority, EDGE triggered */
	irq_register_handler(IRQ_TIMER2, &timer_irq);
	irq_config(IRQ_TIMER2, 0, 1, -1);
	irq_enable(IRQ_TIMER2);
}
