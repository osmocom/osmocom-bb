/* OpenBSC stats helper for the VTY */
/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2009-2014 by Holger Hans Peter Freyther
 * (C) 2015      by Sysmocom s.f.m.c. GmbH
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

#include <stdlib.h>
#include <string.h>

#include "../../config.h"

#include <osmocom/vty/command.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/misc.h>

#define CFG_STATS_STR "Configure stats sub-system\n"
#define CFG_REPORTER_STR "Configure a stats reporter\n"

#define SHOW_STATS_STR "Show statistical values\n"

DEFUN(show_stats,
      show_stats_cmd,
      "show stats",
      SHOW_STR SHOW_STATS_STR)
{
	vty_out_statistics_full(vty, "");

	return CMD_SUCCESS;
}

void stats_vty_add_cmds(const struct log_info *cat)
{
	install_element_ve(&show_stats_cmd);
}
