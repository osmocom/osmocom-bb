#pragma once

#include <osmocom/core/write_queue.h>
#include <osmocom/ctrl/control_cmd.h>

int ctrl_parse_get_num(vector vline, int i, long *num);

typedef int (*ctrl_cmd_lookup)(void *data, vector vline, int *node_type,
				void **node_data, int *i);

struct ctrl_handle {
	struct osmo_fd listen_fd;
	void *data;

	ctrl_cmd_lookup lookup;

	/* List of control connections */
	struct llist_head ccon_list;
};


int ctrl_cmd_send(struct osmo_wqueue *queue, struct ctrl_cmd *cmd);
struct ctrl_handle *ctrl_interface_setup(void *data, uint16_t port,
					 ctrl_cmd_lookup lookup);
struct ctrl_handle *ctrl_interface_setup_dynip(void *data,
					       const char *bind_addr,
					       uint16_t port,
					       ctrl_cmd_lookup lookup);

int ctrl_cmd_handle(struct ctrl_handle *ctrl, struct ctrl_cmd *cmd, void *data);
