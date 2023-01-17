#pragma once

#include <stdbool.h>

struct osmocom_ms;

int modem_llc_init(struct osmocom_ms *ms, const char *cipher_plugin_path);

