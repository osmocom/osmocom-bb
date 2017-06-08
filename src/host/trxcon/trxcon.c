/*
 * OsmocomBB <-> SDR connection bridge
 *
 * (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/select.h>
#include <osmocom/core/application.h>

#include "trxcon.h"
#include "trx_if.h"
#include "logging.h"
#include "l1ctl.h"
#include "l1ctl_link.h"
#include "l1ctl_proto.h"

#define COPYRIGHT \
	"Copyright (C) 2016-2017 by Vadim Yanitskiy <axilirator@gmail.com>\n" \
	"License GPLv2+: GNU GPL version 2 or later " \
	"<http://gnu.org/licenses/gpl.html>\n" \
	"This is free software: you are free to change and redistribute it.\n" \
	"There is NO WARRANTY, to the extent permitted by law.\n\n"

static struct {
	const char *debug_mask;
	int daemonize;
	int quit;

	/* L1CTL specific */
	struct l1ctl_link *l1l;
	const char *bind_socket;

	/* TRX specific */
	struct trx_instance *trx;
	const char *trx_ip;
	uint16_t trx_base_port;
} app_data;

void *tall_trx_ctx = NULL;
struct osmo_fsm_inst *trxcon_fsm;

static void trxcon_fsm_idle_action(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	if (event == L1CTL_EVENT_CONNECT)
		osmo_fsm_inst_state_chg(trxcon_fsm, TRXCON_STATE_MANAGED, 0, 0);
}

static void trxcon_fsm_managed_action(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	uint16_t *band_arfcn;

	switch (event) {
	case L1CTL_EVENT_DISCONNECT:
		osmo_fsm_inst_state_chg(trxcon_fsm, TRXCON_STATE_IDLE, 0, 0);

		if (app_data.trx->fsm->state != TRX_STATE_OFFLINE) {
			trx_if_flush_ctrl(app_data.trx);
			trx_if_cmd_poweroff(app_data.trx);
		}
		break;
	case L1CTL_EVENT_RESET_REQ:
		trx_if_cmd_echo(app_data.trx);
		break;
	case TRX_EVENT_RESET_IND:
		/* TODO: send proper reset type */
		l1ctl_tx_reset_conf(app_data.l1l, L1CTL_RES_T_BOOT);
		break;
	case L1CTL_EVENT_FBSB_REQ:
		band_arfcn = (uint16_t *) data;
		trx_if_cmd_rxtune(app_data.trx, *band_arfcn);
		trx_if_cmd_txtune(app_data.trx, *band_arfcn);
		trx_if_cmd_poweron(app_data.trx);
		break;
	case TRX_EVENT_RSP_ERROR:
	case TRX_EVENT_OFFLINE:
	case SCH_EVENT_CLCK_IND:
	case SCH_EVENT_CLCK_LOSS:
		/* TODO: notify L2 & L3 about that */
		break;
	default:
		LOGPFSML(fi, LOGL_ERROR, "Unhandled event %u\n", event);
	}
}

static struct osmo_fsm_state trxcon_fsm_states[] = {
	[TRXCON_STATE_IDLE] = {
		.in_event_mask = GEN_MASK(L1CTL_EVENT_CONNECT),
		.out_state_mask = GEN_MASK(TRXCON_STATE_MANAGED),
		.name = "IDLE",
		.action = trxcon_fsm_idle_action,
	},
	[TRXCON_STATE_MANAGED] = {
		.in_event_mask = (
			GEN_MASK(L1CTL_EVENT_DISCONNECT) |
			GEN_MASK(L1CTL_EVENT_FBSB_REQ) |
			GEN_MASK(L1CTL_EVENT_RESET_REQ) |
			GEN_MASK(TRX_EVENT_RESET_IND) |
			GEN_MASK(TRX_EVENT_RSP_ERROR) |
			GEN_MASK(TRX_EVENT_OFFLINE) |
			GEN_MASK(SCH_EVENT_CLCK_IND) |
			GEN_MASK(SCH_EVENT_CLCK_LOSS)),
		.out_state_mask = GEN_MASK(TRXCON_STATE_IDLE),
		.name = "MANAGED",
		.action = trxcon_fsm_managed_action,
	},
};

