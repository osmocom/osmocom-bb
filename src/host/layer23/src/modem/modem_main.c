/* modem app (gprs) */

/* (C) 2022 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/lienses/>.
 *
 */

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/modem/modem.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/application.h>

#include <arpa/inet.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void *tall_modem_ctx = NULL;
int daemonize = 0;

static void print_usage(const char *app)
{
	printf("Usage: %s\n", app);
}

static void print_help(void)
{
	printf(" Some help...\n");
	printf("  -h --help		this text\n");
	printf("  -d --debug		Change debug flags.\n");
	printf("  -D --daemonize	Run as daemon\n");
}

static int handle_options(int argc, char **argv)
{
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"debug", 1, 0, 'd'},
			{"daemonize", 0, 0, 'D'},
			{0, 0, 0, 0},
		};

		c = getopt_long(argc, argv, "hi:u:c:v:d:Dm",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_usage(argv[0]);
			print_help();
			exit(0);
			break;
		case 'd':
			log_parse_category_mask(osmo_stderr_target, optarg);
			break;
		case 'D':
			daemonize = 1;
			break;
		default:
			/* Unknown parameter passed */
			return -EINVAL;
		}
	}

	return 0;
}

void signal_handler(int signum)
{
	fprintf(stdout, "signal %u received\n", signum);

	switch (signum) {
	case SIGINT:
	case SIGTERM:
		exit(0);
		break;
	case SIGABRT:
		/* in case of abort, we want to obtain a talloc report and
		 * then run default SIGABRT handler, who will generate coredump
		 * and abort the process. abort() should do this for us after we
		 * return, but program wouldn't exit if an external SIGABRT is
		 * received.
		 */
		talloc_report_full(tall_modem_ctx, stderr);
		signal(SIGABRT, SIG_DFL);
		raise(SIGABRT);
		break;
	case SIGUSR1:
		talloc_report(tall_modem_ctx, stderr);
		break;
	case SIGUSR2:
		talloc_report_full(tall_modem_ctx, stderr);
		break;
	default:
		break;
	}
}

int modem_start(void)
{
	printf("Nothing to be done yet\n");
	return 0;
}

int main(int argc, char **argv)
{
	int rc;

	tall_modem_ctx = talloc_named_const(NULL, 1, "modem");
	msgb_talloc_ctx_init(tall_modem_ctx, 0);
	osmo_signal_talloc_ctx_init(tall_modem_ctx);

	osmo_init_logging2(tall_modem_ctx, &log_info);

	rc = handle_options(argc, argv);
	if (rc) { /* Abort in case of parsing errors */
		fprintf(stderr, "Error in command line options. Exiting.\n");
		return 1;
	}

	signal(SIGINT, &signal_handler);
	signal(SIGTERM, &signal_handler);
	signal(SIGABRT, &signal_handler);
	signal(SIGUSR1, &signal_handler);
	signal(SIGUSR2, &signal_handler);
	osmo_init_ignore_signals();

	if (daemonize) {
		printf("Running as daemon\n");
		rc = osmo_daemonize();
		if (rc)
			fprintf(stderr, "Failed to run as daemon\n");
	}

	modem_start();

	while (!osmo_select_shutdown_done()) {
		osmo_select_main_ctx(0);
	}

	talloc_report_full(tall_modem_ctx, stderr);
	return 0;
}
