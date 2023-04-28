#pragma once

#include <stdbool.h>
#include <stdint.h>

struct osmocom_ms;

int modem_gmm_init(struct osmocom_ms *ms);

int modem_gmm_gmmreg_attach_req(const struct osmocom_ms *ms);
int modem_gmm_gmmreg_detach_req(const struct osmocom_ms *ms);
int modem_gmm_gmmreg_sim_auth_rsp(const struct osmocom_ms *ms,
				  uint8_t *sres, uint8_t *kc, uint8_t kc_len);
