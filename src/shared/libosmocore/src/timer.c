/*
 * (C) 2008,2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2011 by Harald Welte <laforge@gnumonks.org>
 * All Rights Reserved
 *
 * Authors: Holger Hans Peter Freyther <zecke@selfish.org>
 *	    Harald Welte <laforge@gnumonks.org>
 *	    Pablo Neira Ayuso <pablo@gnumonks.org>
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

/* These store the amount of time that we wait until next timer expires. */
static struct timeval nearest;
static struct timeval *nearest_p;

/*! \addtogroup timer
 *  @{
 */

/*! \file timer.c
 */

#include <assert.h>
#include <string.h>
#include <limits.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/timer_compat.h>
#include <osmocom/core/linuxlist.h>

static struct rb_root timer_root = RB_ROOT;

static void __add_timer(struct osmo_timer_list *timer)
{
	struct rb_node **new = &(timer_root.rb_node);
	struct rb_node *parent = NULL;

	while (*new) {
		struct osmo_timer_list *this;

		this = container_of(*new, struct osmo_timer_list, node);

		parent = *new;
		if (timercmp(&timer->timeout, &this->timeout, <))
			new = &((*new)->rb_left);
		else
			new = &((*new)->rb_right);
        }

        rb_link_node(&timer->node, parent, new);
        rb_insert_color(&timer->node, &timer_root);
}

/*! \brief add a new timer to the timer management
 *  \param[in] timer the timer that should be added
 */
void osmo_timer_add(struct osmo_timer_list *timer)
{
	osmo_timer_del(timer);
	timer->active = 1;
	INIT_LLIST_HEAD(&timer->list);
	__add_timer(timer);
}

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
void
osmo_timer_schedule(struct osmo_timer_list *timer, int seconds, int microseconds)
{
	struct timeval current_time;

	gettimeofday(&current_time, NULL);
	timer->timeout.tv_sec = seconds;
	timer->timeout.tv_usec = microseconds;
	timeradd(&timer->timeout, &current_time, &timer->timeout);
	osmo_timer_add(timer);
}

/*! \brief delete a timer from timer management
 *  \param[in] timer the to-be-deleted timer
 *
 * This function can be used to delete a previously added/scheduled
 * timer from the timer management code.
 */
void osmo_timer_del(struct osmo_timer_list *timer)
{
	if (timer->active) {
		timer->active = 0;
		rb_erase(&timer->node, &timer_root);
		/* make sure this is not already scheduled for removal. */
		if (!llist_empty(&timer->list))
			llist_del_init(&timer->list);
	}
}

/*! \brief check if given timer is still pending
 *  \param[in] timer the to-be-checked timer
 *  \return 1 if pending, 0 otherwise
 *
 * This function can be used to determine whether a given timer
 * has alredy expired (returns 0) or is still pending (returns 1)
 */
int osmo_timer_pending(struct osmo_timer_list *timer)
{
	return timer->active;
}

/*! \brief compute the remaining time of a timer
 *  \param[in] timer the to-be-checked timer
 *  \param[in] the current time (NULL if not known)
 *  \param[out] remaining remaining time until timer fires
 *  \return 0 if timer has not expired yet, -1 if it has
 *
 *  This function can be used to determine the amount of time
 *  remaining until the expiration of the timer.
 */
int osmo_timer_remaining(const struct osmo_timer_list *timer,
			 const struct timeval *now,
			 struct timeval *remaining)
{
	struct timeval current_time;

	if (!now) {
		gettimeofday(&current_time, NULL);
		now = &current_time;
	}

	timersub(&timer->timeout, &current_time, remaining);

	if (remaining->tv_sec < 0)
		return -1;

	return 0;
}

/*
 * if we have a nearest time return the delta between the current
 * time and the time of the nearest timer.
 * If the nearest timer timed out return NULL and then we will
 * dispatch everything after the select
 */
struct timeval *osmo_timers_nearest(void)
{
	/* nearest_p is exactly what we need already: NULL if nothing is
	 * waiting, {0,0} if we must dispatch immediately, and the correct
	 * delay if we need to wait */
	return nearest_p;
}

static void update_nearest(struct timeval *cand, struct timeval *current)
{
	if (cand->tv_sec != LONG_MAX) {
		if (timercmp(cand, current, >))
			timersub(cand, current, &nearest);
		else {
			/* loop again inmediately */
			nearest.tv_sec = 0;
			nearest.tv_usec = 0;
		}
		nearest_p = &nearest;
	} else {
		nearest_p = NULL;
	}
}

/*
 * Find the nearest time and update s_nearest_time
 */
void osmo_timers_prepare(void)
{
	struct rb_node *node;
	struct timeval current;

	gettimeofday(&current, NULL);

	node = rb_first(&timer_root);
	if (node) {
		struct osmo_timer_list *this;
		this = container_of(node, struct osmo_timer_list, node);
		update_nearest(&this->timeout, &current);
	} else {
		nearest_p = NULL;
	}
}

/*
 * fire all timers... and remove them
 */
int osmo_timers_update(void)
{
	struct timeval current_time;
	struct rb_node *node;
	struct llist_head timer_eviction_list;
	struct osmo_timer_list *this;
	int work = 0;

	gettimeofday(&current_time, NULL);

	INIT_LLIST_HEAD(&timer_eviction_list);
	for (node = rb_first(&timer_root); node; node = rb_next(node)) {
		this = container_of(node, struct osmo_timer_list, node);

		if (timercmp(&this->timeout, &current_time, >))
			break;

		llist_add(&this->list, &timer_eviction_list);
	}

	/*
	 * The callbacks might mess with our list and in this case
	 * even llist_for_each_entry_safe is not safe to use. To allow
	 * osmo_timer_del to be called from within the callback we need
	 * to restart the iteration for each element scheduled for removal.
	 *
	 * The problematic scenario is the following: Given two timers A
	 * and B that have expired at the same time. Thus, they are both
	 * in the eviction list in this order: A, then B. If we remove
	 * timer B from the A's callback, we continue with B in the next
	 * iteration step, leading to an access-after-release.
	 */
restart:
	llist_for_each_entry(this, &timer_eviction_list, list) {
		osmo_timer_del(this);
		this->cb(this->data);
		work = 1;
		goto restart;
	}

	return work;
}

int osmo_timers_check(void)
{
	struct rb_node *node;
	int i = 0;

	for (node = rb_first(&timer_root); node; node = rb_next(node)) {
		i++;
	}
	return i;
}

/*! @} */
