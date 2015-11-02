#pragma once

/*! \file statistics.h
 *  \brief Common routines regarding statistics */

/*! structure representing a single counter */
struct osmo_counter {
	struct llist_head list;		/*!< \brief internal list head */
	const char *name;		/*!< \brief human-readable name */
	const char *description;	/*!< \brief humn-readable description */
	unsigned long value;		/*!< \brief current value */
	unsigned long previous;		/*!< \brief previous value */
};

/*! \brief Increment counter */
static inline void osmo_counter_inc(struct osmo_counter *ctr)
{
	ctr->value++;
}

/*! \brief Get current value of counter */
static inline unsigned long osmo_counter_get(struct osmo_counter *ctr)
{
	return ctr->value;
}

/*! \brief Reset current value of counter to 0 */
static inline void osmo_counter_reset(struct osmo_counter *ctr)
{
	ctr->value = 0;
}

/*! \brief Allocate a new counter */
struct osmo_counter *osmo_counter_alloc(const char *name);

/*! \brief Free the specified counter
 *  \param[in] ctr Counter
 */
void osmo_counter_free(struct osmo_counter *ctr);

/*! \brief Iterate over all counters
 *  \param[in] handle_counter Call-back function, aborts if rc < 0
 *  \param[in] data Private dtata handed through to \a handle_counter
 */
int osmo_counters_for_each(int (*handle_counter)(struct osmo_counter *, void *), void *data);

/*! \brief Resolve counter by human-readable name
 *  \param[in] name human-readable name of counter
 *  \returns pointer to counter (\ref osmo_counter) or NULL otherwise
 */
struct osmo_counter *osmo_counter_get_by_name(const char *name);

/*! \brief Return the counter difference since the last call to this function */
int osmo_counter_difference(struct osmo_counter *ctr);
