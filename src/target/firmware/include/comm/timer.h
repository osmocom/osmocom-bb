/*
 * (C) 2008, 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#ifndef TIMER_H
#define TIMER_H

#include <sys/time.h>

#include <osmocom/core/linuxlist.h>

#define HZ 100

/**
 * Timer management:
 *      - Create a struct osmo_timer_list
 *      - Fill out timeout and use osmo_timer_add or
 *        use osmo_timer_schedule to schedule a timer in
 *        x seconds and microseconds from now...
 *      - Use osmo_timer_del to remove the timer
 *
 *  Internally:
 *      - We hook into select.c to give a timeval of the
 *        nearest timer. On already passed timers we give
 *        it a 0 to immediately fire after the select
 *      - osmo_timers_update will call the callbacks and remove
 *        the timers.
 *
 */
struct osmo_timer_list {
	struct llist_head entry;
	unsigned long expires;

	unsigned int active  : 1;
	unsigned int handled : 1;
	unsigned int in_list : 1;

	void (*cb)(void*);
	void *data;
};

extern unsigned long volatile jiffies;

/**
 * timer management
 */
void osmo_timer_add(struct osmo_timer_list *timer);
void osmo_timer_schedule(struct osmo_timer_list *timer, int miliseconds);
void osmo_timer_del(struct osmo_timer_list *timer);
int osmo_timer_pending(struct osmo_timer_list *timer);


/**
 * internal timer list management
 */
int osmo_timers_update(void);
int osmo_timers_check(void);

void timer_init(void);

#endif
