#ifndef _CONTROL_CMD_H
#define _CONTROL_CMD_H

#include <osmocom/core/msgb.h>
#include <osmocom/core/write_queue.h>

#include <osmocom/vty/vector.h>

#define CTRL_CMD_ERROR		-1
#define CTRL_CMD_HANDLED	0
#define CTRL_CMD_REPLY		1

enum ctrl_node_type {
	CTRL_NODE_ROOT,	/* Root elements */
	CTRL_NODE_NET,	/* Network specific (net.) */
	CTRL_NODE_BTS,	/* BTS specific (net.btsN.) */
	CTRL_NODE_TRX,	/* TRX specific (net.btsN.trxM.) */
	CTRL_NODE_TS,	/* TS specific (net.btsN.trxM.tsI.) */
	_LAST_CTRL_NODE
};

enum ctrl_type {
	CTRL_TYPE_UNKNOWN,
	CTRL_TYPE_GET,
	CTRL_TYPE_SET,
	CTRL_TYPE_GET_REPLY,
	CTRL_TYPE_SET_REPLY,
	CTRL_TYPE_TRAP,
	CTRL_TYPE_ERROR
};

struct ctrl_connection {
	struct llist_head list_entry;

	/* The queue for sending data back */
	struct osmo_wqueue write_queue;

	/* Callback if the connection was closed */
	void (*closed_cb)(struct ctrl_connection *conn);

	/* Pending commands for this connection */
	struct llist_head cmds;
};

struct ctrl_cmd {
	struct ctrl_connection *ccon;
	enum ctrl_type type;
	char *id;
	void *node;
	char *variable;
	char *value;
	char *reply;
};

struct ctrl_cmd_struct {
	int nr_commands;
	char **command;
};

struct ctrl_cmd_element {
	const char *name;
	const char *param;
	struct ctrl_cmd_struct strcmd;
	int (*set)(struct ctrl_cmd *cmd, void *data);
	int (*get)(struct ctrl_cmd *cmd, void *data);
	int (*verify)(struct ctrl_cmd *cmd, const char *value, void *data);
};

struct ctrl_cmd_map {
	char *cmd;
	enum ctrl_type type;
};

int ctrl_cmd_exec(vector vline, struct ctrl_cmd *command, vector node, void *data);
int ctrl_cmd_install(enum ctrl_node_type node, struct ctrl_cmd_element *cmd);
int ctrl_cmd_handle(struct ctrl_cmd *cmd, void *data);
int ctrl_cmd_send(struct osmo_wqueue *queue, struct ctrl_cmd *cmd);
struct ctrl_cmd *ctrl_cmd_parse(void *ctx, struct msgb *msg);
struct msgb *ctrl_cmd_make(struct ctrl_cmd *cmd);

#endif /* _CONTROL_CMD_H */
