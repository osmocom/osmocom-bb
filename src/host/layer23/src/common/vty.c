/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/crypt/auth.h>

#include <osmocom/bb/common/vty.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_ms.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/app_mobile.h>
#include <osmocom/bb/mobile/gsm480_ss.h>
#include <osmocom/bb/mobile/gsm411_sms.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/misc.h>

static struct cmd_node ms_node = {
	MS_NODE,
	"%s(ms)# ",
	1
};

struct osmocom_ms *l23_vty_get_ms(const char *name, struct vty *vty)
{
	struct osmocom_ms *ms;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, name)) {
			if (ms->shutdown != MS_SHUTDOWN_NONE) {
				vty_out(vty, "MS '%s' is admin down.%s", name,
					VTY_NEWLINE);
				return NULL;
			}
			return ms;
		}
	}
	vty_out(vty, "MS name '%s' does not exist.%s", name, VTY_NEWLINE);

	return NULL;
}

/* placeholder for layer23 shared MS info to be dumped */
void l23_ms_dump(struct osmocom_ms *ms, struct vty *vty)
{
	char *service = "";

	if (!ms->started)
		service = ", radio is not started";
	else if (ms->mmlayer.state == GSM48_MM_ST_MM_IDLE) {
		/* current MM idle state */
		switch (ms->mmlayer.substate) {
		case GSM48_MM_SST_NORMAL_SERVICE:
		case GSM48_MM_SST_PLMN_SEARCH_NORMAL:
			service = ", service is normal";
			break;
		case GSM48_MM_SST_LOC_UPD_NEEDED:
		case GSM48_MM_SST_ATTEMPT_UPDATE:
			service = ", service is limited (pending)";
			break;
		case GSM48_MM_SST_NO_CELL_AVAIL:
			service = ", service is unavailable";
			break;
		default:
			if (ms->subscr.sim_valid)
				service = ", service is limited";
			else
				service = ", service is limited "
					"(IMSI detached)";
			break;
		}
	} else
		service = ", MM connection active";

	vty_out(vty, "MS '%s' is %s%s%s%s", ms->name,
		(ms->shutdown != MS_SHUTDOWN_NONE) ? "administratively " : "",
		(ms->shutdown != MS_SHUTDOWN_NONE || !ms->started) ? "down" : "up",
		(ms->shutdown == MS_SHUTDOWN_NONE) ? service : "",
		VTY_NEWLINE);
}


gDEFUN(l23_show_ms, l23_show_ms_cmd, "show ms [MS_NAME]",
	SHOW_STR "Display available MS entities\n")
{
	struct osmocom_ms *ms;

	if (argc) {
		llist_for_each_entry(ms, &ms_list, entity) {
			if (!strcmp(ms->name, argv[0])) {
				l23_ms_dump(ms, vty);
				return CMD_SUCCESS;
			}
		}
		vty_out(vty, "MS name '%s' does not exist.%s", argv[0],
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	llist_for_each_entry(ms, &ms_list, entity) {
		l23_ms_dump(ms, vty);
		vty_out(vty, "%s", VTY_NEWLINE);
	}

	return CMD_SUCCESS;
}

/* per MS config */
gDEFUN(l23_cfg_ms, l23_cfg_ms_cmd, "ms MS_NAME",
	"Select a mobile station to configure\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, argv[0])) {
			vty->index = ms;
			vty->node = MS_NODE;
			return CMD_SUCCESS;
		}
	}

	vty_out(vty, "MS name '%s' does not exits%s", argv[0],
		VTY_NEWLINE);
	return CMD_WARNING;
}

void l23_vty_config_write_ms_node(struct vty *vty, const struct osmocom_ms *ms, const char *prefix)
{
	size_t prefix_len = strlen(prefix);
	char *prefix_content = alloca(prefix_len + 1 + 1);

	memcpy(prefix_content, prefix, prefix_len);
	prefix_content[prefix_len] = ' ';
	prefix_content[prefix_len + 1] = '\0';

	vty_out(vty, "%sms %s%s", prefix, ms->name, VTY_NEWLINE);
	l23_vty_config_write_ms_node_contents(vty, ms, prefix_content);
}

void l23_vty_config_write_ms_node_contents(struct vty *vty, const struct osmocom_ms *ms, const char *prefix)
{
	/* placeholder for shared VTY commands */
}

int l23_vty_init(int (*config_write_ms_node_cb)(struct vty *))
{
	install_node(&ms_node, config_write_ms_node_cb);

	/* Register the talloc context introspection command */
	osmo_talloc_vty_add_cmds();

	return 0;
}

