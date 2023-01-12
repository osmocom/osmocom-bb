#ifndef OSMOCOM_VTY_H
#define OSMOCOM_VTY_H

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/command.h>

#include <osmocom/bb/common/vty.h>

enum ms_vty_node {
	TESTSIM_NODE = _LAST_L23VTY_NODE + 1,
	SUPPORT_NODE,
	AUDIO_NODE,
};

int ms_vty_init(void);
extern void vty_notify(struct osmocom_ms *ms, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

#endif

