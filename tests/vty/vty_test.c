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

#include <osmocom/core/application.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/stats.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/signal.h>
#include <osmocom/vty/misc.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/logging.h>
#include <osmocom/vty/stats.h>

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

/* Handle the events from telnet_interface.c */
static int vty_event_cb(unsigned int subsys, unsigned int signal,
			void *handler_data, void *_signal_data)
{
	struct vty_signal_data *signal_data;

	if (subsys != SS_L_VTY)
		return 0;
	if (signal != S_VTY_EVENT)
		return 0;

	signal_data = _signal_data;
	last_vty_connection_event = signal_data->event;

	fprintf(stderr, "Got VTY event: %d\n", signal_data->event);
	return 0;
}

struct vty_test {
	int sock[2];
};

static struct vty* create_test_vty(struct vty_test *data)
{
	struct vty *vty;
	/* Fake connection. */
	socketpair(AF_UNIX, SOCK_STREAM, 0, data->sock);

	vty = vty_create(data->sock[0], NULL);
	OSMO_ASSERT(vty != NULL);
	OSMO_ASSERT(vty->status != VTY_CLOSE);

	return vty;
}

static void destroy_test_vty(struct vty_test *data, struct vty *vty)
{
	vty_close(vty);
	OSMO_ASSERT(last_vty_connection_event == VTY_CLOSED);
}

static void test_node_tree_structure(void)
{
	struct vty_test test;
	struct vty *vty;

	printf("Going to test VTY node tree structure\n");
	vty = create_test_vty(&test);

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

	destroy_test_vty(&test, vty);
}

static void check_srep_vty_config(struct vty* vty,
	struct osmo_stats_reporter *srep)
{
	OSMO_ASSERT(srep->enabled == 0);

	OSMO_ASSERT(do_vty_command(vty, "prefix myprefix") == CMD_SUCCESS);
	OSMO_ASSERT(srep->name_prefix != NULL);
	OSMO_ASSERT(strcmp(srep->name_prefix, "myprefix") == 0);
	OSMO_ASSERT(do_vty_command(vty, "no prefix") == CMD_SUCCESS);
	OSMO_ASSERT(srep->name_prefix == NULL || strlen(srep->name_prefix) == 0);

	OSMO_ASSERT(srep->max_class == OSMO_STATS_CLASS_GLOBAL);
	OSMO_ASSERT(do_vty_command(vty, "level peer") == CMD_SUCCESS);
	OSMO_ASSERT(srep->max_class == OSMO_STATS_CLASS_PEER);
	OSMO_ASSERT(do_vty_command(vty, "level subscriber") == CMD_SUCCESS);
	OSMO_ASSERT(srep->max_class == OSMO_STATS_CLASS_SUBSCRIBER);
	OSMO_ASSERT(do_vty_command(vty, "level global") == CMD_SUCCESS);
	OSMO_ASSERT(srep->max_class == OSMO_STATS_CLASS_GLOBAL);
	OSMO_ASSERT(do_vty_command(vty, "level foobar") == CMD_ERR_NO_MATCH);

	if (srep->have_net_config) {
		OSMO_ASSERT(do_vty_command(vty, "remote-ip 127.0.0.99") ==
			CMD_SUCCESS);
		OSMO_ASSERT(srep->dest_addr_str &&
			strcmp(srep->dest_addr_str, "127.0.0.99") == 0);
		OSMO_ASSERT(do_vty_command(vty, "remote-ip 678.0.0.99") ==
			CMD_WARNING);
		OSMO_ASSERT(srep->dest_addr_str &&
			strcmp(srep->dest_addr_str, "127.0.0.99") == 0);

		OSMO_ASSERT(do_vty_command(vty, "remote-port 12321") ==
			CMD_SUCCESS);
		OSMO_ASSERT(srep->dest_port == 12321);

		OSMO_ASSERT(srep->bind_addr_str == NULL);
		OSMO_ASSERT(do_vty_command(vty, "local-ip 127.0.0.98") ==
			CMD_SUCCESS);
		OSMO_ASSERT(srep->bind_addr_str &&
			strcmp(srep->bind_addr_str, "127.0.0.98") == 0);
		OSMO_ASSERT(do_vty_command(vty, "no local-ip") == CMD_SUCCESS);
		OSMO_ASSERT(srep->bind_addr_str == NULL);

		OSMO_ASSERT(srep->mtu == 0);
		OSMO_ASSERT(do_vty_command(vty, "mtu 987") == CMD_SUCCESS);
		OSMO_ASSERT(srep->mtu == 987);
		OSMO_ASSERT(do_vty_command(vty, "no mtu") == CMD_SUCCESS);
		OSMO_ASSERT(srep->mtu == 0);
	};

