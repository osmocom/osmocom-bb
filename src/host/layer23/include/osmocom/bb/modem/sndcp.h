#pragma once

#include <stdbool.h>

struct osmocom_ms;
struct osmobb_apn;

int modem_sndcp_init(struct osmocom_ms *ms);
int modem_sndcp_sn_xid_req(struct osmobb_apn *apn);
int modem_sndcp_sn_unitdata_req(struct osmobb_apn *apn, uint8_t *npdu, size_t npdu_len);
