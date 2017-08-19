#pragma once

#include <osmocom/core/logging.h>

#define DEBUG_DEFAULT "DAPP:DL1C:DL1D:DTRX:DTRXD:DSCH"

enum {
	DAPP,
	DL1C,
	DL1D,
	DTRX,
	DTRXD,
	DSCH,
};

int trx_log_init(const char *category_mask);
