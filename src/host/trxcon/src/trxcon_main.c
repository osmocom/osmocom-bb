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
#include <osmocom/bb/trxcon/trxcon_fsm.h>
#include <osmocom/bb/trxcon/phyif.h>
#include <osmocom/bb/trxcon/trx_if.h>
#include <osmocom/bb/trxcon/logging.h>
#include <osmocom/bb/trxcon/l1ctl.h>
#include <osmocom/bb/trxcon/l1ctl_server.h>
#include <osmocom/bb/l1sched/l1sched.h>

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

	/* GSMTAP specific */
	struct gsmtap_inst *gsmtap;
	const char *gsmtap_ip;
} app_data = {
	.max_clients = 1, /* only one L1CTL client by default */
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
	{
		struct trxcon_param_set_phy_config_req req = {
			.type = TRXCON_PHY_CFGT_PCHAN_COMB,
			.pchan_comb = {
				.tn = cr->pchan_comb.tn,
				.pchan = cr->pchan_comb.pchan,
			},
		};

		return osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_SET_PHY_CONFIG_REQ, &req);
	}
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
	const struct phyif_burst_req phybr = {
		.fn = br->fn,
		.tn = br->tn,
		.pwr = br->pwr,
		.burst = &br->burst[0],
		.burst_len = br->burst_len,
	};

	return phyif_handle_burst_req(trxcon->phyif, &phybr);
}

/* External L1 API for the PHYIF */
int phyif_handle_burst_ind(void *phyif, const struct phyif_burst_ind *bi)
{
	struct trx_instance *trx = phyif;
	struct trxcon_inst *trxcon = trx->priv;
	const struct l1sched_meas_set meas = {
		.fn = bi->fn,
		.toa256 = bi->toa256,
		.rssi = bi->rssi,
	};

	/* Poke scheduler */
	l1sched_handle_rx_burst(trxcon->sched, bi->tn, bi->fn,
				bi->burst, bi->burst_len, &meas);

	/* Correct local clock counter */
	if (bi->fn % 51 == 0)
		l1sched_clck_handle(trxcon->sched, bi->fn);

	return 0;
}

int phyif_handle_burst_req(void *phyif, const struct phyif_burst_req *br)
{
	return trx_if_handle_phyif_burst_req(phyif, br);
}

int phyif_handle_cmd(void *phyif, const struct phyif_cmd *cmd)
{
	return trx_if_handle_phyif_cmd(phyif, cmd);
}

int phyif_handle_rsp(void *phyif, const struct phyif_rsp *rsp)
{
	struct trx_instance *trx = phyif;
	struct trxcon_inst *trxcon = trx->priv;

	switch (rsp->type) {
	case PHYIF_CMDT_MEASURE:
	{
		const struct phyif_rspp_measure *meas = &rsp->param.measure;
		struct trxcon_param_full_power_scan_res res = {
			.band_arfcn = meas->band_arfcn,
			.dbm = meas->dbm,
		};

		return osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_FULL_POWER_SCAN_RES, &res);
	}
	default:
		LOGPFSML(trxcon->fi, LOGL_ERROR,
			 "Unhandled PHYIF response (type 0x%02x)\n", rsp->type);
		return -ENODEV;
	}
}

