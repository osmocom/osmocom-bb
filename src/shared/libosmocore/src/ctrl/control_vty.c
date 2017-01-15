/* VTY configuration for Control interface
 *
 * (C) 2016 by sysmocom s.m.f.c. GmbH <info@sysmocom.de>
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

#include <stdlib.h>
#include <talloc.h>
#include <osmocom/ctrl/control_vty.h>
#include <osmocom/vty/command.h>

static void *ctrl_vty_ctx = NULL;
static const char *ctrl_vty_bind_addr = NULL;

DEFUN(cfg_ctrl_bind_addr,
      cfg_ctrl_bind_addr_cmd,
      "bind A.B.C.D",
      "Set bind address to listen for Control connections\n"
      "Local IP address (default 127.0.0.1)\n")
{
	talloc_free((char*)ctrl_vty_bind_addr);
	ctrl_vty_bind_addr = NULL;
	ctrl_vty_bind_addr = talloc_strdup(ctrl_vty_ctx, argv[0]);
	return CMD_SUCCESS;
}

const char *ctrl_vty_get_bind_addr(void)
{
	if (!ctrl_vty_bind_addr)
		return "127.0.0.1";
	return ctrl_vty_bind_addr;
}

static struct cmd_node ctrl_node = {
	L_CTRL_NODE,
	"%s(config-ctrl)# ",
	1,
};

DEFUN(cfg_ctrl,
      cfg_ctrl_cmd,
      "ctrl", "Configure the Control Interface")
{
	vty->index = NULL;
	vty->node = L_CTRL_NODE;

	return CMD_SUCCESS;
}

static int config_write_ctrl(struct vty *vty)
{
	/* So far there's only one element. Omit the entire section if the bind
	 * element is omitted. */
	if (!ctrl_vty_bind_addr)
		return CMD_SUCCESS;

	vty_out(vty, "ctrl%s", VTY_NEWLINE);
	vty_out(vty, " bind %s%s", ctrl_vty_bind_addr, VTY_NEWLINE);

	return CMD_SUCCESS;
}

int ctrl_vty_init(void *ctx)
{
	ctrl_vty_ctx = ctx;
	install_element(CONFIG_NODE, &cfg_ctrl_cmd);
	install_node(&ctrl_node, config_write_ctrl);

	install_element(L_CTRL_NODE, &cfg_ctrl_bind_addr_cmd);
	return 0;
}

