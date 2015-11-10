/* utility routines for keeping conters about events and the event rates */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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

/*! \addtogroup rate_ctr
 *  @{
 */

/*! \file rate_ctr.c */


#include <stdint.h>
#include <string.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/rate_ctr.h>

static LLIST_HEAD(rate_ctr_groups);

static void *tall_rate_ctr_ctx;

/*! \brief Allocate a new group of counters according to description
 *  \param[in] ctx \ref talloc context
 *  \param[in] desc Rate counter group description
 *  \param[in] idx Index of new counter group
 */
struct rate_ctr_group *rate_ctr_group_alloc(void *ctx,
					    const struct rate_ctr_group_desc *desc,
					    unsigned int idx)
{
	unsigned int size;
	struct rate_ctr_group *group;

	size = sizeof(struct rate_ctr_group) +
			desc->num_ctr * sizeof(struct rate_ctr);

	if (!ctx)
		ctx = tall_rate_ctr_ctx;

	group = talloc_zero_size(ctx, size);
	if (!group)
		return NULL;

	group->desc = desc;
	group->idx = idx;

	llist_add(&group->list, &rate_ctr_groups);

	return group;
}

/*! \brief Free the memory for the specified group of counters */
void rate_ctr_group_free(struct rate_ctr_group *grp)
{
	llist_del(&grp->list);
	talloc_free(grp);
}

/*! \brief Add a number to the counter */
void rate_ctr_add(struct rate_ctr *ctr, int inc)
{
	ctr->current += inc;
}

/*! \brief Return the counter difference since the last call to this function */
int64_t rate_ctr_difference(struct rate_ctr *ctr)
{
	int64_t result = ctr->current - ctr->previous;
	ctr->previous = ctr->current;

	return result;
}

/* TODO: support update intervals > 1s */
/* TODO: implement this as a special stats reporter */

static void interval_expired(struct rate_ctr *ctr, enum rate_ctr_intv intv)
{
	/* calculate rate over last interval */
	ctr->intv[intv].rate = ctr->current - ctr->intv[intv].last;
	/* save current counter for next interval */
	ctr->intv[intv].last = ctr->current;

	/* update the rate of the next bigger interval.  This will
	 * be overwritten when that next larger interval expires */
	if (intv + 1 < ARRAY_SIZE(ctr->intv))
		ctr->intv[intv+1].rate += ctr->intv[intv].rate;
}

static struct osmo_timer_list rate_ctr_timer;
static uint64_t timer_ticks;

/* The one-second interval has expired */
static void rate_ctr_group_intv(struct rate_ctr_group *grp)
{
	unsigned int i;

	for (i = 0; i < grp->desc->num_ctr; i++) {
		struct rate_ctr *ctr = &grp->ctr[i];

		interval_expired(ctr, RATE_CTR_INTV_SEC);
		if ((timer_ticks % 60) == 0)
			interval_expired(ctr, RATE_CTR_INTV_MIN);
		if ((timer_ticks % (60*60)) == 0)
			interval_expired(ctr, RATE_CTR_INTV_HOUR);
		if ((timer_ticks % (24*60*60)) == 0)
			interval_expired(ctr, RATE_CTR_INTV_DAY);
	}
}

static void rate_ctr_timer_cb(void *data)
{
	struct rate_ctr_group *ctrg;

	/* Increment number of ticks before we calculate intervals,
	 * as a counter value of 0 would already wrap all counters */
	timer_ticks++;

	llist_for_each_entry(ctrg, &rate_ctr_groups, list)
		rate_ctr_group_intv(ctrg);

	osmo_timer_schedule(&rate_ctr_timer, 1, 0);
}

/*! \brief Initialize the counter module */
int rate_ctr_init(void *tall_ctx)
{
	tall_rate_ctr_ctx = tall_ctx;
	rate_ctr_timer.cb = rate_ctr_timer_cb;
	osmo_timer_schedule(&rate_ctr_timer, 1, 0);

	return 0;
}

/*! \brief Search for counter group based on group name and index */
struct rate_ctr_group *rate_ctr_get_group_by_name_idx(const char *name, const unsigned int idx)
{
	struct rate_ctr_group *ctrg;

	llist_for_each_entry(ctrg, &rate_ctr_groups, list) {
		if (!ctrg->desc)
			continue;

		if (!strcmp(ctrg->desc->group_name_prefix, name) &&
				ctrg->idx == idx) {
			return ctrg;
		}
	}
	return NULL;
}

/*! \brief Search for counter group based on group name */
const struct rate_ctr *rate_ctr_get_by_name(const struct rate_ctr_group *ctrg, const char *name)
{
	int i;
	const struct rate_ctr_desc *ctr_desc;

	if (!ctrg->desc)
		return NULL;

	for (i = 0; i < ctrg->desc->num_ctr; i++) {
		ctr_desc = &ctrg->desc->ctr_desc[i];

		if (!strcmp(ctr_desc->name, name)) {
			return &ctrg->ctr[i];
		}
	}
	return NULL;
}

int rate_ctr_for_each_counter(struct rate_ctr_group *ctrg,
	rate_ctr_handler_t handle_counter, void *data)
{
	int rc = 0;
	int i;

	for (i = 0; i < ctrg->desc->num_ctr; i++) {
		struct rate_ctr *ctr = &ctrg->ctr[i];
		rc = handle_counter(ctrg,
			ctr, &ctrg->desc->ctr_desc[i], data);
		if (rc < 0)
			return rc;
	}

	return rc;
}

int rate_ctr_for_each_group(rate_ctr_group_handler_t handle_group, void *data)
{
	struct rate_ctr_group *statg;
	int rc = 0;

	llist_for_each_entry(statg, &rate_ctr_groups, list) {
		rc = handle_group(statg, data);
		if (rc < 0)
			return rc;
	}

	return rc;
}


/*! @} */
