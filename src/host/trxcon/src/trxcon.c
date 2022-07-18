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

#include <arpa/inet.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/select.h>
#include <osmocom/core/application.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/core/gsmtap.h>

#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/bb/trxcon/trxcon.h>
#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/logging.h>
#include <osmocom/bb/trxcon/l1ctl.h>
#include <osmocom/bb/trxcon/l1ctl_server.h>
#include <osmocom/bb/trxcon/l1ctl_proto.h>
#include <osmocom/bb/trxcon/l1sched.h>

#define S(x)	(1 << (x))

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
	const char *bind_socket;

	/* TRX specific */
	const char *trx_bind_ip;
	const char *trx_remote_ip;
	uint16_t trx_base_port;
	uint32_t trx_fn_advance;

	/* GSMTAP specific */
	struct gsmtap_inst *gsmtap;
	const char *gsmtap_ip;
} app_data = {
	.bind_socket = "/tmp/osmocom_l2",
	.trx_remote_ip = "127.0.0.1",
	.trx_bind_ip = "0.0.0.0",
	.trx_base_port = 6700,
	.trx_fn_advance = 3,
};

static void *tall_trxcon_ctx = NULL;

static void trxcon_gsmtap_send(const struct l1sched_lchan_desc *lchan_desc,
			       uint32_t fn, uint8_t tn, uint16_t band_arfcn,
			       int8_t signal_dbm, uint8_t snr,
			       const uint8_t *data, size_t data_len)
{
	/* GSMTAP logging may be not enabled */
	if (app_data.gsmtap == NULL)
		return;

	/* Omit frames with unknown channel type */
	if (lchan_desc->gsmtap_chan_type == GSMTAP_CHANNEL_UNKNOWN)
		return;

	/* TODO: distinguish GSMTAP_CHANNEL_PCH and GSMTAP_CHANNEL_AGCH */
	gsmtap_send(app_data.gsmtap, band_arfcn, tn, lchan_desc->gsmtap_chan_type,
		    lchan_desc->ss_nr, fn, signal_dbm, snr, data, data_len);
}

/* External L1 API for the scheduler */
int l1sched_handle_config_req(struct l1sched_state *sched,
			      const struct l1sched_config_req *cr)
{
	struct trxcon_inst *trxcon = sched->priv;

	switch (cr->type) {
	case L1SCHED_CFG_PCHAN_COMB:
		return trx_if_cmd_setslot(trxcon->trx,
					  cr->pchan_comb.tn,
					  cr->pchan_comb.pchan);
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled config request (type 0x%02x)\n", cr->type);
		return -ENODEV;
	}
}

int l1sched_handle_burst_req(struct l1sched_state *sched,
			     const struct l1sched_burst_req *br)
{
	struct trxcon_inst *trxcon = sched->priv;

	return trx_if_tx_burst(trxcon->trx, br);
}

/* External L2 API for the scheduler */
int l1sched_handle_data_ind(struct l1sched_lchan_state *lchan,
			    const uint8_t *data, size_t data_len,
			    int n_errors, int n_bits_total,
			    enum l1sched_data_type dt)
{
	const struct l1sched_meas_set *meas = &lchan->meas_avg;
	const struct l1sched_lchan_desc *lchan_desc;
	struct l1sched_state *sched = lchan->ts->sched;
	struct trxcon_inst *trxcon = sched->priv;
	struct l1ctl_info_dl dl_hdr;
	int rc;

	lchan_desc = &l1sched_lchan_desc[lchan->type];

	dl_hdr = (struct l1ctl_info_dl) {
		.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
		.link_id = lchan_desc->link_id,
		.frame_nr = htonl(meas->fn),
		.band_arfcn = htons(trxcon->trx->band_arfcn),
		.fire_crc = data_len > 0 ? 0 : 2,
		.rx_level = dbm2rxlev(meas->rssi),
		.num_biterr = n_errors,
		/* TODO: set proper .snr */
	};

