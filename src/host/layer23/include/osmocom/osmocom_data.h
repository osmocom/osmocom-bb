#ifndef osmocom_data_h
#define osmocom_data_h

#include <osmocore/select.h>
#include <osmocore/gsm_utils.h>
#include <osmocore/write_queue.h>

struct osmocom_ms;

#include <osmocom/support.h>
#include <osmocom/subscriber.h>
#include <osmocom/lapdm.h>
#include <osmocom/gsm48_rr.h>
#include <osmocom/sysinfo.h>
#include <osmocom/gsm322.h>
#include <osmocom/gsm48_mm.h>
#include <osmocom/gsm48_cc.h>

/* A layer2 entity */
struct osmol2_entity {
	struct lapdm_entity lapdm_dcch;
	struct lapdm_entity lapdm_acch;
	osmol2_cb_t msg_handler;
};

/* One Mobilestation for osmocom */
struct osmocom_ms {
	char name[32];
	struct write_queue wq;
	uint16_t test_arfcn;

	struct gsm_support support;

	struct gsm_subscriber subscr;

	struct osmol2_entity l2_entity;

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
	S_L1CTL_CCCH_RESP,
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
