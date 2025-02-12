/*
 * OsmocomBB <-> SDR connection bridge
 *
 * (C) 2016-2022 by Vadim Yanitskiy <axilirator@gmail.com>
 * Contributions by sysmocom - s.f.m.c. GmbH
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/select.h>
#include <osmocom/core/application.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/gsmtap.h>

#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/trxcon_fsm.h>
#include <osmocom/bb/trxcon/phyif.h>
#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/logging.h>
#include <osmocom/bb/trxcon/l1ctl_server.h>

#define COPYRIGHT \
	"Copyright (C) 2016-2022 by Vadim Yanitskiy <axilirator@gmail.com>\n" \
	"Contributions by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>\n" \
	"License GPLv2+: GNU GPL version 2 or later " \
	"<http://gnu.org/licenses/gpl.html>\n" \
	"This is free software: you are free to change and redistribute it.\n" \
	"There is NO WARRANTY, to the extent permitted by law.\n\n"

static struct {
	const char *debug_mask;
	int daemonize;
	int quit;

	/* L1CTL specific */
	unsigned int max_clients;
	const char *bind_socket;

	/* TRX specific */
	const char *trx_bind_ip;
	const char *trx_remote_ip;
	uint16_t trx_base_port;
	uint32_t trx_fn_advance;

	/* PHY quirk: FBSB timeout extension (in TDMA FNs) */
	unsigned int phyq_fbsb_extend_fns;

	/* GSMTAP specific */
	struct gsmtap_inst *gsmtap;
	const char *gsmtap_ip;
} app_data = {
	.max_clients = 1, /* only one L1CTL client by default */
	.bind_socket = "/tmp/osmocom_l2",
	.trx_remote_ip = "127.0.0.1",
	.trx_bind_ip = "0.0.0.0",
	.trx_base_port = 6700,
	.trx_fn_advance = 2,
	.phyq_fbsb_extend_fns = 0,
};

static void *tall_trxcon_ctx = NULL;

int trxcon_phyif_handle_burst_req(void *phyif, const struct trxcon_phyif_burst_req *br)
{
	return trx_if_handle_phyif_burst_req(phyif, br);
}

int trxcon_phyif_handle_cmd(void *phyif, const struct trxcon_phyif_cmd *cmd)
{
	return trx_if_handle_phyif_cmd(phyif, cmd);
}

void trxcon_phyif_close(void *phyif)
{
	trx_if_close(phyif);
}

void trxcon_l1ctl_close(struct trxcon_inst *trxcon)
{
	/* Avoid use-after-free: both *fi and *trxcon are children of
	 * the L2IF (L1CTL connection), so we need to re-parent *fi
	 * to NULL before calling l1ctl_client_conn_close(). */
	talloc_steal(NULL, trxcon->fi);
	l1ctl_client_conn_close(trxcon->l2if);
}

int trxcon_l1ctl_send(struct trxcon_inst *trxcon, struct msgb *msg)
{
	struct l1ctl_client *l1c = trxcon->l2if;

	return l1ctl_client_send(l1c, msg);
}

static int l1ctl_rx_cb(struct l1ctl_client *l1c, struct msgb *msg)
{
	struct trxcon_inst *trxcon = l1c->priv;

	return trxcon_l1ctl_receive(trxcon, msg);
}

static void l1ctl_conn_accept_cb(struct l1ctl_client *l1c)
{
	struct trxcon_inst *trxcon;

	trxcon = trxcon_inst_alloc(l1c, l1c->id);
	if (trxcon == NULL) {
		l1ctl_client_conn_close(l1c);
		return;
	}

	l1c->log_prefix = talloc_strdup(l1c, trxcon->log_prefix);
	l1c->priv = trxcon;
	trxcon->l2if = l1c;

	const struct trx_if_params trxcon_phyif_params = {
		.local_host = app_data.trx_bind_ip,
		.remote_host = app_data.trx_remote_ip,
		.base_port = app_data.trx_base_port,
		.fn_advance = app_data.trx_fn_advance,
		.instance = trxcon->id,

		.parent_fi = trxcon->fi,
		.parent_term_event = TRXCON_EV_PHYIF_FAILURE,
		.priv = trxcon,
	};

	/* Init transceiver interface */
	trxcon->phyif = trx_if_open(&trxcon_phyif_params);
	if (trxcon->phyif == NULL) {
		/* TRXCON_EV_PHYIF_FAILURE triggers l1ctl_client_conn_close() */
		osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_PHYIF_FAILURE, NULL);
		return;
	}

	trxcon->gsmtap = app_data.gsmtap;
	trxcon->phy_quirks.fbsb_extend_fns = app_data.phyq_fbsb_extend_fns;
}