	switch (dt) {
	case L1SCHED_DT_TRAFFIC:
	case L1SCHED_DT_PACKET_DATA:
		rc = l1ctl_tx_dt_ind(trxcon->l1c, &dl_hdr, data, data_len, true);
		break;
	case L1SCHED_DT_SIGNALING:
		rc = l1ctl_tx_dt_ind(trxcon->l1c, &dl_hdr, data, data_len, false);
		break;
	case L1SCHED_DT_OTHER:
		if (lchan->type == L1SCHED_SCH) {
			if (trxcon->fbsb_conf_sent)
				return 0;
			rc = l1ctl_tx_fbsb_conf(trxcon->l1c, 0, &dl_hdr, sched->bsic);
			break;
		}
		/* fall through */
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled L2 DATA.ind (type 0x%02x)\n", dt);
		return -ENODEV;
	}

	if (data != NULL && data_len > 0) {
		trxcon_gsmtap_send(lchan_desc, meas->fn, lchan->ts->index,
				   trxcon->trx->band_arfcn, meas->rssi, 0,
				   data, data_len);
	}

	return rc;
}

int l1sched_handle_data_cnf(struct l1sched_lchan_state *lchan,
			    uint32_t fn, enum l1sched_data_type dt)
{
	const struct l1sched_lchan_desc *lchan_desc;
	struct l1sched_state *sched = lchan->ts->sched;
	struct trxcon_inst *trxcon = sched->priv;
	struct l1ctl_info_dl dl_hdr;
	const uint8_t *data;
	uint8_t ra_buf[2];
	size_t data_len;
	int rc;

	lchan_desc = &l1sched_lchan_desc[lchan->type];

	dl_hdr = (struct l1ctl_info_dl) {
		.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
		.link_id = lchan_desc->link_id,
		.frame_nr = htonl(fn),
		.band_arfcn = htons(trxcon->trx->band_arfcn),
	};

	switch (dt) {
	case L1SCHED_DT_TRAFFIC:
	case L1SCHED_DT_PACKET_DATA:
		rc = l1ctl_tx_dt_conf(trxcon->l1c, &dl_hdr, true);
		data_len = lchan->prim->payload_len;
		data = lchan->prim->payload;
		break;
	case L1SCHED_DT_SIGNALING:
		rc = l1ctl_tx_dt_conf(trxcon->l1c, &dl_hdr, false);
		data_len = lchan->prim->payload_len;
		data = lchan->prim->payload;
		break;
	case L1SCHED_DT_OTHER:
		if (L1SCHED_PRIM_IS_RACH(lchan->prim)) {
			const struct l1sched_ts_prim_rach *rach;

			rach = (struct l1sched_ts_prim_rach *)lchan->prim->payload;

			rc = l1ctl_tx_rach_conf(trxcon->l1c, trxcon->trx->band_arfcn, fn);
			if (lchan->prim->type == L1SCHED_PRIM_RACH11) {
				ra_buf[0] = (uint8_t)(rach->ra >> 3);
				ra_buf[1] = (uint8_t)(rach->ra & 0x07);
				data = &ra_buf[0];
				data_len = 2;
			} else {
				ra_buf[0] = (uint8_t)(rach->ra);
				data = &ra_buf[0];
				data_len = 1;
			}
			break;
		}
		/* fall through */
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled L2 DATA.cnf (type 0x%02x)\n", dt);
		return -ENODEV;
	}

	trxcon_gsmtap_send(lchan_desc, fn, lchan->ts->index,
			   trxcon->trx->band_arfcn | ARFCN_UPLINK,
			   0, 0, data, data_len);

	return rc;
}

/* The trxcon state machine */
static void trxcon_fsm_idle_action(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	if (event == L1CTL_EVENT_CONNECT)
		osmo_fsm_inst_state_chg(fi, TRXCON_STATE_MANAGED, 0, 0);
}

static void trxcon_fsm_managed_action(struct osmo_fsm_inst *fi,
	uint32_t event, void *data)
{
	struct trxcon_inst *trxcon = fi->priv;

	switch (event) {
	case L1CTL_EVENT_DISCONNECT:
		osmo_fsm_inst_state_chg(fi, TRXCON_STATE_IDLE, 0, 0);

		if (trxcon->trx->fsm->state != TRX_STATE_OFFLINE) {
			/* Reset scheduler and clock counter */
			l1sched_reset(trxcon->sched, true);

			/* TODO: implement trx_if_reset() */
			trx_if_cmd_poweroff(trxcon->trx);
			trx_if_cmd_echo(trxcon->trx);
		}
		break;
	case TRX_EVENT_RSP_ERROR:
	case TRX_EVENT_OFFLINE:
		/* TODO: notify L2 & L3 about that */
		break;
	default:
		LOGPFSML(fi, LOGL_ERROR, "Unhandled event %u\n", event);
	}
}

