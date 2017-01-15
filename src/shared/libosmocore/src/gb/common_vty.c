/* OpenBSC VTY common helpers */
/* (C) 2009-2012 by Harald Welte <laforge@gnumonks.org>
 * (C) 2009-2010 by Holger Hans Peter Freyther
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>

#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/command.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/vty.h>

#include <osmocom/gprs/gprs_msgb.h>

#include "common_vty.h"

/* Down vty node level. */
gDEFUN(libgb_exit,
       libgb_exit_cmd, "exit", "Exit current mode and down to previous mode\n")
{
	switch (vty->node) {
	case L_NS_NODE:
	case L_BSSGP_NODE:
		vty->node = CONFIG_NODE;
		vty->index = NULL;
		break;
	default:
		break;
	}
	return CMD_SUCCESS;
}

/* End of configuration. */
gDEFUN(libgb_end,
       libgb_end_cmd, "end", "End current mode and change to enable mode.")
{
	switch (vty->node) {
	case L_NS_NODE:
	case L_BSSGP_NODE:
		vty_config_unlock(vty);
		vty->node = ENABLE_NODE;
		vty->index = NULL;
		vty->index_sub = NULL;
		break;
	default:
		break;
	}
	return CMD_SUCCESS;
}

int gprs_log_filter_fn(const struct log_context *ctx,
			struct log_target *tar)
{
	const struct gprs_nsvc *nsvc = ctx->ctx[GPRS_CTX_NSVC];
	const struct gprs_bvc *bvc = ctx->ctx[GPRS_CTX_BVC];

	/* Filter on the NS Virtual Connection */
	if ((tar->filter_map & (1 << FLT_NSVC)) != 0
	    && nsvc && (nsvc == tar->filter_data[FLT_NSVC]))
		return 1;

	/* Filter on the NS Virtual Connection */
	if ((tar->filter_map & (1 << FLT_BVC)) != 0
	    && bvc && (bvc == tar->filter_data[FLT_BVC]))
		return 1;

	return 0;
}


int DNS, DBSSGP;
