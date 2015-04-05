/* SNMP-like status interface
 *
 * (C) 2010-2011 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2010-2011 by On-Waves
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <osmocom/ctrl/control_cmd.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>

#include <osmocom/vty/command.h>
#include <osmocom/vty/vector.h>

extern vector ctrl_node_vec;

static struct ctrl_cmd_map ccm[] = {
	{"GET", CTRL_TYPE_GET},
	{"SET", CTRL_TYPE_SET},
	{"GET_REPLY", CTRL_TYPE_GET_REPLY},
	{"SET_REPLY", CTRL_TYPE_SET_REPLY},
	{"TRAP", CTRL_TYPE_TRAP},
	{"ERROR", CTRL_TYPE_ERROR},
	{NULL}
};

static int ctrl_cmd_str2type(char *s)
{
	int i;
	for (i=0; ccm[i].cmd != NULL; i++) {
		if (strcasecmp(s, ccm[i].cmd) == 0)
			return ccm[i].type;
	}
	return CTRL_TYPE_UNKNOWN;
}

static char *ctrl_cmd_type2str(int type)
{
	int i;
	for (i=0; ccm[i].cmd != NULL; i++) {
		if (ccm[i].type == type)
			return ccm[i].cmd;
	}
	return NULL;
}

/* Functions from libosmocom */
extern vector cmd_make_descvec(const char *string, const char *descstr);

/* Get the ctrl_cmd_element that matches this command */
static struct ctrl_cmd_element *ctrl_cmd_get_element_match(vector vline, vector node)
{
	int index, j;
	const char *desc;
	struct ctrl_cmd_element *cmd_el;
	struct ctrl_cmd_struct *cmd_desc;
	char *str;

	for (index = 0; index < vector_active(node); index++) {
		if ((cmd_el = vector_slot(node, index))) {
			cmd_desc = &cmd_el->strcmd;
			if (cmd_desc->nr_commands > vector_active(vline))
				continue;
			for (j =0; j < vector_active(vline) && j < cmd_desc->nr_commands; j++) {
				str = vector_slot(vline, j);
				desc = cmd_desc->command[j];
				if (desc[0] == '*')
					return cmd_el; /* Partial match */
				if (strcmp(desc, str) != 0)
					break;
			}
			/* We went through all the elements and all matched */
			if (j == cmd_desc->nr_commands)
				return cmd_el;
		}
	}

	return NULL;
}

int ctrl_cmd_exec(vector vline, struct ctrl_cmd *command, vector node, void *data)
{
	int ret = CTRL_CMD_ERROR;
	struct ctrl_cmd_element *cmd_el;

	if ((command->type != CTRL_TYPE_GET) && (command->type != CTRL_TYPE_SET)) {
		command->reply = "Trying to execute something not GET or SET";
		goto out;
	}
	if ((command->type == CTRL_TYPE_SET) && (!command->value)) {
		command->reply = "SET without a value";
		goto out;
	}

	if (!vline)
		goto out;

	cmd_el = ctrl_cmd_get_element_match(vline, node);

	if (!cmd_el) {
		command->reply = "Command not found";
		goto out;
	}

	if (command->type == CTRL_TYPE_SET) {
		if (!cmd_el->set) {
			command->reply = "SET not implemented";
			goto out;
		}
		if (cmd_el->verify) {
			if ((ret = cmd_el->verify(command, command->value, data))) {
				ret = CTRL_CMD_ERROR;
				/* If verify() set an appropriate error message, don't change it. */
				if (!command->reply)
					command->reply = "Value failed verification.";
				goto out;
			}
		}
		ret =  cmd_el->set(command, data);
		goto out;
	} else if (command->type == CTRL_TYPE_GET) {
		if (!cmd_el->get) {
			command->reply = "GET not implemented";
			goto out;
		}
		ret = cmd_el->get(command, data);
		goto out;
	}
out:
	if (ret == CTRL_CMD_REPLY) {
		if (command->type == CTRL_TYPE_SET) {
			command->type = CTRL_TYPE_SET_REPLY;
		} else if (command->type == CTRL_TYPE_GET) {
			command->type = CTRL_TYPE_GET_REPLY;
		}
	} else if (ret == CTRL_CMD_ERROR) {
		command->type = CTRL_TYPE_ERROR;
	}
	return ret;
}

