#pragma once

#include <osmocom/core/logging.h>

#define DEBUG_DEFAULT "DAPP:DL1C:DTRX"

enum {
	DAPP,
	DL1C,
	DTRX,
};

int trx_log_init(const char *category_mask);
