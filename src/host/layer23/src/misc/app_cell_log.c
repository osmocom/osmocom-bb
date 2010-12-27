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

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/misc/cell_log.h>

#include <osmocore/talloc.h>

extern struct log_target *stderr_target;
extern void *l23_ctx;

char *logname = "/var/log/osmocom.log";

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

static int l23_cfg_print_help()
{
	printf("\nApplication specific\n");
	printf("  -l --logfile LOGFILE		Logfile for the cell log.\n");
	return 0;
}

static int l23_cfg_handle(int c, const char *optarg)
{
	switch (c) {
	case 'l':
		logname = talloc_strdup(l23_ctx, optarg);
		break;
	}

	return 0;
}

static struct l23_app_info info = {
	.copyright	= "Copyright (C) 2010 Andreas Eversberg\n",
	.getopt_string	= "l:",
	.cfg_supported	= l23_cfg_supported,
	.cfg_handle_opt	= l23_cfg_handle,
	.cfg_print_help	= l23_cfg_print_help,
};

struct l23_app_info *l23_app_info()
{
	return &info;
}