static struct osmo_fsm_state trxcon_fsm_states[] = {
	[TRXCON_STATE_IDLE] = {
		.in_event_mask = S(L1CTL_EVENT_CONNECT),
		.out_state_mask = S(TRXCON_STATE_MANAGED),
		.name = "IDLE",
		.action = trxcon_fsm_idle_action,
	},
	[TRXCON_STATE_MANAGED] = {
		.in_event_mask = (
			S(L1CTL_EVENT_DISCONNECT) |
			S(TRX_EVENT_RSP_ERROR) |
			S(TRX_EVENT_OFFLINE)),
		.out_state_mask = S(TRXCON_STATE_IDLE),
		.name = "MANAGED",
		.action = trxcon_fsm_managed_action,
	},
};

static const struct value_string trxcon_fsm_event_names[] = {
	OSMO_VALUE_STRING(L1CTL_EVENT_CONNECT),
	OSMO_VALUE_STRING(L1CTL_EVENT_DISCONNECT),
	OSMO_VALUE_STRING(TRX_EVENT_OFFLINE),
	OSMO_VALUE_STRING(TRX_EVENT_RSP_ERROR),
	{ 0, NULL }
};

static struct osmo_fsm trxcon_fsm_def = {
	.name = "trxcon",
	.states = trxcon_fsm_states,
	.num_states = ARRAY_SIZE(trxcon_fsm_states),
	.log_subsys = DAPP,
	.event_names = trxcon_fsm_event_names,
};

struct trxcon_inst *trxcon_inst_alloc(void *ctx)
{
	struct trxcon_inst *trxcon;

	trxcon = talloc_zero(ctx, struct trxcon_inst);
	OSMO_ASSERT(trxcon != NULL);

	trxcon->fi = osmo_fsm_inst_alloc(&trxcon_fsm_def, tall_trxcon_ctx,
					 trxcon, LOGL_DEBUG, NULL);
	OSMO_ASSERT(trxcon->fi != NULL);

	/* Init transceiver interface */
	trxcon->trx = trx_if_open(trxcon,
				  app_data.trx_bind_ip,
				  app_data.trx_remote_ip,
				  app_data.trx_base_port);
	if (trxcon->trx == NULL) {
		trxcon_inst_free(trxcon);
		return NULL;
	}

	/* Init scheduler */
	trxcon->sched = l1sched_alloc(trxcon, app_data.trx_fn_advance, trxcon);
	if (trxcon->sched == NULL) {
		trxcon_inst_free(trxcon);
		return NULL;
	}

	return trxcon;
}

void trxcon_inst_free(struct trxcon_inst *trxcon)
{
	/* Shutdown the scheduler */
	if (trxcon->sched != NULL)
		l1sched_free(trxcon->sched);
	/* Close active connections */
	if (trxcon->l1c != NULL)
		l1ctl_client_conn_close(trxcon->l1c);
	if (trxcon->trx != NULL)
		trx_if_close(trxcon->trx);

	if (osmo_timer_pending(&trxcon->fbsb_timer))
		osmo_timer_del(&trxcon->fbsb_timer);

	if (trxcon->fi != NULL)
		osmo_fsm_inst_free(trxcon->fi);
	talloc_free(trxcon);
}

static void l1ctl_conn_accept_cb(struct l1ctl_client *l1c)
{
	struct trxcon_inst *trxcon;

	trxcon = trxcon_inst_alloc(l1c);
	if (trxcon == NULL) {
		l1ctl_client_conn_close(l1c);
		return;
	}

	l1c->priv = trxcon;
	trxcon->l1c = l1c;
}

