/* "Application" code of the layer2/3 stack */

/* (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <getopt.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/misc/cell_log.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>

#include <l1ctl_proto.h>

extern struct log_target *stderr_target;
extern void *l23_ctx;

extern uint16_t basic_band_range[][2];
extern uint16_t (*band_range)[][2];

char *logname = "/var/log/osmocom.log";
int RACH_MAX = 2;

int _scan_work(struct osmocom_ms *ms)
{
	return 0;
}

int _scan_exit(struct osmocom_ms *ms)
{
	/* in case there is a lockup during exit */
	signal(SIGINT, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGPIPE, SIG_DFL);

	scan_exit();

	return 0;
}

int l23_app_init(struct osmocom_ms *ms)
{
	int rc;

	srand(time(NULL));

//	log_parse_category_mask(stderr_target, "DL1C:DRSL:DRR:DGPS:DSUM");
	log_parse_category_mask(stderr_target, "DSUM");
	log_set_log_level(stderr_target, LOGL_INFO);

	l23_app_work = _scan_work;
	l23_app_exit = _scan_exit;

	rc = scan_init(ms);
	if (rc)
		return rc;

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	printf("Mobile initialized, please start phone now!\n");
	return 0;
}

static int l23_cfg_supported()
{
	return L23_OPT_TAP | L23_OPT_DBG;
}

static int l23_getopt_options(struct option **options)
{
	static struct option opts [] = {
		{"logfile", 1, 0, 'l'},
		{"rach", 1, 0, 'r'},
		{"no-rach", 1, 0, 'n'},
#ifdef _HAVE_GPSD
		{"gpsd-host", 1, 0, 'g'},
		{"gpsd-port", 1, 0, 'p'},
#endif
		{"gps", 1, 0, 'g'},
		{"baud", 1, 0, 'b'},
		{"arfcns", 1, 0, 'A'}
	};

	*options = opts;
	return ARRAY_SIZE(opts);
}

static char* print_band_range(uint16_t range[][2], char* buf, size_t buf_len)
{
	int i = 0;
	int idx = 0;
	while (idx < buf_len && (range[i][0] != 0 || range[i][1] != 0)) {
		idx += snprintf(&buf[idx], buf_len - idx, "%u-%u,", range[i][0], range[i][1]);
		i++;
	}
	buf[idx-1] = '\0';
	return buf;
}

static void parse_band_range(char* s)
{
	unsigned i = 0;
	char* idx = strtok(s, ",");
	unsigned single_range_size = sizeof(uint16_t) * 2;
	unsigned start;
	unsigned end;

	band_range = (uint16_t(*)[][2])calloc(single_range_size, i + 1);
	while (idx != NULL) {
		start = 0;
		end = 0;
		sscanf(idx, "%u-%u", &start, &end);
		if (end == 0)
			end = start;
		if (end < start) {
			fprintf(stderr, "Starting frequency must me lower than ending.\n\n");
			exit(1);
		}
		(*band_range)[i][0] = start;
		(*band_range)[i][1] = end;
		idx = strtok(NULL, ",");
		i++;
		band_range = realloc(band_range, (i+1) * single_range_size);
	}
	(*band_range)[i][0] = 0;
	(*band_range)[i][1] = 0;
}

static int l23_cfg_print_help()
{
	printf("\nApplication specific\n");
	printf("  -l --logfile LOGFILE	Logfile for the cell log.\n");
	printf("  -r --rach RACH	Nr. of RACH bursts to send.\n");
	printf("  -n --no-rach		Send no rach bursts.\n");
	printf("  -g --gpsd-host HOST	127.0.0.1. gpsd host.\n");
	printf("  -p --port PORT	2947. gpsd port\n");
	printf("  -f --gps DEVICE	/dev/ttyACM0. GPS serial device.\n");
	printf("  -b --baud BAUDRAT	The baud rate of the GPS device\n");
	printf("  -A --arfcns ARFCNS    The list of arfcns to be monitored\n");

	return 0;
}

static int l23_cfg_handle(int c, const char *optarg)
{
	char buf[1000];

	switch (c) {
	case 'l':
		logname = talloc_strdup(l23_ctx, optarg);
		break;
	case 'r':
		RACH_MAX = atoi(optarg);
		break;
	case 'n':
		RACH_MAX = 0;
		break;
	case 'g':
#ifdef _HAVE_GPSD
		snprintf(g.gpsd_host, ARRAY_SIZE(g.gpsd_host), "%s", optarg);
		/* force string terminator */
		g.gpsd_host[ARRAY_SIZE(g.gpsd_host) - 1] = '\0';
		if (g.gps_type != GPS_TYPE_UNDEF)
			goto cmd_line_error;
		g.gps_type = GPS_TYPE_GPSD;
		LOGP(DGPS, LOGL_INFO, "Using gpsd host %s\n", g.gpsd_host);
#else
		printf("Gpsd support not compiled.\n");
		exit(1);
#endif
		break;
	case 'p':
#ifdef _HAVE_GPSD
		snprintf(g.gpsd_port, ARRAY_SIZE(g.gpsd_port), "%s", optarg);
		/* force string terminator */
		g.gpsd_port[ARRAY_SIZE(g.gpsd_port) - 1] = '\0';
		g.gps_type = GPS_TYPE_GPSD;
		LOGP(DGPS, LOGL_INFO, "Using gpsd port %s\n", g.gpsd_port);
#else
		printf("Gpsd support not compiled.\n");
		exit(1);
#endif
		break;
	case 'f':
		snprintf(g.device, ARRAY_SIZE(g.device), "%s", optarg);
		/* force string terminator */
		g.device[ARRAY_SIZE(g.device) - 1] = '\0';
		if (g.gps_type != GPS_TYPE_UNDEF)
			goto cmd_line_error;
		g.gps_type = GPS_TYPE_SERIAL;
		LOGP(DGPS, LOGL_INFO, "Using GPS serial device %s\n", g.device);
		break;
	case 'b':
		g.baud = atoi(optarg);
		g.gps_type = GPS_TYPE_SERIAL;
		LOGP(DGPS, LOGL_INFO, "Setting GPS baudrate to %u\n", g.baud);
		break;
	case 'A':
		parse_band_range((char*)optarg);
		printf("New frequencies range: %s\n", print_band_range(*band_range, buf, sizeof(buf)));
		break;
	}
	return 0;

cmd_line_error:
	printf("\nYou can't specify both gpsd and serial gps!!\n\n");
	exit(1);
}

static struct l23_app_info info = {
	.copyright	= "Copyright (C) 2010 Andreas Eversberg\n",
	.getopt_string	= "g:p:l:r:nf:b:A:",
	.cfg_supported	= l23_cfg_supported,
	.cfg_getopt_opt = l23_getopt_options,
	.cfg_handle_opt	= l23_cfg_handle,
	.cfg_print_help	= l23_cfg_print_help,
};

struct l23_app_info *l23_app_info()
{
	return &info;
}
