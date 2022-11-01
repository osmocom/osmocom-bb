#pragma once

struct l1sched_state;

extern struct osmo_fsm trxcon_fsm_def;

enum trxcon_fsm_states {
	TRXCON_ST_RESET,
	TRXCON_ST_FULL_POWER_SCAN,
	TRXCON_ST_FBSB_SEARCH,
	TRXCON_ST_BCCH_CCCH,
	TRXCON_ST_DEDICATED,
	TRXCON_ST_PACKET_DATA,
};

enum trxcon_fsm_events {
	TRXCON_EV_PHYIF_FAILURE,
	TRXCON_EV_L2IF_FAILURE,
	TRXCON_EV_RESET_FULL_REQ,
	TRXCON_EV_RESET_SCHED_REQ,
	TRXCON_EV_FULL_POWER_SCAN_REQ,
	TRXCON_EV_FULL_POWER_SCAN_RES,
	TRXCON_EV_FBSB_SEARCH_REQ,
	TRXCON_EV_FBSB_SEARCH_RES,
	TRXCON_EV_SET_CCCH_MODE_REQ,
	TRXCON_EV_SET_TCH_MODE_REQ,
	TRXCON_EV_SET_PHY_CONFIG_REQ,
	TRXCON_EV_TX_ACCESS_BURST_REQ,
	TRXCON_EV_TX_ACCESS_BURST_CNF,
	TRXCON_EV_UPDATE_SACCH_CACHE_REQ,
	TRXCON_EV_DEDICATED_ESTABLISH_REQ,
	TRXCON_EV_DEDICATED_RELEASE_REQ,
	TRXCON_EV_TX_DATA_REQ,
	TRXCON_EV_TX_DATA_CNF,
	TRXCON_EV_RX_DATA_IND,
	TRXCON_EV_CRYPTO_REQ,
};

/* param of TRXCON_EV_FULL_POWER_SCAN_REQ */
struct trxcon_param_full_power_scan_req {
	uint16_t band_arfcn_start;
	uint16_t band_arfcn_stop;
};

/* param of TRXCON_EV_FULL_POWER_SCAN_RES */
struct trxcon_param_full_power_scan_res {
	bool last_result;
	uint16_t band_arfcn;
	int dbm;
};

/* param of TRXCON_EV_FBSB_SEARCH_REQ */
struct trxcon_param_fbsb_search_req {
	uint16_t band_arfcn;
	uint16_t timeout_ms;
	uint8_t pchan_config;
};

/* param of TRXCON_EV_SET_{CCCH,TCH}_MODE_REQ */
struct trxcon_param_set_ccch_tch_mode_req {
	uint8_t mode;
	struct {
		uint8_t start_codec;
		uint8_t codecs_bitmask;
	} amr;
	bool applied;
};

/* param of TRXCON_EV_SET_PHY_CONFIG_REQ */
struct trxcon_param_set_phy_config_req {
	enum {
		TRXCON_PHY_CFGT_PCHAN_COMB,
		TRXCON_PHY_CFGT_TX_PARAMS,
	} type;
	union {
		struct {
			uint8_t tn;
			uint8_t pchan;
		} pchan_comb;
		struct {
			uint8_t timing_advance;
			uint8_t tx_power;
		} tx_params;
	};
};

/* param of TRXCON_EV_TX_DATA_REQ */
struct trxcon_param_tx_data_req {
	bool traffic;
	uint8_t chan_nr;
	uint8_t link_id;
	size_t data_len;
	const uint8_t *data;
};

/* param of TRXCON_EV_TX_DATA_CNF */
struct trxcon_param_tx_data_cnf {
	bool traffic;
	uint8_t chan_nr;
	uint8_t link_id;
	uint16_t band_arfcn;
	uint32_t frame_nr;
};

/* param of TRXCON_EV_RX_DATA_IND */
struct trxcon_param_rx_data_ind {
	bool traffic;
	uint8_t chan_nr;
	uint8_t link_id;
	uint16_t band_arfcn;
	uint32_t frame_nr;
	int16_t toa256;
	int8_t rssi;
	int n_errors;
	int n_bits_total;
	size_t data_len;
	const uint8_t *data;
};

/* param of TRXCON_EV_TX_ACCESS_BURST_REQ */
struct trxcon_param_tx_access_burst_req {
	uint8_t chan_nr;
	uint8_t link_id;
	uint8_t offset;
	uint8_t synch_seq;
	uint16_t ra;
	bool is_11bit;
};

/* param of TRXCON_EV_TX_ACCESS_BURST_CNF */
struct trxcon_param_tx_access_burst_cnf {
	uint16_t band_arfcn;
	uint32_t frame_nr;
};

/* param of TRXCON_EV_DEDICATED_ESTABLISH_REQ */
struct trxcon_param_dedicated_establish_req {
	uint8_t chan_nr;
	uint8_t tch_mode;
	uint8_t tsc;

	bool hopping;
	union {
		struct { /* hopping=false */
			uint16_t band_arfcn;
		} h0;
		struct { /* hopping=true */
			uint8_t hsn;
			uint8_t maio;
			uint8_t n;
			uint16_t ma[64];
		} h1;
	};
};

/* param of TRXCON_EV_CRYPTO_REQ */
struct trxcon_param_crypto_req {
	uint8_t chan_nr;
	uint8_t a5_algo; /* 0 is A5/0 */
	uint8_t key_len;
	const uint8_t *key;
};

struct trxcon_inst {
	struct osmo_fsm_inst *fi;
	unsigned int id;

	/* Logging context for sched and l1c */
	const char *log_prefix;

	/* The L1 scheduler */
	struct l1sched_state *sched;
	/* PHY interface (e.g. TRXC/TRXD) */
	void *phyif;
	/* L2 interface (e.g. L1CTL) */
	void *l2if;

	/* L1 parameters */
	struct {
		uint16_t band_arfcn;
		uint8_t tx_power;
		int8_t ta;
	} l1p;
};

struct trxcon_inst *trxcon_inst_alloc(void *ctx, unsigned int id);
void trxcon_inst_free(struct trxcon_inst *trxcon);
