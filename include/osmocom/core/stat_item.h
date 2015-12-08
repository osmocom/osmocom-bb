#pragma once

/*! \defgroup osmo_stat_item Statistics value item
 *  @{
 */

/*! \file stat_item.h */

#include <stdint.h>

#include <osmocom/core/linuxlist.h>

struct osmo_stat_item_desc;

#define OSMO_STAT_ITEM_NOVALUE_ID 0
#define OSMO_STAT_ITEM_NO_UNIT NULL

struct osmo_stat_item_value {
	int32_t id;
	int32_t value;
};

/*! \brief data we keep for each actual value */
struct osmo_stat_item {
	const struct osmo_stat_item_desc *desc;
	/*! \brief the index of the freshest value */
	int32_t last_value_index;
	/*! \brief offset to the freshest value in the value fifo */
	int16_t last_offs;
	/*! \brief value fifo */
	struct osmo_stat_item_value values[0];
};

/*! \brief statistics value description */
struct osmo_stat_item_desc {
	const char *name;	/*!< \brief name of the item */
	const char *description;/*!< \brief description of the item */
	const char *unit;	/*!< \brief unit of a value */
	unsigned int num_values;/*!< \brief number of values to store */
	int32_t default_value;
};

/*! \brief description of a statistics value group */
struct osmo_stat_item_group_desc {
	/*! \brief The prefix to the name of all values in this group */
	const char *group_name_prefix;
	/*! \brief The human-readable description of the group */
	const char *group_description;
	/*! \brief The class to which this group belongs */
	int class_id;
	/*! \brief The number of values in this group */
	const unsigned int num_items;
	/*! \brief Pointer to array of value names */
	const struct osmo_stat_item_desc *item_desc;
};

/*! \brief One instance of a counter group class */
struct osmo_stat_item_group {
	/*! \brief Linked list of all value groups in the system */
	struct llist_head list;
	/*! \brief Pointer to the counter group class */
	const struct osmo_stat_item_group_desc *desc;
	/*! \brief The index of this value group within its class */
	unsigned int idx;
	/*! \brief Actual counter structures below */
	struct osmo_stat_item *items[0];
};

struct osmo_stat_item_group *osmo_stat_item_group_alloc(
	void *ctx,
	const struct osmo_stat_item_group_desc *desc,
	unsigned int idx);

static inline void osmo_stat_item_group_udp_idx(
	struct osmo_stat_item_group *grp, unsigned int idx)
{
	grp->idx = idx;
}

void osmo_stat_item_group_free(struct osmo_stat_item_group *statg);

void osmo_stat_item_set(struct osmo_stat_item *item, int32_t value);

int osmo_stat_item_init(void *tall_ctx);

struct osmo_stat_item_group *osmo_stat_item_get_group_by_name_idx(
	const char *name, const unsigned int idx);

const struct osmo_stat_item *osmo_stat_item_get_by_name(
	const struct osmo_stat_item_group *statg, const char *name);

/*! \brief Retrieve the next value from the osmo_stat_item object.
 * If a new value has been set, it is returned. The idx is used to decide
 * which value to return.
 * On success, *idx is updated to refer to the next unread value. If
 * values have been missed due to FIFO overflow, *idx is incremented by
 * (1 + num_lost).
 * This way, the osmo_stat_item object can be kept stateless from the reader's
 * perspective and therefore be used by several backends simultaneously.
 *
 * \param val	the osmo_stat_item object
 * \param idx	identifies the next value to be read
 * \param value	a pointer to store the value
 * \returns  the increment of the index (0: no value has been read,
 *           1: one value has been taken,
 *           (1+n): n values have been skipped, one has been taken)
 */
int osmo_stat_item_get_next(const struct osmo_stat_item *item, int32_t *idx, int32_t *value);

/*! \brief Get the last (freshest) value */
static int32_t osmo_stat_item_get_last(const struct osmo_stat_item *item);

/*! \brief Skip all values of the item and update idx accordingly */
int osmo_stat_item_discard(const struct osmo_stat_item *item, int32_t *idx);

/*! \brief Skip all values of all items and update idx accordingly */
int osmo_stat_item_discard_all(int32_t *idx);

typedef int (*osmo_stat_item_handler_t)(
	struct osmo_stat_item_group *, struct osmo_stat_item *, void *);

typedef int (*osmo_stat_item_group_handler_t)(struct osmo_stat_item_group *, void *);

/*! \brief Iteate over all items
 *  \param[in] handle_item Call-back function, aborts if rc < 0
 *  \param[in] data Private data handed through to \a handle_item
 */
int osmo_stat_item_for_each_item(struct osmo_stat_item_group *statg,
	osmo_stat_item_handler_t handle_item, void *data);

int osmo_stat_item_for_each_group(osmo_stat_item_group_handler_t handle_group, void *data);

static inline int32_t osmo_stat_item_get_last(const struct osmo_stat_item *item)
{
	return item->values[item->last_offs].value;
}
/*! @} */
