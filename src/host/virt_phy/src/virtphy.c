/* osmocom includes */

/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
 * (C) 2017 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <osmocom/core/msgb.h>
#include <osmocom/core/select.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/application.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/gsmtapl1_if.h>
#include <virtphy/logging.h>
#include <virtphy/virt_l1_sched.h>

#define DEFAULT_LOG_MASK "DL1C,2:DL1P,2:DVIRPHY,2:DMAIN,1"

/* this exists once in the program, and contains the state that we
 * only keep once:  L1CTL server socket, GSMTAP/VirtUM socket */
struct virtphy_context {
	/* L1CTL socket server */
	struct l1ctl_sock_inst *l1ctl_sock;
	/* Virtual Um layer based on GSMTAP multicast */
	struct virt_um_inst *virt_um;
};

static struct virtphy_context g_vphy;

static char *dl_rx_grp = DEFAULT_MS_MCAST_GROUP;
static char *ul_tx_grp = DEFAULT_BTS_MCAST_GROUP;
static int port = GSMTAP_UDP_PORT;
static char *log_mask = DEFAULT_LOG_MASK;
static char *l1ctl_sock_path = L1CTL_SOCK_PATH;
static char *arfcn_sig_lev_red_mask = NULL;
static char *pm_timeout = NULL;

static void handle_options(int argc, char **argv)
{
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"dl-rx-grp", required_argument, 0, 'z'},
		        {"ul-tx-grp", required_argument, 0, 'y'},
		        {"port", required_argument, 0, 'x'},
		        {"log-mask", required_argument, 0, 'd'},
		        {"l1ctl-sock", required_argument, 0, 's'},
		        {"arfcn-sig-lev-red", required_argument, 0, 'r'},
		        {"pm-timeout", required_argument, 0, 't'},
		        {0, 0, 0, 0},
		};
		c = getopt_long(argc, argv, "z:y:x:d:s:r:t:", long_options,
		                &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'z':
			dl_rx_grp = optarg;
			break;
		case 'y':
			ul_tx_grp = optarg;
			break;
		case 'x':
			port = atoi(optarg);
			break;
		case 'd':
			log_mask = optarg;
			break;
		case 's':
			l1ctl_sock_path = optarg;
			break;
		case 'r':
			arfcn_sig_lev_red_mask = optarg;
			break;
		case 't':
			pm_timeout = optarg;
			break;
		default:
			break;
		}
	}
}

void parse_pm_timeout(struct l1_model_ms *model, char *pm_timeout) {

	if (!pm_timeout || (strcmp(pm_timeout, "") == 0))
		return;

	/* seconds */
	char *buf = strtok(pm_timeout, ":");
	model->state.pm.timeout_s = atoi(buf);
	/* microseconds */
	buf = strtok(NULL, ":");
	if (buf)
		model->state.pm.timeout_us = atoi(buf);
}

/**
 * arfcn_sig_lev_red_mask has to be formatted like 666,12:888,43:176,22
 */
void parse_arfcn_sig_lev_red(struct l1_model_ms *model, char * arfcn_sig_lev_red_mask) {

	if (!arfcn_sig_lev_red_mask || (strcmp(arfcn_sig_lev_red_mask, "") == 0))
		return;

	char *token = strtok(arfcn_sig_lev_red_mask, ":");
	do {
		char* colon = strstr(token, ",");
		uint16_t arfcn;
		uint8_t red;
		if (!colon)
			continue;

		colon[0] = '\0';

		arfcn = atoi(token);
		red = atoi(colon + 1);

		/* TODO: this may go wild if the token string is not properly formatted */
		model->state.pm.meas.arfcn_sig_lev_red_dbm[arfcn] = red;
	} while ((token = strtok(NULL, ":")));
}

/* create a new l1_model_ms instance when L1CTL socket accept()s new connection */
static int l1ctl_accept_cb(struct l1ctl_sock_client *lsc)
{
	struct l1_model_ms *ms = l1_model_ms_init(lsc, lsc, g_vphy.virt_um);

	if (!ms)
		return -ENOMEM;

	/* apply timeout and arfcn reduction value config to model */
	parse_pm_timeout(ms, pm_timeout);
	parse_arfcn_sig_lev_red(ms, arfcn_sig_lev_red_mask);

	lsc->priv = ms;

	return 0;
}

static void l1ctl_close_cb(struct l1ctl_sock_client *lsc)
{
	struct l1_model_ms *ms = lsc->priv;
	l1_model_ms_destroy(ms);
}

static void *tall_vphy_ctx;

static void signal_handler(int signum)
{
	LOGP(DMAIN, LOGL_NOTICE, "Signal %d received\n", signum);

	switch (signum) {
	case SIGINT:
	case SIGTERM:
		exit(0);
		break;
	case SIGUSR1:
		talloc_report_full(tall_vphy_ctx, stderr);
		break;
	default:
		break;
	}
}

int main(int argc, char *argv[])
{
	tall_vphy_ctx = talloc_named_const(NULL, 1, "root");

	msgb_talloc_ctx_init(tall_vphy_ctx, 0);
	signal(SIGINT, &signal_handler);
	signal(SIGTERM, &signal_handler);
	signal(SIGUSR1, &signal_handler);
	osmo_init_ignore_signals();

	/* init loginfo */
	handle_options(argc, argv);

	ms_log_init(log_mask);

	LOGP(DVIRPHY, LOGL_INFO, "Virtual physical layer starting up...\n");

	g_vphy.virt_um = virt_um_init(tall_vphy_ctx, ul_tx_grp, port, dl_rx_grp, port,
					gsmtapl1_rx_from_virt_um_inst_cb);

	g_vphy.l1ctl_sock = l1ctl_sock_init(tall_vphy_ctx, l1ctl_sap_rx_from_l23_inst_cb,
					    l1ctl_accept_cb, l1ctl_close_cb, l1ctl_sock_path);
	g_vphy.virt_um->priv = g_vphy.l1ctl_sock;

	LOGP(DVIRPHY, LOGL_INFO, "Virtual physical layer ready, waiting for l23 app(s) on %s\n",
	     l1ctl_sock_path);

	while (1) {
		/* handle osmocom fd READ events (l1ctl-unix-socket, virtual-um-mcast-socket) */
		osmo_select_main(0);
	}

	l1ctl_sock_destroy(g_vphy.l1ctl_sock);
	virt_um_destroy(g_vphy.virt_um);

	/* not reached */
	return EXIT_FAILURE;
}
