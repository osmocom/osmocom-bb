/* utility routines for keeping some statistics */

/* (C) 2009 by Harald Welte <laforge@gnumonks.org>
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

#include <string.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/statistics.h>

static LLIST_HEAD(counters);

void *tall_ctr_ctx;

struct osmo_counter *osmo_counter_alloc(const char *name)
{
	struct osmo_counter *ctr = talloc_zero(tall_ctr_ctx, struct osmo_counter);

	if (!ctr)
		return NULL;

	ctr->name = name;
	llist_add_tail(&ctr->list, &counters);

	return ctr;
}

void osmo_counter_free(struct osmo_counter *ctr)
{
	llist_del(&ctr->list);
	talloc_free(ctr);
}

int osmo_counters_for_each(int (*handle_counter)(struct osmo_counter *, void *),
			   void *data)
{
	struct osmo_counter *ctr;
	int rc = 0;

	llist_for_each_entry(ctr, &counters, list) {
		rc = handle_counter(ctr, data);
		if (rc < 0)
			return rc;
	}

	return rc;
}

struct osmo_counter *osmo_counter_get_by_name(const char *name)
{
	struct osmo_counter *ctr;

	llist_for_each_entry(ctr, &counters, list) {
		if (!strcmp(ctr->name, name))
			return ctr;
	}
	return NULL;
}

int osmo_counter_difference(struct osmo_counter *ctr)
{
	int delta = ctr->value - ctr->previous;
	ctr->previous = ctr->value;

	return delta;
}
