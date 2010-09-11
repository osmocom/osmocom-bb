#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocore/select.h>
#include <osmocore/gsm_utils.h>
#include <osmocore/write_queue.h>

struct osmocom_ms;

	/* FIXME no 'mobile' specific stuff should be here */
#include <osmocom/bb/mobile/support.h>
#include <osmocom/bb/mobile/settings.h>
#include <osmocom/bb/mobile/subscriber.h>
#include <osmocom/bb/common/lapdm.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/mobile/gsm48_rr.h>
#include <osmocom/bb/mobile/sysinfo.h>
#include <osmocom/bb/mobile/gsm322.h>
#include <osmocom/bb/mobile/gsm48_mm.h>
#include <osmocom/bb/mobile/gsm48_cc.h>

/* A layer2 entity */
struct osmol2_entity {
	struct lapdm_entity lapdm_dcch;
	struct lapdm_entity lapdm_acch;
	osmol2_cb_t msg_handler;
};

struct osmosap_entity {
	osmosap_cb_t msg_handler;
};

/* RX measurement statistics */
struct rx_meas_stat {
	uint32_t last_fn;
	uint32_t frames;
	uint32_t snr;
	uint32_t berr;
	uint32_t rxlev;
};

/* One Mobilestation for osmocom */
struct osmocom_ms {
	struct llist_head entity;
	char name[32];
	struct write_queue l2_wq, sap_wq;
	uint16_t test_arfcn;

	struct gsm_support support;

	struct gsm_settings settings;

	struct gsm_subscriber subscr;

	struct osmol2_entity l2_entity;

	struct osmosap_entity sap_entity;

	struct rx_meas_stat meas;

	struct gsm48_rrlayer rrlayer;
	struct gsm322_plmn plmn;
	struct gsm322_cellsel cellsel;
	struct gsm48_mmlayer mmlayer;
	struct gsm48_cclayer cclayer;
	struct llist_head trans_list;
};

enum osmobb_sig_subsys {
	SS_L1CTL,
};

enum osmobb_meas_sig {
	S_L1CTL_FBSB_ERR,
	S_L1CTL_FBSB_RESP,
	S_L1CTL_RESET,
	S_L1CTL_PM_RES,
	S_L1CTL_PM_DONE,
	S_L1CTL_CCCH_MODE_CONF,
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

#endif
