#pragma once

#include <stdbool.h>
#include <stdint.h>

struct msgb;
struct osmocom_ms;
struct lapdm_entity;

int modem_grr_rslms_cb(struct msgb *msg, struct lapdm_entity *le, void *ctx);
int modem_grr_tx_chan_req(struct osmocom_ms *ms, uint8_t chan_req);
uint8_t modem_grr_gen_chan_req(bool single_block);
