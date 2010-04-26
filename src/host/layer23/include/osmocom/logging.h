#ifndef _LOGGING_H
#define _LOGGING_H

#define DEBUG
#include <osmocore/logging.h>

enum {
	DRSL,
	DRR,
	DPLMN,
	DCS,
	DMM,
	DCC,
	DSMS,
	DMNCC,
	DMEAS,
	DPAG,
	DLAPDM,
	DL1C,
};

extern const struct log_info log_info;

#endif /* _LOGGING_H */
