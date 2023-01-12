#pragma once

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/command.h>

struct osmocom_ms;

enum l23_vty_node {
	MS_NODE = _LAST_OSMOVTY_NODE + 1,
	_LAST_L23VTY_NODE,
};

int l23_vty_init(int (*config_write_ms_node_cb)(struct vty *));

struct osmocom_ms *l23_vty_get_ms(const char *name, struct vty *vty);
void l23_ms_dump(struct osmocom_ms *ms, struct vty *vty);
void l23_vty_config_write_ms_node(struct vty *vty, const struct osmocom_ms *ms, const char *prefix);
void l23_vty_config_write_ms_node_contents(struct vty *vty, const struct osmocom_ms *ms, const char *prefix);

extern struct llist_head ms_list;

extern struct cmd_element l23_show_ms_cmd;
extern struct cmd_element l23_cfg_ms_cmd;
