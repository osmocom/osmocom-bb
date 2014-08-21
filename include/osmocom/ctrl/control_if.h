#ifndef _CONTROL_IF_H
#define _CONTROL_IF_H

#include <osmocom/core/write_queue.h>
#include <osmocom/ctrl/control_cmd.h>

typedef int (*ctrl_cmd_handler)(struct ctrl_cmd *, void *);

struct ctrl_handle {
	struct osmo_fd listen_fd;
	void *data;

	ctrl_cmd_handler handler;

	/* List of control connections */
	struct llist_head ccon_list;
};


int ctrl_cmd_send(struct osmo_wqueue *queue, struct ctrl_cmd *cmd);
struct ctrl_handle *controlif_setup(void *data, uint16_t port,
					ctrl_cmd_handler handler);

int bsc_ctrl_cmd_handle(struct ctrl_cmd *cmd, void *data);

#endif /* _CONTROL_IF_H */

