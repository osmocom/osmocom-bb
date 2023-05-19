/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
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
#include <osmocom/core/gsmtap.h>
#include <osmocom/core/gsmtap_util.h>
#include <osmocom/crypt/auth.h>
#include <osmocom/vty/cpu_sched_vty.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/stats.h>
#include <osmocom/vty/misc.h>

#include <osmocom/bb/common/vty.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/common/l1l2_interface.h>

extern struct llist_head active_connections; /* libosmocore */

bool l23_vty_reading = false;

bool l23_vty_hide_default = false;

static struct cmd_node ms_node = {
	MS_NODE,
	"%s(ms)# ",
	1
};

static struct cmd_node gsmtap_node = {
	GSMTAP_NODE,
	"%s(gsmtap)# ",
	1
};

struct cmd_node testsim_node = {
	TESTSIM_NODE,
	"%s(test-sim)# ",
	1
};

static void l23_vty_restart_required_warn(struct vty *vty, struct osmocom_ms *ms)
{
	if (l23_vty_reading)
		return;
	if (ms->shutdown != MS_SHUTDOWN_NONE)
		return;
	vty_out(vty, "You must restart MS '%s' ('shutdown / no shutdown') for "
		"change to take effect!%s", ms->name, VTY_NEWLINE);
}

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

void l23_vty_ms_notify(struct osmocom_ms *ms, const char *fmt, ...)
{
	struct telnet_connection *connection;
	char buffer[1000];
	va_list args;
	struct vty *vty;

	if (fmt) {
		va_start(args, fmt);
		vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
		buffer[sizeof(buffer) - 1] = '\0';
		va_end(args);

		if (!buffer[0])
			return;
	}

	llist_for_each_entry(connection, &active_connections, entry) {
		vty = connection->vty;
		if (!vty)
			continue;
		if (!fmt) {
			vty_out(vty, "%s%% (MS %s)%s", VTY_NEWLINE, ms->name,
				VTY_NEWLINE);
			continue;
		}
		if (buffer[strlen(buffer) - 1] == '\n') {
			buffer[strlen(buffer) - 1] = '\0';
			vty_out(vty, "%% %s%s", buffer, VTY_NEWLINE);
			buffer[strlen(buffer)] = '\n';
		} else
			vty_out(vty, "%% %s", buffer);
	}
}

void l23_vty_printf(void *priv, const char *fmt, ...)
{
	char buffer[1000];
	struct vty *vty = priv;
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
	buffer[sizeof(buffer) - 1] = '\0';
	va_end(args);

	if (buffer[0]) {
		if (buffer[strlen(buffer) - 1] == '\n') {
			buffer[strlen(buffer) - 1] = '\0';
			vty_out(vty, "%s%s", buffer, VTY_NEWLINE);
		} else
			vty_out(vty, "%s", buffer);
	}
}

/* placeholder for layer23 shared MS info to be dumped */
void l23_ms_dump(struct osmocom_ms *ms, struct vty *vty)
{
	struct gsm_settings *set = &ms->settings;
	char *service = "";

	if (!ms->started)
		service = ", radio is not started";
	else if (ms->mmlayer.state == GSM48_MM_ST_NULL) {
		service = ", MM connection not yet set up";
	} else if (ms->mmlayer.state == GSM48_MM_ST_MM_IDLE) {
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

	vty_out(vty, " IMEI: %s%s", set->imei, VTY_NEWLINE);
	vty_out(vty, " IMEISV: %s%s", set->imeisv, VTY_NEWLINE);
	if (set->imei_random)
		vty_out(vty, " IMEI generation: random (%d trailing "
			"digits)%s", set->imei_random, VTY_NEWLINE);
	else
		vty_out(vty, " IMEI generation: fixed%s", VTY_NEWLINE);
}

/* CONFIG NODE: */
DEFUN(cfg_hide_default, cfg_hide_default_cmd, "hide-default",
	"Hide most default values in config to make it more compact")
{
	l23_vty_hide_default = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_hide_default, cfg_no_hide_default_cmd, "no hide-default",
	NO_STR "Show default values in config")
{
	l23_vty_hide_default = 0;

	return CMD_SUCCESS;
}

DEFUN(show_support, show_support_cmd, "show support [MS_NAME]",
	SHOW_STR "Display information about MS support\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	if (argc) {
		ms = l23_vty_get_ms(argv[0], vty);
		if (!ms)
			return CMD_WARNING;
		gsm_support_dump(ms, l23_vty_printf, vty);
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			gsm_support_dump(ms, l23_vty_printf, vty);
			vty_out(vty, "%s", VTY_NEWLINE);
		}
	}

	return CMD_SUCCESS;
}

DEFUN(show_subscr, show_subscr_cmd, "show subscriber [MS_NAME]",
	SHOW_STR "Display information about subscriber\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	if (argc) {
		ms = l23_vty_get_ms(argv[0], vty);
		if (!ms)
			return CMD_WARNING;
		gsm_subscr_dump(&ms->subscr, l23_vty_printf, vty);
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			if (ms->shutdown == MS_SHUTDOWN_NONE) {
				gsm_subscr_dump(&ms->subscr, l23_vty_printf, vty);
				vty_out(vty, "%s", VTY_NEWLINE);
			}
		}
	}

	return CMD_SUCCESS;
}

