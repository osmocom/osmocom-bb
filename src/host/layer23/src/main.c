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

#include <osmocom/osmocom_data.h>
#include <osmocom/l1ctl.h>
#include <osmocom/l1l2_interface.h>
#include <osmocom/layer3.h>
#include <osmocom/lapdm.h>
#include <osmocom/gsmtap_util.h>
#include <osmocom/logging.h>
#include <osmocom/l23_app.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>
#include <osmocore/linuxlist.h>

#include <arpa/inet.h>

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>

struct log_target *stderr_target;

void *l23_ctx = NULL;
static char *socket_path = "/tmp/osmocom_l2";
struct llist_head ms_list;
static struct osmocom_ms *ms = NULL;
static uint32_t gsmtap_ip = 0;
int (*l23_app_work) (struct osmocom_ms *ms) = NULL;
int (*l23_app_exit) (struct osmocom_ms *ms) = NULL;
int quit = 0;

const char *openbsc_copyright =
	"Copyright (C) 2008-2010 ...\n"
	"Contributions by ...\n\n"
	"License GPLv2+: GNU GPL version 2 or later <http://gnu.org/licenses/gpl.html>\n"
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
	printf("  -s --socket		/tmp/osmocom_l2. Path to the unix domain socket\n");
	printf("  -a --arfcn NR		The ARFCN to be used for layer2.\n");
	printf("  -i --gsmtap-ip	The destination IP used for GSMTAP.\n");
}

static void handle_options(int argc, char **argv)
{
	struct sockaddr_in gsmtap;
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"socket", 1, 0, 's'},
			{"arfcn", 1, 0, 'a'},
			{"gsmtap-ip", 1, 0, 'i'},
			{"debug", 1, 0, 'd'},
			{0, 0, 0, 0},
		};

		c = getopt_long(argc, argv, "hs:a:i:d:",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			printf("%s\n", openbsc_copyright);
			print_usage(argv[0]);
			print_help();
			exit(0);
			break;
		case 's':
			socket_path = talloc_strdup(l23_ctx, optarg);
			break;
		case 'a':
			ms->test_arfcn = atoi(optarg);
			break;
		case 'i':
			if (!inet_aton(optarg, &gsmtap.sin_addr)) {
				perror("inet_aton");
				exit(2);
			}
			gsmtap_ip = ntohl(gsmtap.sin_addr.s_addr);
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
	int rc = 0;

	if (sigset == SIGHUP || sigset == SIGPIPE)
		return;

	fprintf(stderr, "Signal %d recevied.\n", sigset);
	if (l23_app_exit)
		rc = l23_app_exit(ms);

	if (rc != -EBUSY)
		exit (0);
}

int main(int argc, char **argv)
{
	int rc;

	INIT_LLIST_HEAD(&ms_list);
	log_init(&log_info);
	stderr_target = log_target_create_stderr();
	log_add_target(stderr_target);
	log_set_all_filter(stderr_target, 1);

	l23_ctx = talloc_named_const(NULL, 1, "layer2 context");

	ms = talloc_zero(l23_ctx, struct osmocom_ms);
	if (!ms) {
		fprintf(stderr, "Failed to allocate MS\n");
		exit(1);
	}
	llist_add_tail(&ms->entity, &ms_list);

	sprintf(ms->name, "1");

	ms->test_arfcn = 871;

	handle_options(argc, argv);

	rc = layer2_open(ms, socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during layer2_open()\n");
		exit(1);
	}

	lapdm_init(&ms->l2_entity.lapdm_dcch, ms);
	lapdm_init(&ms->l2_entity.lapdm_acch, ms);

	rc = l23_app_init(ms);
	if (rc < 0)
		exit(1);

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

	while (!quit) {
		if (l23_app_work)
			l23_app_work(ms);
		bsc_select_main(0);
	}

	return 0;
}
