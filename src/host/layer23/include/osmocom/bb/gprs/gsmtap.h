#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

int gsmtap_init(const char *addr);
void gsmtap_send_rlcmac(uint8_t *msg, size_t len, uint8_t ts, bool ul);
void gsmtap_send_llc(uint8_t *data, size_t len, bool ul);