/* "gsmtap" config */
gDEFUN(l23_cfg_gsmtap, l23_cfg_gsmtap_cmd, "gsmtap",
	"Configure GSMTAP\n")
{
	vty->node = GSMTAP_NODE;
	return CMD_SUCCESS;
}

static const struct value_string gsmtap_categ_gprs_names[] = {
	{ L23_GSMTAP_GPRS_C_DL_UNKNOWN,		"dl-unknown" },
	{ L23_GSMTAP_GPRS_C_DL_DUMMY,		"dl-dummy" },
	{ L23_GSMTAP_GPRS_C_DL_CTRL,		"dl-ctrl" },
	{ L23_GSMTAP_GPRS_C_DL_DATA_GPRS,	"dl-data-gprs" },
	{ L23_GSMTAP_GPRS_C_DL_DATA_EGPRS,	"dl-data-egprs" },
	{ L23_GSMTAP_GPRS_C_UL_UNKNOWN,		"ul-unknown" },
	{ L23_GSMTAP_GPRS_C_UL_DUMMY,		"ul-dummy" },
	{ L23_GSMTAP_GPRS_C_UL_CTRL,		"ul-ctrl" },
	{ L23_GSMTAP_GPRS_C_UL_DATA_GPRS,	"ul-data-gprs" },
	{ L23_GSMTAP_GPRS_C_UL_DATA_EGPRS,	"ul-data-egprs" },
	{ 0, NULL }
};

static const struct value_string gsmtap_categ_gprs_help[] = {
	{ L23_GSMTAP_GPRS_C_DL_UNKNOWN,		"Unknown / Unparseable / Erroneous Downlink Blocks" },
	{ L23_GSMTAP_GPRS_C_DL_DUMMY,		"Downlink Dummy Blocks" },
	{ L23_GSMTAP_GPRS_C_DL_CTRL,		"Downlink Control Blocks" },
	{ L23_GSMTAP_GPRS_C_DL_DATA_GPRS,	"Downlink Data Blocks (GPRS)" },
	{ L23_GSMTAP_GPRS_C_DL_DATA_EGPRS,	"Downlink Data Blocks (EGPRS)" },
	{ L23_GSMTAP_GPRS_C_UL_UNKNOWN,		"Unknown / Unparseable / Erroneous Downlink Blocks" },
	{ L23_GSMTAP_GPRS_C_UL_DUMMY,		"Uplink Dummy Blocks" },
	{ L23_GSMTAP_GPRS_C_UL_CTRL,		"Uplink Control Blocks" },
	{ L23_GSMTAP_GPRS_C_UL_DATA_GPRS,	"Uplink Data Blocks (GPRS)" },
	{ L23_GSMTAP_GPRS_C_UL_DATA_EGPRS,	"Uplink Data Blocks (EGPRS)" },
	{ 0, NULL }
};

