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

/*! \defgroup timer Osmocom timers
 *  @{
 */

/*! \file timer.h
 *  \brief Osmocom timer handling routines
 */

#pragma once

#include <sys/time.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/linuxrbtree.h>

/**
 * Timer management:
 *      - Create a struct osmo_timer_list
 *      - Fill out timeout and use add_timer or
 *        use osmo_timer_schedule to schedule a timer in
 *        x seconds and microseconds from now...
 *      - Use osmo_timer_del to remove the timer
 *
 *  Internally:
 *      - We hook into select.c to give a timeval of the
 *        nearest timer. On already passed timers we give
 *        it a 0 to immediately fire after the select
 *      - osmo_timers_update will call the callbacks and
 *        remove the timers.
 *
 */
/*! \brief A structure representing a single instance of a timer */
struct osmo_timer_list {
	struct rb_node node;	  /*!< \brief rb-tree node header */
	struct llist_head list;   /*!< \brief internal list header */
	struct timeval timeout;   /*!< \brief expiration time */
	unsigned int active  : 1; /*!< \brief is it active? */

	void (*cb)(void*);	  /*!< \brief call-back called at timeout */
	void *data;		  /*!< \brief user data for callback */
};

/**
 * timer management
 */

void osmo_timer_add(struct osmo_timer_list *timer);

void osmo_timer_schedule(struct osmo_timer_list *timer, int seconds, int microseconds);

void osmo_timer_del(struct osmo_timer_list *timer);

int osmo_timer_pending(struct osmo_timer_list *timer);

int osmo_timer_remaining(const struct osmo_timer_list *timer,
			 const struct timeval *now,
			 struct timeval *remaining);
/*
 * internal timer list management
 */
struct timeval *osmo_timers_nearest(void);
void osmo_timers_prepare(void);
int osmo_timers_update(void);
int osmo_timers_check(void);

/*! @} */
