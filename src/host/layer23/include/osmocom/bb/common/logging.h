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
	DGCC,
	DBCC,
	DSS,
	DSMS,
	DMNCC,
	DMEAS,
	DPAG,
	DL1C,
	DSAP,
	DSUM,
	DSIM,
	DGPS,
	DMOB,
	DPRIM,
	DLUA,
	DGAPK,
	DCSD,
	DTUN,
	DRLCMAC,
	DLLC,
	DSNDCP,
	DGMM,
	DSM
};

extern const struct log_info log_info;

#endif /* _LOGGING_H */
