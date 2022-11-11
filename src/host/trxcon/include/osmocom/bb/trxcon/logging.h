#pragma once

#include <osmocom/core/logging.h>

enum {
	DAPP,
	DL1C,
	DL1D,
	DTRXC,
	DTRXD,
	DSCH,
	DSCHD,
};

int trxcon_logging_init(void *tall_ctx, const char *category_mask);
