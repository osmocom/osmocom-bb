#ifndef _OSMOCORE_LOGGING_H
#define _OSMOCORE_LOGGING_H

#include <stdio.h>
#include <stdint.h>
#include <osmocore/linuxlist.h>

#define LOG_MAX_CATEGORY	32
#define LOG_MAX_CTX		8
#define LOG_MAX_FILTERS	8

#define DEBUG

#ifdef DEBUG
#define DEBUGP(ss, fmt, args...) logp(ss, __FILE__, __LINE__, 0, fmt, ## args)
#define DEBUGPC(ss, fmt, args...) logp(ss, __FILE__, __LINE__, 1, fmt, ## args)
#else
#define DEBUGP(xss, fmt, args...)
#define DEBUGPC(ss, fmt, args...)
#endif

#define static_assert(exp, name) typedef int dummy##name [(exp) ? 1 : -1];

void logp(unsigned int subsys, char *file, int line, int cont, const char *format, ...) __attribute__ ((format (printf, 5, 6)));

/* new logging interface */
#define LOGP(ss, level, fmt, args...) \
	logp2(ss, level, __FILE__, __LINE__, 0, fmt, ##args)
#define LOGPC(ss, level, fmt, args...) \
	logp2(ss, level, __FILE__, __LINE__, 1, fmt, ##args)

/* different levels */
#define LOGL_DEBUG	1	/* debugging information */
#define LOGL_INFO	3
#define LOGL_NOTICE	5	/* abnormal/unexpected condition */
#define LOGL_ERROR	7	/* error condition, requires user action */
#define LOGL_FATAL	8	/* fatal, program aborted */

#define LOG_FILTER_ALL	0x0001

struct log_category {
	uint8_t loglevel;
	uint8_t enabled;
};

struct log_info_cat {
	const char *name;
	const char *color;
	const char *description;
	uint8_t loglevel;
	uint8_t enabled;
};

/* log context information, passed to filter */
struct log_context {
	void *ctx[LOG_MAX_CTX+1];
};

struct log_target;

typedef int log_filter(const struct log_context *ctx,
		       struct log_target *target);

struct log_info {
	/* filter callback function */
	log_filter *filter_fn;

	/* per-category information */
	const struct log_info_cat *cat;
	unsigned int num_cat;
};

struct log_target {
        struct llist_head entry;

	int filter_map;
	void *filter_data[LOG_MAX_FILTERS+1];

	struct log_category categories[LOG_MAX_CATEGORY+1];
	uint8_t loglevel;
	int use_color:1;
	int print_timestamp:1;

	union {
		struct {
			FILE *out;
			const char *fname;
		} tgt_file;

		struct {
			int priority;
		} tgt_syslog;

		struct {
			void *vty;
		} tgt_vty;
	};

        void (*output) (struct log_target *target, const char *string);
};

/* use the above macros */
void logp2(unsigned int subsys, unsigned int level, char *file,
	   int line, int cont, const char *format, ...)
				__attribute__ ((format (printf, 6, 7)));
void log_init(const struct log_info *cat);

/* context management */
void log_reset_context(void);
int log_set_context(uint8_t ctx, void *value);

/* filter on the targets */
void log_set_all_filter(struct log_target *target, int);

void log_set_use_color(struct log_target *target, int);
void log_set_print_timestamp(struct log_target *target, int);
void log_set_log_level(struct log_target *target, int log_level);
void log_parse_category_mask(struct log_target *target, const char* mask);
int log_parse_level(const char *lvl);
const char *log_level_str(unsigned int lvl);
int log_parse_category(const char *category);
void log_set_category_filter(struct log_target *target, int category,
			       int enable, int level);

/* management of the targets */
struct log_target *log_target_create(void);
void log_target_destroy(struct log_target *target);
struct log_target *log_target_create_stderr(void);
struct log_target *log_target_create_file(const char *fname);
int log_target_file_reopen(struct log_target *tgt);

void log_add_target(struct log_target *target);
void log_del_target(struct log_target *target);

/* Gernerate command argument strings for VTY use */
const char *log_vty_category_string(struct log_info *info);
const char *log_vty_level_string(struct log_info *info);

#endif /* _OSMOCORE_LOGGING_H */
