/*
 * main.c
 *
 * MS-side Transceiver main program
 *
 * Copyright (C) 2014  Sylvain Munaut <tnt@246tNt.com>
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

#include <stdio.h>
#include <string.h>

#include <osmocom/core/talloc.h>

#include <osmocom/bb/common/logging.h>

#include "app.h"
#include "l1ctl.h"
#include "l1ctl_link.h"
#include "trx.h"


void *l23_ctx = NULL;


int main(int argc, char *argv[])
{
	struct app_state _as, *as = &_as;
	int rv;

	/* Options */

	/* App state init */
	memset(as, 0x00, sizeof(struct app_state));

	/* Init talloc */
	l23_ctx = talloc_named_const(NULL, 1, "l23 app context");

	/* Init logging */
	log_init(&log_info, l23_ctx);

	as->stderr_target = log_target_create_stderr();

	log_add_target(as->stderr_target);
	log_set_all_filter(as->stderr_target, 1);
	log_set_log_level(as->stderr_target, LOGL_DEBUG);

	/* Init TRX interface */
	as->trx = trx_alloc("127.0.0.1", 5700);
	if (!as->trx)
		exit(-1);

	/* Start L1CTL server */
	l1l_start_server(&as->l1s, "/tmp/osmocom_l2", l1ctl_new_cb, as);

	/* Main loop */
	while (1) {
		osmo_select_main(0);
	}

	return 0;
}
