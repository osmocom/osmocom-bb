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

/*! \file timer.h
 *  \brief Osmocom timer handling routines
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
/*! \brief A structure representing a single instance of a timer */
struct osmo_timer_list {
	struct llist_head entry;  /*!< \brief linked list header */
	struct timeval timeout;   /*!< \brief expiration time */
	unsigned int active  : 1; /*!< \brief is it active? */
	unsigned int handled : 1; /*!< \brief did we already handle it */
	unsigned int in_list : 1; /*!< \brief is it in the global list? */

	void (*cb)(void*);	  /*!< \brief call-back called at timeout */
	void *data;		  /*!< \brief user data for callback */
};

/**
 * timer management
 */

/*! \brief add a new timer to the timer management
 *  \param[in] timer the timer that should be added
 */
void osmo_timer_add(struct osmo_timer_list *timer);

/*! \brief schedule a timer at a given future relative time
 *  \param[in] timer the to-be-added timer
 *  \param[in] seconds number of seconds from now
 *  \param[in] microseconds number of microseconds from now
 *
 * This function can be used to (re-)schedule a given timer at a
 * specified number of seconds+microseconds in the future.  It will
 * internally add it to the timer management data structures, thus
 * osmo_timer_add() is automatically called.
 */
void osmo_timer_schedule(struct osmo_timer_list *timer, int seconds, int microseconds);

/*! \brief delete a timer from timer management
 *  \param[in] timer the to-be-deleted timer
 *
 * This function can be used to delete a previously added/scheduled
 * timer from the timer management code.
 */
void osmo_timer_del(struct osmo_timer_list *timer);

/*! \brief check if given timer is still pending
 *  \param[in] timer the to-be-checked timer
 *  \return 1 if pending, 0 otherwise
 *
 * This function can be used to determine whether a given timer
 * has alredy expired (returns 0) or is still pending (returns 1)
 */
int osmo_timer_pending(struct osmo_timer_list *timer);


/*
 * internal timer list management
 */
struct timeval *osmo_timers_nearest(void);
void osmo_timers_prepare(void);
int osmo_timers_update(void);
int osmo_timers_check(void);

#endif
