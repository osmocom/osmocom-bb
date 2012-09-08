#ifndef _RATE_CTR_H
#define _RATE_CTR_H

/*! \defgroup rate_ctr Rate counters
 *  @{
 */

/*! \file rate_ctr.h */

#include <stdint.h>

#include <osmocom/core/linuxlist.h>

/*! \brief Number of rate counter intervals */
#define RATE_CTR_INTV_NUM	4

/*! \brief Rate counter interval */
enum rate_ctr_intv {
	RATE_CTR_INTV_SEC,	/*!< \brief last second */
	RATE_CTR_INTV_MIN,	/*!< \brief last minute */
	RATE_CTR_INTV_HOUR,	/*!< \brief last hour */
	RATE_CTR_INTV_DAY,	/*!< \brief last day */
};

/*! \brief data we keep for each of the intervals */
struct rate_ctr_per_intv {
	uint64_t last;		/*!< \brief counter value in last interval */
	uint64_t rate;		/*!< \brief counter rate */
};

/*! \brief data we keep for each actual value */
struct rate_ctr {
	uint64_t current;	/*!< \brief current value */
	/*! \brief per-interval data */
	struct rate_ctr_per_intv intv[RATE_CTR_INTV_NUM];
};

/*! \brief rate counter description */
struct rate_ctr_desc {
	const char *name;	/*!< \brief name of the counter */
	const char *description;/*!< \brief description of the counter */
};

/*! \brief description of a rate counter group */
struct rate_ctr_group_desc {
	/*! \brief The prefix to the name of all counters in this group */
	const char *group_name_prefix;
	/*! \brief The human-readable description of the group */
	const char *group_description;
	/*! \brief The number of counters in this group */
	const unsigned int num_ctr;
	/*! \brief Pointer to array of counter names */
	const struct rate_ctr_desc *ctr_desc;
};

/*! \brief One instance of a counter group class */
struct rate_ctr_group {
	/*! \brief Linked list of all counter groups in the system */
	struct llist_head list;
	/*! \brief Pointer to the counter group class */
	const struct rate_ctr_group_desc *desc;
	/*! \brief The index of this ctr_group within its class */
	unsigned int idx;
	/*! \brief Actual counter structures below */
	struct rate_ctr ctr[0];
};

struct rate_ctr_group *rate_ctr_group_alloc(void *ctx,
					    const struct rate_ctr_group_desc *desc,
					    unsigned int idx);

void rate_ctr_group_free(struct rate_ctr_group *grp);

void rate_ctr_add(struct rate_ctr *ctr, int inc);

/*! \brief Increment the counter by 1 */
static inline void rate_ctr_inc(struct rate_ctr *ctr)
{
	rate_ctr_add(ctr, 1);
}

int rate_ctr_init(void *tall_ctx);

struct rate_ctr_group *rate_ctr_get_group_by_name_idx(const char *name, const unsigned int idx);
const struct rate_ctr *rate_ctr_get_by_name(const struct rate_ctr_group *ctrg, const char *name);

/*! @} */
#endif /* RATE_CTR_H */
