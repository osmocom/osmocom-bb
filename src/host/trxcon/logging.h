#pragma once

#include <osmocom/core/logging.h>

#define DEBUG_DEFAULT "DAPP:DL1C"

enum {
	DAPP,
	DL1C,
};

int trx_log_init(const char *category_mask);