static struct osmo_fsm trxcon_fsm_def = {
	.name = "trxcon_app_fsm",
	.states = trxcon_fsm_states,
	.num_states = ARRAY_SIZE(trxcon_fsm_states),
	.log_subsys = DAPP,
};

static void print_usage(const char *app)
{
	printf("Usage: %s\n", app);
}

static void print_help(void)
{
	printf(" Some help...\n");
	printf("  -h --help         this text\n");
	printf("  -d --debug        Change debug flags. Default: %s\n", DEBUG_DEFAULT);
	printf("  -i --trx-ip       IP address of host runing TRX (default 127.0.0.1)\n");
	printf("  -p --trx-port     Base port of TRX instance (default 5700)\n");
	printf("  -s --socket       Listening socket for layer23 (default /tmp/osmocom_l2)\n");
	printf("  -D --daemonize    Run as daemon\n");
}

static void handle_options(int argc, char **argv)
{
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"debug", 1, 0, 'd'},
			{"socket", 1, 0, 's'},
			{"trx-ip", 1, 0, 'i'},
			{"trx-port", 1, 0, 'p'},
			{"daemonize", 0, 0, 'D'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "d:i:p:s:Dh",
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
			app_data.debug_mask = optarg;
			break;
		case 'i':
			app_data.trx_ip = optarg;
			break;
		case 'p':
			app_data.trx_base_port = atoi(optarg);
			break;
		case 's':
			app_data.bind_socket = optarg;
			break;
		case 'D':
			app_data.daemonize = 1;
			break;
		default:
			break;
		}
	}
}

static void init_defaults(void)
{
	app_data.bind_socket = "/tmp/osmocom_l2";
	app_data.trx_ip = "127.0.0.1";
	app_data.trx_base_port = 5700;

	app_data.debug_mask = NULL;
	app_data.daemonize = 0;
	app_data.quit = 0;
}

static void signal_handler(int signal)
{
	fprintf(stderr, "signal %u received\n", signal);

	switch (signal) {
	case SIGINT:
		app_data.quit++;
		break;
	case SIGABRT:
	case SIGUSR1:
	case SIGUSR2:
		talloc_report_full(tall_trx_ctx, stderr);
		break;
	default:
		break;
	}
}

int main(int argc, char **argv)
{
	int rc = 0;

	printf("%s", COPYRIGHT);
	init_defaults();
	handle_options(argc, argv);

	/* Init talloc memory management system */
	tall_trx_ctx = talloc_init("trxcon context");
	msgb_talloc_ctx_init(tall_trx_ctx, 0);

	/* Setup signal handlers */
	signal(SIGINT, &signal_handler);
	signal(SIGUSR1, &signal_handler);
	signal(SIGUSR2, &signal_handler);
	osmo_init_ignore_signals();

	/* Init logging system */
	trx_log_init(app_data.debug_mask);

	/* Allocate the application state machine */
	osmo_fsm_register(&trxcon_fsm_def);
	trxcon_fsm = osmo_fsm_inst_alloc(&trxcon_fsm_def, tall_trx_ctx,
		NULL, LOGL_DEBUG, "main");

	/* Init L1CTL server */
	rc = l1ctl_link_init(&app_data.l1l, app_data.bind_socket);
	if (rc)
		goto exit;

	/* Init transceiver interface */
	rc = trx_if_open(&app_data.trx, app_data.trx_ip, app_data.trx_base_port);
	if (rc)
		goto exit;

	LOGP(DAPP, LOGL_NOTICE, "Init complete\n");

	if (app_data.daemonize) {
		rc = osmo_daemonize();
		if (rc < 0) {
			perror("Error during daemonize");
			goto exit;
		}
	}

	while (!app_data.quit)
		osmo_select_main(0);

exit:
	/* Close active connections */
	l1ctl_link_shutdown(app_data.l1l);
	trx_if_close(app_data.trx);

	/* Shutdown main state machine */
	osmo_fsm_inst_free(trxcon_fsm);

	/* Make Valgrind happy */
	log_fini();
	talloc_free(tall_trx_ctx);

	return rc;
}
