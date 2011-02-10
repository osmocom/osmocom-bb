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

#include <osmocore/talloc.h>
#include <osmocore/utils.h>

extern struct log_target *stderr_target;
extern void *l23_ctx;

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
#ifdef _USE_GPSD
		{"gpsd-host", 1, 0, 'g'},
		{"gpsd-port", 1, 0, 'p'}
#else
		{"gps", 1, 0, 'g'},
		{"baud", 1, 0, 'b'}
#endif
	};

	*options = opts;
	return ARRAY_SIZE(opts);
}

static int l23_cfg_print_help()
{
	printf("\nApplication specific\n");
	printf("  -l --logfile LOGFILE	Logfile for the cell log.\n");
	printf("  -r --rach    RACH		Nr. of RACH bursts to send.\n");
	printf("  -n --no-rach			Send no rach bursts.\n");
#ifdef _USE_GPSD
	printf("  -g --gpsd-host HOST	127.0.0.1. gpsd host.\n");
	printf("  -p --port PORT		2947. gpsd port\n");
#else
	printf("  -g --gps DEVICE		/dev/ttyACM0. GPS device.\n");
	printf("  -b --baud BAUDRAT		The baud rate of the GPS device\n");
#endif
	return 0;
}

static int l23_cfg_handle(int c, const char *optarg)
{
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
#ifdef _USE_GPSD
	case 'g':
		snprintf(gps.gpsd_host, ARRAY_SIZE(gps.gpsd_host), "%s", optarg);
		/* force string terminator */
		gps.gpsd_host[ARRAY_SIZE(gps.gpsd_host) - 1] = '\0';
		LOGP(DGPS, LOGL_INFO, "Using gpsd host %s\n", gps.gpsd_host);
		break;
	case 'p':
		snprintf(gps.gpsd_port, ARRAY_SIZE(gps.gpsd_port), "%s", optarg);
		/* force string terminator */
		gps.gpsd_port[ARRAY_SIZE(gps.gpsd_port) - 1] = '\0';
		LOGP(DGPS, LOGL_INFO, "Using gpsd port %s\n", gps.gpsd_port);
		break;
#else
	case 'g':
		snprintf(gps.device, ARRAY_SIZE(gps.device), "%s", optarg);
		/* force string terminator */
		gps.device[ARRAY_SIZE(gps.device) - 1] = '\0';
		LOGP(DGPS, LOGL_INFO, "Using GPS device %s\n", gps.device);
		break;
	case 'b':
		gps.baud = atoi(optarg);
		LOGP(DGPS, LOGL_INFO, "Setting GPS baudrate to %u\n", gps.baud);
		break;
#endif
	}
	return 0;
}

static struct l23_app_info info = {
	.copyright	= "Copyright (C) 2010 Andreas Eversberg\n",
#ifdef _USE_GPSD
	.getopt_string	= "l:r:ng:p:",
#else
	.getopt_string	= "l:r:ng:b:",
#endif
	.cfg_supported	= l23_cfg_supported,
	.cfg_getopt_opt = l23_getopt_options,
	.cfg_handle_opt	= l23_cfg_handle,
	.cfg_print_help	= l23_cfg_print_help,
};

struct l23_app_info *l23_app_info()
{
	return &info;
}
