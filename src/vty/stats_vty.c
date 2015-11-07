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
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/misc.h>

#include <osmocom/core/stats.h>

#define CFG_STATS_STR "Configure stats sub-system\n"
#define CFG_REPORTER_STR "Configure a stats reporter\n"

#define SHOW_STATS_STR "Show statistical values\n"

struct cmd_node cfg_stats_node = {
	CFG_STATS_NODE,
	"%s(config-stats)# ",
	1
};

static const struct value_string stats_class_strs[] = {
	{ OSMO_STATS_CLASS_GLOBAL,     "global" },
	{ OSMO_STATS_CLASS_PEER,       "peer" },
	{ OSMO_STATS_CLASS_SUBSCRIBER, "subscriber" },
	{ 0, NULL }
};

static struct osmo_stats_reporter *osmo_stats_vty2srep(struct vty *vty)
{
	if (vty->node == CFG_STATS_NODE)
		return vty->index;

	return NULL;
}

static int set_srep_parameter_str(struct vty *vty,
	int (*fun)(struct osmo_stats_reporter *, const char *),
	const char *val, const char *param_name)
{
	int rc;
	struct osmo_stats_reporter *srep = osmo_stats_vty2srep(vty);
	OSMO_ASSERT(srep);

	rc = fun(srep, val);
	if (rc < 0) {
		vty_out(vty, "%% Unable to set %s: %s%s",
			param_name, strerror(-rc), VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

static int set_srep_parameter_int(struct vty *vty,
	int (*fun)(struct osmo_stats_reporter *, int),
	const char *val, const char *param_name)
{
	int rc;
	int int_val;
	struct osmo_stats_reporter *srep = osmo_stats_vty2srep(vty);
	OSMO_ASSERT(srep);

	int_val = atoi(val);

	rc = fun(srep, int_val);
	if (rc < 0) {
		vty_out(vty, "%% Unable to set %s: %s%s",
			param_name, strerror(-rc), VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_stats_reporter_local_ip, cfg_stats_reporter_local_ip_cmd,
	"local-ip ADDR",
	"Set the IP address to which we bind locally\n"
	"IP Address\n")
{
	return set_srep_parameter_str(vty, osmo_stats_reporter_set_local_addr,
		argv[0], "local address");
}

DEFUN(cfg_no_stats_reporter_local_ip, cfg_no_stats_reporter_local_ip_cmd,
	"no local-ip",
	NO_STR
	"Set the IP address to which we bind locally\n")
{
	return set_srep_parameter_str(vty, osmo_stats_reporter_set_local_addr,
		NULL, "local address");
}

DEFUN(cfg_stats_reporter_remote_ip, cfg_stats_reporter_remote_ip_cmd,
	"remote-ip ADDR",
	"Set the remote IP address to which we connect\n"
	"IP Address\n")
{
	return set_srep_parameter_str(vty, osmo_stats_reporter_set_remote_addr,
		argv[0], "remote address");
}

DEFUN(cfg_stats_reporter_remote_port, cfg_stats_reporter_remote_port_cmd,
	"remote-port <1-65535>",
	"Set the remote port to which we connect\n"
	"Remote port number\n")
{
	return set_srep_parameter_int(vty, osmo_stats_reporter_set_remote_port,
		argv[0], "remote port");
}

DEFUN(cfg_stats_reporter_mtu, cfg_stats_reporter_mtu_cmd,
	"mtu <100-65535>",
	"Set the maximum packet size\n"
	"Size in byte\n")
{
	return set_srep_parameter_int(vty, osmo_stats_reporter_set_mtu,
		argv[0], "mtu");
}

DEFUN(cfg_no_stats_reporter_mtu, cfg_no_stats_reporter_mtu_cmd,
	"no mtu",
	NO_STR "Set the maximum packet size\n")
{
	return set_srep_parameter_int(vty, osmo_stats_reporter_set_mtu,
		"0", "mtu");
}

DEFUN(cfg_stats_reporter_prefix, cfg_stats_reporter_prefix_cmd,
	"prefix PREFIX",
	"Set the item name prefix\n"
	"The prefix string\n")
{
	return set_srep_parameter_str(vty, osmo_stats_reporter_set_name_prefix,
		argv[0], "prefix string");
}

DEFUN(cfg_no_stats_reporter_prefix, cfg_no_stats_reporter_prefix_cmd,
	"no prefix",
	NO_STR
	"Set the item name prefix\n")
{
	return set_srep_parameter_str(vty, osmo_stats_reporter_set_name_prefix,
		"", "prefix string");
}

DEFUN(cfg_stats_reporter_level, cfg_stats_reporter_level_cmd,
	"level (global|peer|subscriber)",
	"Set the maximum group level\n"
	"Report global groups only\n"
	"Report global and network peer related groups\n"
	"Report global, peer, and subscriber groups\n")
{
	int level = get_string_value(stats_class_strs, argv[0]);
	int rc;
	struct osmo_stats_reporter *srep = osmo_stats_vty2srep(vty);

	OSMO_ASSERT(srep);
	rc = osmo_stats_reporter_set_max_class(srep, level);
	if (rc < 0) {
		vty_out(vty, "%% Unable to set level: %s%s",
			strerror(-rc), VTY_NEWLINE);
		return CMD_WARNING;
	}

	return 0;
}

DEFUN(cfg_stats_reporter_enable, cfg_stats_reporter_enable_cmd,
	"enable",
	"Enable the reporter\n")
{
	int rc;
	struct osmo_stats_reporter *srep = osmo_stats_vty2srep(vty);
	OSMO_ASSERT(srep);

	rc = osmo_stats_reporter_enable(srep);
	if (rc < 0) {
		vty_out(vty, "%% Unable to enable the reporter: %s%s",
			strerror(-rc), VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_stats_reporter_disable, cfg_stats_reporter_disable_cmd,
	"disable",
	"Disable the reporter\n")
{
	int rc;
	struct osmo_stats_reporter *srep = osmo_stats_vty2srep(vty);
	OSMO_ASSERT(srep);

	rc = osmo_stats_reporter_disable(srep);
	if (rc < 0) {
		vty_out(vty, "%% Unable to disable the reporter: %s%s",
			strerror(-rc), VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_stats_reporter_statsd, cfg_stats_reporter_statsd_cmd,
	"stats reporter statsd",
	CFG_STATS_STR CFG_REPORTER_STR "Report to a STATSD server\n")
{
	struct osmo_stats_reporter *srep;

	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_STATSD, NULL);
	if (!srep) {
		srep = osmo_stats_reporter_create_statsd(NULL);
		if (!srep) {
			vty_out(vty, "%% Unable to create statsd reporter%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
		srep->max_class = OSMO_STATS_CLASS_GLOBAL;
		/* TODO: if needed, add osmo_stats_add_reporter(srep); */
	}

	vty->index = srep;
	vty->node = CFG_STATS_NODE;

	return CMD_SUCCESS;
}

DEFUN(cfg_stats_interval, cfg_stats_interval_cmd,
	"stats interval <1-65535>",
	CFG_STATS_STR "Set the reporting interval\n"
	"Interval in seconds\n")
{
	int rc;
	int interval = atoi(argv[0]);
	rc = osmo_stats_set_interval(interval);
	if (rc < 0) {
		vty_out(vty, "%% Unable to set interval: %s%s",
			strerror(-rc), VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}


DEFUN(cfg_no_stats_reporter_statsd, cfg_no_stats_reporter_statsd_cmd,
	"no stats reporter statsd",
	NO_STR CFG_STATS_STR CFG_REPORTER_STR "Report to a STATSD server\n")
{
	struct osmo_stats_reporter *srep;

	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_STATSD, NULL);
	if (!srep) {
		vty_out(vty, "%% No statsd logging active%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	osmo_stats_reporter_free(srep);

	return CMD_SUCCESS;
}

DEFUN(cfg_stats_reporter_log, cfg_stats_reporter_log_cmd,
	"stats reporter log",
	CFG_STATS_STR CFG_REPORTER_STR "Report to the logger\n")
{
	struct osmo_stats_reporter *srep;

	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_LOG, NULL);
	if (!srep) {
		srep = osmo_stats_reporter_create_log(NULL);
		if (!srep) {
			vty_out(vty, "%% Unable to create log reporter%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
		srep->max_class = OSMO_STATS_CLASS_GLOBAL;
		/* TODO: if needed, add osmo_stats_add_reporter(srep); */
	}

	vty->index = srep;
	vty->node = CFG_STATS_NODE;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_stats_reporter_log, cfg_no_stats_reporter_log_cmd,
	"no stats reporter log",
	NO_STR CFG_STATS_STR CFG_REPORTER_STR "Report to the logger\n")
{
	struct osmo_stats_reporter *srep;

	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_LOG, NULL);
	if (!srep) {
		vty_out(vty, "%% No log reporting active%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	osmo_stats_reporter_free(srep);

	return CMD_SUCCESS;
}

DEFUN(show_stats,
      show_stats_cmd,
      "show stats",
      SHOW_STR SHOW_STATS_STR)
{
	vty_out_statistics_full(vty, "");

	return CMD_SUCCESS;
}

DEFUN(show_stats_level,
      show_stats_level_cmd,
      "show stats level (global|peer|subscriber)",
      SHOW_STR SHOW_STATS_STR
      "Set the maximum group level\n"
      "Show global groups only\n"
      "Show global and network peer related groups\n"
      "Show global, peer, and subscriber groups\n")
{
	int level = get_string_value(stats_class_strs, argv[0]);
	vty_out_statistics_partial(vty, "", level);

	return CMD_SUCCESS;
}

static int config_write_stats_reporter(struct vty *vty, struct osmo_stats_reporter *srep)
{
	if (srep == NULL)
		return 0;

	switch (srep->type) {
	case OSMO_STATS_REPORTER_STATSD:
		vty_out(vty, "stats reporter statsd%s", VTY_NEWLINE);
		break;
	case OSMO_STATS_REPORTER_LOG:
		vty_out(vty, "stats reporter log%s", VTY_NEWLINE);
		break;
	}

	vty_out(vty, "  disable%s", VTY_NEWLINE);

	if (srep->have_net_config) {
		if (srep->dest_addr_str)
			vty_out(vty, "  remote-ip %s%s",
				srep->dest_addr_str, VTY_NEWLINE);
		if (srep->dest_port)
			vty_out(vty, "  remote-port %d%s",
				srep->dest_port, VTY_NEWLINE);
		if (srep->bind_addr_str)
			vty_out(vty, "  local-ip %s%s",
				srep->bind_addr_str, VTY_NEWLINE);
		if (srep->mtu)
			vty_out(vty, "  mtu %d%s",
				srep->mtu, VTY_NEWLINE);
	}

	if (srep->max_class)
		vty_out(vty, "  level %s%s",
			get_value_string(stats_class_strs, srep->max_class),
			VTY_NEWLINE);

	if (srep->name_prefix && *srep->name_prefix)
		vty_out(vty, "  prefix %s%s",
			srep->name_prefix, VTY_NEWLINE);
	else
		vty_out(vty, "  no prefix%s", VTY_NEWLINE);

	if (srep->enabled)
		vty_out(vty, "  enable%s", VTY_NEWLINE);

	return 1;
}

static int config_write_stats(struct vty *vty)
{
	struct osmo_stats_reporter *srep;

	/* TODO: loop through all reporters */
	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_STATSD, NULL);
	config_write_stats_reporter(vty, srep);
	srep = osmo_stats_reporter_find(OSMO_STATS_REPORTER_LOG, NULL);
	config_write_stats_reporter(vty, srep);

	vty_out(vty, "stats interval %d%s", osmo_stats_config->interval, VTY_NEWLINE);

	return 1;
}

void osmo_stats_vty_add_cmds()
{
	install_element_ve(&show_stats_cmd);
	install_element_ve(&show_stats_level_cmd);

	install_element(CONFIG_NODE, &cfg_stats_reporter_statsd_cmd);
	install_element(CONFIG_NODE, &cfg_no_stats_reporter_statsd_cmd);
	install_element(CONFIG_NODE, &cfg_stats_reporter_log_cmd);
	install_element(CONFIG_NODE, &cfg_no_stats_reporter_log_cmd);
	install_element(CONFIG_NODE, &cfg_stats_interval_cmd);

	install_node(&cfg_stats_node, config_write_stats);
	vty_install_default(CFG_STATS_NODE);

	install_element(CFG_STATS_NODE, &cfg_stats_reporter_local_ip_cmd);
	install_element(CFG_STATS_NODE, &cfg_no_stats_reporter_local_ip_cmd);
	install_element(CFG_STATS_NODE, &cfg_stats_reporter_remote_ip_cmd);
	install_element(CFG_STATS_NODE, &cfg_stats_reporter_remote_port_cmd);
	install_element(CFG_STATS_NODE, &cfg_stats_reporter_mtu_cmd);
	install_element(CFG_STATS_NODE, &cfg_no_stats_reporter_mtu_cmd);
	install_element(CFG_STATS_NODE, &cfg_stats_reporter_prefix_cmd);
	install_element(CFG_STATS_NODE, &cfg_no_stats_reporter_prefix_cmd);
	install_element(CFG_STATS_NODE, &cfg_stats_reporter_level_cmd);
	install_element(CFG_STATS_NODE, &cfg_stats_reporter_enable_cmd);
	install_element(CFG_STATS_NODE, &cfg_stats_reporter_disable_cmd);
}
