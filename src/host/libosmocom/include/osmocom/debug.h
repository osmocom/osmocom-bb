#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdio.h>
#include "linuxlist.h"

#define DEBUG

/* Debug Areas of the code */
enum {
	DRLL,
	DCC,
	DMM,
	DRR,
	DRSL,
	DNM,
	DMNCC,
	DSMS,
	DPAG,
	DMEAS,
	DMI,
	DMIB,
	DMUX,
	DINP,
	DSCCP,
	DMSC,
	DMGCP,
	DHO,
	DDB,
	DREF,
	Debug_LastEntry,
};

#ifdef DEBUG
#define DEBUGP(ss, fmt, args...) debugp(ss, __FILE__, __LINE__, 0, fmt, ## args)
#define DEBUGPC(ss, fmt, args...) debugp(ss, __FILE__, __LINE__, 1, fmt, ## args)
#else
#define DEBUGP(xss, fmt, args...)
#define DEBUGPC(ss, fmt, args...)
#endif


#define static_assert(exp, name) typedef int dummy##name [(exp) ? 1 : -1];

char *hexdump(const unsigned char *buf, int len);
void debugp(unsigned int subsys, char *file, int line, int cont, const char *format, ...) __attribute__ ((format (printf, 5, 6)));

/* new logging interface */
#define LOGP(ss, level, fmt, args...) debugp2(ss, level, __FILE__, __LINE__, 0, fmt, ##args)
#define LOGPC(ss, level, fmt, args...) debugp2(ss, level, __FILE__, __LINE__, 1, fmt, ##args)

/* different levels */
#define LOGL_DEBUG	1	/* debugging information */
#define LOGL_INFO	3
#define LOGL_NOTICE	5	/* abnormal/unexpected condition */
#define LOGL_ERROR	7	/* error condition, requires user action */
#define LOGL_FATAL	8	/* fatal, program aborted */

/* context */
#define BSC_CTX_LCHAN	0
#define BSC_CTX_SUBSCR	1
#define BSC_CTX_BTS	2
#define BSC_CTX_SCCP	3

/* target */

enum {
	DEBUG_FILTER_IMSI = 1 << 0,
	DEBUG_FILTER_ALL = 1 << 1,
};

struct debug_category {
	int enabled;
	int loglevel;
};

struct debug_target {
	int filter_map;
	char *imsi_filter;


	struct debug_category categories[Debug_LastEntry];
	int use_color;
	int print_timestamp;
	int loglevel;

	union {
		struct {
			FILE *out;
		} tgt_stdout;

		struct {
			int priority;
		} tgt_syslog;

		struct {
			void *vty;
		} tgt_vty;
	};

        void (*output) (struct debug_target *target, const char *string);

        struct llist_head entry;
};

/* use the above macros */
void debugp2(unsigned int subsys, unsigned int level, char *file, int line, int cont, const char *format, ...) __attribute__ ((format (printf, 6, 7)));
void debug_init(void);

/* context management */
void debug_reset_context(void);
void debug_set_context(int ctx, void *value);

/* filter on the targets */
void debug_set_imsi_filter(struct debug_target *target, const char *imsi);
void debug_set_all_filter(struct debug_target *target, int);
void debug_set_use_color(struct debug_target *target, int);
void debug_set_print_timestamp(struct debug_target *target, int);
void debug_set_log_level(struct debug_target *target, int log_level);
void debug_parse_category_mask(struct debug_target *target, const char* mask);
int debug_parse_level(const char *lvl);
int debug_parse_category(const char *category);
void debug_set_category_filter(struct debug_target *target, int category, int enable, int level);


/* management of the targets */
struct debug_target *debug_target_create(void);
struct debug_target *debug_target_create_stderr(void);
void debug_add_target(struct debug_target *target);
void debug_del_target(struct debug_target *target);
#endif /* _DEBUG_H */
