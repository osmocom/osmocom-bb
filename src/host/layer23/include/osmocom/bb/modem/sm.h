#pragma once

#include <stdbool.h>

struct osmocom_ms;

int modem_sm_init(struct osmocom_ms *ms);
int modem_sm_smreg_pdp_act_req(const struct osmocom_ms *ms, const struct osmobb_apn *apn);
