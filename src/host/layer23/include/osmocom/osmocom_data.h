#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocore/select.h>
#include <osmocore/gsm_utils.h>
#include <osmocore/write_queue.h>

#include <osmocom/lapdm.h>

struct osmocom_ms;

/* A layer2 entity */
struct osmol2_entity {
	struct lapdm_entity lapdm_dcch;
	struct lapdm_entity lapdm_acch;
	osmol2_cb_t msg_handler;
};

/* One Mobilestation for osmocom */
struct osmocom_ms {
	struct write_queue wq;
	enum gsm_band band;
	int arfcn;

	struct osmol2_entity l2_entity;
};

enum osmobb_sig_subsys {
	SS_L1CTL,
};

enum osmobb_meas_sig {
	S_L1CTL_RESET,
	S_L1CTL_PM_RES,
	S_L1CTL_PM_DONE,
};

struct osmobb_meas_res {
	struct osmocom_ms *ms;
	uint16_t band_arfcn;
	uint8_t rx_lev;
};

#endif
