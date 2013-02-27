/*
 * main.c
 *
 * Tranceiver main program
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
 *
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <osmocom/core/select.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/application.h>
#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/bb/common/logging.h>

#include "app.h"
#include "l1ctl.h"
#include "l1ctl_link.h"
#include "trx.h"
#include "gmsk.h"
#include "gsm_ab.h"


void *l23_ctx = NULL;
static int arfcn_sync = 0;
static int daemonize = 0;
static int second_phone = 0;

static void print_help(char *argv[])
{
	fprintf(stderr, "Usage: %s -a arfcn_sync\n", argv[0]);

	printf( "Some useful options:\n"
		"  -h   --help             this text\n"
		"  -d   --debug MASK       Enable debugging (e.g. -d DL1C:DTRX)\n"
		"  -e   --log-level LOGL   Set log level (1=debug, 3=info, 5=notice)\n"
		"  -D   --daemonize        For the process into a background daemon\n"
		"  -s   --disable-color    Don't use colors in stderr log output\n"
		"  -a   --arfcn-sync ARFCN Set ARFCN to sync to\n"
		"  -p   --arfcn-sync-pcs   The ARFCN above is PCS\n"
		"  -2   --second-phone     Use second phone for TS 1\n"
		);
}

static void handle_options(int argc, char **argv, struct app_state *as)
{
	while (1) {
		int option_idx = 0, c;
		static const struct option long_options[] = {
			{ "help", 0, 0, 'h' },
			{ "debug", 1, 0, 'd' },
			{ "log-level", 1, 0, 'e' },
			{ "daemonize", 0, 0, 'D' },
			{ "disable-color", 0, 0, 's' },
			{ "arfcn-sync", 1, 0, 'a' },
			{ "arfcn-sync-pcs", 0, 0, 'p' },
			{ "second-phone", 0, 0, '2' },
		};

		c = getopt_long(argc, argv, "hd:e:Dsa:p2",
			long_options, &option_idx);

		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_help(argv);
			exit(0);
			break;
		case 'd':
			log_parse_category_mask(as->stderr_target, optarg);
			break;
		case 'D':
			daemonize = 1;
			break;
		case 's':
			log_set_use_color(as->stderr_target, 0);
			break;
		case 'a':
			as->arfcn_sync |= atoi(optarg);
			arfcn_sync = 1;
			break;
		case 'p':
			as->arfcn_sync |= ARFCN_PCS;
			break;
		case 'e':
			log_set_log_level(as->stderr_target, atoi(optarg));
			break;
		case '2':
			second_phone = 1;
			break;
		default:
			fprintf(stderr, "Unknow option %s\n", optarg);
			exit(0);
		}
	}
}


int main(int argc, char *argv[])
{
	struct app_state _as, *as = &_as;
	int rv;

	memset(as, 0x00, sizeof(struct app_state));

	/* Init talloc */
	l23_ctx = talloc_named_const(NULL, 1, "l23 app context");

	/* Init logging */
	log_init(&log_info, l23_ctx);

	as->stderr_target = log_target_create_stderr();

	log_add_target(as->stderr_target);
	log_set_all_filter(as->stderr_target, 1);

	/* Options */
	if (argc < 2) {
		print_help(argv);
		return -1;
	}
	handle_options(argc, argv, as);
	if (!arfcn_sync) {
		fprintf(stderr, "Use --arfcn-sync <ARFCN>\n");
		exit(0);
	}

	/* App state init */
	printf("%d\n", as->arfcn_sync);

	/* Init signal processing */
		/* Init GMSK tables */
	as->gs = osmo_gmsk_init(1);
	if (!as->gs)
		exit(-1);

		/* Init AB corr seq */
	as->train_ab = osmo_gmsk_trainseq_generate(as->gs, gsm_ab_train, GSM_AB_TRAIN_LEN);
	if (!as->train_ab)
		exit(-1);

	/* TRX interface with OpenBTS */
	as->trx[0] = trx_alloc("127.0.0.1", 5700, as, 1);
	if (!as->trx)
		exit(-1);

	/* Establish l1ctl link */
	rv = l1l_open(&as->l1l[0], "/tmp/osmocom_l2", l1ctl_recv, &as->l1l[0]);
	if (rv)
		exit(-1);
	as->l1l[0].trx = as->trx[0];
	as->l1l[0].as = as;
	as->l1l[0].tx_mask = 0xe3; /* TS 5,6,7,0,1 */
	as->l1l[0].rx_mask = 0x01; /* TS 0 */
	as->trx[0]->l1l[0] = &as->l1l[0];

	/* Reset phone */
	l1ctl_tx_reset_req(&as->l1l[0], L1CTL_RES_T_FULL);

	if (second_phone) {
		/* Establish l1ctl link */
		rv = l1l_open(&as->l1l[1], "/tmp/osmocom_l2.2", l1ctl_recv, &as->l1l[1]);
		if (rv)
			exit(-1);
		as->l1l[1].trx = as->trx[0];
		as->l1l[1].as = as;
		as->l1l[1].tx_mask = 0x02; /* TS 1 */
		as->l1l[1].rx_mask = 0x02; /* TS 1 */
		as->l1l[0].tx_mask = 0xe1; /* TS 5,6,7,0 */
		as->l1l[0].rx_mask = 0x01; /* TS 0 */
		as->trx[0]->l1l[1] = &as->l1l[1];

		/* Reset phone */
		l1ctl_tx_reset_req(&as->l1l[1], L1CTL_RES_T_FULL);
	}

	if (daemonize) {
		rv = osmo_daemonize();
		if (rv < 0) {
			perror("Error during daemonize");
			exit(1);
		}
	}


	/* Main loop */
	while (1) {
		osmo_select_main(0);
	}

	return 0;
}
