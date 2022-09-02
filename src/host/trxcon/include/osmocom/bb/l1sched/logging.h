#pragma once

extern int l1sched_log_cat_common;
extern int l1sched_log_cat_data;

/* Messages using l1sched_state as the context */
#define LOGP_SCHED_CAT(sched, cat, level, fmt, args...) \
	LOGP(l1sched_log_cat_##cat, level, "%s" fmt, \
	     (sched)->log_prefix, ## args)

/* Common messages using l1sched_state as the context */
#define LOGP_SCHEDC(sched, level, fmt, args...) \
	LOGP_SCHED_CAT(sched, common, level, fmt, ## args)

/* Data messages using l1sched_state as the context */
#define LOGP_SCHEDD(sched, level, fmt, args...) \
	LOGP_SCHED_CAT(sched, common, level, fmt, ## args)


#define LOGP_LCHAN_NAME_FMT "TS%u-%s"
#define LOGP_LCHAN_NAME_ARGS(lchan) \
	(lchan)->ts->index, l1sched_lchan_desc[(lchan)->type].name

/* Messages using l1sched_lchan_state as the context */
#define LOGP_LCHAN_CAT(lchan, cat, level, fmt, args...) \
	LOGP_SCHED_CAT((lchan)->ts->sched, cat, level, LOGP_LCHAN_NAME_FMT " " fmt, \
		       LOGP_LCHAN_NAME_ARGS(lchan), ## args)

/* Common messages using l1sched_lchan_state as the context */
#define LOGP_LCHANC(lchan, level, fmt, args...) \
	LOGP_LCHAN_CAT(lchan, common, level, fmt, ## args)

/* Data messages using l1sched_lchan_state as the context */
#define LOGP_LCHAND(lchan, level, fmt, args...) \
	LOGP_LCHAN_CAT(lchan, data, level, fmt, ## args)
