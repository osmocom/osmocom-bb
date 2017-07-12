#pragma once

#include <osmocom/core/logging.h>

#define DL1C 0
#define DVIRPHY 1

extern const struct log_info ms_log_info;

int ms_log_init(char *cat_mask);
const char *getL1ctlPrimName(uint8_t type);
