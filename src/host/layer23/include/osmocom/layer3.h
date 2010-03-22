#ifndef _OSMOCOM_L3_H
#define _OSMOCOM_L3_H

#include <osmocore/msgb.h>
#include <osmocom/osmocom_data.h>

int gsm48_rx_ccch(struct msgb *msg, struct osmocom_ms *ms);
int gsm48_rx_dcch(struct msgb *msg, struct osmocom_ms *ms);
int gsm48_rx_bcch(struct msgb *msg, struct osmocom_ms *ms);

#endif
