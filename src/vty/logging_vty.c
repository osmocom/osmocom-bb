/* OpenBSC logging helper for the VTY */
/* (C) 2009-2010 by Harald Welte <laforge@gnumonks.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "../../config.h"

#include <osmocore/talloc.h>
#include <osmocore/logging.h>
#include <osmocore/utils.h>

//#include <openbsc/vty.h>

#include <osmocom/vty/command.h>
#include <osmocom/vty/buffer.h>
#include <osmocom/vty/vty.h>
#include <osmocom/vty/telnet_interface.h>
#include <osmocom/vty/logging.h>

#define LOG_STR "Configure logging sub-system\n"

extern const struct log_info *osmo_log_info;

static void _vty_output(struct log_target *tgt,
			unsigned int level, const char *line)
{
	struct vty *vty = tgt->tgt_vty.vty;
	vty_out(vty, "%s", line);
	/* This is an ugly hack, but there is no easy way... */
	if (strchr(line, '\n'))
		vty_out(vty, "\r");
}

struct log_target *log_target_create_vty(struct vty *vty)
{
	struct log_target *target;

	target = log_target_create();
	if (!target)
		return NULL;

	target->tgt_vty.vty = vty;
	target->output = _vty_output;
	return target;
}

DEFUN(enable_logging,
      enable_logging_cmd,
      "logging enable",
	LOGGING_STR
      "Enables logging to this vty\n")
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (conn->dbg) {
		vty_out(vty, "Logging already enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	conn->dbg = log_target_create_vty(vty);
	if (!conn->dbg)
		return CMD_WARNING;

	log_add_target(conn->dbg);
	return CMD_SUCCESS;
}

DEFUN(logging_fltr_all,
      logging_fltr_all_cmd,
      "logging filter all (0|1)",
	LOGGING_STR FILTER_STR
	"Do you want to log all messages?\n"
	"Only print messages matched by other filters\n"
	"Bypass filter and print all messages\n")
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (!conn->dbg) {
		vty_out(vty, "Logging was not enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_set_all_filter(conn->dbg, atoi(argv[0]));
	return CMD_SUCCESS;
}

DEFUN(logging_use_clr,
      logging_use_clr_cmd,
      "logging color (0|1)",
	LOGGING_STR "Configure color-printing for log messages\n"
      "Don't use color for printing messages\n"
      "Use color for printing messages\n")
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (!conn->dbg) {
		vty_out(vty, "Logging was not enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_set_use_color(conn->dbg, atoi(argv[0]));
	return CMD_SUCCESS;
}

DEFUN(logging_prnt_timestamp,
      logging_prnt_timestamp_cmd,
      "logging timestamp (0|1)",
	LOGGING_STR "Configure log message timestamping\n"
	"Don't prefix each log message\n"
	"Prefix each log message with current timestamp\n")
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (!conn->dbg) {
		vty_out(vty, "Logging was not enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_set_print_timestamp(conn->dbg, atoi(argv[0]));
	return CMD_SUCCESS;
}

/* FIXME: those have to be kept in sync with the log levels and categories */
#define VTY_DEBUG_CATEGORIES "(rll|cc|mm|rr|rsl|nm|sms|pag|mncc|inp|mi|mib|mux|meas|sccp|msc|mgcp|ho|db|ref|gprs|ns|bssgp|llc|sndcp|isup|m2ua|pcap|all)"
#define CATEGORIES_HELP	\
	"A-bis Radio Link Layer (RLL)\n"			\
	"Layer3 Call Control (CC)\n"				\
	"Layer3 Mobility Management (MM)\n"			\
	"Layer3 Radio Resource (RR)\n"				\
	"A-bis Radio Signalling Link (RSL)\n"			\
	"A-bis Network Management / O&M (NM/OML)\n"		\
	"Layer3 Short Messagaging Service (SMS)\n"		\
	"Paging Subsystem\n"					\
	"MNCC API for Call Control application\n"		\
	"A-bis Input Subsystem\n"				\
	"A-bis Input Driver for Signalling\n"			\
	"A-bis Input Driver for B-Channel (voice data)\n"	\
	"A-bis B-Channel / Sub-channel Multiplexer\n"		\
	"Radio Measurements\n"					\
	"SCCP\n"						\
	"Mobile Switching Center\n"				\
	"Media Gateway Control Protocol\n"			\
	"Hand-over\n"						\
	"Database Layer\n"					\
	"Reference Counting\n"					\
	"GPRS Core\n"						\
	"GPRS Network Service (NS)\n"				\
	"GPRS BSS Gateway Protocol (BSSGP)\n"			\
	"GPRS Logical Link Control Protocol (LLC)\n"		\
	"GPRS Sub-Network Dependent Control Protocol (SNDCP)\n"	\
	"ISDN User Part (ISUP)\n"				\
	"SCTP M2UA\n"						\
	"Trace message IO\n"					\
	"Global setting for all subsytems\n"

#define VTY_DEBUG_LEVELS "(everything|debug|info|notice|error|fatal)"
#define LEVELS_HELP	\
	"Log simply everything\n"				\
	"Log debug messages and higher levels\n"		\
	"Log informational messages and higher levels\n"	\
	"Log noticable messages and higher levels\n"		\
	"Log error messages and higher levels\n"		\
	"Log only fatal messages\n"

static int _logging_level(struct vty *vty, struct log_target *dbg,
			  const char *cat_str, const char *lvl_str)
{
	int category = log_parse_category(cat_str);
	int level = log_parse_level(lvl_str);

	if (level < 0) {
		vty_out(vty, "Invalid level `%s'%s", lvl_str, VTY_NEWLINE);
		return CMD_WARNING;
	}

	/* Check for special case where we want to set global log level */
	if (!strcmp(cat_str, "all")) {
		log_set_log_level(dbg, level);
		return CMD_SUCCESS;
	}

	if (category < 0) {
		vty_out(vty, "Invalid category `%s'%s", cat_str, VTY_NEWLINE);
		return CMD_WARNING;
	}

	dbg->categories[category].enabled = 1;
	dbg->categories[category].loglevel = level;

	return CMD_SUCCESS;
}

DEFUN(logging_level,
      logging_level_cmd,
      "logging level " VTY_DEBUG_CATEGORIES " " VTY_DEBUG_LEVELS,
      LOGGING_STR
      "Set the log level for a specified category\n"
      CATEGORIES_HELP
      LEVELS_HELP)
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (!conn->dbg) {
		vty_out(vty, "Logging was not enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return _logging_level(vty, conn->dbg, argv[0], argv[1]);
}

DEFUN(logging_set_category_mask,
      logging_set_category_mask_cmd,
      "logging set log mask MASK",
	LOGGING_STR
      "Decide which categories to output.\n")
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (!conn->dbg) {
		vty_out(vty, "Logging was not enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_parse_category_mask(conn->dbg, argv[0]);
	return CMD_SUCCESS;
}

DEFUN(diable_logging,
      disable_logging_cmd,
      "logging disable",
	LOGGING_STR
      "Disables logging to this vty\n")
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (!conn->dbg) {
		vty_out(vty, "Logging was not enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_del_target(conn->dbg);
	talloc_free(conn->dbg);
	conn->dbg = NULL;
	return CMD_SUCCESS;
}

static void vty_print_logtarget(struct vty *vty, const struct log_info *info,
				const struct log_target *tgt)
{
	unsigned int i;

	vty_out(vty, " Global Loglevel: %s%s",
		log_level_str(tgt->loglevel), VTY_NEWLINE);
	vty_out(vty, " Use color: %s, Print Timestamp: %s%s",
		tgt->use_color ? "On" : "Off",
		tgt->print_timestamp ? "On" : "Off", VTY_NEWLINE);

	vty_out(vty, " Log Level specific information:%s", VTY_NEWLINE);

	for (i = 0; i < info->num_cat; i++) {
		const struct log_category *cat = &tgt->categories[i];
		vty_out(vty, "  %-10s %-10s %-8s %s%s",
			info->cat[i].name+1, log_level_str(cat->loglevel),
			cat->enabled ? "Enabled" : "Disabled",
 			info->cat[i].description,
			VTY_NEWLINE);
	}
}

#define SHOW_LOG_STR "Show current logging configuration\n"

DEFUN(show_logging_vty,
      show_logging_vty_cmd,
      "show logging vty",
	SHOW_STR SHOW_LOG_STR
	"Show current logging configuration for this vty\n")
{
	struct telnet_connection *conn;

	conn = (struct telnet_connection *) vty->priv;
	if (!conn->dbg) {
		vty_out(vty, "Logging was not enabled.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	vty_print_logtarget(vty, osmo_log_info, conn->dbg);

	return CMD_SUCCESS;
}

gDEFUN(cfg_description, cfg_description_cmd,
	"description .TEXT",
	"Save human-readable decription of the object\n")
{
	char **dptr = vty->index_sub;

	if (!dptr) {
		vty_out(vty, "vty->index_sub == NULL%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	*dptr = argv_concat(argv, argc, 0);
	if (!dptr)
		return CMD_WARNING;

	return CMD_SUCCESS;
}

gDEFUN(cfg_no_description, cfg_no_description_cmd,
	"no description",
	NO_STR
	"Remove description of the object\n")
{
	char **dptr = vty->index_sub;

	if (!dptr) {
		vty_out(vty, "vty->index_sub == NULL%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (*dptr) {
		talloc_free(*dptr);
		*dptr = NULL;
	}

	return CMD_SUCCESS;
}

/* Support for configuration of log targets != the current vty */

struct cmd_node cfg_log_node = {
	CFG_LOG_NODE,
	"%s(config-log)# ",
	1
};

DEFUN(cfg_log_fltr_all,
      cfg_log_fltr_all_cmd,
      "logging filter all (0|1)",
	LOGGING_STR FILTER_STR
	"Do you want to log all messages?\n"
	"Only print messages matched by other filters\n"
	"Bypass filter and print all messages\n")
{
	struct log_target *dbg = vty->index;

	log_set_all_filter(dbg, atoi(argv[0]));
	return CMD_SUCCESS;
}

DEFUN(cfg_log_use_clr,
      cfg_log_use_clr_cmd,
      "logging color (0|1)",
	LOGGING_STR "Configure color-printing for log messages\n"
      "Don't use color for printing messages\n"
      "Use color for printing messages\n")
{
	struct log_target *dbg = vty->index;

	log_set_use_color(dbg, atoi(argv[0]));
	return CMD_SUCCESS;
}

DEFUN(cfg_log_timestamp,
      cfg_log_timestamp_cmd,
      "logging timestamp (0|1)",
	LOGGING_STR "Configure log message timestamping\n"
	"Don't prefix each log message\n"
	"Prefix each log message with current timestamp\n")
{
	struct log_target *dbg = vty->index;

	log_set_print_timestamp(dbg, atoi(argv[0]));
	return CMD_SUCCESS;
}

DEFUN(cfg_log_level,
      cfg_log_level_cmd,
      "logging level " VTY_DEBUG_CATEGORIES " " VTY_DEBUG_LEVELS,
      LOGGING_STR
      "Set the log level for a specified category\n"
      CATEGORIES_HELP
      LEVELS_HELP)
{
	struct log_target *dbg = vty->index;

	return _logging_level(vty, dbg, argv[0], argv[1]);
}

#ifdef HAVE_SYSLOG_H

#include <syslog.h>

static const int local_sysl_map[] = {
	[0] = LOG_LOCAL0,
	[1] = LOG_LOCAL1,
	[2] = LOG_LOCAL2,
	[3] = LOG_LOCAL3,
	[4] = LOG_LOCAL4,
	[5] = LOG_LOCAL5,
	[6] = LOG_LOCAL6,
	[7] = LOG_LOCAL7
};

static int _cfg_log_syslog(struct vty *vty, int facility)
{
	struct log_target *tgt;

	/* First delete the old syslog target, if any */
	tgt = log_target_find(LOG_TGT_TYPE_SYSLOG, NULL);
	if (tgt)
		log_target_destroy(tgt);

	tgt = log_target_create_syslog("FIXME", 0, facility);
	if (!tgt) {
		vty_out(vty, "%% Unable to open syslog%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	log_add_target(tgt);

	vty->index = tgt;
	vty->node = CFG_LOG_NODE;

	return CMD_SUCCESS;
}

DEFUN(cfg_log_syslog_local, cfg_log_syslog_local_cmd,
      "log syslog local <0-7>",
	LOG_STR "Logging via syslog\n" "Syslog LOCAL facility\n"
	"Local facility number\n")
{
	int local = atoi(argv[0]);
	int facility = local_sysl_map[local];

	return _cfg_log_syslog(vty, facility);
}

static struct value_string sysl_level_names[] = {
	{ LOG_AUTHPRIV, "authpriv" },
	{ LOG_CRON, 	"cron" },
	{ LOG_DAEMON,	"daemon" },
	{ LOG_FTP,	"ftp" },
	{ LOG_LPR,	"lpr" },
	{ LOG_MAIL,	"mail" },
	{ LOG_NEWS,	"news" },
	{ LOG_USER,	"user" },
	{ LOG_UUCP,	"uucp" },
	/* only for value -> string conversion */
	{ LOG_LOCAL0,	"local 0" },
	{ LOG_LOCAL1,	"local 1" },
	{ LOG_LOCAL2,	"local 2" },
	{ LOG_LOCAL3,	"local 3" },
	{ LOG_LOCAL4,	"local 4" },
	{ LOG_LOCAL5,	"local 5" },
	{ LOG_LOCAL6,	"local 6" },
	{ LOG_LOCAL7,	"local 7" },
	{ 0, NULL }
};

DEFUN(cfg_log_syslog, cfg_log_syslog_cmd,
      "log syslog (authpriv|cron|daemon|ftp|lpr|mail|news|user|uucp)",
	LOG_STR "Logging via syslog\n")
{
	int facility = get_string_value(sysl_level_names, argv[0]);

	return _cfg_log_syslog(vty, facility);
}

DEFUN(cfg_no_log_syslog, cfg_no_log_syslog_cmd,
	"no log syslog",
	NO_STR LOG_STR "Logging via syslog\n")
{
	struct log_target *tgt;

	tgt = log_target_find(LOG_TGT_TYPE_SYSLOG, NULL);
	if (!tgt) {
		vty_out(vty, "%% No syslog target found%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_target_destroy(tgt);

	return CMD_SUCCESS;
}
#endif /* HAVE_SYSLOG_H */

DEFUN(cfg_log_stderr, cfg_log_stderr_cmd,
	"log stderr",
	LOG_STR "Logging via STDERR of the process\n")
{
	struct log_target *tgt;

	tgt = log_target_find(LOG_TGT_TYPE_STDERR, NULL);
	if (!tgt) {
		tgt = log_target_create_stderr();
		if (!tgt) {
			vty_out(vty, "%% Unable to create stderr log%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
		log_add_target(tgt);
	}

	vty->index = tgt;
	vty->node = CFG_LOG_NODE;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_log_stderr, cfg_no_log_stderr_cmd,
	"no log stderr",
	NO_STR LOG_STR "Logging via STDERR of the process\n")
{
	struct log_target *tgt;

	tgt = log_target_find(LOG_TGT_TYPE_STDERR, NULL);
	if (!tgt) {
		vty_out(vty, "%% No stderr logging active%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_target_destroy(tgt);

	return CMD_SUCCESS;
}

DEFUN(cfg_log_file, cfg_log_file_cmd,
	"log file .FILENAME",
	LOG_STR "Logging to text file\n" "Filename\n")
{
	const char *fname = argv[0];
	struct log_target *tgt;

	tgt = log_target_find(LOG_TGT_TYPE_FILE, fname);
	if (!tgt) {
		tgt = log_target_create_file(fname);
		if (!tgt) {
			vty_out(vty, "%% Unable to create file `%s'%s",
				fname, VTY_NEWLINE);
			return CMD_WARNING;
		}
		log_add_target(tgt);
	}

	vty->index = tgt;
	vty->node = CFG_LOG_NODE;

	return CMD_SUCCESS;
}


DEFUN(cfg_no_log_file, cfg_no_log_file_cmd,
	"no log file .FILENAME",
	NO_STR LOG_STR "Logging to text file\n" "Filename\n")
{
	const char *fname = argv[0];
	struct log_target *tgt;

	tgt = log_target_find(LOG_TGT_TYPE_FILE, fname);
	if (!tgt) {
		vty_out(vty, "%% No such log file `%s'%s",
			fname, VTY_NEWLINE);
		return CMD_WARNING;
	}

	log_target_destroy(tgt);

	return CMD_SUCCESS;
}

static int config_write_log_single(struct vty *vty, struct log_target *tgt)
{
	int i;
	char level_lower[32];

	switch (tgt->type) {
	case LOG_TGT_TYPE_VTY:
		return 1;
		break;
	case LOG_TGT_TYPE_STDERR:
		vty_out(vty, "log stderr%s", VTY_NEWLINE);
		break;
	case LOG_TGT_TYPE_SYSLOG:
#ifdef HAVE_SYSLOG_H
		vty_out(vty, "log syslog %s%s",
			get_value_string(sysl_level_names,
					 tgt->tgt_syslog.facility),
			VTY_NEWLINE);
#endif
		break;
	case LOG_TGT_TYPE_FILE:
		vty_out(vty, "log file %s%s", tgt->tgt_file.fname, VTY_NEWLINE);
		break;
	}

	vty_out(vty, "  logging color %u%s", tgt->use_color ? 1 : 0,
		VTY_NEWLINE);
	vty_out(vty, "  logging timestamp %u%s", tgt->print_timestamp ? 1 : 0,
		VTY_NEWLINE);

	/* stupid old osmo logging API uses uppercase strings... */
	osmo_str2lower(level_lower, log_level_str(tgt->loglevel));
	vty_out(vty, "  logging level all %s%s", level_lower, VTY_NEWLINE);

	for (i = 0; i < osmo_log_info->num_cat; i++) {
		const struct log_category *cat = &tgt->categories[i];
		char cat_lower[32];

		/* stupid old osmo logging API uses uppercase strings... */
		osmo_str2lower(cat_lower, osmo_log_info->cat[i].name+1);
		osmo_str2lower(level_lower, log_level_str(cat->loglevel));

		vty_out(vty, "  logging level %s %s%s", cat_lower, level_lower,
			VTY_NEWLINE);
	}

	/* FIXME: levels */

	return 1;
}

static int config_write_log(struct vty *vty)
{
	struct log_target *dbg = vty->index;

	llist_for_each_entry(dbg, &osmo_log_target_list, entry)
		config_write_log_single(vty, dbg);

	return 1;
}

void logging_vty_add_cmds()
{
	install_element_ve(&enable_logging_cmd);
	install_element_ve(&disable_logging_cmd);
	install_element_ve(&logging_fltr_all_cmd);
	install_element_ve(&logging_use_clr_cmd);
	install_element_ve(&logging_prnt_timestamp_cmd);
	install_element_ve(&logging_set_category_mask_cmd);
	install_element_ve(&logging_level_cmd);
	install_element_ve(&show_logging_vty_cmd);

	install_node(&cfg_log_node, config_write_log);
	install_element(CFG_LOG_NODE, &cfg_log_fltr_all_cmd);
	install_element(CFG_LOG_NODE, &cfg_log_use_clr_cmd);
	install_element(CFG_LOG_NODE, &cfg_log_timestamp_cmd);
	install_element(CFG_LOG_NODE, &cfg_log_level_cmd);

	install_element(CONFIG_NODE, &cfg_log_stderr_cmd);
	install_element(CONFIG_NODE, &cfg_no_log_stderr_cmd);
	install_element(CONFIG_NODE, &cfg_log_file_cmd);
	install_element(CONFIG_NODE, &cfg_no_log_file_cmd);
#ifdef HAVE_SYSLOG_H
	install_element(CONFIG_NODE, &cfg_log_syslog_cmd);
	install_element(CONFIG_NODE, &cfg_log_syslog_local_cmd);
	install_element(CONFIG_NODE, &cfg_no_log_syslog_cmd);
#endif
}
