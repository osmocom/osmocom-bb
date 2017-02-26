#pragma once

#include <osmocom/core/select.h>
#include <osmocom/core/msgb.h>
#include "osmo_mcast_sock.h"

#define VIRT_UM_MSGB_SIZE	256
#define DEFAULT_MS_MCAST_GROUP "224.0.0.1"
#define DEFAULT_MS_MCAST_PORT 4729 /* IANA-registered port for GSMTAP */
#define DEFAULT_BTS_MCAST_GROUP "225.0.0.1"
#define DEFAULT_BTS_MCAST_PORT 4729 /* IANA-registered port for GSMTAP */

struct virt_um_inst {
	void *priv;
	struct mcast_bidir_sock *mcast_sock;
	void (*recv_cb)(struct virt_um_inst *vui, struct msgb *msg);
};

struct virt_um_inst *virt_um_init(
                void *ctx, char *tx_mcast_group, uint16_t tx_mcast_port,
                char *rx_mcast_group, uint16_t rx_mcast_port,
                void (*recv_cb)(struct virt_um_inst *vui, struct msgb *msg));

void virt_um_destroy(struct virt_um_inst *vui);

int virt_um_write_msg(struct virt_um_inst *vui, struct msgb *msg);
