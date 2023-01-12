#pragma once

#include <stdint.h>

struct osmocom_ms;
struct gapk_io_state;

enum osmobb_sig_subsys {
	SS_L1CTL,
	SS_GLOBAL,
};

enum osmobb_l1ctl_sig {
	S_L1CTL_FBSB_ERR,
	S_L1CTL_FBSB_RESP,
	S_L1CTL_RESET,
	S_L1CTL_PM_RES,
	S_L1CTL_PM_DONE,
	S_L1CTL_CCCH_MODE_CONF,
	S_L1CTL_TCH_MODE_CONF,
	S_L1CTL_LOSS_IND,
	S_L1CTL_NEIGH_PM_IND,
};

enum osmobb_global_sig {
	S_GLOBAL_SHUTDOWN,
};

struct osmobb_fbsb_res {
	struct osmocom_ms *ms;
	int8_t snr;
	uint8_t bsic;
	uint16_t band_arfcn;
};

struct osmobb_meas_res {
	struct osmocom_ms *ms;
	uint16_t band_arfcn;
	uint8_t rx_lev;
};

struct osmobb_ccch_mode_conf {
	struct osmocom_ms *ms;
	uint8_t ccch_mode;
};

struct osmobb_tch_mode_conf {
	struct osmocom_ms *ms;
	uint8_t tch_mode;
	uint8_t audio_mode;
};

struct osmobb_neigh_pm_ind {
	struct osmocom_ms *ms;
	uint16_t band_arfcn;
	uint8_t rx_lev;
};
