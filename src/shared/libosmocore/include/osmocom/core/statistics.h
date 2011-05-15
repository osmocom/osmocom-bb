#ifndef _STATISTICS_H
#define _STATISTICS_H

struct osmo_counter {
	struct llist_head list;
	const char *name;
	const char *description;
	unsigned long value;
};

static inline void osmo_counter_inc(struct osmo_counter *ctr)
{
	ctr->value++;
}

static inline unsigned long osmo_counter_get(struct osmo_counter *ctr)
{
	return ctr->value;
}

static inline void osmo_counter_reset(struct osmo_counter *ctr)
{
	ctr->value = 0;
}

struct osmo_counter *osmo_counter_alloc(const char *name);
void osmo_counter_free(struct osmo_counter *ctr);

int osmo_counters_for_each(int (*handle_counter)(struct osmo_counter *, void *), void *data);

struct osmo_counter *osmo_counter_get_by_name(const char *name);

#endif /* _STATISTICS_H */
