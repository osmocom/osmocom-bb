#pragma once

#include <stdint.h>
#include <osmocom/core/msgb.h>

#include "l1ctl_link.h"
#include "l1ctl_proto.h"

/* Event handlers */
int l1ctl_rx_cb(struct l1ctl_link *l1l, struct msgb *msg);
void l1ctl_shutdown_cb(struct l1ctl_link *l1l);

int l1ctl_tx_fbsb_conf(struct l1ctl_link *l1l, uint8_t result,
	struct l1ctl_info_dl *dl_info, uint8_t bsic);
int l1ctl_tx_ccch_mode_conf(struct l1ctl_link *l1l, uint8_t mode);
int l1ctl_tx_pm_conf(struct l1ctl_link *l1l, uint16_t band_arfcn,
	int dbm, int last);
int l1ctl_tx_reset_conf(struct l1ctl_link *l1l, uint8_t type);
int l1ctl_tx_reset_ind(struct l1ctl_link *l1l, uint8_t type);

int l1ctl_tx_data_ind(struct l1ctl_link *l1l,
	struct l1ctl_info_dl *data, uint8_t msg_type);
int l1ctl_tx_data_conf(struct l1ctl_link *l1l,
	struct l1ctl_info_dl *data, uint8_t msg_type);
int l1ctl_tx_rach_conf(struct l1ctl_link *l1l, uint32_t fn);
