#ifndef osmocom_layer2_h
#define osmocom_layer2_h

#include <osmocom/msgb.h>

struct osmocom_ms;

int osmo_recv(struct osmocom_ms *ms, struct msgb *msg);

extern int osmo_send_l1(struct osmocom_ms *ms, struct msgb *msg);

#endif
