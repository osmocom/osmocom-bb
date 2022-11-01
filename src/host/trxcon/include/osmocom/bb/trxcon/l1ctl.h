#pragma once

#include <stdint.h>
#include <osmocom/core/msgb.h>

#include <osmocom/bb/trxcon/l1ctl_server.h>
#include <osmocom/bb/trxcon/l1ctl_proto.h>

/* Event handlers */
int l1ctl_rx_cb(struct l1ctl_client *l1c, struct msgb *msg);

int l1ctl_tx_fbsb_conf(struct l1ctl_client *l1c, uint16_t band_arfcn, uint8_t bsic);
int l1ctl_tx_fbsb_fail(struct l1ctl_client *l1c, uint16_t band_arfcn);
int l1ctl_tx_ccch_mode_conf(struct l1ctl_client *l1c, uint8_t mode);
int l1ctl_tx_pm_conf(struct l1ctl_client *l1c, uint16_t band_arfcn,
	int dbm, int last);
int l1ctl_tx_reset_conf(struct l1ctl_client *l1c, uint8_t type);
int l1ctl_tx_reset_ind(struct l1ctl_client *l1c, uint8_t type);

int l1ctl_tx_dt_ind(struct l1ctl_client *l1c, bool traffic,
		    const struct trxcon_param_rx_traffic_data_ind *ind);
int l1ctl_tx_dt_conf(struct l1ctl_client *l1c,
	struct l1ctl_info_dl *data, bool traffic);
int l1ctl_tx_rach_conf(struct l1ctl_client *l1c,
	uint16_t band_arfcn, uint32_t fn);
