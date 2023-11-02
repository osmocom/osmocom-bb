#ifndef OSMOCOM_VTY_H
#define OSMOCOM_VTY_H

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/command.h>

#include <osmocom/bb/common/vty.h>

enum ms_vty_node {
	SUPPORT_NODE = _LAST_L23VTY_NODE + 1,
	TCH_VOICE_NODE,
	TCH_DATA_NODE,
	VGCS_NODE,
	VBS_NODE,
};

int ms_vty_init(void);
#endif

