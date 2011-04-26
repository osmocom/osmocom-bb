#ifndef _OSMOCOM_L3_H
#define _OSMOCOM_L3_H

#include <osmocom/core/msgb.h>
#include <osmocom/bb/common/osmocom_data.h>

int gsm48_rx_ccch(struct msgb *msg, struct osmocom_ms *ms);
int gsm48_rx_dcch(struct msgb *msg, struct osmocom_ms *ms);
int gsm48_rx_bcch(struct msgb *msg, struct osmocom_ms *ms);

/* Initialize layer3 for the MS, hook it to L2 */
int layer3_init(struct osmocom_ms *ms);

/* Reset the 'aplication' state */
void layer3_app_reset(void);

#endif