DEFUN(cfg_gsmtap_gsmtap_remote_host,
      cfg_gsmtap_gsmtap_remote_host_cmd,
      "remote-host [HOSTNAME]",
      "Enable GSMTAP Um logging\n"
      "Remote IP address or hostname ('localhost' if omitted)\n")
{
	osmo_talloc_replace_string(l23_ctx, &l23_cfg.gsmtap.remote_host,
				   argc > 0 ? argv[0] : "localhost");

	if (vty->type != VTY_FILE)
		vty_out(vty, "%% This command requires restart%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_no_gsmtap_remote_host,
      cfg_gsmtap_no_gsmtap_remote_host_cmd,
      "no remote-host",
      NO_STR "Disable GSMTAP Um logging\n")
{
	TALLOC_FREE(l23_cfg.gsmtap.remote_host);
	if (vty->type != VTY_FILE)
		vty_out(vty, "%% This command requires restart%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_gsmtap_local_host,
      cfg_gsmtap_gsmtap_local_host_cmd,
      "local-host " VTY_IPV46_CMD,
      "Set source for GSMTAP Um logging\n"
      "Local IPv4 address\n" "Local IPv6 address\n")
{
	osmo_talloc_replace_string(l23_ctx, &l23_cfg.gsmtap.local_host, argv[0]);

	if (vty->type != VTY_FILE)
		vty_out(vty, "%% This command requires restart%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_no_gsmtap_local_host,
      cfg_gsmtap_no_gsmtap_local_host_cmd,
      "no local-host",
      NO_STR "Disable explicit source for GSMTAP Um logging\n")
{
	TALLOC_FREE(l23_cfg.gsmtap.local_host);
	if (vty->type != VTY_FILE)
		vty_out(vty, "%% This command requires restart%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_gsmtap_lchan_all, cfg_gsmtap_gsmtap_lchan_all_cmd,
	"lchan (enable-all|disable-all)",
	"Enable/disable sending of UL/DL messages over GSMTAP\n"
	"Enable all kinds of messages (all LCHAN)\n"
	"Disable all kinds of messages (all LCHAN)\n")
{
	if (argv[0][0] == 'e') {
		l23_cfg.gsmtap.lchan_mask = UINT32_MAX;
		l23_cfg.gsmtap.lchan_acch_mask = UINT32_MAX;
		l23_cfg.gsmtap.lchan_acch = true;
	} else {
		l23_cfg.gsmtap.lchan_mask = 0x00;
		l23_cfg.gsmtap.lchan_acch_mask = 0x00;
		l23_cfg.gsmtap.lchan_acch = false;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_gsmtap_lchan, cfg_gsmtap_gsmtap_lchan_cmd,
	"HIDDEN", "HIDDEN")
{
	unsigned int channel;

	if (osmo_str_startswith(argv[0], "sacch")) {
		if (strcmp(argv[0], "sacch") == 0) {
			l23_cfg.gsmtap.lchan_acch = true;
		} else {
			channel = get_string_value(gsmtap_gsm_channel_names, argv[0]);
			channel &= ~GSMTAP_CHANNEL_ACCH;
			l23_cfg.gsmtap.lchan_acch_mask |= (1 << channel);
		}
	} else {
		channel = get_string_value(gsmtap_gsm_channel_names, argv[0]);
		l23_cfg.gsmtap.lchan_mask |= (1 << channel);
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_no_gsmtap_lchan, cfg_gsmtap_no_gsmtap_lchan_cmd,
	"HIDDEN", "HIDDEN")
{
	unsigned int channel;

	if (osmo_str_startswith(argv[0], "sacch")) {
		if (strcmp(argv[0], "sacch") == 0) {
			l23_cfg.gsmtap.lchan_acch = false;
		} else {
			channel = get_string_value(gsmtap_gsm_channel_names, argv[0]);
			channel &= ~GSMTAP_CHANNEL_ACCH;
			l23_cfg.gsmtap.lchan_acch_mask &= ~(1 << channel);
		}
	} else {
		channel = get_string_value(gsmtap_gsm_channel_names, argv[0]);
		l23_cfg.gsmtap.lchan_mask &= ~(1 << channel);
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_gsmtap_categ_gprs_all, cfg_gsmtap_gsmtap_categ_gprs_all_cmd,
	"category gprs (enable-all|disable-all)",
	"Enable/disable sending of UL/DL messages over GSMTAP\n"
	"Enable all kinds of messages (all categories)\n"
	"Disable all kinds of messages (all categories)\n")
{

	if (strcmp(argv[0], "enable-all") == 0)
		l23_cfg.gsmtap.categ_gprs_mask = UINT32_MAX;
	else
		l23_cfg.gsmtap.categ_gprs_mask = 0x00;

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_gsmtap_categ_gprs, cfg_gsmtap_gsmtap_categ_gprs_cmd, "HIDDEN", "HIDDEN")
{
	int categ;

	categ = get_string_value(gsmtap_categ_gprs_names, argv[0]);
	if (categ < 0)
		return CMD_WARNING;

	l23_cfg.gsmtap.categ_gprs_mask |= (1 << categ);

	return CMD_SUCCESS;
}

DEFUN(cfg_gsmtap_no_gsmtap_categ_gprs, cfg_gsmtap_no_gsmtap_categ_gprs_cmd, "HIDDEN", "HIDDEN")
{
	int categ;

	categ = get_string_value(gsmtap_categ_gprs_names, argv[0]);
	if (categ < 0)
		return CMD_WARNING;

	l23_cfg.gsmtap.categ_gprs_mask &= ~(1 << categ);

	return CMD_SUCCESS;
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

static int _sim_testcard_cmd(struct vty *vty, int argc, const char *argv[],
	int attached)
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;
	int rc;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already attached, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	set = &ms->settings;
	set->sim_type = GSM_SIM_TYPE_TEST;

	if (argc == 2) {
		vty_out(vty, "Give MNC together with MCC%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (argc >= 3) {
		struct osmo_plmn_id plmn;
		if (osmo_mcc_from_str(argv[1], &plmn.mcc) < 0) {
			vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (osmo_mnc_from_str(argv[2], &plmn.mnc, &plmn.mnc_3_digits) < 0) {
			vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		memcpy(&set->test_sim.rplmn, &plmn, sizeof(plmn));
		set->test_sim.rplmn_valid = 1;
	} else {
		set->test_sim.rplmn_valid = 0;
	}

	if (argc >= 4)
		set->test_sim.lac = strtoul(argv[3], NULL, 16);

	if (argc >= 5)
		set->test_sim.tmsi = strtoul(argv[4], NULL, 16);

	set->test_sim.imsi_attached = attached;

	rc = gsm_subscr_insert(ms);
	if (rc < 0) {
		vty_out(vty, "Attach test SIM card failed: %d%s", rc, VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(sim_testcard, sim_testcard_cmd,
	"sim testcard MS_NAME [MCC] [MNC] [LAC] [TMSI]",
	"SIM actions\nAttach built in test SIM\nName of MS (see \"show ms\")\n"
	"Optionally set mobile Country Code of RPLMN\n"
	"Optionally set mobile Network Code of RPLMN\n"
	"Optionally set location area code of RPLMN\n"
	"Optionally set current assigned TMSI")
{
	return _sim_testcard_cmd(vty, argc, argv, 0);
}

DEFUN(sim_testcard_att, sim_testcard_att_cmd,
	"sim testcard MS_NAME MCC MNC LAC TMSI attached",
	"SIM actions\nAttach built in test SIM\nName of MS (see \"show ms\")\n"
	"Set mobile Country Code of RPLMN\nSet mobile Network Code of RPLMN\n"
	"Set location area code\nSet current assigned TMSI\n"
	"Indicate to MM that card is already attached")
{
	return _sim_testcard_cmd(vty, argc, argv, 1);
}

DEFUN(sim_sap, sim_sap_cmd, "sim sap MS_NAME",
	"SIM actions\nAttach SIM over SAP interface\n"
	"Name of MS (see \"show ms\")\n")
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already attached, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	set = &ms->settings;
	set->sim_type = GSM_SIM_TYPE_SAP;
	if (gsm_subscr_insert(ms) != 0)
		return CMD_WARNING;

	return CMD_SUCCESS;
}

DEFUN(sim_reader, sim_reader_cmd, "sim reader MS_NAME",
	"SIM actions\nAttach SIM from reader\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already attached, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	set = &ms->settings;
	set->sim_type = GSM_SIM_TYPE_L1PHY;
	gsm_subscr_insert(ms);

	return CMD_SUCCESS;
}

DEFUN(sim_remove, sim_remove_cmd, "sim remove MS_NAME",
	"SIM actions\nDetach SIM card\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (!ms->subscr.sim_valid) {
		vty_out(vty, "No SIM attached!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_remove(ms);
	return CMD_SUCCESS;
}

DEFUN(sim_pin, sim_pin_cmd, "sim pin MS_NAME PIN",
	"SIM actions\nEnter PIN for SIM card\nName of MS (see \"show ms\")\n"
	"PIN number")
{
	struct osmocom_ms *ms;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (strlen(argv[1]) < 4 || strlen(argv[1]) > 8) {
		vty_out(vty, "PIN must be in range 4..8!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (!ms->subscr.sim_pin_required) {
		vty_out(vty, "No PIN is required at this time!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_sim_pin(ms, (char *)argv[1], "", 0);

	return CMD_SUCCESS;
}

DEFUN(sim_disable_pin, sim_disable_pin_cmd, "sim disable-pin MS_NAME PIN",
	"SIM actions\nDisable PIN of SIM card\nName of MS (see \"show ms\")\n"
	"PIN number")
{
	struct osmocom_ms *ms;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (strlen(argv[1]) < 4 || strlen(argv[1]) > 8) {
		vty_out(vty, "PIN must be in range 4..8!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_sim_pin(ms, (char *)argv[1], "", -1);

	return CMD_SUCCESS;
}

DEFUN(sim_enable_pin, sim_enable_pin_cmd, "sim enable-pin MS_NAME PIN",
	"SIM actions\nEnable PIN of SIM card\nName of MS (see \"show ms\")\n"
	"PIN number")
{
	struct osmocom_ms *ms;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (strlen(argv[1]) < 4 || strlen(argv[1]) > 8) {
		vty_out(vty, "PIN must be in range 4..8!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_sim_pin(ms, (char *)argv[1], "", 1);

	return CMD_SUCCESS;
}

DEFUN(sim_change_pin, sim_change_pin_cmd, "sim change-pin MS_NAME OLD NEW",
	"SIM actions\nChange PIN of SIM card\nName of MS (see \"show ms\")\n"
	"Old PIN number\nNew PIN number")
{
	struct osmocom_ms *ms;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (strlen(argv[1]) < 4 || strlen(argv[1]) > 8) {
		vty_out(vty, "Old PIN must be in range 4..8!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (strlen(argv[2]) < 4 || strlen(argv[2]) > 8) {
		vty_out(vty, "New PIN must be in range 4..8!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_sim_pin(ms, (char *)argv[1], (char *)argv[2], 2);

	return CMD_SUCCESS;
}

DEFUN(sim_unblock_pin, sim_unblock_pin_cmd, "sim unblock-pin MS_NAME PUC NEW",
	"SIM actions\nChange PIN of SIM card\nName of MS (see \"show ms\")\n"
	"Personal Unblock Key\nNew PIN number")
{
	struct osmocom_ms *ms;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (strlen(argv[1]) != 8) {
		vty_out(vty, "PUC must be 8 digits!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (strlen(argv[2]) < 4 || strlen(argv[2]) > 8) {
		vty_out(vty, "PIN must be in range 4..8!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_sim_pin(ms, (char *)argv[1], (char *)argv[2], 99);

	return CMD_SUCCESS;
}

DEFUN(sim_lai, sim_lai_cmd, "sim lai MS_NAME MCC MNC LAC",
	"SIM actions\nChange LAI of SIM card\nName of MS (see \"show ms\")\n"
	"Mobile Country Code\nMobile Network Code\nLocation Area Code "
	" (use 0000 to remove LAI)")
{
	struct osmocom_ms *ms;
	struct osmo_plmn_id plmn;
	uint16_t lac;

	ms = l23_vty_get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (osmo_mcc_from_str(argv[1], &plmn.mcc) < 0) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (osmo_mnc_from_str(argv[2], &plmn.mnc, &plmn.mnc_3_digits) < 0) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	lac = strtoul(argv[3], NULL, 0);

	memcpy(&ms->subscr.lai.plmn, &plmn, sizeof(plmn));
	ms->subscr.lai.lac = lac;
	ms->subscr.tmsi = GSM_RESERVED_TMSI;

	gsm_subscr_write_loci(ms);

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

DEFUN(cfg_ms_layer2, cfg_ms_layer2_cmd, "layer2-socket PATH",
	"Define socket path to connect between layer 2 and layer 1\n"
	"Unix socket, default '" L2_DEFAULT_SOCKET_PATH "'")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	OSMO_STRLCPY_ARRAY(set->layer2_socket_path, argv[0]);

	l23_vty_restart_required_warn(vty, ms);
	return CMD_SUCCESS;
}

DEFUN(cfg_ms_imei, cfg_ms_imei_cmd, "imei IMEI [SV]",
	"Set IMEI (enter without control digit)\n15 Digits IMEI\n"
	"Software version digit")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	char *error, *sv = "0";

	if (argc >= 2)
		sv = (char *)argv[1];

	error = gsm_check_imei(argv[0], sv);
	if (error) {
		vty_out(vty, "%s%s", error, VTY_NEWLINE);
		return CMD_WARNING;
	}

	OSMO_STRLCPY_ARRAY(set->imei, argv[0]);
	OSMO_STRLCPY_ARRAY(set->imeisv, argv[0]);
	OSMO_STRLCPY_ARRAY(set->imeisv + 15, sv);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_imei_fixed, cfg_ms_imei_fixed_cmd, "imei-fixed",
	"Use fixed IMEI on every power on")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->imei_random = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_imei_random, cfg_ms_imei_random_cmd, "imei-random <0-15>",
	"Use random IMEI on every power on\n"
	"Number of trailing digits to randomize")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->imei_random = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sim, cfg_ms_sim_cmd, "sim (none|reader|test|sap)",
	"Set SIM card to attach when powering on\nAttach no SIM\n"
	"Attach SIM from reader\nAttach build in test SIM\n"
	"Attach SIM over SAP interface")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	switch (argv[0][0]) {
	case 'n':
		set->sim_type = GSM_SIM_TYPE_NONE;
		break;
	case 'r':
		set->sim_type = GSM_SIM_TYPE_L1PHY;
		break;
	case 't':
		set->sim_type = GSM_SIM_TYPE_TEST;
		break;
	case 's':
		set->sim_type = GSM_SIM_TYPE_SAP;
		break;
	default:
		vty_out(vty, "unknown SIM type%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	l23_vty_restart_required_warn(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_shutdown, cfg_ms_no_shutdown_cmd, "no shutdown",
	NO_STR "Activate and run MS")
{
	struct osmocom_ms *ms = vty->index;

	struct osmobb_l23_vty_sig_data data;
	memset(&data, 0, sizeof(data));

	data.vty = vty;
	data.ms_start.ms = ms;
	data.ms_start.rc = CMD_SUCCESS;
	osmo_signal_dispatch(SS_L23_VTY, S_L23_VTY_MS_START, &data);

	return data.ms_start.rc;
}

DEFUN(cfg_ms_shutdown, cfg_ms_shutdown_cmd, "shutdown",
	"Shut down and deactivate MS")
{
	struct osmocom_ms *ms = vty->index;

	struct osmobb_l23_vty_sig_data data;
	memset(&data, 0, sizeof(data));

	data.vty = vty;
	data.ms_stop.ms = ms;
	data.ms_stop.force = false;
	data.ms_stop.rc = CMD_SUCCESS;
	osmo_signal_dispatch(SS_L23_VTY, S_L23_VTY_MS_STOP, &data);

	return data.ms_stop.rc;
}

DEFUN(cfg_ms_shutdown_force, cfg_ms_shutdown_force_cmd, "shutdown force",
	"Shut down and deactivate MS\nDo not perform IMSI detach")
{
	struct osmocom_ms *ms = vty->index;

	struct osmobb_l23_vty_sig_data data;
	memset(&data, 0, sizeof(data));

	data.vty = vty;
	data.ms_stop.ms = ms;
	data.ms_stop.force = true;
	data.ms_stop.rc = CMD_SUCCESS;
	osmo_signal_dispatch(SS_L23_VTY, S_L23_VTY_MS_STOP, &data);

	return data.ms_stop.rc;
}

/* per testsim config */
DEFUN(cfg_ms_testsim, cfg_ms_testsim_cmd, "test-sim",
	"Configure test SIM emulation")
{
	vty->node = TESTSIM_NODE;

	return CMD_SUCCESS;
}

DEFUN(cfg_testsim_imsi, cfg_testsim_imsi_cmd, "imsi IMSI",
	"Set IMSI on test card\n15 digits IMSI")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	if (!osmo_imsi_str_valid(argv[0])) {
		vty_out(vty, "Wrong IMSI format%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	OSMO_STRLCPY_ARRAY(set->test_sim.imsi, argv[0]);

	l23_vty_restart_required_warn(vty, ms);

	return CMD_SUCCESS;
}

#define HEX_STR "\nByte as two digits hexadecimal"
DEFUN(cfg_testsim_ki_xor, cfg_testsim_ki_xor_cmd, "ki xor HEX HEX HEX HEX HEX HEX "
	"HEX HEX HEX HEX HEX HEX",
	"Set Key (Ki) on test card\nUse XOR algorithm" HEX_STR HEX_STR HEX_STR
	HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR)
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	uint8_t ki[12];
	const char *p;
	int i;

	for (i = 0; i < 12; i++) {
		p = argv[i];
		if (!strncmp(p, "0x", 2))
			p += 2;
		if (strlen(p) != 2) {
			vty_out(vty, "Expecting two digits hex value (with or "
				"without 0x in front)%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		ki[i] = strtoul(p, NULL, 16);
	}

	set->test_sim.ki_type = OSMO_AUTH_ALG_XOR;
	memcpy(set->test_sim.ki, ki, 12);
	return CMD_SUCCESS;
}

DEFUN(cfg_testsim_ki_comp128, cfg_testsim_ki_comp128_cmd, "ki comp128 HEX HEX HEX "
	"HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX",
	"Set Key (Ki) on test card\nUse XOR algorithm" HEX_STR HEX_STR HEX_STR
	HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR HEX_STR
	HEX_STR HEX_STR HEX_STR HEX_STR)
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	uint8_t ki[16];
	const char *p;
	int i;

	for (i = 0; i < 16; i++) {
		p = argv[i];
		if (!strncmp(p, "0x", 2))
			p += 2;
		if (strlen(p) != 2) {
			vty_out(vty, "Expecting two digits hex value (with or "
				"without 0x in front)%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		ki[i] = strtoul(p, NULL, 16);
	}

	set->test_sim.ki_type = OSMO_AUTH_ALG_COMP128v1;
	memcpy(set->test_sim.ki, ki, 16);
	return CMD_SUCCESS;
}

DEFUN(cfg_testsim_barr, cfg_testsim_barr_cmd, "barred-access",
	"Allow access to barred cells")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->test_sim.barr = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_testsim_no_barr, cfg_testsim_no_barr_cmd, "no barred-access",
	NO_STR "Deny access to barred cells")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->test_sim.barr = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_testsim_no_rplmn, cfg_testsim_no_rplmn_cmd, "no rplmn",
	NO_STR "Unset Registered PLMN")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->test_sim.rplmn_valid = 0;
	set->test_sim.rplmn.mcc = 1;
	set->test_sim.rplmn.mnc = 1;
	set->test_sim.rplmn.mnc_3_digits = false;
	set->test_sim.lac = 0x0000;
	set->test_sim.tmsi = GSM_RESERVED_TMSI;

	l23_vty_restart_required_warn(vty, ms);

	return CMD_SUCCESS;
}

static int _testsim_rplmn_cmd(struct vty *vty, int argc, const char *argv[], bool attached)
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct osmo_plmn_id plmn;

	if (osmo_mcc_from_str(argv[0], &plmn.mcc) < 0) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (osmo_mnc_from_str(argv[1], &plmn.mnc, &plmn.mnc_3_digits) < 0) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	set->test_sim.rplmn_valid = 1;
	memcpy(&set->test_sim.rplmn, &plmn, sizeof(plmn));

	if (argc >= 3)
		set->test_sim.lac = strtoul(argv[2], NULL, 16);
	else
		set->test_sim.lac = 0xfffe;

	if (argc >= 4)
		set->test_sim.tmsi = strtoul(argv[3], NULL, 16);
	else
		set->test_sim.tmsi = GSM_RESERVED_TMSI;

	set->test_sim.imsi_attached = attached;

	l23_vty_restart_required_warn(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_testsim_rplmn, cfg_testsim_rplmn_cmd,
	"rplmn MCC MNC [LAC] [TMSI]",
	"Set Registered PLMN\nMobile Country Code\nMobile Network Code\n"
	"Optionally set location area code\n"
	"Optionally set current assigned TMSI")
{
	return _testsim_rplmn_cmd(vty, argc, argv, false);
}

DEFUN(cfg_testsim_rplmn_att, cfg_testsim_rplmn_att_cmd,
	"rplmn MCC MNC LAC TMSI attached",
	"Set Registered PLMN\nMobile Country Code\nMobile Network Code\n"
	"Set location area code\nSet current assigned TMSI\n"
	"Indicate to MM that card is already attached")
{
	return _testsim_rplmn_cmd(vty, argc, argv, true);
}

DEFUN(cfg_testsim_hplmn, cfg_testsim_hplmn_cmd, "hplmn-search (everywhere|foreign-country)",
	"Set Home PLMN search mode\n"
	"Search for HPLMN when on any other network\n"
	"Search for HPLMN when in a different country")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	switch (argv[0][0]) {
	case 'e':
		set->test_sim.always_search_hplmn = true;
		break;
	case 'f':
		set->test_sim.always_search_hplmn = false;
		break;
	}

	l23_vty_restart_required_warn(vty, ms);

	return CMD_SUCCESS;
}

static int l23_vty_config_write_gsmtap_node(struct vty *vty)
{
	const char *chan_buf;
	unsigned int i;

	vty_out(vty, "gsmtap%s", VTY_NEWLINE);

	if (l23_cfg.gsmtap.remote_host)
		vty_out(vty, " remote-host %s%s", l23_cfg.gsmtap.remote_host, VTY_NEWLINE);
	else
		vty_out(vty, " no remote-host%s", VTY_NEWLINE);

	if (l23_cfg.gsmtap.local_host)
		vty_out(vty, " local-host %s%s", l23_cfg.gsmtap.local_host, VTY_NEWLINE);
	else
		vty_out(vty, " no local-host%s", VTY_NEWLINE);

	if (l23_cfg.gsmtap.lchan_acch)
		vty_out(vty, " lchan sacch%s", VTY_NEWLINE);

	for (i = 0; i < sizeof(uint32_t) * 8; i++) {
		if (l23_cfg.gsmtap.lchan_acch_mask & ((uint32_t) 1 << i)) {
			chan_buf = get_value_string_or_null(gsmtap_gsm_channel_names, GSMTAP_CHANNEL_ACCH | i);
			if (chan_buf == NULL)
				continue;
			chan_buf = osmo_str_tolower(chan_buf);
			vty_out(vty, " lchan %s%s", chan_buf, VTY_NEWLINE);
		}
	}

	for (i = 0; i < sizeof(uint32_t) * 8; i++) {
		if (l23_cfg.gsmtap.lchan_mask & ((uint32_t) 1 << i)) {
			chan_buf = get_value_string_or_null(gsmtap_gsm_channel_names, i);
			if (chan_buf == NULL)
				continue;
			chan_buf = osmo_str_tolower(chan_buf);
			vty_out(vty, " lchan %s%s", chan_buf, VTY_NEWLINE);
		}
	}

	for (i = 0; i < 32; i++) {
		if (l23_cfg.gsmtap.categ_gprs_mask & ((uint32_t)1 << i)) {
			const char *category_buf;
			if (!(category_buf = get_value_string_or_null(gsmtap_categ_gprs_names, i)))
				continue;
			vty_out(vty, " category gprs %s%s", category_buf, VTY_NEWLINE);
		}
	}

	return CMD_SUCCESS;
}

static int l23_vty_config_write_testsim_node(struct vty *vty, const struct osmocom_ms *ms, const char *prefix)
{
	const struct gsm_settings *set = &ms->settings;
	vty_out(vty, "%stest-sim%s", prefix, VTY_NEWLINE);
	vty_out(vty, "%s imsi %s%s", prefix, set->test_sim.imsi, VTY_NEWLINE);
	switch (set->test_sim.ki_type) {
	case OSMO_AUTH_ALG_XOR:
		vty_out(vty, "%s ki xor %s%s",
			prefix, osmo_hexdump(set->test_sim.ki, 12), VTY_NEWLINE);
		break;
	case OSMO_AUTH_ALG_COMP128v1:
		vty_out(vty, "%s ki comp128 %s%s",
			prefix, osmo_hexdump(set->test_sim.ki, 16), VTY_NEWLINE);
		break;
	}
	if (!l23_vty_hide_default || set->test_sim.barr)
		vty_out(vty, "%s %sbarred-access%s", prefix,
			(set->test_sim.barr) ? "" : "no ", VTY_NEWLINE);
	if (set->test_sim.rplmn_valid) {
		vty_out(vty, "%s rplmn %s %s", prefix,
			osmo_mcc_name(set->test_sim.rplmn.mcc),
			osmo_mnc_name(set->test_sim.rplmn.mnc, set->test_sim.rplmn.mnc_3_digits));
		if (set->test_sim.lac > 0x0000 && set->test_sim.lac < 0xfffe) {
			vty_out(vty, " 0x%04x", set->test_sim.lac);
			if (set->test_sim.tmsi != GSM_RESERVED_TMSI) {
				vty_out(vty, " 0x%08x", set->test_sim.tmsi);
				if (set->test_sim.imsi_attached)
					vty_out(vty, " attached");
			}
		}
		vty_out(vty, "%s", VTY_NEWLINE);
	} else
		if (!l23_vty_hide_default)
			vty_out(vty, "%s no rplmn%s", prefix, VTY_NEWLINE);
	if (!l23_vty_hide_default || set->test_sim.always_search_hplmn)
		vty_out(vty, "%s hplmn-search %s%s", prefix,
			set->test_sim.always_search_hplmn ? "everywhere" : "foreign-country",
			VTY_NEWLINE);
	return CMD_SUCCESS;
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
	l23_vty_config_write_ms_node_contents_final(vty, ms, prefix_content);
}

/* placeholder for shared VTY commands */
void l23_vty_config_write_ms_node_contents(struct vty *vty, const struct osmocom_ms *ms, const char *prefix)
{
	const struct gsm_settings *set = &ms->settings;

	vty_out(vty, "%slayer2-socket %s%s", prefix, set->layer2_socket_path,
		VTY_NEWLINE);

	vty_out(vty, "%simei %s %s%s", prefix, set->imei,
		set->imeisv + strlen(set->imei), VTY_NEWLINE);
	if (set->imei_random)
		vty_out(vty, "%simei-random %d%s", prefix, set->imei_random, VTY_NEWLINE);
	else if (!l23_vty_hide_default)
		vty_out(vty, "%simei-fixed%s", prefix, VTY_NEWLINE);

	switch (set->sim_type) {
	case GSM_SIM_TYPE_NONE:
		vty_out(vty, "%ssim none%s", prefix,  VTY_NEWLINE);
		break;
	case GSM_SIM_TYPE_L1PHY:
		vty_out(vty, "%ssim reader%s", prefix,  VTY_NEWLINE);
		break;
	case GSM_SIM_TYPE_TEST:
		vty_out(vty, "%ssim test%s", prefix,  VTY_NEWLINE);
		break;
	case GSM_SIM_TYPE_SAP:
		vty_out(vty, "%ssim sap%s", prefix,  VTY_NEWLINE);
		break;
	default:
		OSMO_ASSERT(0);
	}

	l23_vty_config_write_testsim_node(vty, ms, prefix);
}

/* placeholder for shared VTY commands. Must be put at the end of the node: */
void l23_vty_config_write_ms_node_contents_final(struct vty *vty, const struct osmocom_ms *ms, const char *prefix)
{
	/* no shutdown must be written to config, because shutdown is default */
	vty_out(vty, "%s%sshutdown%s", prefix, (ms->shutdown != MS_SHUTDOWN_NONE) ? "" : "no ",
		VTY_NEWLINE);
	vty_out(vty, "!%s", VTY_NEWLINE);
}

static void l23_vty_init_gsmtap(void)
{
	cfg_gsmtap_gsmtap_lchan_cmd.string = vty_cmd_string_from_valstr(l23_ctx, gsmtap_gsm_channel_names,
						"lchan (sacch|",
						"|", ")", VTY_DO_LOWER);
	cfg_gsmtap_gsmtap_lchan_cmd.doc = vty_cmd_string_from_valstr(l23_ctx, gsmtap_gsm_channel_names,
						"Enable sending of UL/DL messages over GSMTAP\n" "SACCH\n",
						"\n", "", 0);

	cfg_gsmtap_no_gsmtap_lchan_cmd.string = vty_cmd_string_from_valstr(l23_ctx, gsmtap_gsm_channel_names,
						"no lchan (sacch|",
						"|", ")", VTY_DO_LOWER);
	cfg_gsmtap_no_gsmtap_lchan_cmd.doc = vty_cmd_string_from_valstr(l23_ctx, gsmtap_gsm_channel_names,
						NO_STR "Disable sending of UL/DL messages over GSMTAP\n" "SACCH\n",
						"\n", "", 0);


	cfg_gsmtap_gsmtap_categ_gprs_cmd.string = vty_cmd_string_from_valstr(l23_ctx, gsmtap_categ_gprs_names,
						"category gprs (",
						"|", ")", VTY_DO_LOWER);
	cfg_gsmtap_gsmtap_categ_gprs_cmd.doc = vty_cmd_string_from_valstr(l23_ctx, gsmtap_categ_gprs_help,
						"GSMTAP Category\n" "GPRS\n",
						"\n", "", 0);
	cfg_gsmtap_no_gsmtap_categ_gprs_cmd.string = vty_cmd_string_from_valstr(l23_ctx, gsmtap_categ_gprs_names,
						"no category gprs (",
						"|", ")", VTY_DO_LOWER);
	cfg_gsmtap_no_gsmtap_categ_gprs_cmd.doc = vty_cmd_string_from_valstr(l23_ctx, gsmtap_categ_gprs_help,
						NO_STR "GSMTAP Category\n" "GPRS\n",
						"\n", "", 0);

	install_element(CONFIG_NODE, &l23_cfg_gsmtap_cmd);

	install_node(&gsmtap_node, l23_vty_config_write_gsmtap_node);
	install_element(GSMTAP_NODE, &cfg_gsmtap_gsmtap_remote_host_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_no_gsmtap_remote_host_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_gsmtap_local_host_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_no_gsmtap_local_host_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_gsmtap_lchan_all_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_gsmtap_lchan_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_no_gsmtap_lchan_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_gsmtap_categ_gprs_all_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_gsmtap_categ_gprs_cmd);
	install_element(GSMTAP_NODE, &cfg_gsmtap_no_gsmtap_categ_gprs_cmd);
}

int l23_vty_init(int (*config_write_ms_node_cb)(struct vty *), osmo_signal_cbfn *l23_vty_signal_cb)
{
	int rc = 0;

	if (l23_app_info.opt_supported & L23_OPT_TAP)
		l23_vty_init_gsmtap();

	if (l23_app_info.opt_supported & L23_OPT_VTY)
		osmo_stats_vty_add_cmds();

	install_element_ve(&show_subscr_cmd);
	install_element_ve(&show_support_cmd);

	install_element(ENABLE_NODE, &sim_testcard_cmd);
	install_element(ENABLE_NODE, &sim_testcard_att_cmd);
	install_element(ENABLE_NODE, &sim_sap_cmd);
	install_element(ENABLE_NODE, &sim_reader_cmd);
	install_element(ENABLE_NODE, &sim_remove_cmd);
	install_element(ENABLE_NODE, &sim_pin_cmd);
	install_element(ENABLE_NODE, &sim_disable_pin_cmd);
	install_element(ENABLE_NODE, &sim_enable_pin_cmd);
	install_element(ENABLE_NODE, &sim_change_pin_cmd);
	install_element(ENABLE_NODE, &sim_unblock_pin_cmd);
	install_element(ENABLE_NODE, &sim_lai_cmd);

	install_element(CONFIG_NODE, &cfg_hide_default_cmd);
	install_element(CONFIG_NODE, &cfg_no_hide_default_cmd);

	install_node(&ms_node, config_write_ms_node_cb);
	install_element(MS_NODE, &cfg_ms_layer2_cmd);
	install_element(MS_NODE, &cfg_ms_imei_cmd);
	install_element(MS_NODE, &cfg_ms_imei_fixed_cmd);
	install_element(MS_NODE, &cfg_ms_imei_random_cmd);
	install_element(MS_NODE, &cfg_ms_sim_cmd);
	install_element(MS_NODE, &cfg_ms_testsim_cmd);
	install_node(&testsim_node, NULL);
	install_element(TESTSIM_NODE, &cfg_testsim_imsi_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_ki_xor_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_ki_comp128_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_barr_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_no_barr_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_no_rplmn_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_rplmn_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_rplmn_att_cmd);
	install_element(TESTSIM_NODE, &cfg_testsim_hplmn_cmd);
	install_element(MS_NODE, &cfg_ms_shutdown_cmd);
	install_element(MS_NODE, &cfg_ms_shutdown_force_cmd);
	install_element(MS_NODE, &cfg_ms_no_shutdown_cmd);

	/* Register the talloc context introspection command */
	osmo_talloc_vty_add_cmds();
	osmo_cpu_sched_vty_init(l23_ctx);
	if (l23_vty_signal_cb)
		rc = osmo_signal_register_handler(SS_L23_VTY, l23_vty_signal_cb, NULL);
	return rc;
}

