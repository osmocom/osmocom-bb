#pragma once

/*! \defgroup stat_item Statistics value item
 *  @{
 */

/*! \file stat_item.h */

#include <stdint.h>

#include <osmocom/core/linuxlist.h>

struct stat_item_desc;

/*! \brief data we keep for each actual value */
struct stat_item {
	const struct stat_item_desc *desc;
	/*! \brief the index of the freshest value */
	int32_t last_value_index;
	/*! \brief offset to the freshest value in the value fifo */
	int16_t last_offs;
	/*! \brief value fifo */
	int32_t values[0];
};

/*! \brief statistics value description */
struct stat_item_desc {
	const char *name;	/*!< \brief name of the item */
	const char *description;/*!< \brief description of the item */
	const char *unit;	/*!< \brief unit of a value */
	unsigned int num_values;/*!< \brief number of values to store */
	int32_t default_value;
};

/*! \brief description of a statistics value group */
struct stat_item_group_desc {
	/*! \brief The prefix to the name of all values in this group */
	const char *group_name_prefix;
	/*! \brief The human-readable description of the group */
	const char *group_description;
	/*! \brief The number of values in this group */
	const unsigned int num_items;
	/*! \brief Pointer to array of value names */
	const struct stat_item_desc *item_desc;
};

/*! \brief One instance of a counter group class */
struct stat_item_group {
	/*! \brief Linked list of all value groups in the system */
	struct llist_head list;
	/*! \brief Pointer to the counter group class */
	const struct stat_item_group_desc *desc;
	/*! \brief The index of this value group within its class */
	unsigned int idx;
	/*! \brief Actual counter structures below */
	struct stat_item *items[0];
};

struct stat_item_group *stat_item_group_alloc(
	void *ctx,
	const struct stat_item_group_desc *desc,
	unsigned int idx);

void stat_item_group_free(struct stat_item_group *grp);

void stat_item_set(struct stat_item *val, int32_t value);

int stat_item_init(void *tall_ctx);

struct stat_item_group *stat_item_get_group_by_name_idx(
	const char *name, const unsigned int idx);

const struct stat_item *stat_item_get_by_name(
	const struct stat_item_group *valg, const char *name);

/*! \brief Retrieve the next value from the stat_item object.
 * If a new value has been set, it is returned. The idx is used to decide
 * which value to return.
 * On success, *idx is updated to refer to the next unread value. If
 * values have been missed due to FIFO overflow, *idx is incremented by
 * (1 + num_lost).
 * This way, the stat_item object can be kept stateless from the reader's
 * perspective and therefore be used by several backends simultaneously.
 *
 * \param val	the stat_item object
 * \param idx	identifies the next value to be read
 * \param value	a pointer to store the value
 * \returns  the increment of the index (0: no value has been read,
 *           1: one value has been taken,
 *           (1+n): n values have been skipped, one has been taken)
 */
int stat_item_get_next(const struct stat_item *val, int32_t *idx, int32_t *value);

/*! \brief Get the last (freshest) value */
static int32_t stat_item_get_last(const struct stat_item *val);

/*! \brief Skip all values of the item and update idx accordingly */
int stat_item_discard(const struct stat_item *val, int32_t *idx);

static inline int32_t stat_item_get_last(const struct stat_item *val)
{
	return val->values[val->last_offs];
}
/*! @} */
