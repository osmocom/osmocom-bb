/* utility routines for keeping conters about events and the event rates */

/* (C) 2015 by Sysmocom s.f.m.c. GmbH
 * (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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

/*! \addtogroup stat_item
 *  @{
 */

/*! \file stat_item.c */


#include <stdint.h>
#include <string.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/stat_item.h>

static LLIST_HEAD(stat_item_groups);

static void *tall_stat_item_ctx;

/*! \brief Allocate a new group of counters according to description
 *  \param[in] ctx \ref talloc context
 *  \param[in] desc Statistics item group description
 *  \param[in] idx Index of new stat item group
 */
struct stat_item_group *stat_item_group_alloc(void *ctx,
					    const struct stat_item_group_desc *desc,
					    unsigned int idx)
{
	unsigned int group_size;
	unsigned int items_size = 0;
	unsigned int item_idx;
	void *items;

	struct stat_item_group *group;

	group_size = sizeof(struct stat_item_group) +
			desc->num_items * sizeof(struct stat_item *);

	if (!ctx)
		ctx = tall_stat_item_ctx;

	group = talloc_zero_size(ctx, group_size);
	if (!group)
		return NULL;

	group->desc = desc;
	group->idx = idx;

	/* Get combined size of all items */
	for (item_idx = 0; item_idx < desc->num_items; item_idx++) {
		unsigned int size;
		size = sizeof(struct stat_item) +
			sizeof(int32_t) * desc->item_desc[item_idx].num_values;
		/* Align to pointer size */
		size = (size + sizeof(void *) - 1) & ~(sizeof(void *) - 1);

		/* Store offsets into the item array */
		group->items[item_idx] = (void *)items_size;

		items_size += size;
	}

	items = talloc_zero_size(group, items_size);
	if (!items) {
		talloc_free(group);
		return NULL;
	}

	/* Update item pointers */
	for (item_idx = 0; item_idx < desc->num_items; item_idx++) {
		struct stat_item *item = (struct stat_item *)
			((uint8_t *)items + (int)group->items[item_idx]);
		unsigned int i;

		group->items[item_idx] = item;
		item->last_offs = desc->item_desc[item_idx].num_values - 1;
		item->last_value_index = -1;
		item->desc = &desc->item_desc[item_idx];

		for (i = 0; i <= item->last_offs; i++)
			item->values[i] = desc->item_desc[item_idx].default_value;
	}

	llist_add(&group->list, &stat_item_groups);

	return group;
}

/*! \brief Free the memory for the specified group of counters */
void stat_item_group_free(struct stat_item_group *grp)
{
	llist_del(&grp->list);
	talloc_free(grp);
}

void stat_item_set(struct stat_item *item, int32_t value)
{
	item->last_offs += 1;
	if (item->last_offs >= item->desc->num_values)
		item->last_offs = 0;

	item->last_value_index += 1;

	item->values[item->last_offs] = value;
}

int stat_item_get_next(const struct stat_item *item, int32_t *next_idx,
	int32_t *value)
{
	int32_t delta = item->last_value_index + 1 - *next_idx;
	int n_values = 0;
	int next_offs;

	if (delta == 0)
		/* All items have been read */
		return 0;

	if (delta < 0 || delta > item->desc->num_values) {
		n_values = delta - item->desc->num_values;
		delta = item->desc->num_values;
	}

	next_offs = item->last_offs + 1 - delta;
	if (next_offs < 0)
		next_offs += item->desc->num_values;

	*value = item->values[next_offs];

	n_values += 1;
	delta -= 1;
	*next_idx = item->last_value_index + 1 - delta;

	return n_values;
}

/*! \brief Skip all values and update idx accordingly */
int stat_item_discard(const struct stat_item *item, int32_t *idx)
{
	int discarded = item->last_value_index + 1 - *idx;
	*idx = item->last_value_index + 1;

	return discarded;
}


/*! \brief Initialize the stat item module */
int stat_item_init(void *tall_ctx)
{
	tall_stat_item_ctx = tall_ctx;

	return 0;
}

/*! \brief Search for item group based on group name and index */
struct stat_item_group *stat_item_get_group_by_name_idx(
	const char *name, const unsigned int idx)
{
	struct stat_item_group *statg;

	llist_for_each_entry(statg, &stat_item_groups, list) {
		if (!statg->desc)
			continue;

		if (!strcmp(statg->desc->group_name_prefix, name) &&
				statg->idx == idx)
			return statg;
	}
	return NULL;
}

/*! \brief Search for item group based on group name */
const struct stat_item *stat_item_get_by_name(
	const struct stat_item_group *statg, const char *name)
{
	int i;
	const struct stat_item_desc *item_desc;

	if (!statg->desc)
		return NULL;

	for (i = 0; i < statg->desc->num_items; i++) {
		item_desc = &statg->desc->item_desc[i];

		if (!strcmp(item_desc->name, name)) {
			return statg->items[i];
		}
	}
	return NULL;
}

/*! @} */
