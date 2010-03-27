#ifndef _LOGGING_H
#define _LOGGING_H

#define DEBUG
#include <osmocore/logging.h>

enum {
	DRSL,
	DRR,
	DMM,
	DCC,
	DSMS,
	DMEAS,
	DPAG,
	DLAPDM,
	DL1C,
};

extern const struct log_info log_info;

#endif /* _LOGGING_H */
