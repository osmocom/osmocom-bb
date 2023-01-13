/*
 * (C) 2023 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
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

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>

#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>

#include <osmocom/bb/common/vty.h>
#include <osmocom/bb/common/ms.h>


static int config_write(struct vty *vty)
{
	struct osmocom_ms *ms;
	llist_for_each_entry(ms, &ms_list, entity)
		l23_vty_config_write_ms_node(vty, ms, "");
	return CMD_SUCCESS;
}

int modem_vty_init(void)
{
	int rc;

	if ((rc = l23_vty_init(config_write, NULL)) < 0)
		return rc;
	install_element_ve(&l23_show_ms_cmd);
	install_element(CONFIG_NODE, &l23_cfg_ms_cmd);

	return 0;
}
