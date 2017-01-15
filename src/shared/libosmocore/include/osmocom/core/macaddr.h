#pragma once

int osmo_macaddr_parse(uint8_t *out, const char *in);
int osmo_get_macaddr(uint8_t *mac_out, const char *dev_name);
