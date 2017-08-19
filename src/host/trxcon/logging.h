#pragma once

#include <osmocom/core/logging.h>

#define DEBUG_DEFAULT "DAPP:DL1C:DL1D:DTRX:DTRXD:DSCH:DSCHD"

enum {
	DAPP,
	DL1C,
	DL1D,
	DTRX,
	DTRXD,
	DSCH,
	DSCHD,
};

int trx_log_init(const char *category_mask);