static void l1ctl_conn_close_cb(struct l1ctl_client *l1c)
{
	struct trxcon_inst *trxcon = l1c->priv;

	if (trxcon == NULL || trxcon->fi == NULL)
		return;

	osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_L2IF_FAILURE, NULL);
}

static void print_usage(const char *app)
{
	printf("Usage: %s\n", app);
}

static void print_help(void)
{
	printf(" Some help...\n");
	printf("  -h --help         this text\n");
	printf("  -d --debug        Change debug flags (e.g. DL1C:DSCH)\n");
	printf("  -b --trx-bind     TRX bind IP address (default 0.0.0.0)\n");
	printf("  -i --trx-remote   TRX remote IP address (default 127.0.0.1)\n");
	printf("  -p --trx-port     Base port of TRX instance (default 6700)\n");
	printf("  -f --trx-advance  Uplink burst scheduling advance (default 2)\n");
	printf("  -F --fbsb-extend  FBSB timeout extension (in TDMA FNs, default 0)\n");
	printf("  -s --socket       Listening socket for layer23 (default /tmp/osmocom_l2)\n");
	printf("  -g --gsmtap-ip    The destination IP used for GSMTAP (disabled by default)\n");
	printf("  -C --max-clients  Maximum number of L1CTL connections (default 1)\n");
	printf("  -D --daemonize    Run as daemon\n");
}

