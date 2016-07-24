#pragma once

#include <stdint.h>
#include <osmocom/core/msgb.h>

#include "l1ctl_link.h"

int l1ctl_tx_pm_conf(struct l1ctl_link *l1l, uint16_t band_arfcn,
	int dbm, int last);
int l1ctl_tx_reset_conf(struct l1ctl_link *l1l, uint8_t type);
int l1ctl_tx_reset_ind(struct l1ctl_link *l1l, uint8_t type);
int l1ctl_rx_cb(struct l1ctl_link *l1l, struct msgb *msg);
