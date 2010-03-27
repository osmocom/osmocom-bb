#ifndef osmocom_l1ctl_h
#define osmocom_l1ctl_h

#include <osmocore/msgb.h>
#include <osmocom/osmocom_data.h>

struct osmocom_ms;

/* Receive incoming data from L1 using L1CTL format */
int l1ctl_recv(struct osmocom_ms *ms, struct msgb *msg);

/* Transmit L1CTL_DATA_REQ */
int tx_ph_data_req(struct osmocom_ms *ms, struct msgb *msg,
		   uint8_t chan_nr, uint8_t link_id);

/* Transmit L1CTL_RACH_REQ */
int tx_ph_rach_req(struct osmocom_ms *ms);

/* Transmit L1CTL_DM_EST_REQ */
int tx_ph_dm_est_req(struct osmocom_ms *ms, uint16_t band_arfcn, uint8_t chan_nr);

int l1ctl_tx_echo_req(struct osmocom_ms *ms, unsigned int len);

/* Transmit L1CTL_PM_REQ */
int l1ctl_tx_pm_req_range(struct osmocom_ms *ms, uint16_t arfcn_from,
			  uint16_t arfcm_to);

extern int osmo_send_l1(struct osmocom_ms *ms, struct msgb *msg);


#endif
