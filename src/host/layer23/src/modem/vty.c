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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <osmocom/core/utils.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/linuxlist.h>

#include <osmocom/vty/vty.h>
#include <osmocom/vty/command.h>

#include <osmocom/bb/common/settings.h>
#include <osmocom/bb/common/vty.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/vty.h>

static struct cmd_node apn_node = {
	APN_NODE,
	"%s(apn)# ",
	1
};

int modem_vty_go_parent(struct vty *vty)
{
	struct osmobb_apn *apn;

	switch (vty->node) {
	case APN_NODE:
		apn = vty->index;
		vty->index = apn->ms;
		vty->node = MS_NODE;
		break;
	}
	return vty->node;
}

/* per APN config */
DEFUN(cfg_ms_apn, cfg_ms_apn_cmd, "apn APN_NAME",
	"Configure an APN\n"
	"Name of APN\n")
{
	struct osmocom_ms *ms = vty->index;
	struct osmobb_apn *apn;

	apn = ms_find_apn_by_name(ms, argv[0]);
	if (!apn)
		apn = apn_alloc(ms, argv[0]);
	if (!apn) {
		vty_out(vty, "Unable to create APN '%s'%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	vty->index = apn;
	vty->node = APN_NODE;
	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_apn, cfg_ms_no_apn_cmd, "no apn APN_NAME",
	NO_STR "Configure an APN\n"
	"Name of APN\n")
{
	struct osmocom_ms *ms = vty->index;
	struct osmobb_apn *apn;

	apn = ms_find_apn_by_name(ms, argv[0]);
	if (!apn) {
		vty_out(vty, "Unable to find APN '%s'%s", argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	apn_free(apn);

	return CMD_SUCCESS;
}

DEFUN(cfg_apn_tun_dev_name, cfg_apn_tun_dev_name_cmd,
	"tun-device NAME",
	"Configure tun device name\n"
	"TUN device name")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	osmo_talloc_replace_string(apn, &apn->cfg.dev_name, argv[0]);
	return CMD_SUCCESS;
}

DEFUN(cfg_apn_tun_netns_name, cfg_apn_tun_netns_name_cmd,
	"tun-netns NAME",
	"Configure tun device network namespace name\n"
	"TUN device network namespace name")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	osmo_talloc_replace_string(apn, &apn->cfg.dev_netns_name, argv[0]);
	return CMD_SUCCESS;
}

DEFUN(cfg_apn_no_tun_netns_name, cfg_apn_no_tun_netns_name_cmd,
	"no tun-netns",
	"Configure tun device to use default network namespace name\n")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	TALLOC_FREE(apn->cfg.dev_netns_name);
	return CMD_SUCCESS;
}

static const struct value_string pdp_type_names[] = {
	{ APN_TYPE_IPv4, "v4" },
	{ APN_TYPE_IPv6, "v6" },
	{ APN_TYPE_IPv4v6, "v4v6" },
	{ 0, NULL }
};

#define V4V6V46_STRING	"IPv4(-only) PDP Type\n"	\
			"IPv6(-only) PDP Type\n"	\
			"IPv4v6 (dual-stack) PDP Type\n"

DEFUN(cfg_apn_type_support, cfg_apn_type_support_cmd,
	"type-support (v4|v6|v4v6)",
	"Enable support for PDP Type\n"
	V4V6V46_STRING)
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;
	uint32_t type = get_string_value(pdp_type_names, argv[0]);

	apn->cfg.apn_type_mask |= type;
	return CMD_SUCCESS;
}

DEFUN(cfg_apn_shutdown, cfg_apn_shutdown_cmd,
	"shutdown",
	"Put the APN in administrative shut-down\n")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;

	if (!apn->cfg.shutdown) {
		if (apn_stop(apn)) {
			vty_out(vty, "%% Failed to Stop APN%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		apn->cfg.shutdown = true;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_apn_no_shutdown, cfg_apn_no_shutdown_cmd,
	"no shutdown",
	NO_STR "Remove the APN from administrative shut-down\n")
{
	struct osmobb_apn *apn = (struct osmobb_apn *) vty->index;

	if (apn->cfg.shutdown) {
		if (!apn->cfg.dev_name) {
			vty_out(vty, "%% Failed to start APN, tun-device is not configured%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (apn_start(apn) < 0) {
			vty_out(vty, "%% Failed to start APN, check log for details%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		apn->cfg.shutdown = false;
	}

	return CMD_SUCCESS;
}

static void config_write_apn(struct vty *vty, const struct osmobb_apn *apn)
{
	unsigned int i;

	vty_out(vty, " apn %s%s", apn->cfg.name, VTY_NEWLINE);

	if (apn->cfg.dev_name)
		vty_out(vty, "  tun-device %s%s", apn->cfg.dev_name, VTY_NEWLINE);
	if (apn->cfg.dev_netns_name)
		vty_out(vty, "  tun-netns %s%s", apn->cfg.dev_netns_name, VTY_NEWLINE);

	for (i = 0; i < 32; i++) {
		if (!(apn->cfg.apn_type_mask & (UINT32_C(1) << i)))
			continue;
		vty_out(vty, "  type-support %s%s", get_value_string(pdp_type_names, (UINT32_C(1) << i)),
			VTY_NEWLINE);
	}

	/* must be last */
	vty_out(vty, "  %sshutdown%s", apn->cfg.shutdown ? "" : "no ", VTY_NEWLINE);
}

static void config_write_ms(struct vty *vty, const struct osmocom_ms *ms)
{
	struct osmobb_apn *apn;

	vty_out(vty, "ms %s%s", ms->name, VTY_NEWLINE);

	l23_vty_config_write_ms_node_contents(vty, ms, " ");

	llist_for_each_entry(apn, &ms->gprs.apn_list, list)
		config_write_apn(vty, apn);

	l23_vty_config_write_ms_node_contents_final(vty, ms, " ");
}

static int config_write(struct vty *vty)
{
	struct osmocom_ms *ms;
	llist_for_each_entry(ms, &ms_list, entity)
		config_write_ms(vty, ms);
	return CMD_SUCCESS;
}

int modem_vty_init(void)
{
	int rc;

	if ((rc = l23_vty_init(config_write, NULL)) < 0)
		return rc;
	install_element_ve(&l23_show_ms_cmd);
	install_element(CONFIG_NODE, &l23_cfg_ms_cmd);

	install_element(MS_NODE, &cfg_ms_apn_cmd);
	install_element(MS_NODE, &cfg_ms_no_apn_cmd);
	install_node(&apn_node, NULL);
	install_element(APN_NODE, &cfg_apn_tun_dev_name_cmd);
	install_element(APN_NODE, &cfg_apn_tun_netns_name_cmd);
	install_element(APN_NODE, &cfg_apn_no_tun_netns_name_cmd);
	install_element(APN_NODE, &cfg_apn_type_support_cmd);
	install_element(APN_NODE, &cfg_apn_shutdown_cmd);
	install_element(APN_NODE, &cfg_apn_no_shutdown_cmd);

	return 0;
}
