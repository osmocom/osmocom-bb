#ifndef osmocom_l1ctl_h
#define osmocom_l1ctl_h

#include <osmocom/core/msgb.h>
#include <osmocom/bb/common/osmocom_data.h>

struct osmocom_ms;

/* Receive incoming data from L1 using L1CTL format */
int l1ctl_recv(struct osmocom_ms *ms, struct msgb *msg);

/* Transmit L1CTL_DATA_REQ */
int l1ctl_tx_data_req(struct osmocom_ms *ms, struct msgb *msg, uint8_t chan_nr,
	uint8_t link_id);

/* Transmit L1CTL_PARAM_REQ */
int l1ctl_tx_param_req(struct osmocom_ms *ms, uint8_t ta, uint8_t tx_power);

int l1ctl_tx_crypto_req(struct osmocom_ms *ms, uint8_t algo, uint8_t *key,
	uint8_t len);

/* Transmit L1CTL_RACH_REQ */
int l1ctl_tx_rach_req(struct osmocom_ms *ms, uint8_t ra, uint16_t offset,
	uint8_t combined);

/* Transmit L1CTL_DM_EST_REQ */
int l1ctl_tx_dm_est_req_h0(struct osmocom_ms *ms, uint16_t band_arfcn,
	uint8_t chan_nr, uint8_t tsc, uint8_t tch_mode, uint8_t audio_mode);
int l1ctl_tx_dm_est_req_h1(struct osmocom_ms *ms, uint8_t maio, uint8_t hsn,
	uint16_t *ma, uint8_t ma_len, uint8_t chan_nr, uint8_t tsc,
	uint8_t tch_mode, uint8_t audio_mode);

/* Transmit L1CTL_DM_FREQ_REQ */
int l1ctl_tx_dm_freq_req_h0(struct osmocom_ms *ms, uint16_t band_arfcn,
	uint8_t tsc, uint16_t fn);
int l1ctl_tx_dm_freq_req_h1(struct osmocom_ms *ms, uint8_t maio, uint8_t hsn,
	uint16_t *ma, uint8_t ma_len, uint8_t tsc, uint16_t fn);

/* Transmit L1CTL_DM_REL_REQ */
int l1ctl_tx_dm_rel_req(struct osmocom_ms *ms);

/* Transmit FBSB_REQ */
int l1ctl_tx_fbsb_req(struct osmocom_ms *ms, uint16_t arfcn,
		      uint8_t flags, uint16_t timeout, uint8_t sync_info_idx,
		      uint8_t ccch_mode, uint8_t rxlev_exp);

/* Transmit CCCH_MODE_REQ */
int l1ctl_tx_ccch_mode_req(struct osmocom_ms *ms, uint8_t ccch_mode);

/* Transmit TCH_MODE_REQ */
int l1ctl_tx_tch_mode_req(struct osmocom_ms *ms, uint8_t tch_mode,
	uint8_t audio_mode);

/* Transmit ECHO_REQ */
int l1ctl_tx_echo_req(struct osmocom_ms *ms, unsigned int len);

/* Transmit L1CTL_RESET_REQ */
int l1ctl_tx_reset_req(struct osmocom_ms *ms, uint8_t type);

/* Transmit L1CTL_PM_REQ */
int l1ctl_tx_pm_req_range(struct osmocom_ms *ms, uint16_t arfcn_from,
			  uint16_t arfcn_to);

int l1ctl_tx_sim_req(struct osmocom_ms *ms, uint8_t *data, uint16_t length);

/* Transmit L1CTL_VOICE_REQ */
int l1ctl_tx_traffic_req(struct osmocom_ms *ms, struct msgb *msg,
			uint8_t chan_nr, uint8_t link_id);

/* LAPDm wants to send a PH-* primitive to the physical layer (L1) */
int l1ctl_ph_prim_cb(struct osmo_prim_hdr *oph, void *ctx);

/* Transmit L1CTL_NEIGH_PM_REQ */
int l1ctl_tx_neigh_pm_req(struct osmocom_ms *ms, int num, uint16_t *arfcn);

#endif
