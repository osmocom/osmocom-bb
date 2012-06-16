/* OpenBSC VTY common helpers */
/* (C) 2009-2012 by Harald Welte <laforge@gnumonks.org>
 * (C) 2009-2010 by Holger Hans Peter Freyther
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
 *
 */

#include <stdlib.h>
#include <string.h>

#include <osmocom/core/talloc.h>

#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/command.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/vty.h>

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

int DNS, DBSSGP;