static void l1ctl_conn_close_cb(struct l1ctl_client *l1c)
{
	struct trxcon_inst *trxcon = l1c->priv;

	if (trxcon == NULL)
		return;

	/* l1c is free()ed by the caller */
	trxcon->l1c = NULL;
	trxcon_inst_free(trxcon);
}

static void print_usage(const char *app)
{
	printf("Usage: %s\n", app);
}

static void print_help(void)
{
	printf(" Some help...\n");
	printf("  -h --help         this text\n");
	printf("  -d --debug        Change debug flags. Default: %s\n", DEBUG_DEFAULT);
	printf("  -b --trx-bind     TRX bind IP address (default 0.0.0.0)\n");
	printf("  -i --trx-remote   TRX remote IP address (default 127.0.0.1)\n");
	printf("  -p --trx-port     Base port of TRX instance (default 6700)\n");
	printf("  -f --trx-advance  Uplink burst scheduling advance (default 3)\n");
	printf("  -s --socket       Listening socket for layer23 (default /tmp/osmocom_l2)\n");
	printf("  -g --gsmtap-ip    The destination IP used for GSMTAP (disabled by default)\n");
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
			{"trx-bind", 1, 0, 'b'},
			/* NOTE: 'trx-ip' is now an alias for 'trx-remote'
			 * due to backward compatibility reasons! */
			{"trx-ip", 1, 0, 'i'},
			{"trx-remote", 1, 0, 'i'},
			{"trx-port", 1, 0, 'p'},
			{"trx-advance", 1, 0, 'f'},
			{"gsmtap-ip", 1, 0, 'g'},
			{"daemonize", 0, 0, 'D'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "d:b:i:p:f:s:g:Dh",
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
		case 'b':
			app_data.trx_bind_ip = optarg;
			break;
		case 'i':
			app_data.trx_remote_ip = optarg;
			break;
		case 'p':
			app_data.trx_base_port = atoi(optarg);
			break;
		case 'f':
			app_data.trx_fn_advance = atoi(optarg);
			break;
		case 's':
			app_data.bind_socket = optarg;
			break;
		case 'g':
			app_data.gsmtap_ip = optarg;
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
	struct l1ctl_server server;
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
	signal(SIGABRT, &signal_handler);
	signal(SIGUSR1, &signal_handler);
	signal(SIGUSR2, &signal_handler);
	osmo_init_ignore_signals();

	/* Init logging system */
	trx_log_init(tall_trxcon_ctx, app_data.debug_mask);

	/* Configure pretty logging */
	log_set_print_extended_timestamp(osmo_stderr_target, 1);
	log_set_print_category_hex(osmo_stderr_target, 0);
	log_set_print_category(osmo_stderr_target, 1);
	log_set_print_level(osmo_stderr_target, 1);

	log_set_print_filename2(osmo_stderr_target, LOG_FILENAME_BASENAME);
	log_set_print_filename_pos(osmo_stderr_target, LOG_FILENAME_POS_LINE_END);

	/* Optional GSMTAP  */
	if (app_data.gsmtap_ip != NULL) {
		app_data.gsmtap = gsmtap_source_init(app_data.gsmtap_ip, GSMTAP_UDP_PORT, 1);
		if (!app_data.gsmtap) {
			LOGP(DAPP, LOGL_ERROR, "Failed to init GSMTAP\n");
			goto exit;
		}
		/* Suppress ICMP "destination unreachable" errors */
		gsmtap_source_add_sink(app_data.gsmtap);
	}

	/* Register the trxcon state machine */
	OSMO_ASSERT(osmo_fsm_register(&trxcon_fsm_def) == 0);

	/* Start the L1CTL server */
	server_cfg = (struct l1ctl_server_cfg) {
		.sock_path = app_data.bind_socket,
		.talloc_ctx = tall_trxcon_ctx,
		.num_clients_max = 1, /* only one connection for now */
		.conn_read_cb = &l1ctl_rx_cb,
		.conn_accept_cb = &l1ctl_conn_accept_cb,
		.conn_close_cb = &l1ctl_conn_close_cb,
	};

	if (l1ctl_server_start(&server, &server_cfg) != 0) {
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
	l1ctl_server_shutdown(&server);

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
