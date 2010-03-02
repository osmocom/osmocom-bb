#ifndef osmocom_layer2_h
#define osmocom_layer2_h

#include <osmocore/msgb.h>

struct osmocom_ms;

int osmo_recv(struct osmocom_ms *ms, struct msgb *msg);

int tx_ph_rach_req(struct osmocom_ms *ms);
int tx_ph_dm_est_req(struct osmocom_ms *ms, uint16_t band_arfcn, uint8_t chan_nr);

extern int osmo_send_l1(struct osmocom_ms *ms, struct msgb *msg);


#endif
