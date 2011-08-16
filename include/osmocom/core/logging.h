#ifndef _OSMOCORE_LOGGING_H
#define _OSMOCORE_LOGGING_H

/*! \file logging.h
 *  \brief Osmocom logging framework
 */

#include <stdio.h>
#include <stdint.h>
#include <osmocom/core/linuxlist.h>

/*! \brief Maximum number of logging contexts */
#define LOG_MAX_CTX		8
/*! \brief Maximum number of logging filters */
#define LOG_MAX_FILTERS	8

#define DEBUG

#ifdef DEBUG
#define DEBUGP(ss, fmt, args...) logp(ss, __FILE__, __LINE__, 0, fmt, ## args)
#define DEBUGPC(ss, fmt, args...) logp(ss, __FILE__, __LINE__, 1, fmt, ## args)
#else
#define DEBUGP(xss, fmt, args...)
#define DEBUGPC(ss, fmt, args...)
#endif


void logp(int subsys, char *file, int line, int cont, const char *format, ...) __attribute__ ((format (printf, 5, 6)));

/*! \brief Log a new message through the Osmocom logging framework
 *  \param[in] ss logging subsystem (e.g. \ref DLGLOBAL)
 *  \param[in] level logging level (e.g. \ref LOGL_NOTICE)
 *  \param[in] fmt format string
 *  \param[in] args variable argument list
 */
#define LOGP(ss, level, fmt, args...) \
	logp2(ss, level, __FILE__, __LINE__, 0, fmt, ##args)

/*! \brief Continue a log message through the Osmocom logging framework
 *  \param[in] ss logging subsystem (e.g. \ref DLGLOBAL)
 *  \param[in] level logging level (e.g. \ref LOGL_NOTICE)
 *  \param[in] fmt format string
 *  \param[in] args variable argument list
 */
#define LOGPC(ss, level, fmt, args...) \
	logp2(ss, level, __FILE__, __LINE__, 1, fmt, ##args)

/* different levels */
#define LOGL_DEBUG	1	/* debugging information */
#define LOGL_INFO	3
#define LOGL_NOTICE	5	/* abnormal/unexpected condition */
#define LOGL_ERROR	7	/* error condition, requires user action */
#define LOGL_FATAL	8	/* fatal, program aborted */

#define LOG_FILTER_ALL	0x0001

/* logging levels defined by the library itself */
#define DLGLOBAL	-1
#define DLLAPDM		-2
#define DLINP		-3
#define DLMUX		-4
#define DLMI		-5
#define DLMIB		-6
#define OSMO_NUM_DLIB	6

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
	struct log_info_cat *cat;
	unsigned int num_cat;
	unsigned int num_cat_user;
};

enum log_target_type {
	LOG_TGT_TYPE_VTY,
	LOG_TGT_TYPE_SYSLOG,
	LOG_TGT_TYPE_FILE,
	LOG_TGT_TYPE_STDERR,
};

struct log_target {
        struct llist_head entry;

	int filter_map;
	void *filter_data[LOG_MAX_FILTERS+1];

	struct log_category *categories;

	uint8_t loglevel;
	unsigned int use_color:1;
	unsigned int print_timestamp:1;

	enum log_target_type type;

	union {
		struct {
			FILE *out;
			const char *fname;
		} tgt_file;

		struct {
			int priority;
			int facility;
		} tgt_syslog;

		struct {
			void *vty;
		} tgt_vty;
	};

        void (*output) (struct log_target *target, unsigned int level,
			const char *string);
};

/* use the above macros */
void logp2(int subsys, unsigned int level, char *file,
	   int line, int cont, const char *format, ...)
				__attribute__ ((format (printf, 6, 7)));
int log_init(const struct log_info *inf, void *talloc_ctx);

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
struct log_target *log_target_create_syslog(const char *ident, int option,
					    int facility);
int log_target_file_reopen(struct log_target *tgt);

/*! \brief Add a new logging target
 */
void log_add_target(struct log_target *target);

/*! \brief Deelete an existing logging target
 */
void log_del_target(struct log_target *target);

/* Generate command string for VTY use */
const char *log_vty_command_string(const struct log_info *info);
const char *log_vty_command_description(const struct log_info *info);

struct log_target *log_target_find(int type, const char *fname);
extern struct llist_head osmo_log_target_list;

#endif /* _OSMOCORE_LOGGING_H */
