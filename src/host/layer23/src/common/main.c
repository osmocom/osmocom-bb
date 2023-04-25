/* Main method of the layer2/3 stack */

/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
 */

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/misc/layer3.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/vty.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/application.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/utils.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/logging.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/ports.h>

#include <arpa/inet.h>

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

#include "config.h"

void *l23_ctx = NULL;
struct l23_global_config l23_cfg;

static char *sap_socket_path = "/tmp/osmocom_sap";
struct llist_head ms_list;
static char *gsmtap_ip = NULL;
static char *config_file = NULL;

int (*l23_app_start)(void) = NULL;
int (*l23_app_work)(void) = NULL;
int (*l23_app_exit)(void) = NULL;
int quit = 0;

static void print_usage(const char *app)
{
	printf("Usage: %s\n", app);
}

static void print_help(void)
{
	printf(" Some help...\n");
	printf("  -h --help		this text\n");
	printf("  -s --socket		/tmp/osmocom_l2. Path to the unix "
		"domain socket (l2)\n");

	if (l23_app_info.opt_supported & L23_OPT_SAP)
		printf("  -S --sap		/tmp/osmocom_sap. Path to the "
			"unix domain socket (BTSAP)\n");

	if (l23_app_info.opt_supported & L23_OPT_ARFCN)
		printf("  -a --arfcn NR		The ARFCN to be used for layer2.\n");

	if (l23_app_info.opt_supported & L23_OPT_TAP)
		printf("  -i --gsmtap-ip	The destination IP used for GSMTAP.\n");

	if (l23_app_info.opt_supported & L23_OPT_VTY)
		printf("  -c --config-file	The path to the VTY configuration file.\n");

	if (l23_app_info.opt_supported & L23_OPT_DBG)
		printf("  -d --debug		Change debug flags.\n");

	if (l23_app_info.cfg_print_help != NULL)
		l23_app_info.cfg_print_help();
}

static void build_config(char **opt, struct option **option)
{
	struct option *app_opp = NULL;
	int app_len = 0, len;

	static struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"socket", 1, 0, 's'},
		{"sap", 1, 0, 'S'},
		{"arfcn", 1, 0, 'a'},
		{"gsmtap-ip", 1, 0, 'i'},
		{"config-file", 1, 0, 'c'},
		{"debug", 1, 0, 'd'},
	};


	*opt = talloc_asprintf(l23_ctx, "hs:S:a:i:c:d:%s",
			       l23_app_info.getopt_string ? l23_app_info.getopt_string : "");

	len = ARRAY_SIZE(long_options);
	if (l23_app_info.cfg_getopt_opt != NULL)
		app_len = l23_app_info.cfg_getopt_opt(&app_opp);

	*option = talloc_zero_array(l23_ctx, struct option, len + app_len + 1);
	memcpy(*option, long_options, sizeof(long_options));
	if (app_opp)
		memcpy(*option + len, app_opp, app_len * sizeof(struct option));
}

static void handle_options(int argc, char **argv)
{
	struct option *long_options;
	char *opt;

	build_config(&opt, &long_options);

	while (1) {
		int option_index = 0, c;

		c = getopt_long(argc, argv, opt,
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_usage(argv[0]);
			print_help();
			exit(0);
			break;
		case 's':
			layer2_socket_path = optarg;
			break;
		case 'S':
			sap_socket_path = talloc_strdup(l23_ctx, optarg);
			break;
		case 'a':
			cfg_test_arfcn = atoi(optarg);
			break;
		case 'i':
			gsmtap_ip = optarg;
			break;
		case 'c':
			config_file = optarg;
			break;
		case 'd':
			log_parse_category_mask(osmo_stderr_target, optarg);
			break;
		default:
			if (l23_app_info.cfg_handle_opt != NULL)
				l23_app_info.cfg_handle_opt(c, optarg);
			break;
		}
	}

	talloc_free(opt);
	talloc_free(long_options);
}

void sighandler(int sigset)
{
	int rc = 0;

	if (sigset == SIGHUP || sigset == SIGPIPE)
		return;

	fprintf(stderr, "Signal %d received.\n", sigset);
	if (l23_app_exit)
		rc = l23_app_exit();

	if (l23_app_info.opt_supported & L23_OPT_VTY)
		telnet_exit();

	if (rc != -EBUSY)
		exit (0);
}

