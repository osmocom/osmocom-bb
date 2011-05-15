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

/**
 * Timer management:
 *      - Create a struct osmo_timer_list
 *      - Fill out timeout and use add_timer or
 *        use schedule_timer to schedule a timer in
 *        x seconds and microseconds from now...
 *      - Use del_timer to remove the timer
 *
 *  Internally:
 *      - We hook into select.c to give a timeval of the
 *        nearest timer. On already passed timers we give
 *        it a 0 to immediately fire after the select
 *      - update_timers will call the callbacks and remove
 *        the timers.
 *
 */
struct osmo_timer_list {
	struct llist_head entry;
	struct timeval timeout;
	unsigned int active  : 1;
	unsigned int handled : 1;
	unsigned int in_list : 1;

	void (*cb)(void*);
	void *data;
};

/**
 * timer management
 */
void osmo_timer_add(struct osmo_timer_list *timer);
void osmo_timer_schedule(struct osmo_timer_list *timer, int seconds, int microseconds);
void osmo_timer_del(struct osmo_timer_list *timer);
int osmo_timer_pending(struct osmo_timer_list *timer);


/**
 * internal timer list management
 */
struct timeval *osmo_timers_nearest();
void osmo_timers_prepare();
int osmo_timers_update();
int osmo_timers_check(void);

#endif
