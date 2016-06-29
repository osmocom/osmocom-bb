#pragma once

#include <osmocom/core/logging.h>

#define DEBUG_DEFAULT "DAPP"

enum {
	DAPP
};

int trx_log_init(const char *category_mask);