static void print_copyright(void)
{
	printf("%s"
	       "%s\n"
	       "License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
	       "This is free software: you are free to change and redistribute it.\n"
	       "There is NO WARRANTY, to the extent permitted by law.\n\n",
	       l23_app_info.copyright ? l23_app_info.copyright : "",
	       l23_app_info.contribution ? l23_app_info.contribution : "");
}

static int _vty_init(void)
{
	int rc;

	OSMO_ASSERT(l23_app_info.vty_info != NULL);
	l23_app_info.vty_info->tall_ctx = l23_ctx;

	vty_init(l23_app_info.vty_info);
	logging_vty_add_cmds();

	if (l23_app_info.vty_init != NULL)
		l23_app_info.vty_init();
	if (config_file) {
		LOGP(DLGLOBAL, LOGL_INFO, "Using configuration from '%s'\n", config_file);
		l23_vty_reading = true;
		rc = vty_read_config_file(config_file, NULL);
		l23_vty_reading = false;
		if (rc < 0) {
			LOGP(DLGLOBAL, LOGL_FATAL,
				"Failed to parse the configuration file '%s'\n", config_file);
			return rc;
		}
	}
	rc = telnet_init_default(l23_ctx, NULL, OSMO_VTY_PORT_BB);
	if (rc < 0) {
		LOGP(DMOB, LOGL_FATAL, "Cannot init VTY on %s port %u: %s\n",
			vty_get_bind_addr(), OSMO_VTY_PORT_BB, strerror(errno));
		return rc;
	}
	return rc;
}

int main(int argc, char **argv)
{
	int rc;

	INIT_LLIST_HEAD(&ms_list);

	l23_ctx = talloc_named_const(NULL, 1, "layer2 context");

	osmo_init_logging2(l23_ctx, &log_info);

	log_set_print_category_hex(osmo_stderr_target, 0);
	log_set_print_category(osmo_stderr_target, 1);
	log_set_print_level(osmo_stderr_target, 1);

	log_set_print_filename2(osmo_stderr_target, LOG_FILENAME_BASENAME);
	log_set_print_filename_pos(osmo_stderr_target, LOG_FILENAME_POS_HEADER_END);

	print_copyright();

	handle_options(argc, argv);

	rc = l23_app_init();
	if (rc < 0) {
		fprintf(stderr, "Failed during l23_app_init()\n");
		exit(1);
	}

	if (l23_app_info.opt_supported & L23_OPT_VTY) {
		if (_vty_init() < 0)
			exit(1);
	}

	if (l23_app_info.opt_supported & L23_OPT_TAP) {
		if (gsmtap_ip) {
			if (l23_cfg.gsmtap.remote_host != NULL) {
				LOGP(DLGLOBAL, LOGL_NOTICE,
				     "Command line argument '-i %s' overrides node "
				     "'gsmtap' cmd 'remote-host %s' from the config file\n",
				     gsmtap_ip, l23_cfg.gsmtap.remote_host);
				talloc_free(l23_cfg.gsmtap.remote_host);
			}
			l23_cfg.gsmtap.remote_host = talloc_strdup(l23_ctx, gsmtap_ip);
		}

		if (l23_cfg.gsmtap.remote_host) {
			LOGP(DLGLOBAL, LOGL_NOTICE,
			     "Setting up GSMTAP Um forwarding to '%s:%u'\n",
			     l23_cfg.gsmtap.remote_host, GSMTAP_UDP_PORT);
			l23_cfg.gsmtap.inst = gsmtap_source_init(l23_cfg.gsmtap.remote_host, GSMTAP_UDP_PORT, 1);
			if (!l23_cfg.gsmtap.inst) {
				fprintf(stderr, "Failed during gsmtap_init()\n");
				exit(1);
			}
			gsmtap_source_add_sink(l23_cfg.gsmtap.inst);
		}
	}

	if (l23_app_start) {
		rc = l23_app_start();
		if (rc < 0) {
			fprintf(stderr, "Failed during l23_app_start()\n");
			exit(1);
		}
	}

	signal(SIGINT, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, sighandler);

	while (!quit) {
		if (l23_app_work)
			l23_app_work();
		osmo_select_main(0);
	}

	return 0;
}
