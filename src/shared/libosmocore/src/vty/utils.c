/* utility routines for printing common objects in the Osmocom world */

/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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
#include <inttypes.h>

#include <osmocore/linuxlist.h>
#include <osmocore/talloc.h>
#include <osmocore/timer.h>
#include <osmocore/rate_ctr.h>

#include <osmocom/vty/vty.h>

void vty_out_rate_ctr_group(struct vty *vty, const char *prefix,
			    struct rate_ctr_group *ctrg)
{
	unsigned int i;

	vty_out(vty, "%s%s:%s", prefix, ctrg->desc->group_description, VTY_NEWLINE);
	for (i = 0; i < ctrg->desc->num_ctr; i++) {
		struct rate_ctr *ctr = &ctrg->ctr[i];
		vty_out(vty, " %s%s: %8" PRIu64 " "
			"(%" PRIu64 "/s %" PRIu64 "/m %" PRIu64 "/h %" PRIu64 "/d)%s",
			prefix, ctrg->desc->ctr_desc[i].description, ctr->current,
			ctr->intv[RATE_CTR_INTV_SEC].rate,
			ctr->intv[RATE_CTR_INTV_MIN].rate,
			ctr->intv[RATE_CTR_INTV_HOUR].rate,
			ctr->intv[RATE_CTR_INTV_DAY].rate,
			VTY_NEWLINE);
	};
}
