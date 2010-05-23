#ifndef _RATE_CTR_H
#define _RATE_CTR_H

#include <stdint.h>

#include <osmocore/linuxlist.h>

#define RATE_CTR_INTV_NUM	4

enum rate_ctr_intv {
	RATE_CTR_INTV_SEC,
	RATE_CTR_INTV_MIN,
	RATE_CTR_INTV_HOUR,
	RATE_CTR_INTV_DAY,
};

/* for each of the intervals, we keep the following values */
struct rate_ctr_per_intv {
	uint64_t last;
	uint64_t rate;
};

/* for each actual value, we keep the following data */
struct rate_ctr {
	uint64_t current;
	struct rate_ctr_per_intv intv[RATE_CTR_INTV_NUM];
};

struct rate_ctr_desc {
	const char *name;
	const char *description;
};

/* Describe a counter group class */
struct rate_ctr_group_desc {
	/* The prefix to the name of all counters in this group */
	const char *group_name_prefix;
	/* The human-readable description of the group */
	const char *group_description;
	/* The number of counters in this group */
	const unsigned int num_ctr;
	/* Pointer to array of counter names */
	const struct rate_ctr_desc *ctr_desc;
};

/* One instance of a counter group class */
struct rate_ctr_group {
	/* Linked list of all counter groups in the system */
	struct llist_head list;
	/* Pointer to the counter group class */
	const struct rate_ctr_group_desc *desc;
	/* The index of this ctr_group within its class */
	unsigned int idx;
	/* Actual counter structures below */
	struct rate_ctr ctr[0];
};

/* Allocate a new group of counters according to description */
struct rate_ctr_group *rate_ctr_group_alloc(void *ctx,
					    const struct rate_ctr_group_desc *desc,
					    unsigned int idx);

/* Free the memory for the specified group of counters */
void rate_ctr_group_free(struct rate_ctr_group *grp);

/* Add a number to the counter */
void rate_ctr_add(struct rate_ctr *ctr, int inc);

/* Increment the counter by 1 */
static inline void rate_ctr_inc(struct rate_ctr *ctr)
{
	rate_ctr_add(ctr, 1);
}

/* Initialize the counter module */
int rate_ctr_init(void *tall_ctx);

struct vty;
void vty_out_rate_ctr_group(struct vty *vty, const char *prefix,
			    struct rate_ctr_group *ctrg);
#endif /* RATE_CTR_H */
