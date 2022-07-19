#pragma once

#include <stdint.h>
#include <osmocom/core/msgb.h>

#include <osmocom/bb/trxcon/l1ctl_link.h>
#include <osmocom/bb/trxcon/l1ctl_proto.h>

/* Event handlers */
int l1ctl_rx_cb(struct l1ctl_client *l1c, struct msgb *msg);

int l1ctl_tx_fbsb_conf(struct l1ctl_client *l1c, uint8_t result,
		       const struct l1ctl_info_dl *dl_info, uint8_t bsic);
int l1ctl_tx_ccch_mode_conf(struct l1ctl_client *l1c, uint8_t mode);
int l1ctl_tx_pm_conf(struct l1ctl_client *l1c, uint16_t band_arfcn,
	int dbm, int last);
int l1ctl_tx_reset_conf(struct l1ctl_client *l1c, uint8_t type);
int l1ctl_tx_reset_ind(struct l1ctl_client *l1c, uint8_t type);

int l1ctl_tx_dt_ind(struct l1ctl_client *l1c,
		    const struct l1ctl_info_dl *dl_info,
		    const uint8_t *l2, size_t l2_len,
		    bool traffic);
int l1ctl_tx_dt_conf(struct l1ctl_client *l1c,
	struct l1ctl_info_dl *data, bool traffic);
int l1ctl_tx_rach_conf(struct l1ctl_client *l1c,
	uint16_t band_arfcn, uint32_t fn);
