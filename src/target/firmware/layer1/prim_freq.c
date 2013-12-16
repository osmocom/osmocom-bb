/* Layer 1 Frequency redefinition at "starting time" */

/* (C) 2010 by Andreas Eversverg <jolly@eversberg.eu>
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <defines.h>
#include <debug.h>
#include <memory.h>
#include <byteorder.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/core/msgb.h>
#include <calypso/dsp_api.h>
#include <calypso/irq.h>
#include <calypso/tpu.h>
#include <calypso/tsp.h>
#include <calypso/dsp.h>
#include <calypso/timer.h>
#include <comm/sercomm.h>
#include <asm/system.h>

#include <layer1/sync.h>
#include <layer1/async.h>
#include <layer1/tdma_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/l23_api.h>
#include <layer1/sched_gsmtime.h>

#include <l1ctl_proto.h>

struct {
	uint32_t fn;
	uint16_t band_arfcn;
} last_rach;

/* if the "starting time" is reached, use frequencies "after time" */
static int l1s_freq_cmd(__unused uint8_t p1, __unused uint8_t p2, __unused uint16_t p3)
{
	putchart('F');

	printf("Reached starting time, altering frequency set\n");

	l1s.dedicated.tsc = l1s.dedicated.st_tsc;
	l1s.dedicated.h = l1s.dedicated.st_h;
	if (l1s.dedicated.h)
		memcpy(&l1s.dedicated.h1, &l1s.dedicated.st_h1,
			sizeof(l1s.dedicated.h1));
	else
		memcpy(&l1s.dedicated.h0, &l1s.dedicated.st_h0,
			sizeof(l1s.dedicated.h0));

	return 0;
}

/* sched set for frequency change */
const struct tdma_sched_item freq_sched_set[] = {
	SCHED_ITEM(l1s_freq_cmd, -3, 1, 0),
	SCHED_END_SET()
};

/* request a frequency change at the given frame number
 * Note: The fn_sched parameter must be in range 0..42431. */
void l1a_freq_req(uint32_t fn_sched)
{
	int32_t diff;
	unsigned long flags;

	/* We must check here, if the time already elapsed.
	 * This is required, because we may have an undefined delay between
	 * layer 1 and layer 3.
	 */
	diff = fn_sched - (l1s.current_time.fn % 42432);
	if (diff < 0)
		diff += 42432;
	/* note: 5 is used to give scheduler some time */
	if (diff == 5 || diff >= 32024) {
		l1s_freq_cmd(0, 0, 0);
		return;
	}

	/* calculate (full range) frame number */
	fn_sched = l1s.current_time.fn + diff;
	if (fn_sched >= GSM_MAX_FN)
		fn_sched -= GSM_MAX_FN;
	printf("Scheduling frequency change at fn=%"PRIu32", currently fn=%"PRIu32"\n",
		fn_sched, l1s.current_time.fn);

	local_firq_save(flags);
	sched_gsmtime(freq_sched_set, fn_sched, 0);
	local_irq_restore(flags);
}