static void add_word(struct ctrl_cmd_struct *cmd,
		     const char *start, const char *end)
{
	if (!cmd->command) {
		cmd->command = talloc_zero_array(tall_vty_vec_ctx,
						 char*, 1);
		cmd->nr_commands = 0;
	} else {
		cmd->command = talloc_realloc(tall_vty_vec_ctx,
					      cmd->command, char*,
					      cmd->nr_commands + 1);
	}

	cmd->command[cmd->nr_commands++] = talloc_strndup(cmd->command,
							  start, end - start);
}

static void create_cmd_struct(struct ctrl_cmd_struct *cmd, const char *name)
{
	const char *cur, *word;

	for (cur = name, word = NULL; cur[0] != '\0'; ++cur) {
		/* warn about optionals */
		if (cur[0] == '(' || cur[0] == ')' || cur[0] == '|') {
			LOGP(DLCTRL, LOGL_ERROR,
			     "Optionals are not supported in '%s'\n", name);
			goto failure;
		}

		if (isspace(cur[0])) {
			if (word) {
				add_word(cmd, word, cur);
				word = NULL;
			}
			continue;
		}

		if (!word)
			word = cur;
	}

	if (word)
		add_word(cmd, word, cur);

	return;
failure:
	cmd->nr_commands = 0;
	talloc_free(cmd->command);
}

int ctrl_cmd_install(enum ctrl_node_type node, struct ctrl_cmd_element *cmd)
{
	vector cmds_vec;

	cmds_vec = vector_lookup_ensure(ctrl_node_vec, node);

	if (!cmds_vec) {
		cmds_vec = vector_init(5);
		if (!cmds_vec) {
			LOGP(DLCTRL, LOGL_ERROR, "vector_init failed.\n");
			return -ENOMEM;
		}
		vector_set_index(ctrl_node_vec, node, cmds_vec);
	}

	vector_set(cmds_vec, cmd);

	create_cmd_struct(&cmd->strcmd, cmd->name);
	return 0;
}

struct ctrl_cmd *ctrl_cmd_create(void *ctx, enum ctrl_type type)
{
	struct ctrl_cmd *cmd;

	cmd = talloc_zero(ctx, struct ctrl_cmd);
	if (!cmd)
		return NULL;

	cmd->type = type;
	return cmd;
}

struct ctrl_cmd *ctrl_cmd_cpy(void *ctx, struct ctrl_cmd *cmd)
{
	struct ctrl_cmd *cmd2;

	cmd2 = talloc_zero(ctx, struct ctrl_cmd);
	if (!cmd2)
		return NULL;

	cmd2->type = cmd->type;
	if (cmd->id) {
		cmd2->id = talloc_strdup(cmd2, cmd->id);
		if (!cmd2->id)
			goto err;
	}
	if (cmd->variable) {
		cmd2->variable = talloc_strdup(cmd2, cmd->variable);
		if (!cmd2->variable)
			goto err;
	}
	if (cmd->value) {
		cmd2->value = talloc_strdup(cmd2, cmd->value);
		if (!cmd2->value)
			goto err;
	}
	if (cmd->reply) {
		cmd2->reply = talloc_strdup(cmd2, cmd->reply);
		if (!cmd2->reply)
			goto err;
	}

	return cmd2;
err:
	talloc_free(cmd2);
	return NULL;
}

struct ctrl_cmd *ctrl_cmd_parse(void *ctx, struct msgb *msg)
{
	char *str, *tmp, *saveptr = NULL;
	char *var, *val;
	struct ctrl_cmd *cmd;

	cmd = talloc_zero(ctx, struct ctrl_cmd);
	if (!cmd) {
		LOGP(DLCTRL, LOGL_ERROR, "Failed to allocate.\n");
		return NULL;
	}

	/* Make sure input is NULL terminated */
	msgb_put_u8(msg, 0);
	str = (char *) msg->l2h;

