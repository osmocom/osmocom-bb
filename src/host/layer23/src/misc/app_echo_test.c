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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/lapdm.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/misc/layer3.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>


static struct {
	struct timer_list timer;
} test_data;

static void test_tmr_cb(void *data)
{
	struct osmocom_ms *ms = data;

	l1ctl_tx_echo_req(ms, 62);
	bsc_schedule_timer(&test_data.timer, 1, 0);
}

int l23_app_init(struct osmocom_ms *ms)
{
	test_data.timer.cb = &test_tmr_cb;
	test_data.timer.data = ms;

	bsc_schedule_timer(&test_data.timer, 1, 0);

	return 0;
}
