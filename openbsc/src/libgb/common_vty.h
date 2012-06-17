#include <osmocom/vty/command.h>
#include <osmocom/core/logging.h>

extern int DNS, DBSSGP;

enum log_filter {
	_FLT_ALL = LOG_FILTER_ALL,	/* libosmocore */
	FLT_NSVC = 1,
	FLT_BVC  = 2,
};

extern struct cmd_element libgb_exit_cmd;
extern struct cmd_element libgb_end_cmd;

