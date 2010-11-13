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
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/sap_interface.h>
#include <osmocom/bb/misc/layer3.h>
#include <osmocom/bb/common/lapdm.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/vty/telnet_interface.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>
#include <osmocore/linuxlist.h>
#include <osmocore/gsmtap_util.h>
#include <osmocore/signal.h>

#include <arpa/inet.h>

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

struct log_target *stderr_target;

void *l23_ctx = NULL;
static const char *config_file = "/etc/osmocom/osmocom.cfg";
struct llist_head ms_list;
static uint32_t gsmtap_ip = 0;
unsigned short vty_port = 4247;
int (*l23_app_work) (struct osmocom_ms *ms) = NULL;
int (*l23_app_exit) (struct osmocom_ms *ms, int force) = NULL;
int quit = 0;

int mobile_delete(struct osmocom_ms *ms, int force);
int mobile_signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data);
int mobile_work(struct osmocom_ms *ms);
int mobile_exit(struct osmocom_ms *ms, int force);
extern int vty_reading;


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

	fprintf(stderr, "Signal %d recevied.\n", sigset);

	/* in case there is a lockup during exit */
	signal(SIGINT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);

	dispatch_signal(SS_GLOBAL, S_GLOBAL_SHUTDOWN, NULL);
}

int global_signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms, *ms2;

	if (subsys != SS_GLOBAL)
		return 0;

	switch (signal) {
	case S_GLOBAL_SHUTDOWN:
		llist_for_each_entry_safe(ms, ms2, &ms_list, entity)
			mobile_delete(ms, quit);

		/* if second signal is received, force to exit */
		quit = 1;
		break;
	}
	return 0;
}

struct osmocom_ms *mobile_new(char *name)
{
	static struct osmocom_ms *ms;

	ms = talloc_zero(l23_ctx, struct osmocom_ms);
	if (!ms) {
		fprintf(stderr, "Failed to allocate MS\n");
		exit(1);
	}
	llist_add_tail(&ms->entity, &ms_list);

	strcpy(ms->name, name);

	ms->l2_wq.bfd.fd = -1;
	ms->sap_wq.bfd.fd = -1;

	gsm_support_init(ms);
	gsm_settings_init(ms);

	ms->shutdown = 2; /* being down */

	return ms;
}

int mobile_delete(struct osmocom_ms *ms, int force)
{
	int rc;

	ms->delete = 1;

	if (ms->shutdown == 0 || (ms->shutdown == 1 && force)) {
		rc = l23_app_exit(ms, force);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static struct vty_app_info vty_info = {
	.name = "OsmocomBB",
	.version = PACKAGE_VERSION,
	.go_parent_cb = ms_vty_go_parent,
};

int main(int argc, char **argv)
{
	struct osmocom_ms *ms, *ms2;
	struct telnet_connection dummy_conn;
	int rc;

	printf("%s\n", openbsc_copyright);

	srand(time(NULL));

	INIT_LLIST_HEAD(&ms_list);
	log_init(&log_info);
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);

	l23_ctx = talloc_named_const(NULL, 1, "layer2 context");

	handle_options(argc, argv);

//	log_parse_category_mask(stderr_target, "DL1C:DRSL:DLAPDM:DCS:DPLMN:DRR:DMM:DSIM:DCC:DMNCC:DPAG:DSUM");
	log_parse_category_mask(stderr_target, "DCS:DPLMN:DRR:DMM:DSIM:DCC:DMNCC:DPAG:DSUM");
	log_set_log_level(stderr_target, LOGL_INFO);

	gps_init();

	l23_app_work = mobile_work;
	register_signal_handler(SS_GLOBAL, &global_signal_cb, NULL);
	register_signal_handler(SS_L1CTL, &mobile_signal_cb, NULL);
	register_signal_handler(SS_L1CTL, &gsm322_l1_signal, NULL);
	l23_app_exit = mobile_exit;

	vty_init(&vty_info);
	ms_vty_init();
	dummy_conn.priv = NULL;
	vty_reading = 1;
	rc = vty_read_config_file(config_file, &dummy_conn);
	if (rc < 0) {
		fprintf(stderr, "Failed to parse the config file: '%s'\n",
			config_file);
		fprintf(stderr, "Please check or create config file using: "
			"'touch %s'\n", config_file);
		return rc;
	}
	vty_reading = 0;
	telnet_init(l23_ctx, NULL, vty_port);
	if (rc < 0)
		return rc;
	printf("VTY available on port %u.\n", vty_port);

	if (llist_empty(&ms_list)) {
		struct osmocom_ms *ms;

		printf("No Mobile Station defined, creating: MS '1'\n");
		ms = mobile_new("1");
		if (ms)
			l23_app_init(ms);
	}

	if (gsmtap_ip) {
		rc = gsmtap_init(gsmtap_ip);
		if (rc < 0) {
			fprintf(stderr, "Failed during gsmtap_init()\n");
			exit(1);
		}
	}

	signal(SIGINT, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, sighandler);

	while (1) {
		llist_for_each_entry_safe(ms, ms2, &ms_list, entity) {
			if (ms->shutdown != 2)
				l23_app_work(ms);
			if (ms->shutdown == 2) {
				if (ms->l2_wq.bfd.fd > -1) {
					layer2_close(ms);
					ms->l2_wq.bfd.fd = -1;
				}

				if (ms->sap_wq.bfd.fd > -1) {
					sap_close(ms);
					ms->sap_wq.bfd.fd = -1;
				}

				if (ms->delete) {
					gsm_settings_exit(ms);
					llist_del(&ms->entity);
					talloc_free(ms);
				}
			}
		}
		if (quit && llist_empty(&ms_list))
			break;
		bsc_select_main(0);
	}

	unregister_signal_handler(SS_L1CTL, &gsm322_l1_signal, NULL);
	unregister_signal_handler(SS_L1CTL, &mobile_signal_cb, NULL);
	unregister_signal_handler(SS_GLOBAL, &global_signal_cb, NULL);

	gps_close();

	return 0;
}
