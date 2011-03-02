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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/app_mobile.h>

#include <osmocore/talloc.h>
#include <osmocore/linuxlist.h>
#include <osmocore/gsmtap_util.h>
#include <osmocore/signal.h>

#include <arpa/inet.h>

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <libgen.h>

struct log_target *stderr_target;

void *l23_ctx = NULL;
struct llist_head ms_list;
static uint32_t gsmtap_ip = 0;
unsigned short vty_port = 4247;
int debug_set = 0;
char *config_dir = NULL;

int mobile_delete(struct osmocom_ms *ms, int force);
int mobile_signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data);
int mobile_work(struct osmocom_ms *ms);
int mobile_exit(struct osmocom_ms *ms, int force);


const char *openbsc_copyright =
	"Copyright (C) 2008-2010 ...\n"
	"Contributions by ...\n\n"
	"License GPLv2+: GNU GPL version 2 or later "
		"<http://gnu.org/licenses/gpl.html>\n"
	"This is free software: you are free to change and redistribute it.\n"
	"There is NO WARRANTY, to the extent permitted by law.\n";

static void print_usage(const char *app)
{
	printf("Usage: %s\n", app);
}

static void print_help()
{
	printf(" Some help...\n");
	printf("  -h --help		this text\n");
	printf("  -i --gsmtap-ip	The destination IP used for GSMTAP.\n");
	printf("  -v --vty-port		The VTY port number to telnet to. "
		"(default %u)\n", vty_port);
	printf("  -d --debug		Change debug flags.\n");
}

static void handle_options(int argc, char **argv)
{
	struct sockaddr_in gsmtap;
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"gsmtap-ip", 1, 0, 'i'},
			{"vty-port", 1, 0, 'v'},
			{"debug", 1, 0, 'd'},
			{0, 0, 0, 0},
		};

		c = getopt_long(argc, argv, "hi:v:d:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_usage(argv[0]);
			print_help();
			exit(0);
			break;
		case 'i':
			if (!inet_aton(optarg, &gsmtap.sin_addr)) {
				perror("inet_aton");
				exit(2);
			}
			gsmtap_ip = ntohl(gsmtap.sin_addr.s_addr);
			break;
		case 'v':
			vty_port = atoi(optarg);
			break;
		case 'd':
			log_parse_category_mask(stderr_target, optarg);
			debug_set = 1;
			break;
		default:
			break;
		}
	}
}

void sighandler(int sigset)
{
	if (sigset == SIGHUP || sigset == SIGPIPE)
		return;

	fprintf(stderr, "Signal %d received.\n", sigset);

	/* in case there is a lockup during exit */
	signal(SIGINT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);

	dispatch_signal(SS_GLOBAL, S_GLOBAL_SHUTDOWN, NULL);
}

int main(int argc, char **argv)
{
	int quit = 0;
	int rc;
	char const * home;
	size_t len;
	const char osmocomcfg[] = ".osmocom/bb/mobile.cfg";
	char *config_file = NULL;

	printf("%s\n", openbsc_copyright);

	srand(time(NULL));

	INIT_LLIST_HEAD(&ms_list);
	log_init(&log_info);
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);

	l23_ctx = talloc_named_const(NULL, 1, "layer2 context");

	handle_options(argc, argv);

	if (!debug_set)
		log_parse_category_mask(stderr_target, "DCS:DPLMN:DRR:DMM:DSIM:DCC:DMNCC:DPAG:DSUM");
	log_set_log_level(stderr_target, LOGL_INFO);

	if (gsmtap_ip) {
		rc = gsmtap_init(gsmtap_ip);
		if (rc < 0) {
			fprintf(stderr, "Failed during gsmtap_init()\n");
			exit(1);
		}
	}

	home = getenv("HOME");
	if (home != NULL) {
		len = strlen(home) + 1 + sizeof(osmocomcfg);
		config_file = talloc_size(l23_ctx, len);
		if (config_file != NULL)
			snprintf(config_file, len, "%s/%s", home, osmocomcfg);
	}
	/* save the config file directory name */
	config_dir = talloc_strdup(l23_ctx, config_file);
	config_dir = dirname(config_dir);

	rc = l23_app_init(NULL, config_file, vty_port);
	if (rc)
		exit(rc);

	signal(SIGINT, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, sighandler);

	while (1) {
		l23_app_work(&quit);
		if (quit && llist_empty(&ms_list))
			break;
		bsc_select_main(0);
	}

	l23_app_exit();

	return 0;
}
