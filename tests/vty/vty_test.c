/* (C) 2013 by Jacob Erlbeck <jerlbeck@sysmocom.de>
 * All Rights Reserved
 *
 * This program is iree software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/vty/misc.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>
#include <osmocom/vty/buffer.h>

static enum event last_vty_connection_event = -1;

static void test_cmd_string_from_valstr(void)
{
	char *cmd;
	const struct value_string printf_seq_vs[] = {
		{ .value = 42, .str = "[foo%s%s%s%s%s]"},
		{ .value = 43, .str = "[bar%s%s%s%s%s]"},
		{ .value = 0,  .str = NULL}
	};

	printf("Going to test vty_cmd_string_from_valstr()\n");

	/* check against character strings that could break printf */

	cmd = vty_cmd_string_from_valstr (NULL, printf_seq_vs, "[prefix%s%s%s%s%s]", "[sep%s%s%s%s%s]", "[end%s%s%s%s%s]", 1);
	printf ("Tested with %%s-strings, resulting cmd = '%s'\n", cmd);
	talloc_free (cmd);
}

static int do_vty_command(struct vty *vty, const char *cmd)
{
	vector vline;
	int ret;

	printf("Going to execute '%s'\n", cmd);
	vline = cmd_make_strvec(cmd);
	ret = cmd_execute_command(vline, vty, NULL, 0);
	cmd_free_strvec(vline);
	printf("Returned: %d, Current node: %d '%s'\n", ret, vty->node, cmd_prompt(vty->node));
	return ret;
}

/* Override the implementation from telnet_interface.c */
void vty_event(enum event event, int sock, struct vty *vty)
{
	last_vty_connection_event = event;

	fprintf(stderr, "Got VTY event: %d\n", event);
}

static void test_node_tree_structure(void)
{
	struct vty_app_info vty_info = {
		.name 		= "VtyTest",
		.version	= 0,
		.go_parent_cb	= NULL,
		.is_config_node	= NULL,
	};

	const struct log_info_cat default_categories[] = {};

	const struct log_info log_info = {
		.cat = default_categories,
		.num_cat = ARRAY_SIZE(default_categories),
	};

	struct vty *vty;
	vector vline;
	int sock[2];

	printf("Going to test VTY node tree structure\n");

	/* Fake logging. */
	osmo_init_logging(&log_info);

	vty_init(&vty_info);

	logging_vty_add_cmds(&log_info);

	/* Fake connection. */
	socketpair(AF_UNIX, SOCK_STREAM, 0, sock);

	vty = vty_create(sock[0], NULL);

	OSMO_ASSERT(vty != NULL);

	OSMO_ASSERT(do_vty_command(vty, "enable") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);

	OSMO_ASSERT(do_vty_command(vty, "configure terminal") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "exit") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);

	OSMO_ASSERT(do_vty_command(vty, "configure terminal") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "end") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);

	OSMO_ASSERT(do_vty_command(vty, "configure terminal") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "log stderr") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CFG_LOG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "exit") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "log stderr") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CFG_LOG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "end") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);

	OSMO_ASSERT(do_vty_command(vty, "configure terminal") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "line vty") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == VTY_NODE);
	OSMO_ASSERT(do_vty_command(vty, "exit") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "line vty") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == VTY_NODE);
	OSMO_ASSERT(do_vty_command(vty, "end") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);


	/* Check searching the parents nodes for matching commands. */
	OSMO_ASSERT(do_vty_command(vty, "configure terminal") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "log stderr") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CFG_LOG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "line vty") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == VTY_NODE);
	OSMO_ASSERT(do_vty_command(vty, "log stderr") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CFG_LOG_NODE);
	OSMO_ASSERT(do_vty_command(vty, "end") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);

	/* Check for final 'exit' (connection close). */
	OSMO_ASSERT(do_vty_command(vty, "exit") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);
	OSMO_ASSERT(vty->status == VTY_CLOSE);

	vty_close(vty);
	OSMO_ASSERT(last_vty_connection_event == VTY_CLOSED);
}

int main(int argc, char **argv)
{
	test_cmd_string_from_valstr();
	test_node_tree_structure();
	printf("All tests passed\n");

	return 0;
}
