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

#include <osmocom/core/select.h>
#include <osmocom/core/talloc.h>

#include <osmocom/bb/common/logging.h>

#include "app.h"
#include "l1ctl.h"
#include "l1ctl_link.h"
#include "trx.h"
#include "gmsk.h"
#include "gsm_ab.h"


void *l23_ctx = NULL;


int main(int argc, char *argv[])
{
	struct app_state _as, *as = &_as;
	int rv;

	/* Options */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s arfcn_sync\n", argv[0]);
		return -1;
	}

	/* App state init */
	memset(as, 0x00, sizeof(struct app_state));

	as->arfcn_sync = atoi(argv[1]);
	printf("%d\n", as->arfcn_sync);

	/* Init talloc */
	l23_ctx = talloc_named_const(NULL, 1, "l23 app context");

	/* Init logging */
	log_init(&log_info, l23_ctx);

	as->stderr_target = log_target_create_stderr();

	log_add_target(as->stderr_target);
	log_set_all_filter(as->stderr_target, 1);

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
	as->trx = trx_alloc("127.0.0.1", 5700, &as->l1l);
	if (!as->trx)
		exit(-1);

	/* Establish l1ctl link */
	rv = l1l_open(&as->l1l, "/tmp/osmocom_l2", l1ctl_recv, as);
	if (rv)
		exit(-1);

	/* Reset phone */
	l1ctl_tx_reset_req(&as->l1l, L1CTL_RES_T_FULL);

	/* Main loop */
	while (1) {
		osmo_select_main(0);
	}

	return 0;
}
