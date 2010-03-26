#ifndef _OSMOCORE_DEBUG_H
#define _OSMOCORE_DEBUG_H

#include <stdio.h>
#include <stdint.h>
#include <osmocore/linuxlist.h>

#define DEBUG_MAX_CATEGORY	32
#define DEBUG_MAX_CTX		8
#define DEBUG_MAX_FILTERS	8

#define DEBUG

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
#define LOGP(ss, level, fmt, args...) \
	debugp2(ss, level, __FILE__, __LINE__, 0, fmt, ##args)
#define LOGPC(ss, level, fmt, args...) \
	debugp2(ss, level, __FILE__, __LINE__, 1, fmt, ##args)

/* different levels */
#define LOGL_DEBUG	1	/* debugging information */
#define LOGL_INFO	3
#define LOGL_NOTICE	5	/* abnormal/unexpected condition */
#define LOGL_ERROR	7	/* error condition, requires user action */
#define LOGL_FATAL	8	/* fatal, program aborted */

#define DEBUG_FILTER_ALL	0x0001

struct debug_category {
	uint8_t loglevel;
	uint8_t enabled;
};

struct debug_info_cat {
	const char *name;
	const char *color;
	const char *description;
	int number;
	uint8_t loglevel;
	uint8_t enabled;
};

/* debug context information, passed to filter */
struct debug_context {
	void *ctx[DEBUG_MAX_CTX+1];
};

struct debug_target;

typedef int debug_filter(const struct debug_context *ctx,
			 struct debug_target *target);

struct debug_info {
	/* filter callback function */
	debug_filter *filter_fn;

	/* per-category information */
	const struct debug_info_cat *cat;
	unsigned int num_cat;
};

struct debug_target {
        struct llist_head entry;

	int filter_map;
	void *filter_data[DEBUG_MAX_FILTERS+1];

	struct debug_category categories[DEBUG_MAX_CATEGORY+1];
	uint8_t loglevel;
	int use_color:1;
	int print_timestamp:1;

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
};

/* use the above macros */
void debugp2(unsigned int subsys, unsigned int level, char *file,
	     int line, int cont, const char *format, ...)
				__attribute__ ((format (printf, 6, 7)));
void debug_init(const struct debug_info *cat);

/* context management */
void debug_reset_context(void);
int debug_set_context(uint8_t ctx, void *value);

/* filter on the targets */
void debug_set_all_filter(struct debug_target *target, int);

void debug_set_use_color(struct debug_target *target, int);
void debug_set_print_timestamp(struct debug_target *target, int);
void debug_set_log_level(struct debug_target *target, int log_level);
void debug_parse_category_mask(struct debug_target *target, const char* mask);
int debug_parse_level(const char *lvl);
int debug_parse_category(const char *category);
void debug_set_category_filter(struct debug_target *target, int category,
			       int enable, int level);

/* management of the targets */
struct debug_target *debug_target_create(void);
struct debug_target *debug_target_create_stderr(void);
void debug_add_target(struct debug_target *target);
void debug_del_target(struct debug_target *target);

#endif /* _OSMOCORE_DEBUG_H */
