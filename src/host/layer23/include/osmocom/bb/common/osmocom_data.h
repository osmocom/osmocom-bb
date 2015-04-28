#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocom/core/select.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/write_queue.h>

struct osmocom_ms;

	/* FIXME no 'mobile' specific stuff should be here */
#include <osmocom/bb/mobile/support.h>
#include <osmocom/bb/mobile/settings.h>
#include <osmocom/bb/mobile/subscriber.h>
#include <osmocom/gsm/lapdm.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/mobile/gsm48_rr.h>
#include <osmocom/bb/common/sysinfo.h>
#include <osmocom/bb/mobile/gsm322.h>
#include <osmocom/bb/mobile/gsm48_mm.h>
#include <osmocom/bb/mobile/gsm48_cc.h>
#include <osmocom/bb/mobile/mncc_sock.h>
#include <osmocom/bb/common/sim.h>
#include <osmocom/bb/common/l1ctl.h>

struct osmosap_entity {
	osmosap_cb_t msg_handler;
	uint8_t sap_state;
	uint16_t max_msg_size;
};

struct osmol1_entity {
	int (*l1_traffic_ind)(struct osmocom_ms *ms, struct msgb *msg);
};

struct osmomncc_entity {
	int (*mncc_recv)(struct osmocom_ms *ms, int msg_type, void *arg);
	struct mncc_sock_state *sock_state;
	uint32_t ref;
};


/* RX measurement statistics */
struct rx_meas_stat {
	uint32_t last_fn;

	/* cumulated values of current cell from SACCH dl */
	uint32_t frames;
	uint32_t snr;
	uint32_t berr;
	uint32_t rxlev;

	/* counters loss criterion */
	int16_t dsc, ds_fail;
	int16_t s, rl_fail;
};

/* One Mobilestation for osmocom */
struct osmocom_ms {
	struct llist_head entity;
	char name[32];
	struct osmo_wqueue l2_wq, sap_wq;
	uint16_t test_arfcn;
	struct osmol1_entity l1_entity;

	uint8_t deleting, shutdown, started;
	struct gsm_support support;
	struct gsm_settings settings;
	struct gsm_subscriber subscr;
	struct gsm_sim sim;
	struct lapdm_channel lapdm_channel;
	struct osmosap_entity sap_entity;
	struct rx_meas_stat meas;
	struct gsm48_rrlayer rrlayer;
	struct gsm322_plmn plmn;
	struct gsm322_cellsel cellsel;
	struct gsm48_mmlayer mmlayer;
	struct gsm48_cclayer cclayer;
	struct osmomncc_entity mncc_entity;
	struct llist_head trans_list;
};

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

#endif
