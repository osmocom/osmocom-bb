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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <osmocom/osmocom_data.h>
#include <osmocom/l1ctl.h>
#include <osmocom/layer3.h>
#include <osmocom/lapdm.h>
#include <osmocom/gsmtap_util.h>
#include <osmocom/logging.h>

#include <osmocore/msgb.h>
#include <osmocore/talloc.h>
#include <osmocore/select.h>

int l23_app_init(struct osmocom_ms *ms)
{
	return layer3_init(ms);
}
