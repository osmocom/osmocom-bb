#pragma once

#include <osmocom/bb/common/vty.h>

enum modem_vty_node {
	APN_NODE = _LAST_L23VTY_NODE + 1,
};

int modem_vty_init(void);
int modem_vty_go_parent(struct vty *vty);
