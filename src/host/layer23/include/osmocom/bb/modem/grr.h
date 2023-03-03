#pragma once

struct msgb;
struct lapdm_entity;

int modem_grr_rslms_cb(struct msgb *msg, struct lapdm_entity *le, void *ctx);
