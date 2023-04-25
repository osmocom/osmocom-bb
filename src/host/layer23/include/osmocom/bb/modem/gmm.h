#pragma once

#include <stdbool.h>

struct osmocom_ms;

int modem_gmm_init(struct osmocom_ms *ms);

int modem_gmm_gmmreg_attach_req(const struct osmocom_ms *ms);
int modem_gmm_gmmreg_detach_req(const struct osmocom_ms *ms);
