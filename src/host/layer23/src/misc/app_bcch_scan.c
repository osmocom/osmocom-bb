/* "Application" code of the layer2/3 stack */

/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/misc/layer3.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/signal.h>

#include <l1ctl_proto.h>
#include "bcch_scan.h"

static struct osmocom_ms *g_ms;

static int signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		ms = signal_data;
		return fps_start(ms);
	}
	return 0;
}

static int _bcch_scan_start(void)
{
	int rc;

	rc = layer2_open(g_ms, g_ms->settings.layer2_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during layer2_open()\n");
		return rc;
	}

	l1ctl_tx_reset_req(g_ms, L1CTL_RES_T_FULL);
	return 0;
}

int l23_app_init(void)
{
	g_ms = osmocom_ms_alloc(l23_ctx, "1");
	OSMO_ASSERT(g_ms);
	/* don't do layer3_init() as we don't want an actual L3 */
	fps_init();
	l23_app_start = _bcch_scan_start;
	return osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);
}

static struct l23_app_info info = {
	.copyright	= "Copyright (C) 2010 Harald Welte <laforge@gnumonks.org>\n",
	.contribution	= "Contributions by Holger Hans Peter Freyther\n",
};

struct l23_app_info *l23_app_info(void)
{
	return &info;
}
