#pragma once

#include <stdint.h>

struct msgb;
struct trxcon_param_rx_data_ind;
struct trxcon_param_tx_data_cnf;
struct trxcon_param_tx_access_burst_cnf;

int l1ctl_tx_fbsb_conf(struct trxcon_inst *trxcon, uint16_t band_arfcn, uint8_t bsic);
int l1ctl_tx_fbsb_fail(struct trxcon_inst *trxcon, uint16_t band_arfcn);
int l1ctl_tx_ccch_mode_conf(struct trxcon_inst *trxcon, uint8_t mode);
int l1ctl_tx_pm_conf(struct trxcon_inst *trxcon, uint16_t band_arfcn, int dbm, int last);
int l1ctl_tx_reset_conf(struct trxcon_inst *trxcon, uint8_t type);
int l1ctl_tx_reset_ind(struct trxcon_inst *trxcon, uint8_t type);

int l1ctl_tx_dt_ind(struct trxcon_inst *trxcon,
		    const struct trxcon_param_rx_data_ind *ind);
int l1ctl_tx_dt_conf(struct trxcon_inst *trxcon,
		    const struct trxcon_param_tx_data_cnf *cnf);
int l1ctl_tx_rach_conf(struct trxcon_inst *trxcon,
		       const struct trxcon_param_tx_access_burst_cnf *cnf);
