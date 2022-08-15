#pragma once

#include <osmocom/core/logging.h>

/* L1CTL related messages */
enum virtphy_log_cat {
	DL1C,
	DL1P,
	DVIRPHY,
	DGPRS,
	DMAIN
};

#define LOGPMS(ss, lvl, ms, fmt, args ...)	LOGP(ss, lvl, "MS %04u: " fmt, ms->nr, ## args)
#define DEBUGPMS(ss, ms, fmt, args ...)		DEBUGP(ss, "MS %04u: " fmt, ms->nr, ## args)

extern const struct log_info ms_log_info;

int ms_log_init(char *cat_mask);
const char *getL1ctlPrimName(uint8_t type);