	tmp = strtok_r(str, " ",  &saveptr);
	if (!tmp) {
		cmd->type = CTRL_TYPE_ERROR;
		cmd->id = "err";
		cmd->reply = "Request malformed";
		goto err;
	}

	cmd->type = ctrl_cmd_str2type(tmp);
	if (cmd->type == CTRL_TYPE_UNKNOWN) {
		cmd->type = CTRL_TYPE_ERROR;
		cmd->id = "err";
		cmd->reply = "Request type unknown";
		goto err;
	}

	tmp = strtok_r(NULL, " ",  &saveptr);

	if (!tmp) {
		cmd->type = CTRL_TYPE_ERROR;
		cmd->id = "err";
		cmd->reply = "Missing ID";
		goto err;
	}
	cmd->id = talloc_strdup(cmd, tmp);
	if (!cmd->id)
		goto oom;

	switch (cmd->type) {
		case CTRL_TYPE_GET:
			var = strtok_r(NULL, " ", &saveptr);
			if (!var) {
				cmd->type = CTRL_TYPE_ERROR;
				cmd->reply = "GET incomplete";
				LOGP(DLCTRL, LOGL_NOTICE, "GET Command incomplete\n");
				goto err;
			}
			cmd->variable = talloc_strdup(cmd, var);
			LOGP(DLCTRL, LOGL_DEBUG, "Command: GET %s\n", cmd->variable);
			break;
		case CTRL_TYPE_SET:
			var = strtok_r(NULL, " ", &saveptr);
			val = strtok_r(NULL, "\n", &saveptr);
			if (!var || !val) {
				cmd->type = CTRL_TYPE_ERROR;
				cmd->reply = "SET incomplete";
				LOGP(DLCTRL, LOGL_NOTICE, "SET Command incomplete\n");
				goto err;
			}
			cmd->variable = talloc_strdup(cmd, var);
			cmd->value = talloc_strdup(cmd, val);
			if (!cmd->variable || !cmd->value)
				goto oom;
			LOGP(DLCTRL, LOGL_DEBUG, "Command: SET %s = %s\n", cmd->variable, cmd->value);
			break;
		case CTRL_TYPE_GET_REPLY:
		case CTRL_TYPE_SET_REPLY:
		case CTRL_TYPE_TRAP:
			var = strtok_r(NULL, " ", &saveptr);
			val = strtok_r(NULL, " ", &saveptr);
			if (!var || !val) {
				cmd->type = CTRL_TYPE_ERROR;
				cmd->reply = "Trap/Reply incomplete";
				LOGP(DLCTRL, LOGL_NOTICE, "Trap/Reply incomplete\n");
				goto err;
			}
			cmd->variable = talloc_strdup(cmd, var);
			cmd->reply = talloc_strdup(cmd, val);
			if (!cmd->variable || !cmd->reply)
				goto oom;
			LOGP(DLCTRL, LOGL_DEBUG, "Command: TRAP/REPLY %s: %s\n", cmd->variable, cmd->reply);
			break;
		case CTRL_TYPE_ERROR:
			var = strtok_r(NULL, "\0", &saveptr);
			if (!var) {
				cmd->reply = "";
				goto err;
			}
			cmd->reply = talloc_strdup(cmd, var);
			if (!cmd->reply)
				goto oom;
			LOGP(DLCTRL, LOGL_DEBUG, "Command: ERROR %s\n", cmd->reply);
			break;
		case CTRL_TYPE_UNKNOWN:
		default:
			cmd->type = CTRL_TYPE_ERROR;
			cmd->reply = "Unknown type";
			goto err;
	}

	return cmd;
oom:
	cmd->type = CTRL_TYPE_ERROR;
	cmd->id = "err";
	cmd->reply = "OOM";
err:
	talloc_free(cmd);
	return NULL;
}

struct msgb *ctrl_cmd_make(struct ctrl_cmd *cmd)
{
	struct msgb *msg;
	char *type, *tmp;

	if (!cmd->id)
		return NULL;

	msg = msgb_alloc_headroom(4096, 128, "ctrl command make");
	if (!msg)
		return NULL;

	type = ctrl_cmd_type2str(cmd->type);