static void handle_options(int argc, char **argv)
{
	while (1) {
		char *endptr = NULL;
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"debug", 1, 0, 'd'},
			{"socket", 1, 0, 's'},
			{"trx-bind", 1, 0, 'b'},
			/* NOTE: 'trx-ip' is now an alias for 'trx-remote'
			 * due to backward compatibility reasons! */
			{"trx-ip", 1, 0, 'i'},
			{"trx-remote", 1, 0, 'i'},
			{"trx-port", 1, 0, 'p'},
			{"trx-advance", 1, 0, 'f'},
			{"fbsb-extend", 1, 0, 'F'},
			{"gsmtap-ip", 1, 0, 'g'},
			{"max-clients", 1, 0, 'C'},
			{"daemonize", 0, 0, 'D'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "d:b:i:p:f:F:s:g:C:Dh",
				long_options, &option_index);
		if (c == -1)
			break;

		errno = 0;

		switch (c) {
		case 'h':
			print_usage(argv[0]);
			print_help();
			exit(0);
			break;
		case 'd':
			app_data.debug_mask = optarg;
			break;
		case 'b':
			app_data.trx_bind_ip = optarg;
			break;
		case 'i':
			app_data.trx_remote_ip = optarg;
			break;
		case 'p':
			app_data.trx_base_port = strtoul(optarg, &endptr, 10);
			if (errno || *endptr != '\0') {
				fprintf(stderr, "Failed to parse -p/--trx-port=%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'f':
			app_data.trx_fn_advance = strtoul(optarg, &endptr, 10);
			if (errno || *endptr != '\0') {
				fprintf(stderr, "Failed to parse -f/--trx-advance=%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'F':
			app_data.phyq_fbsb_extend_fns = strtoul(optarg, &endptr, 10);
			if (errno || *endptr != '\0') {
				fprintf(stderr, "Failed to parse -F/--fbsb-extend=%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 's':
			app_data.bind_socket = optarg;
			break;
		case 'g':
			app_data.gsmtap_ip = optarg;
			break;
		case 'C':
			app_data.max_clients = strtoul(optarg, &endptr, 10);
			if (errno || *endptr != '\0') {
				fprintf(stderr, "Failed to parse -C/--max-clients=%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'D':
			app_data.daemonize = 1;
			break;
		default:
			break;
		}
	}
}

static void signal_handler(int signum)
{
	fprintf(stderr, "signal %u received\n", signum);

	switch (signum) {
	case SIGINT:
	case SIGTERM:
		app_data.quit++;
		break;
	case SIGABRT:
		/* in case of abort, we want to obtain a talloc report and
		 * then run default SIGABRT handler, who will generate coredump
		 * and abort the process. abort() should do this for us after we
		 * return, but program wouldn't exit if an external SIGABRT is
		 * received.
		 */
		talloc_report_full(tall_trxcon_ctx, stderr);
		signal(SIGABRT, SIG_DFL);
		raise(SIGABRT);
		break;
	case SIGUSR1:
	case SIGUSR2:
		talloc_report_full(tall_trxcon_ctx, stderr);
		break;
	default:
		break;
	}
}

int main(int argc, char **argv)
{
	struct l1ctl_server_cfg server_cfg;
	struct l1ctl_server *server = NULL;
	int rc = 0;

	printf("%s", COPYRIGHT);
	handle_options(argc, argv);

	/* Track the use of talloc NULL memory contexts */
	talloc_enable_null_tracking();

	/* Init talloc memory management system */
	tall_trxcon_ctx = talloc_init("trxcon context");
	msgb_talloc_ctx_init(tall_trxcon_ctx, 0);

	/* Setup signal handlers */
	signal(SIGINT, &signal_handler);
	signal(SIGTERM, &signal_handler);
	signal(SIGABRT, &signal_handler);
	signal(SIGUSR1, &signal_handler);
	signal(SIGUSR2, &signal_handler);
	osmo_init_ignore_signals();

	/* Init logging system */
	trxcon_logging_init(tall_trxcon_ctx, app_data.debug_mask);

	/* Configure pretty logging */
	log_set_print_extended_timestamp(osmo_stderr_target, 1);
	log_set_print_category_hex(osmo_stderr_target, 0);
	log_set_print_category(osmo_stderr_target, 1);
	log_set_print_level(osmo_stderr_target, 1);

	log_set_print_filename2(osmo_stderr_target, LOG_FILENAME_BASENAME);
	log_set_print_filename_pos(osmo_stderr_target, LOG_FILENAME_POS_LINE_END);

	osmo_fsm_log_timeouts(true);

	/* Optional GSMTAP  */
	if (app_data.gsmtap_ip != NULL) {
		struct log_target *lt;

		app_data.gsmtap = gsmtap_source_init(app_data.gsmtap_ip, GSMTAP_UDP_PORT, 1);
		if (!app_data.gsmtap) {
			LOGP(DAPP, LOGL_ERROR, "Failed to init GSMTAP Um logging\n");
			goto exit;
		}

		lt = log_target_create_gsmtap(app_data.gsmtap_ip, GSMTAP_UDP_PORT,
					      "trxcon", false, false);
		if (lt == NULL) {
			LOGP(DAPP, LOGL_ERROR, "Failed to init GSMTAP logging target\n");
			goto exit;
		} else {
			log_add_target(lt);
		}

		/* Suppress ICMP "destination unreachable" errors */
		gsmtap_source_add_sink(app_data.gsmtap);
	}

	/* Start the L1CTL server */
	server_cfg = (struct l1ctl_server_cfg) {
		.sock_path = app_data.bind_socket,
		.num_clients_max = app_data.max_clients,
		.conn_read_cb = &l1ctl_rx_cb,
		.conn_accept_cb = &l1ctl_conn_accept_cb,
		.conn_close_cb = &l1ctl_conn_close_cb,
	};

	server = l1ctl_server_alloc(tall_trxcon_ctx, &server_cfg);
	if (server == NULL) {
		rc = EXIT_FAILURE;
		goto exit;
	}

	LOGP(DAPP, LOGL_NOTICE, "Init complete\n");

	if (app_data.daemonize) {
		rc = osmo_daemonize();
		if (rc < 0) {
			perror("Error during daemonize");
			goto exit;
		}
	}

	/* Initialize pseudo-random generator */
	srand(time(NULL));

	while (!app_data.quit)
		osmo_select_main(0);

exit:
	if (server != NULL)
		l1ctl_server_free(server);

	/* Deinitialize logging */
	log_fini();

	/**
	 * Print report for the root talloc context in order
	 * to be able to find and fix potential memory leaks.
	 */
	talloc_report_full(tall_trxcon_ctx, stderr);
	talloc_free(tall_trxcon_ctx);

	/* Make both Valgrind and ASAN happy */
	talloc_report_full(NULL, stderr);
	talloc_disable_null_tracking();

	return rc;
}
