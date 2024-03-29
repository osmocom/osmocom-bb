/* TEST code, regularly transmit ECHO REQ packet to L1 */

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
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/misc/layer3.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>

static struct {
	struct osmo_timer_list timer;
} test_data;

static void test_tmr_cb(void *data)
{
	struct osmocom_ms *ms = data;

	l1ctl_tx_echo_req(ms, 62);
	osmo_timer_schedule(&test_data.timer, 1, 0);
}

int l23_app_init(void)
{
	struct osmocom_ms *ms = osmocom_ms_alloc(l23_ctx, "1");
	OSMO_ASSERT(ms);

	test_data.timer.cb = &test_tmr_cb;
	test_data.timer.data = ms;

	osmo_timer_schedule(&test_data.timer, 1, 0);

	return 0;
}

const struct l23_app_info l23_app_info = {
	.copyright	= "Copyright (C) 2010 Harald Welte <laforge@gnumonks.org>\n",
	.contribution	= "Contributions by Holger Hans Peter Freyther\n",
};