	switch (cmd->type) {
	case CTRL_TYPE_GET:
		if (!cmd->variable)
			goto err;

		tmp = talloc_asprintf(cmd, "%s %s %s", type, cmd->id, cmd->variable);
		if (!tmp) {
			LOGP(DLCTRL, LOGL_ERROR, "Failed to allocate cmd.\n");
			goto err;
		}

		msg->l2h = msgb_put(msg, strlen(tmp));
		memcpy(msg->l2h, tmp, strlen(tmp));
		talloc_free(tmp);
		break;
	case CTRL_TYPE_SET:
		if (!cmd->variable || !cmd->value)
			goto err;

		tmp = talloc_asprintf(cmd, "%s %s %s %s", type, cmd->id, cmd->variable,
				cmd->value);
		if (!tmp) {
			LOGP(DLCTRL, LOGL_ERROR, "Failed to allocate cmd.\n");
			goto err;
		}

		msg->l2h = msgb_put(msg, strlen(tmp));
		memcpy(msg->l2h, tmp, strlen(tmp));
		talloc_free(tmp);
		break;
	case CTRL_TYPE_GET_REPLY:
	case CTRL_TYPE_SET_REPLY:
	case CTRL_TYPE_TRAP:
		if (!cmd->variable || !cmd->reply)
			goto err;

		tmp = talloc_asprintf(cmd, "%s %s %s %s", type, cmd->id, cmd->variable,
				cmd->reply);
		if (!tmp) {
			LOGP(DLCTRL, LOGL_ERROR, "Failed to allocate cmd.\n");
			goto err;
		}

		msg->l2h = msgb_put(msg, strlen(tmp));
		memcpy(msg->l2h, tmp, strlen(tmp));
		talloc_free(tmp);
		break;
	case CTRL_TYPE_ERROR:
		if (!cmd->reply)
			goto err;

		tmp = talloc_asprintf(cmd, "%s %s %s", type, cmd->id,
				cmd->reply);
		if (!tmp) {
			LOGP(DLCTRL, LOGL_ERROR, "Failed to allocate cmd.\n");
			goto err;
		}

		msg->l2h = msgb_put(msg, strlen(tmp));
		memcpy(msg->l2h, tmp, strlen(tmp));
		talloc_free(tmp);
		break;
	default:
		LOGP(DLCTRL, LOGL_NOTICE, "Unknown command type %i\n", cmd->type);
		goto err;
		break;
	}

	return msg;

err:
	msgb_free(msg);
	return NULL;
}

struct ctrl_cmd_def *
ctrl_cmd_def_make(const void *ctx, struct ctrl_cmd *cmd, void *data, unsigned int secs)
{
	struct ctrl_cmd_def *cd;

	if (!cmd->ccon)
		return NULL;

	cd = talloc_zero(ctx, struct ctrl_cmd_def);

	cd->cmd = cmd;
	cd->data = data;

	/* add to per-connection list of deferred commands */
	llist_add(&cd->list, &cmd->ccon->def_cmds);

	return cd;
}

int ctrl_cmd_def_is_zombie(struct ctrl_cmd_def *cd)
{
	/* luckily we're still alive */
	if (cd->cmd)
		return 0;

	/* if we are a zombie, make sure we really die */
	llist_del(&cd->list);
	talloc_free(cd);

	return 1;
}

int ctrl_cmd_def_send(struct ctrl_cmd_def *cd)
{
	struct ctrl_cmd *cmd = cd->cmd;

	int rc;

	/* Deferred commands can only be responses to GET/SET or ERROR, but
	 * never TRAP or anything else */
	switch (cmd->type) {
	case CTRL_TYPE_GET:
		cmd->type = CTRL_TYPE_GET_REPLY;
		break;
	case CTRL_TYPE_SET:
		cmd->type = CTRL_TYPE_SET_REPLY;
		break;
	default:
		cmd->type = CTRL_TYPE_ERROR;
	}

	rc = ctrl_cmd_send(&cmd->ccon->write_queue, cmd);

	talloc_free(cmd);
	llist_del(&cd->list);
	talloc_free(cd);

	return rc;
}
