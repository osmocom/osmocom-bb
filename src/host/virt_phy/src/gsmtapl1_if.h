#pragma once

#include <osmocom/core/msgb.h>
#include <osmocom/core/gsmtap.h>

#include "l1ctl_sock.h"
#include "virtual_um.h"
#include "virt_l1_model.h"

void gsmtapl1_init(struct l1_model_ms *model);

void gsmtapl1_rx_from_virt_um_inst_cb(struct virt_um_inst *vui, struct msgb *msg);
void gsmtapl1_rx_from_virt_um(struct msgb *msg);

void gsmtapl1_tx_to_virt_um_inst(struct virt_um_inst *vui, uint8_t tn, uint32_t fn, uint8_t gsmtap_chan, struct msgb *msg);
void gsmtapl1_tx_to_virt_um(uint8_t tn, uint32_t fn, uint8_t gsmtap_chan, struct msgb *msg);

uint8_t chantype_gsmtap2rsl(uint8_t gsmtap_chantype);
