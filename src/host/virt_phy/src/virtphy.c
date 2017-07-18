/* osmocom includes */

/* (C) 2016 by Sebastian Stumpf <sebastian.stumpf87@googlemail.com>
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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/gsmtapl1_if.h>
#include <virtphy/logging.h>
#include <virtphy/virt_l1_sched.h>

#define DEFAULT_MCAST_PORT 4729 /* IANA-registered port for GSMTAP */
#define DEFAULT_LOG_MASK "DL1C,1:DVIRPHY,1"

static char *dl_rx_grp = DEFAULT_MS_MCAST_GROUP;
static char *ul_tx_grp = DEFAULT_BTS_MCAST_GROUP;
static int port = DEFAULT_MCAST_PORT;
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

int main(int argc, char *argv[])
{
	/* init loginfo */
	static struct l1_model_ms *model;

	handle_options(argc, argv);

	ms_log_init(log_mask);

	LOGP(DVIRPHY, LOGL_INFO, "Virtual physical layer starting up...\n");

	model = l1_model_ms_init(NULL);

	model->vui = virt_um_init(NULL, ul_tx_grp, port, dl_rx_grp, port,
	                          gsmtapl1_rx_from_virt_um_inst_cb);
	model->lsi = l1ctl_sock_init(NULL, l1ctl_sap_rx_from_l23_inst_cb,
	                             l1ctl_sock_path);

	gsmtapl1_init(model);
	l1ctl_sap_init(model);
	virt_l1_sched_init(model);

	/* apply timeout and arfcn reduction value config to model */
	parse_pm_timeout(model, pm_timeout);
	parse_arfcn_sig_lev_red(model, arfcn_sig_lev_red_mask);

	LOGP(DVIRPHY, LOGL_INFO,
	     "Virtual physical layer ready...\n \
			Waiting for l23 app on %s",
	     l1ctl_sock_path);

	while (1) {
		/* handle osmocom fd READ events (l1ctl-unix-socket, virtual-um-mcast-socket) */
		osmo_select_main(0);
	}

	l1_model_ms_destroy(model);

	/* not reached */
	return EXIT_FAILURE;
}
