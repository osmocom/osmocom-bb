#pragma once

/*! \defgroup logging Osmocom logging framework
 *  @{
 */

/*! \file logging.h */

#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <osmocom/core/linuxlist.h>

/*! \brief Maximum number of logging contexts */
#define LOG_MAX_CTX		8
/*! \brief Maximum number of logging filters */
#define LOG_MAX_FILTERS	8

#define DEBUG

#ifdef DEBUG
/*! \brief Log a debug message through the Osmocom logging framework
 *  \param[in] ss logging subsystem (e.g. \ref DLGLOBAL)
 *  \param[in] fmt format string
 *  \param[in] args variable argument list
 */
#define DEBUGP(ss, fmt, args...) \
	do { \
		if (log_check_level(ss, LOGL_DEBUG)) \
			logp(ss, __BASE_FILE__, __LINE__, 0, fmt, ## args); \
	} while(0)

#define DEBUGPC(ss, fmt, args...) \
	do { \
		if (log_check_level(ss, LOGL_DEBUG)) \
			logp(ss, __BASE_FILE__, __LINE__, 1, fmt, ## args); \
	} while(0)

#else
#define DEBUGP(xss, fmt, args...)
#define DEBUGPC(ss, fmt, args...)
#endif


void osmo_vlogp(int subsys, int level, const char *file, int line,
		int cont, const char *format, va_list ap);

void logp(int subsys, const char *file, int line, int cont, const char *format, ...) __attribute__ ((format (printf, 5, 6)));

/*! \brief Log a new message through the Osmocom logging framework
 *  \param[in] ss logging subsystem (e.g. \ref DLGLOBAL)
 *  \param[in] level logging level (e.g. \ref LOGL_NOTICE)
 *  \param[in] fmt format string
 *  \param[in] args variable argument list
 */
#define LOGP(ss, level, fmt, args...) \
	LOGPSRC(ss, level, NULL, 0, fmt, ## args)

/*! \brief Continue a log message through the Osmocom logging framework
 *  \param[in] ss logging subsystem (e.g. \ref DLGLOBAL)
 *  \param[in] level logging level (e.g. \ref LOGL_NOTICE)
 *  \param[in] fmt format string
 *  \param[in] args variable argument list
 */
#define LOGPC(ss, level, fmt, args...) \
	do { \
		if (log_check_level(ss, level)) \
			logp2(ss, level, __BASE_FILE__, __LINE__, 1, fmt, ##args); \
	} while(0)

/*! \brief Log through the Osmocom logging framework with explicit source.
 *  If caller_file is passed as NULL, __BASE_FILE__ and __LINE__ are used
 *  instead of caller_file and caller_line (so that this macro here defines
 *  both cases in the same place, and to catch cases where callers fail to pass
 *  a non-null filename string).
 *  \param[in] ss logging subsystem (e.g. \ref DLGLOBAL)
 *  \param[in] level logging level (e.g. \ref LOGL_NOTICE)
 *  \param[in] caller_file caller's source file string (e.g. __BASE_FILE__)
 *  \param[in] caller_line caller's source line nr (e.g. __LINE__)
 *  \param[in] fmt format string
 *  \param[in] args variable argument list
 */
#define LOGPSRC(ss, level, caller_file, caller_line, fmt, args...) \
	do { \
		if (log_check_level(ss, level)) {\
			if (caller_file) \
				logp2(ss, level, caller_file, caller_line, 0, fmt, ##args); \
			else \
				logp2(ss, level, __BASE_FILE__, __LINE__, 0, fmt, ##args); \
		}\
	} while(0)

/*! \brief different log levels */
#define LOGL_DEBUG	1	/*!< \brief debugging information */
#define LOGL_INFO	3	/*!< \brief general information */
#define LOGL_NOTICE	5	/*!< \brief abnormal/unexpected condition */
#define LOGL_ERROR	7	/*!< \brief error condition, requires user action */
#define LOGL_FATAL	8	/*!< \brief fatal, program aborted */

#define LOG_FILTER_ALL	0x0001

/* logging levels defined by the library itself */
#define DLGLOBAL	-1	/*!< global logging */
#define DLLAPD		-2	/*!< LAPD implementation */
#define DLINP		-3	/*!< (A-bis) Input sub-system */
#define DLMUX		-4	/*!< Osmocom Multiplex (Osmux) */
#define DLMI		-5	/*!< ISDN-layer below input sub-system */
#define DLMIB		-6	/*!< ISDN layer B-channel */
#define DLSMS		-7	/*!< SMS sub-system */
#define DLCTRL		-8	/*!< Control Interface */
#define DLGTP		-9	/*!< GTP (GPRS Tunneling Protocol */
#define DLSTATS		-10	/*!< Statistics */
#define DLGSUP		-11	/*!< Generic Subscriber Update Protocol */
#define DLOAP		-12	/*!< Osmocom Authentication Protocol */
#define OSMO_NUM_DLIB	12	/*!< Number of logging sub-systems in libraries */

/*! Configuration of singgle log category / sub-system */
struct log_category {
	uint8_t loglevel;	/*!< configured log-level */
	uint8_t enabled;	/*!< is logging enabled? */
};

/*! \brief Information regarding one logging category */
struct log_info_cat {
	const char *name;		/*!< name of category */
	const char *color;		/*!< color string for cateyory */
	const char *description;	/*!< description text */
	uint8_t loglevel;		/*!< currently selected log-level */
	uint8_t enabled;		/*!< is this category enabled or not */
};

/*! \brief Log context information, passed to filter */
struct log_context {
	void *ctx[LOG_MAX_CTX+1];
};

struct log_target;

/*! \brief Log filter function */
typedef int log_filter(const struct log_context *ctx,
		       struct log_target *target);

struct log_info;
struct vty;
struct gsmtap_inst;

typedef void log_print_filters(struct vty *vty,
			       const struct log_info *info,
			       const struct log_target *tgt);

typedef void log_save_filters(struct vty *vty,
			      const struct log_info *info,
			      const struct log_target *tgt);

/*! \brief Logging configuration, passed to \ref log_init */
struct log_info {
	/* \brief filter callback function */
	log_filter *filter_fn;

	/*! \brief per-category information */
	const struct log_info_cat *cat;
	/*! \brief total number of categories */
	unsigned int num_cat;
	/*! \brief total number of user categories (not library) */
	unsigned int num_cat_user;

	/*! \brief filter saving function */
	log_save_filters *save_fn;
	/*! \brief filter saving function */
	log_print_filters *print_fn;
};

/*! \brief Type of logging target */
enum log_target_type {
	LOG_TGT_TYPE_VTY,	/*!< \brief VTY logging */
	LOG_TGT_TYPE_SYSLOG,	/*!< \brief syslog based logging */
	LOG_TGT_TYPE_FILE,	/*!< \brief text file logging */
	LOG_TGT_TYPE_STDERR,	/*!< \brief stderr logging */
	LOG_TGT_TYPE_STRRB,	/*!< \brief osmo_strrb-backed logging */
	LOG_TGT_TYPE_GSMTAP,	/*!< \brief GSMTAP network logging */
};

/*! \brief structure representing a logging target */
struct log_target {
        struct llist_head entry;		/*!< \brief linked list */

	/*! \brief Internal data for filtering */
	int filter_map;
	/*! \brief Internal data for filtering */
	void *filter_data[LOG_MAX_FILTERS+1];

	/*! \brief logging categories */
	struct log_category *categories;

	/*! \brief global log level */
	uint8_t loglevel;
	/*! \brief should color be used when printing log messages? */
	unsigned int use_color:1;
	/*! \brief should log messages be prefixed with a timestamp? */
	unsigned int print_timestamp:1;
	/*! \brief should log messages be prefixed with a filename? */
	unsigned int print_filename:1;
	/*! \brief should log messages be prefixed with a category name? */
	unsigned int print_category:1;
	/*! \brief should log messages be prefixed with an extended timestamp? */
	unsigned int print_ext_timestamp:1;

	/*! \brief the type of this log taget */
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

		struct {
			void *rb;
		} tgt_rb;

		struct {
			struct gsmtap_inst *gsmtap_inst;
			const char *ident;
			const char *hostname;
		} tgt_gsmtap;
	};

	/*! \brief call-back function to be called when the logging framework
	 *	   wants to log a fully formatted string
	 *  \param[in] target logging target
	 *  \param[in] level log level of currnet message
	 *  \param[in] string the string that is to be written to the log
	 */
        void (*output) (struct log_target *target, unsigned int level,
			const char *string);

	/*! \brief alternative call-back function to which the logging
	 *	   framework passes the unfortmatted input arguments,
	 *	   i.e. bypassing the internal string formatter
	 *  \param[in] target logging target
	 *  \param[in] subsys logging sub-system
	 *  \param[in] level logging level
	 *  \param[in] file soure code file name
	 *  \param[in] line source code file line number
	 *  \param[in] cont continuation of previous statement?
	 *  \param[in] format format string
	 *  \param[in] ap vararg list of printf arguments
	 */
	void (*raw_output)(struct log_target *target, int subsys,
			   unsigned int level, const char *file, int line,
			   int cont, const char *format, va_list ap);
};

/* use the above macros */
void logp2(int subsys, unsigned int level, const char *file,
	   int line, int cont, const char *format, ...)
				__attribute__ ((format (printf, 6, 7)));
int log_init(const struct log_info *inf, void *talloc_ctx);
void log_fini(void);
int log_check_level(int subsys, unsigned int level);

/* context management */
void log_reset_context(void);
int log_set_context(uint8_t ctx, void *value);

/* filter on the targets */
void log_set_all_filter(struct log_target *target, int);

void log_set_use_color(struct log_target *target, int);
void log_set_print_extended_timestamp(struct log_target *target, int);
void log_set_print_timestamp(struct log_target *target, int);
void log_set_print_filename(struct log_target *target, int);
void log_set_print_category(struct log_target *target, int);
void log_set_log_level(struct log_target *target, int log_level);
void log_parse_category_mask(struct log_target *target, const char* mask);
const char* log_category_name(int subsys);
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
struct log_target *log_target_create_gsmtap(const char *host, uint16_t port,
					    const char *ident,
					    bool ofd_wq_mode,
					    bool add_sink);
int log_target_file_reopen(struct log_target *tgt);
int log_targets_reopen(void);

void log_add_target(struct log_target *target);
void log_del_target(struct log_target *target);

/* Generate command string for VTY use */
const char *log_vty_command_string(const struct log_info *info);
const char *log_vty_command_description(const struct log_info *info);

struct log_target *log_target_find(int type, const char *fname);
extern struct llist_head osmo_log_target_list;

/*! @} */
