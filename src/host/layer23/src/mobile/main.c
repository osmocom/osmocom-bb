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

#include <osmocom/core/talloc.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/application.h>

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
static char *gsmtap_ip = 0;
struct gsmtap_inst *gsmtap_inst = NULL;
static char *vty_ip = "127.0.0.1";
unsigned short vty_port = 4247;
int debug_set = 0;
char *config_dir = NULL;
int use_mncc_sock = 0;
int daemonize = 0;

int mncc_recv_socket(struct osmocom_ms *ms, int msg_type, void *arg);

int mobile_delete(struct osmocom_ms *ms, int force);
int mobile_signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data);
int mobile_work(struct osmocom_ms *ms);
int mobile_exit(struct osmocom_ms *ms, int force);


const char *debug_default =
	"DCS:DNB:DPLMN:DRR:DMM:DSIM:DCC:DMNCC:DSS:DLSMS:DPAG:DSUM:DSAP";

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
	printf("  -u --vty-ip           The VTY IP to telnet to. "
		"(default %s)\n", vty_ip);
	printf("  -v --vty-port		The VTY port number to telnet to. "
		"(default %u)\n", vty_port);
	printf("  -d --debug		Change debug flags. default: %s\n",
		debug_default);
	printf("  -D --daemonize	Run as daemon\n");
	printf("  -m --mncc-sock	Disable built-in MNCC handler and "
		"offer socket\n");
}

static void handle_options(int argc, char **argv)
{
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"gsmtap-ip", 1, 0, 'i'},
			{"vty-ip", 1, 0, 'u'},
			{"vty-port", 1, 0, 'v'},
			{"debug", 1, 0, 'd'},
			{"daemonize", 0, 0, 'D'},
			{"mncc-sock", 0, 0, 'm'},
			{0, 0, 0, 0},
		};

		c = getopt_long(argc, argv, "hi:u:v:d:Dm",
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
			gsmtap_ip = optarg;
			break;
		case 'u':
			vty_ip = optarg;
			break;
		case 'v':
			vty_port = atoi(optarg);
			break;
		case 'd':
			log_parse_category_mask(stderr_target, optarg);
			debug_set = 1;
			break;
		case 'D':
			daemonize = 1;
			break;
		case 'm':
			use_mncc_sock = 1;
			break;
		default:
			break;
		}
	}
}

void sighandler(int sigset)
{
	static uint8_t _quit = 1, count_int = 0;

	if (sigset == SIGHUP || sigset == SIGPIPE)
		return;

	fprintf(stderr, "\nSignal %d received.\n", sigset);

	switch (sigset) {
	case SIGINT:
		/* The first signal causes initiating of shutdown with detach
		 * procedure. The second signal causes initiating of shutdown
		 * without detach procedure. The third signal will exit process
		 * immidiately. (in case it hangs)
		 */
		if (count_int == 0) {
			fprintf(stderr, "Performing shutdown with clean "
				"detach, if possible...\n");
			osmo_signal_dispatch(SS_GLOBAL, S_GLOBAL_SHUTDOWN,
				NULL);
			count_int = 1;
			break;
		}
		if (count_int == 2) {
			fprintf(stderr, "Unclean exit, please turn off phone "
				"to be sure it is not transmitting!\n");
			exit(0);
		}
		/* fall through */
	case SIGTSTP:
		count_int = 2;
		fprintf(stderr, "Performing shutdown without detach...\n");
		osmo_signal_dispatch(SS_GLOBAL, S_GLOBAL_SHUTDOWN, &_quit);
		break;
	case SIGABRT:
		/* in case of abort, we want to obtain a talloc report
		 * and then return to the caller, who will abort the process
		 */
	case SIGUSR1:
	case SIGUSR2:
		talloc_report_full(l23_ctx, stderr);
		break;
	}
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
	log_init(&log_info, NULL);
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);

	l23_ctx = talloc_named_const(NULL, 1, "layer2 context");
	msgb_set_talloc_ctx(l23_ctx);

	handle_options(argc, argv);

	if (!debug_set)
		log_parse_category_mask(stderr_target, debug_default);
	log_set_log_level(stderr_target, LOGL_DEBUG);

	if (gsmtap_ip) {
		gsmtap_inst = gsmtap_source_init(gsmtap_ip, GSMTAP_UDP_PORT, 1);
		if (!gsmtap_inst) {
			fprintf(stderr, "Failed during gsmtap_init()\n");
			exit(1);
		}
		gsmtap_source_add_sink(gsmtap_inst);
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

	if (use_mncc_sock)
		rc = l23_app_init(mncc_recv_socket, config_file, vty_ip, vty_port);
	else
		rc = l23_app_init(NULL, config_file, vty_ip, vty_port);
	if (rc)
		exit(rc);

	signal(SIGINT, sighandler);
	signal(SIGTSTP, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, sighandler);
	signal(SIGABRT, sighandler);
	signal(SIGUSR1, sighandler);
	signal(SIGUSR2, sighandler);

	if (daemonize) {
		printf("Running as daemon\n");
		rc = osmo_daemonize();
		if (rc)
			fprintf(stderr, "Failed to run as daemon\n");
	}

	while (1) {
		l23_app_work(&quit);
		if (quit && llist_empty(&ms_list))
			break;
		osmo_select_main(0);
	}

	l23_app_exit();

	talloc_free(config_file);
	talloc_free(config_dir);
	talloc_report_full(l23_ctx, stderr);

	signal(SIGINT, SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);
	signal(SIGABRT, SIG_DFL);
	signal(SIGUSR1, SIG_DFL);
	signal(SIGUSR2, SIG_DFL);

	return 0;
}