	OSMO_ASSERT(do_vty_command(vty, "enable") == CMD_SUCCESS);
	OSMO_ASSERT(srep->enabled != 0);
	OSMO_ASSERT(do_vty_command(vty, "disable") == CMD_SUCCESS);
	OSMO_ASSERT(srep->enabled == 0);
}

static void test_stats_vty(void)
{
	struct osmo_stats_reporter *srep;
	struct vty_test test;
	struct vty *vty;

	printf("Going to test VTY configuration of the stats subsystem\n");
	vty = create_test_vty(&test);

	/* Go to config node */
	OSMO_ASSERT(do_vty_command(vty, "enable") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == ENABLE_NODE);
	OSMO_ASSERT(do_vty_command(vty, "configure terminal") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);

	/* Try to create invalid reporter */
	OSMO_ASSERT(do_vty_command(vty, "stats reporter foobar") ==
		CMD_ERR_NO_MATCH);

	/* Set reporting interval */
	OSMO_ASSERT(do_vty_command(vty, "stats interval 42") == CMD_SUCCESS);
	OSMO_ASSERT(osmo_stats_config->interval == 42);

	/* Create log reporter */
	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_LOG, NULL);
	OSMO_ASSERT(srep == NULL);
	OSMO_ASSERT(do_vty_command(vty, "stats reporter log") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CFG_STATS_NODE);
	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_LOG, NULL);
	OSMO_ASSERT(srep != NULL);
	OSMO_ASSERT(srep->type == OSMO_STATS_REPORTER_LOG);
	check_srep_vty_config(vty, srep);
	OSMO_ASSERT(do_vty_command(vty, "exit") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);

	/* Create statsd reporter */
	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_STATSD, NULL);
	OSMO_ASSERT(srep == NULL);
	OSMO_ASSERT(do_vty_command(vty, "stats reporter statsd") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CFG_STATS_NODE);
	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_STATSD, NULL);
	OSMO_ASSERT(srep != NULL);
	OSMO_ASSERT(srep->type == OSMO_STATS_REPORTER_STATSD);
	check_srep_vty_config(vty, srep);
	OSMO_ASSERT(do_vty_command(vty, "exit") == CMD_SUCCESS);
	OSMO_ASSERT(vty->node == CONFIG_NODE);

	/* Destroy log reporter */
	OSMO_ASSERT(osmo_stats_reporter_find(OSMO_STATS_REPORTER_LOG, NULL));
	OSMO_ASSERT(do_vty_command(vty, "no stats reporter log") == CMD_SUCCESS);
	OSMO_ASSERT(!osmo_stats_reporter_find(OSMO_STATS_REPORTER_LOG, NULL));

	/* Destroy statsd reporter */
	OSMO_ASSERT(osmo_stats_reporter_find(OSMO_STATS_REPORTER_STATSD, NULL));
	OSMO_ASSERT(do_vty_command(vty, "no stats reporter statsd") == CMD_SUCCESS);
	OSMO_ASSERT(!osmo_stats_reporter_find(OSMO_STATS_REPORTER_STATSD, NULL));

	destroy_test_vty(&test, vty);
}

int main(int argc, char **argv)
{
	struct vty_app_info vty_info = {
		.name		= "VtyTest",
		.version	= 0,
		.go_parent_cb	= NULL,
		.is_config_node	= NULL,
	};

	const struct log_info_cat default_categories[] = {};

	const struct log_info log_info = {
		.cat = default_categories,
		.num_cat = ARRAY_SIZE(default_categories),
	};
	void *stats_ctx = talloc_named_const(NULL, 1, "stats test context");

	osmo_signal_register_handler(SS_L_VTY, vty_event_cb, NULL);

	/* Fake logging. */
	osmo_init_logging(&log_info);

	/* Init stats */
	osmo_stats_init(stats_ctx);

	vty_init(&vty_info);

	/* Setup VTY commands */
	logging_vty_add_cmds(&log_info);
	osmo_stats_vty_add_cmds();

	test_cmd_string_from_valstr();
	test_node_tree_structure();
	test_stats_vty();

	/* Leak check */
	OSMO_ASSERT(talloc_total_blocks(stats_ctx) == 1);

	printf("All tests passed\n");

	return 0;
}
