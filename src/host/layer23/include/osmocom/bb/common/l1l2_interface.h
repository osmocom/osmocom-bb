#ifndef _L1L2_INTERFACE_H
#define _L1L2_INTERFACE_H

#include <osmocom/core/msgb.h>

#define L2_DEFAULT_SOCKET_PATH "/tmp/osmocom_l2"

int layer2_open(struct osmocom_ms *ms, const char *socket_path);
int layer2_close(struct osmocom_ms *ms);
int osmo_send_l1(struct osmocom_ms *ms, struct msgb *msg);

#endif /* _L1L2_INTERFACE_H */
