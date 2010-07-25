#ifndef osmocom_l1ctl_h
#define osmocom_l1ctl_h

#include <osmocore/msgb.h>
#include <osmocom/bb/common/osmocom_data.h>

struct osmocom_ms;

/* Receive incoming data from L1 using L1CTL format */
int l1ctl_recv(struct osmocom_ms *ms, struct msgb *msg);

/* Transmit L1CTL_DATA_REQ */
int l1ctl_tx_data_req(struct osmocom_ms *ms, struct msgb *msg, uint8_t chan_nr,
	uint8_t link_id);

/* Transmit L1CTL_PARAM_REQ */
int l1ctl_tx_param_req(struct osmocom_ms *ms, uint8_t ta, uint8_t tx_power);

/* Transmit L1CTL_RACH_REQ */
int l1ctl_tx_rach_req(struct osmocom_ms *ms, uint8_t ra, uint8_t fn51,
	uint8_t mf_off);

/* Transmit L1CTL_DM_EST_REQ */
int l1ctl_tx_dm_est_req_h0(struct osmocom_ms *ms, uint16_t band_arfcn,
	uint8_t chan_nr, uint8_t tsc);
int l1ctl_tx_dm_est_req_h1(struct osmocom_ms *ms, uint8_t maio, uint8_t hsn,
	uint16_t *ma, uint8_t ma_len, uint8_t chan_nr, uint8_t tsc);

/* Transmit L1CTL_DM_REL_REQ */
int l1ctl_tx_dm_rel_req(struct osmocom_ms *ms);

/* Transmit FBSB_REQ */
int l1ctl_tx_fbsb_req(struct osmocom_ms *ms, uint16_t arfcn,
		      uint8_t flags, uint16_t timeout, uint8_t sync_info_idx,
		      uint8_t ccch_mode);

/* Transmit CCCH_MODE_REQ */
int l1ctl_tx_ccch_mode_req(struct osmocom_ms *ms, uint8_t ccch_mode);

/* Transmit ECHO_REQ */
int l1ctl_tx_echo_req(struct osmocom_ms *ms, unsigned int len);

/* Transmit L1CTL_RESET_REQ */
int l1ctl_tx_reset_req(struct osmocom_ms *ms, uint8_t type);

/* Transmit L1CTL_PM_REQ */
int l1ctl_tx_pm_req_range(struct osmocom_ms *ms, uint16_t arfcn_from,
			  uint16_t arfcm_to);

#endif
