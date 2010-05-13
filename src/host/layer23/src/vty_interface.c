/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>

#include <vty/command.h>
#include <vty/buffer.h>
#include <vty/vty.h>

#include <osmocom/osmocom_data.h>

extern struct llist_head ms_list;

struct cmd_node ms_node = {
	MS_NODE,
	"%s(ms)#",
	1
};

static void print_vty(void *priv, const char *fmt, ...)
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

static struct osmocom_ms *get_ms(const char *name, struct vty *vty)
{
	struct osmocom_ms *ms;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, name))
			return ms;
	}
	vty_out(vty, "MS name '%s' does not exits.%s", name, VTY_NEWLINE);

	return NULL;
}

DEFUN(show_ms, show_ms_cmd, "show ms",
	SHOW_STR "Display available MS entities\n")
{
	struct osmocom_ms *ms;

	llist_for_each_entry(ms, &ms_list, entity) {
		vty_out(vty, "MS NAME: %s%s", ms->name, VTY_NEWLINE);
		vty_out(vty, " IMEI: %s%s", ms->support.imei, VTY_NEWLINE);
		vty_out(vty, " IMEISV: %s%s", ms->support.imeisv, VTY_NEWLINE);
		vty_out(vty, " IMEI selection: %s%s",
			(ms->support.imei_random) ? "random" : "fixed",
			VTY_NEWLINE);
		vty_out(vty, " network selection mode: %s%s",
			(ms->plmn.mode == PLMN_MODE_AUTO)
				? "automatic" : "manual", VTY_NEWLINE);
	}

	return CMD_SUCCESS;
}

DEFUN(show_support, show_support_cmd, "show support [ms_name]",
	SHOW_STR "Display information about MS support\n")
{
	struct osmocom_ms *ms;

	if (argc) {
		ms = get_ms(argv[0], vty);
		if (!ms)
			return CMD_WARNING;
		gsm_support_dump(&ms->support, print_vty, vty);
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			gsm_support_dump(&ms->support, print_vty, vty);
			vty_out(vty, "%s", VTY_NEWLINE);
		}
	}

	return CMD_SUCCESS;
}

DEFUN(show_subscr, show_subscr_cmd, "show subscriber [ms_name]",
	SHOW_STR "Display information about subscriber\n")
{
	struct osmocom_ms *ms;

	if (argc) {
		ms = get_ms(argv[0], vty);
		if (!ms)
			return CMD_WARNING;
		gsm_subscr_dump(&ms->subscr, print_vty, vty);
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			gsm_subscr_dump(&ms->subscr, print_vty, vty);
			vty_out(vty, "%s", VTY_NEWLINE);
		}
	}

	return CMD_SUCCESS;
}

DEFUN(show_cell, show_cell_cmd, "show cell MS_NAME",
	SHOW_STR "Display information about received cells\n")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	gsm322_dump_cs_list(&ms->cellsel, GSM322_CS_FLAG_SYSINFO, print_vty,
		vty);

	return CMD_SUCCESS;
}

DEFUN(show_cell_si, show_cell_si_cmd, "show cell MS_NAME <0-1023>",
	SHOW_STR "Display information about received cell\n")
{
	struct osmocom_ms *ms;
	int i;
	struct gsm48_sysinfo *s;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	i = atoi(argv[1]);
	if (i < 0 || i > 1023) {
		vty_out(vty, "Given ARFCN '%s' not in range (0..1023)%s",
			argv[1], VTY_NEWLINE);
		return CMD_WARNING;
	}
	s = ms->cellsel.list[i].sysinfo;
	if (!s) {
		vty_out(vty, "Given ARFCN '%s' has no sysinfo available%s",
			argv[1], VTY_NEWLINE);
		return CMD_SUCCESS;
	}

	gsm48_sysinfo_dump(s, print_vty, vty);

	return CMD_SUCCESS;
}

DEFUN(show_ba, show_ba_cmd, "show ba MS_NAME [mcc] [mnc]",
	SHOW_STR "Display information about band allocations\n")
{
	struct osmocom_ms *ms;
	uint16_t mcc = 0, mnc = 0;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (argc >= 3) {
		mcc = atoi(argv[1]);
		mnc = atoi(argv[2]);
	}

	gsm322_dump_ba_list(&ms->cellsel, mcc, mnc, print_vty, vty);

	return CMD_SUCCESS;
}

