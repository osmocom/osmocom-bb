#pragma once

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/command.h>
#include <osmocom/core/signal.h>

struct osmocom_ms;

enum l23_vty_node {
	MS_NODE = _LAST_OSMOVTY_NODE + 1,
	TESTSIM_NODE,
	GSMTAP_NODE,
	_LAST_L23VTY_NODE,
};

int l23_vty_init(int (*config_write_ms_node_cb)(struct vty *), osmo_signal_cbfn *l23_vty_signal_cb);

struct osmocom_ms *l23_vty_get_ms(const char *name, struct vty *vty);
void l23_ms_dump(struct osmocom_ms *ms, struct vty *vty);
void l23_vty_config_write_ms_node(struct vty *vty, const struct osmocom_ms *ms, const char *prefix);
void l23_vty_config_write_ms_node_contents(struct vty *vty, const struct osmocom_ms *ms, const char *prefix);
void l23_vty_config_write_ms_node_contents_final(struct vty *vty, const struct osmocom_ms *ms, const char *prefix);

extern void l23_vty_ms_notify(struct osmocom_ms *ms, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

extern bool l23_vty_reading;
extern bool l23_vty_hide_default;

extern struct llist_head ms_list;

extern struct cmd_element l23_show_ms_cmd;
extern struct cmd_element l23_cfg_ms_cmd;