void phyif_close(void *phyif)
{
	trx_if_close(phyif);
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
	int rc;

	lchan_desc = &l1sched_lchan_desc[lchan->type];

	struct trxcon_param_rx_data_ind ind = {
		/* .traffic is set below */
		.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
		.link_id = lchan_desc->link_id,
		.band_arfcn = trxcon->l1p.band_arfcn,
		.frame_nr = meas->fn,
		.toa256 = meas->toa256,
		.rssi = meas->rssi,
		.n_errors = n_errors,
		.n_bits_total = n_bits_total,
		.data_len = data_len,
		.data = data,
	};

	switch (dt) {
	case L1SCHED_DT_PACKET_DATA:
	case L1SCHED_DT_TRAFFIC:
		ind.traffic = true;
		/* fall-through */
	case L1SCHED_DT_SIGNALING:
		rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_RX_DATA_IND, &ind);
		break;
	case L1SCHED_DT_OTHER:
		if (lchan->type == L1SCHED_SCH) {
			if (trxcon->fi->state != TRXCON_ST_FBSB_SEARCH)
				return 0;
			rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_FBSB_SEARCH_RES, NULL);
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
				   trxcon->l1p.band_arfcn, meas->rssi, 0,
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
	bool is_traffic = false;
	const uint8_t *data;
	uint8_t ra_buf[2];
	size_t data_len;
	int rc;

	lchan_desc = &l1sched_lchan_desc[lchan->type];

	switch (dt) {
	case L1SCHED_DT_TRAFFIC:
	case L1SCHED_DT_PACKET_DATA:
		is_traffic = true;
		/* fall-through */
	case L1SCHED_DT_SIGNALING:
	{
		struct trxcon_param_tx_data_cnf cnf = {
			.traffic = is_traffic,
			.chan_nr = lchan_desc->chan_nr | lchan->ts->index,
			.link_id = lchan_desc->link_id,
			.band_arfcn = trxcon->l1p.band_arfcn,
			.frame_nr = fn,
		};

		rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_TX_DATA_CNF, &cnf);
		data_len = lchan->prim->payload_len;
		data = lchan->prim->payload;
		break;
	}
	case L1SCHED_DT_OTHER:
		if (L1SCHED_PRIM_IS_RACH(lchan->prim)) {
			const struct l1sched_ts_prim_rach *rach;
			struct trxcon_param_tx_access_burst_cnf cnf = {
				.band_arfcn = trxcon->l1p.band_arfcn,
				.frame_nr = fn,
			};

			rc = osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_TX_ACCESS_BURST_CNF, &cnf);

			rach = (struct l1sched_ts_prim_rach *)lchan->prim->payload;
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
			   trxcon->l1p.band_arfcn | ARFCN_UPLINK,
			   0, 0, data, data_len);

	return rc;
}

static void l1ctl_conn_accept_cb(struct l1ctl_client *l1c)
{
	struct trxcon_inst *trxcon;

	trxcon = trxcon_inst_alloc(l1c, l1c->id, app_data.trx_fn_advance);
	if (trxcon == NULL) {
		l1ctl_client_conn_close(l1c);
		return;
	}

	l1c->log_prefix = talloc_strdup(l1c, trxcon->log_prefix);
	l1c->priv = trxcon->fi;
	trxcon->l2if = l1c;

	const struct trx_if_params phyif_params = {
		.local_host = app_data.trx_bind_ip,
		.remote_host = app_data.trx_remote_ip,
		.base_port = app_data.trx_base_port,
		.instance = trxcon->id,

		.parent_fi = trxcon->fi,
		.parent_term_event = TRXCON_EV_PHYIF_FAILURE,
		.priv = trxcon,
	};

	/* Init transceiver interface */
	trxcon->phyif = trx_if_open(&phyif_params);
	if (trxcon->phyif == NULL) {
		/* TRXCON_EV_PHYIF_FAILURE triggers l1ctl_client_conn_close() */
		osmo_fsm_inst_dispatch(trxcon->fi, TRXCON_EV_PHYIF_FAILURE, NULL);
		return;
	}
}

static void l1ctl_conn_close_cb(struct l1ctl_client *l1c)
{
	struct osmo_fsm_inst *fi = l1c->priv;

	if (fi == NULL)
		return;

	osmo_fsm_inst_dispatch(fi, TRXCON_EV_L2IF_FAILURE, NULL);
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
	printf("  -f --trx-advance  Uplink burst scheduling advance (default 3)\n");
	printf("  -s --socket       Listening socket for layer23 (default /tmp/osmocom_l2)\n");
	printf("  -g --gsmtap-ip    The destination IP used for GSMTAP (disabled by default)\n");
	printf("  -C --max-clients  Maximum number of L1CTL connections (default 1)\n");
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
			{"max-clients", 1, 0, 'C'},
			{"daemonize", 0, 0, 'D'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "d:b:i:p:f:s:g:C:Dh",
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
		case 'C':
			app_data.max_clients = atoi(optarg);
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
	l1sched_logging_init(DSCH, DSCHD);

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
		app_data.gsmtap = gsmtap_source_init(app_data.gsmtap_ip, GSMTAP_UDP_PORT, 1);
		if (!app_data.gsmtap) {
			LOGP(DAPP, LOGL_ERROR, "Failed to init GSMTAP\n");
			goto exit;
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
