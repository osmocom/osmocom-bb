/* Mobile Station */
#pragma once

#include <osmocom/core/select.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/write_queue.h>

#include <osmocom/gsm/lapdm.h>
#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/common/subscriber.h>
#include <osmocom/bb/common/support.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/common/sap_proto.h>
#include <osmocom/bb/mobile/gsm48_rr.h>
#include <osmocom/bb/common/sysinfo.h>
#include <osmocom/bb/mobile/gsm322.h>
#include <osmocom/bb/mobile/gsm48_mm.h>
#include <osmocom/bb/mobile/gsm48_cc.h>
#include <osmocom/bb/mobile/mncc_sock.h>
#include <osmocom/bb/common/sim.h>
#include <osmocom/bb/common/l1ctl.h>

struct osmobb_ms_gmm_layer {
	uint8_t ac_ref_nr;
	uint8_t key_seq;
	uint8_t rand[16];
	uint32_t tlli;
};

struct osmosap_entity {
	struct osmo_fsm_inst *fi;
	uint16_t max_msg_size;

	/* Current state of remote SIM card */
	enum sap_card_status_type card_status;

	/* Optional SAP message call-back */
	sap_msg_cb_t sap_msg_cb;
	/* Optional response call-back */
	sap_rsp_cb_t sap_rsp_cb;
};

struct osmol1_entity {
	int (*l1_traffic_ind)(struct osmocom_ms *ms, struct msgb *msg);
	int (*l1_gprs_dl_block_ind)(struct osmocom_ms *ms, struct msgb *msg);
	int (*l1_gprs_rts_ind)(struct osmocom_ms *ms, struct msgb *msg);
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

enum osmobb_ms_shutdown_st {
	MS_SHUTDOWN_NONE = 0,
	MS_SHUTDOWN_IMSI_DETACH = 1,
	MS_SHUTDOWN_WAIT_RESET = 2,
	MS_SHUTDOWN_COMPL = 3,
};

struct osmocom_ms {
	struct llist_head entity;
	char *name;
	struct osmo_wqueue l2_wq, sap_wq;
	uint16_t test_arfcn;
	struct osmol1_entity l1_entity;

	bool started, deleting;
	enum osmobb_ms_shutdown_st shutdown;
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

	/* GPRS */
	struct gprs_settings gprs;
	struct osmobb_ms_gmm_layer gmmlayer;
	struct osmo_fsm_inst *grr_fi;

	struct tch_state *tch_state;

	void *lua_state;
	int lua_cb_ref;
	char *lua_script;
};

struct osmocom_ms *osmocom_ms_alloc(void *ctx, const char *name);

extern uint16_t cfg_test_arfcn;