DEFUN(insert_test, insert_test_cmd, "insert testcard MS_NAME [mcc] [mnc]",
	"Insert ...\nInsert test card\n")
{
	struct osmocom_ms *ms;
	uint16_t mcc = 1, mnc = 1;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "Sim already presend, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (argc >= 3) {
		mcc = atoi(argv[1]);
		mnc = atoi(argv[2]);
	}

	gsm_subscr_testcard(ms, mcc, mnc, "0000000000");

	return CMD_SUCCESS;
}

DEFUN(remove_sim, remove_sim_cmd, "remove sim MS_NAME",
	"Remove ...\nRemove SIM card\n")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (!ms->subscr.sim_valid) {
		vty_out(vty, "No Sim inserted!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_remove(ms);

	return CMD_SUCCESS;
}

DEFUN(network_select, network_select_cmd, "network select MS_NAME MCC MNC",
	"Select ...\nSelect Network\n")
{
	struct osmocom_ms *ms;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_CHOSE_PLMN);
	if (!nmsg)
		return CMD_WARNING;
	ngm = (struct gsm322_msg *) nmsg->data;
	ngm->mcc = atoi(argv[1]);
	ngm->mnc = atoi(argv[2]);
	gsm322_plmn_sendmsg(ms, nmsg);

	return CMD_SUCCESS;
}

DEFUN(network_search, network_search_cmd, "network search MS_NAME",
	"Network ...\nTrigger network search\n")
{
	struct osmocom_ms *ms;
	struct msgb *nmsg;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_USER_RESEL);
	if (!nmsg)
		return CMD_WARNING;
	gsm322_plmn_sendmsg(ms, nmsg);

	return CMD_SUCCESS;
}

/* per MS config */
DEFUN(cfg_ms, cfg_ms_cmd, "ms MS_NAME",
	"Select a mobile station to configure\n")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	vty->index = ms;
	vty->node = MS_NODE;

	return CMD_SUCCESS;
}

static void config_write_ms_single(struct vty *vty, struct osmocom_ms *ms)
{
	vty_out(vty, " ms %s%s", ms->name, VTY_NEWLINE);
	vty_out(vty, "  network-selection-mode %s%s", (ms->plmn.mode
			== PLMN_MODE_AUTO) ? "auto" : "manual", VTY_NEWLINE);
}

static int config_write_ms(struct vty *vty)
{
	struct osmocom_ms *ms;

	llist_for_each_entry(ms, &ms_list, entity)
		config_write_ms_single(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_mode, cfg_ms_mode_cmd, "network-selection-mode (auto|manual)",
	"Set network selection mode\n")
{
	struct osmocom_ms *ms = vty->index;
	struct msgb *nmsg;

	if (!ms->plmn.state) {
		if (argv[0][0] == 'a')
			ms->plmn.mode = PLMN_MODE_AUTO;
		else
			ms->plmn.mode = PLMN_MODE_MANUAL;

		return CMD_SUCCESS;
	}
	if (argv[0][0] == 'a')
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_SEL_AUTO);
	else
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_SEL_MANUAL);
	if (!nmsg)
		return CMD_WARNING;
	gsm322_plmn_sendmsg(ms, nmsg);

	return CMD_SUCCESS;
}

int ms_vty_init(void)
{
	cmd_init(1);
	vty_init();

	install_element(VIEW_NODE, &show_ms_cmd);
	install_element(ENABLE_NODE, &show_ms_cmd);
	install_element(VIEW_NODE, &show_subscr_cmd);
	install_element(ENABLE_NODE, &show_support_cmd);
	install_element(VIEW_NODE, &show_support_cmd);
	install_element(ENABLE_NODE, &show_cell_cmd);
	install_element(VIEW_NODE, &show_cell_cmd);
	install_element(ENABLE_NODE, &show_cell_si_cmd);
	install_element(VIEW_NODE, &show_cell_si_cmd);
	install_element(ENABLE_NODE, &show_ba_cmd);
	install_element(VIEW_NODE, &show_ba_cmd);

	install_element(ENABLE_NODE, &insert_test_cmd);
	install_element(ENABLE_NODE, &remove_sim_cmd);
	install_element(ENABLE_NODE, &network_search_cmd);
	install_element(ENABLE_NODE, &network_select_cmd);

	install_element(CONFIG_NODE, &cfg_ms_cmd);
	install_node(&ms_node, config_write_ms);
	install_default(MS_NODE);
	install_element(MS_NODE, &cfg_ms_mode_cmd);

	return 0;
}

