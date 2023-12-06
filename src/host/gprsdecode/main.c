/*
 * (C) 2017-2018 by sysmocom - s.f.m.c. GmbH, Author: Max <msuraev@sysmocom.de>
 * (C) 2018 by Vadim Yanitskiy <axilirator@gmail.com>
 * (C) 2011-2012 by Luca Melette <luca@srlabs.de>
 *
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-2.0+
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
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/select.h>
#include <osmocom/core/application.h>

#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

#include "l1ctl_proto.h"
#include "gsmtap.h"
#include "gprs.h"

static struct {
	char *capture_file;
	char *gsmtap_ip;
	bool verbose;
	bool quit;
} app_data;

static void burst_handle(struct l1ctl_burst_ind *bi)
{
	struct gsm_time rx_time;
	uint32_t fn;

	fn = ntohl(bi->frame_nr);
	gsm_fn2gsmtime(&rx_time, fn);

	printf("BURST.ind @(%6d = %.4u/%.2u/%.2u): %4d dBm, SNR %3d, %s\n",
	       rx_time.fn, rx_time.t1, rx_time.t2, rx_time.t3,
	       rxlev2dbm(bi->rx_level), bi->snr,
	       rsl_chan_nr_str(bi->chan_nr));

	if ((bi->chan_nr & RSL_CHAN_NR_MASK) != RSL_CHAN_OSMO_PDCH)
		return; /* ignore anything else than PDCH */
	if ((fn % 13) == 12)
		return; /* skip IDLE and PTCCH slots */

	process_pdch(bi, app_data.verbose);
}

static void print_help(const char *app)
{
	printf(" Some help...\n\n");

	printf(" Usage: %s [OPTIONS]\n\n", app);

	printf("  -h --help          this text\n");
	printf("  -c --capture       The capture file to decode\n");
	printf("  -i --gsmtap-ip     The destination IP used for GSMTAP\n");
	printf("  -v --verbose       Increase the verbosity level\n");
}

static int handle_options(int argc, char **argv)
{
	/* Init defaults */
	app_data.capture_file = NULL;
	app_data.gsmtap_ip = NULL;
	app_data.verbose = false;
	app_data.quit = false;

	/* Parse options */
	while (1) {
		int option_index = 0, c;
		static struct option long_options[] = {
			{"help", 0, 0, 'h'},
			{"verbose", 0, 0, 'v'},
			{"capture", 1, 0, 'c'},
			{"gsmtap-ip", 1, 0, 'i'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "c:i:vh",
			long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			print_help(argv[0]);
			return 1;
		case 'c':
			app_data.capture_file = optarg;
			break;
		case 'i':
			app_data.gsmtap_ip = optarg;
			break;
		case 'v':
			app_data.verbose = true;
			break;
		default:
			break;
		}
	}

	/* Make sure we have the capture file path */
	if (!app_data.capture_file) {
		print_help(argv[0]);
		printf("\nPlease specify the capture file\n");
		return -1;
	}

	return 0;
}

static void signal_handler(int signal)
{
	fprintf(stderr, "signal %d received\n", signal);

	switch (signal) {
	case SIGINT:
		app_data.quit = true;
		break;
	default:
		break;
	}
}

int main(int argc, char **argv)
{
	struct l1ctl_burst_ind bi;
	FILE *burst_fd;
	int rc;

	/* Setup signal handlers */
	signal(SIGINT, &signal_handler);
	osmo_init_ignore_signals();

	/* Parse options */
	rc = handle_options(argc, argv);
	if (rc)
		return EXIT_FAILURE;

	/* Attempt to open the capture for reading */
	burst_fd = fopen(app_data.capture_file, "rb");
	if (!burst_fd) {
		printf("Cannot open capture file '%s': %s\n",
			app_data.capture_file, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Init GSMTAP sink if required */
	if (app_data.gsmtap_ip != NULL)
		gsmtap_init(app_data.gsmtap_ip);

	/* Application main loop */
	while (!app_data.quit) {
		/* The end of capture file */
		if (feof(burst_fd))
			break;

		/* Read a single burst */
		rc = fread(&bi, sizeof(bi), 1, burst_fd);
		if (!rc)
			break;

		/* Filter or handle burst */
		burst_handle(&bi);

		osmo_select_main(1);
	}

	/* Close the capture file */
	fclose(burst_fd);

	return 0;
}
