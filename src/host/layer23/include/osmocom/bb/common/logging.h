#ifndef _LOGGING_H
#define _LOGGING_H

#define DEBUG
#include <osmocom/core/logging.h>

enum {
	DRSL,
	DRR,
	DPLMN,
	DCS,
	DNB,
	DMM,
	DCC,
	DSMS,
	DMNCC,
	DMEAS,
	DPAG,
	DLAPDM,
	DL1C,
	DSAP,
	DSUM,
	DSIM,
	DGPS,
};

extern const struct log_info log_info;

#endif /* _LOGGING_H */
