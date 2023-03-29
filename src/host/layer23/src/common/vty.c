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
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/mncc_ms.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/gsm480_ss.h>
#include <osmocom/bb/mobile/gsm411_sms.h>

bool l23_vty_reading = false;

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

	install_node(&ms_node, config_write_ms_node_cb);
	install_element(MS_NODE, &cfg_ms_layer2_cmd);
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

