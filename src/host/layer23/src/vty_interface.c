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

#include <osmocore/gsm48.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/networks.h>
#include <osmocom/mncc.h>
#include <osmocom/transaction.h>

int mncc_call(struct osmocom_ms *ms, char *number);
int mncc_hangup(struct osmocom_ms *ms);
int mncc_answer(struct osmocom_ms *ms);

extern struct llist_head ms_list;

struct cmd_node ms_node = {
	MS_NODE,
	"%s(ms)#",
	1
};

struct cmd_node testsim_node = {
	TESTSIM_NODE,
	"%s(test-sim)#",
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
		struct gsm_settings *set = &ms->settings;

		vty_out(vty, "MS NAME: %s%s", ms->name, VTY_NEWLINE);
		vty_out(vty, " IMEI: %s%s", set->imei, VTY_NEWLINE);
		vty_out(vty, " IMEISV: %s%s", set->imeisv, VTY_NEWLINE);
		if (set->imei_random)
			vty_out(vty, " IMEI generation: random (%d trailing "
				"digits)%s", set->imei_random, VTY_NEWLINE);
		else
			vty_out(vty, " IMEI generation: fixed%s", VTY_NEWLINE);
		vty_out(vty, " network selection mode: %s%s",
			(set->plmn_mode == PLMN_MODE_AUTO)
				? "automatic" : "manual", VTY_NEWLINE);
	}

	return CMD_SUCCESS;
}

DEFUN(show_support, show_support_cmd, "show support [ms_name]",
	SHOW_STR "Display information about MS support\n"
	"Name of MS (see \"show ms\")")
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

static void gsm_states_dump(struct osmocom_ms *ms, struct vty *vty)
{
	struct gsm_trans *trans;

	vty_out(vty, "Current state of MS '%s'%s", ms->name, VTY_NEWLINE);
	if (ms->settings.plmn_mode == PLMN_MODE_AUTO)
		vty_out(vty, " automatic network selection: %s%s", 
			plmn_a_state_names[ms->plmn.state], VTY_NEWLINE);
	else
		vty_out(vty, " manual network selection: %s%s", 
			plmn_m_state_names[ms->plmn.state], VTY_NEWLINE);
	vty_out(vty, " cell selection: %s%s", 
		cs_state_names[ms->cellsel.state], VTY_NEWLINE);
	vty_out(vty, " radio ressource layer: %s%s", 
		gsm48_rr_state_names[ms->rrlayer.state], VTY_NEWLINE);
	vty_out(vty, " mobility management layer: %s", 
		gsm48_mm_state_names[ms->mmlayer.state]);
	if (ms->mmlayer.state == GSM48_MM_ST_MM_IDLE)
		vty_out(vty, ", %s", 
			gsm48_mm_substate_names[ms->mmlayer.substate]);
	vty_out(vty, "%s", VTY_NEWLINE);
	llist_for_each_entry(trans, &ms->trans_list, entry) {
		vty_out(vty, " call control: %s%s", 
			gsm48_cc_state_name(trans->cc.state), VTY_NEWLINE);
	}
}

DEFUN(show_states, show_states_cmd, "show states [ms_name]",
	SHOW_STR "Display current states of given MS\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	if (argc) {
		ms = get_ms(argv[0], vty);
		if (!ms)
			return CMD_WARNING;
		gsm_states_dump(ms, vty);
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			gsm_states_dump(ms, vty);
			vty_out(vty, "%s", VTY_NEWLINE);
		}
	}

	return CMD_SUCCESS;
}

DEFUN(show_subscr, show_subscr_cmd, "show subscriber [ms_name]",
	SHOW_STR "Display information about subscriber\n"
	"Name of MS (see \"show ms\")")
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
	SHOW_STR "Display information about received cells\n"
	"Name of MS (see \"show ms\")")
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
	SHOW_STR "Display information about received cell\n"
	"Name of MS (see \"show ms\")\nRadio frequency number")
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

	gsm48_sysinfo_dump(s, i, print_vty, vty);

	return CMD_SUCCESS;
}

DEFUN(show_ba, show_ba_cmd, "show ba MS_NAME [mcc] [mnc]",
	SHOW_STR "Display information about band allocations\n"
	"Name of MS (see \"show ms\")\nMobile Country Code\n"
	"Mobile Network Code")
{
	struct osmocom_ms *ms;
	uint16_t mcc = 0, mnc = 0;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (argc >= 3) {
		mcc = gsm_input_mcc((char *)argv[1]);
		mnc = gsm_input_mnc((char *)argv[2]);
		if (!mcc) {
			vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (!mnc) {
			vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	gsm322_dump_ba_list(&ms->cellsel, mcc, mnc, print_vty, vty);

	return CMD_SUCCESS;
}

DEFUN(show_forb_plmn, show_forb_plmn_cmd, "show forbidden plmn MS_NAME",
	SHOW_STR "Display information about forbidden cells / networks\n"
	"Display forbidden PLMNs\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	gsm_subscr_dump_forbidden_plmn(ms, print_vty, vty);

	return CMD_SUCCESS;
}

DEFUN(show_forb_la, show_forb_la_cmd, "show forbidden location-area MS_NAME",
	SHOW_STR "Display information about forbidden cells / networks\n"
	"Display forbidden location areas\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	gsm322_dump_forbidden_la(ms, print_vty, vty);

	return CMD_SUCCESS;
}

DEFUN(insert_test, insert_test_cmd, "insert testcard MS_NAME [mcc] [mnc]",
	"Insert ...\nInsert test card\nName of MS (see \"show ms\")\n"
	"Mobile Country Code\nMobile Network Code")
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
		mcc = gsm_input_mcc((char *)argv[1]);
		mnc = gsm_input_mnc((char *)argv[2]);
		if (!mcc) {
			vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (!mnc) {
			vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	gsm_subscr_testcard(ms);

	return CMD_SUCCESS;
}

DEFUN(remove_sim, remove_sim_cmd, "remove sim MS_NAME",
	"Remove ...\nRemove SIM card\nName of MS (see \"show ms\")")
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
	"Select ...\nSelect Network\nName of MS (see \"show ms\")\n"
	"Mobile Country Code\nMobile Network Code")
{
	struct osmocom_ms *ms;
	struct gsm322_plmn *plmn;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;
	struct gsm322_plmn_list *temp;
	uint16_t mcc = gsm_input_mcc((char *)argv[1]),
		 mnc = gsm_input_mnc((char *)argv[2]);
	int found = 0;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;
	plmn = &ms->plmn;

	if (!mcc) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (!mnc) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	llist_for_each_entry(temp, &plmn->sorted_plmn, entry)
		if (temp->mcc == mcc &&  temp->mnc == mnc)
			found = 1;
	if (!found) {
		vty_out(vty, "Network not in list!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_CHOSE_PLMN);
	if (!nmsg)
		return CMD_WARNING;
	ngm = (struct gsm322_msg *) nmsg->data;
	ngm->mcc = mcc;
	ngm->mnc = mnc;
	gsm322_plmn_sendmsg(ms, nmsg);

	return CMD_SUCCESS;
}

DEFUN(call, call_cmd, "call MS_NAME (NUMBER|emergency|answer|hangup)",
	"Make a call\nName of MS (see \"show ms\")\nPhone number to call\n"
	"Make an emergency call\nAnswer an incomming call\nHangup a call")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	switch (argv[1][0]) {
	case 'a':
		mncc_answer(ms);
		break;
	case 'h':
		mncc_hangup(ms);
		break;
	default:
		mncc_call(ms, (char *)argv[1]);
	}

	return CMD_SUCCESS;
}

DEFUN(network_show, network_show_cmd, "network show MS_NAME",
	"Network ...\nShow results of network search (again)\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;
	struct gsm322_plmn *plmn;
	struct gsm322_plmn_list *temp;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;
	plmn = &ms->plmn;

	if (ms->settings.plmn_mode != PLMN_MODE_AUTO
	 && plmn->state != GSM322_M3_NOT_ON_PLMN) {
		vty_out(vty, "Start network search first!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	llist_for_each_entry(temp, &plmn->sorted_plmn, entry)
		vty_out(vty, " Network %s, %s (%s, %s)%s",
			gsm_print_mcc(temp->mcc), gsm_print_mnc(temp->mnc),
			gsm_get_mcc(temp->mcc),
			gsm_get_mnc(temp->mcc, temp->mnc), VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(network_search, network_search_cmd, "network search MS_NAME",
	"Network ...\nTrigger network search\nName of MS (see \"show ms\")")
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
	"Select a mobile station to configure\nName of MS (see \"show ms\")")
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
	struct gsm_settings *set = &ms->settings;

	vty_out(vty, "ms %s%s", ms->name, VTY_NEWLINE);
	switch(ms->settings.simtype) {
		case GSM_SIM_TYPE_NONE:
		vty_out(vty, " sim none%s", VTY_NEWLINE);
		break;
		case GSM_SIM_TYPE_SLOT:
		vty_out(vty, " sim slot%s", VTY_NEWLINE);
		break;
		case GSM_SIM_TYPE_TEST:
		vty_out(vty, " sim test%s", VTY_NEWLINE);
		break;
	}
	vty_out(vty, " network-selection-mode %s%s", (set->plmn_mode
			== PLMN_MODE_AUTO) ? "auto" : "manual", VTY_NEWLINE);
	vty_out(vty, " imei %s %s%s", set->imei,
		set->imeisv + strlen(set->imei), VTY_NEWLINE);
	if (set->imei_random)
		vty_out(vty, " imei-random %d%s", set->imei_random,
			VTY_NEWLINE);
	else
		vty_out(vty, " imei-fixed%s", VTY_NEWLINE);
	vty_out(vty, " emergency-imsi %s%s", (ms->settings.emergency_imsi[0]) ?
		ms->settings.emergency_imsi : "none", VTY_NEWLINE);
	vty_out(vty, " test-sim%s", VTY_NEWLINE);
	vty_out(vty, "  imsi %s%s", ms->settings.test_imsi, VTY_NEWLINE);
	vty_out(vty, "  %sbarred-access%s", (set->test_barr) ? "" : "no ",
		VTY_NEWLINE);
	if (ms->settings.test_rplmn_valid)
		vty_out(vty, "  rplmn %03d %02d%s", ms->settings.test_rplmn_mcc,
			ms->settings.test_rplmn_mnc, VTY_NEWLINE);
	else
		vty_out(vty, "  no rplmn%s", VTY_NEWLINE);
	vty_out(vty, "  hplmn-search %s%s", (set->test_always) ? "everywhere"
			: "foreign-country", VTY_NEWLINE);
	vty_out(vty, " exit%s", VTY_NEWLINE);
	vty_out(vty, "exit%s", VTY_NEWLINE);
	vty_out(vty, "!%s", VTY_NEWLINE);
}

static int config_write_ms(struct vty *vty)
{
	struct osmocom_ms *ms;

	llist_for_each_entry(ms, &ms_list, entity)
		config_write_ms_single(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_mode, cfg_ms_mode_cmd, "network-selection-mode (auto|manual)",
	"Set network selection mode\nAutomatic network selection\n"
	"Manual network selection")
{
	struct osmocom_ms *ms = vty->index;
	struct msgb *nmsg;

	if (!ms->plmn.state) {
		if (argv[0][0] == 'a')
			ms->settings.plmn_mode = PLMN_MODE_AUTO;
		else
			ms->settings.plmn_mode = PLMN_MODE_MANUAL;

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

DEFUN(cfg_ms_imei, cfg_ms_imei_cmd, "imei IMEI [SV]",
	"Set IMEI (enter without control digit)\n15 Digits IMEI\n"
	"Software version digit")
{
	struct osmocom_ms *ms = vty->index;
	char *error, *sv = "0";

	if (argc >= 2)
		sv = (char *)argv[1];

	error = gsm_check_imei(argv[0], sv);
	if (error) {
		vty_out(vty, "%s%s", error, VTY_NEWLINE);
		return CMD_WARNING;
	}

	strcpy(ms->settings.imei, argv[0]);
	strcpy(ms->settings.imeisv, argv[0]);
	strcpy(ms->settings.imeisv + 15, sv);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_imei_fixed, cfg_ms_imei_fixed_cmd, "imei-fixed",
	"Use fixed IMEI on every power on")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.imei_random = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_imei_random, cfg_ms_imei_random_cmd, "imei-random <0-15>",
	"Use random IMEI on every power on\n"
	"Number of trailing digits to randomize")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.imei_random = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_emerg_imsi, cfg_ms_emerg_imsi_cmd, "emergency-imsi (none|IMSI)",
	"Use IMSI for emergency calls\n"
	"Use IMSI of SIM or IMEI for emergency calls\n15 digits IMSI")
{
	struct osmocom_ms *ms = vty->index;
	char *error;

	if (argv[0][0] == 'n') {
		ms->settings.emergency_imsi[0] = '\0';
		return CMD_SUCCESS;
	}

	error = gsm_check_imsi(argv[0]);
	if (error) {
		vty_out(vty, "%s%s", error, VTY_NEWLINE);
		return CMD_WARNING;
	}
	strcpy(ms->settings.emergency_imsi, argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sim, cfg_ms_sim_cmd, "sim (none|test)",
	"Set sim card type when powering on\nNo sim interted\n"
	"Test sim inserted")
{
	struct osmocom_ms *ms = vty->index;

	switch (argv[0][0]) {
	case 'n':
		ms->settings.simtype = GSM_SIM_TYPE_NONE;
		break;
	case 's':
		ms->settings.simtype = GSM_SIM_TYPE_SLOT;
		break;
	case 't':
		ms->settings.simtype = GSM_SIM_TYPE_TEST;
		break;
	}

	return CMD_SUCCESS;
}

/* per MS config */
DEFUN(cfg_testsim, cfg_testsim_cmd, "test-sim",
	"Configure test SIM emulation")
{
	vty->node = TESTSIM_NODE;

	return CMD_SUCCESS;
}

static int config_write_dummy(struct vty *vty)
{
	return CMD_SUCCESS;
}

DEFUN(cfg_test_imsi, cfg_test_imsi_cmd, "imsi IMSI",
	"Set IMSI on test card\n15 digits IMSI")
{
	struct osmocom_ms *ms = vty->index;
	char *error = gsm_check_imsi(argv[0]);

	if (error) {
		vty_out(vty, "%s%s", error, VTY_NEWLINE);
		return CMD_WARNING;
	}

	strcpy(ms->settings.test_imsi, argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_test_barr, cfg_test_barr_cmd, "barred-access",
	"Allow access to barred cells")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.test_barr = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_test_no_barr, cfg_test_no_barr_cmd, "no barred-access",
	NO_STR "Deny access to barred cells")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.test_barr = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_test_no_rplmn, cfg_test_no_rplmn_cmd, "no rplmn",
	NO_STR "Unset Registered PLMN")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.test_rplmn_valid = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_test_rplmn, cfg_test_rplmn_cmd, "rplmn MCC MNC",
	"Set Registered PLMN\nMobile Country Code\nMobile Network Code")
{
	struct osmocom_ms *ms = vty->index;
	uint16_t mcc = gsm_input_mcc((char *)argv[0]),
		 mnc = gsm_input_mnc((char *)argv[1]);

	if (!mcc) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (!mnc) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	ms->settings.test_rplmn_valid = 1;
	ms->settings.test_rplmn_mcc = mcc;
	ms->settings.test_rplmn_mnc = mnc;

	return CMD_SUCCESS;
}

DEFUN(cfg_test_hplmn, cfg_test_hplmn_cmd, "hplmn-search (everywhere|foreign-country)",
	"Set Home PLMN search mode\n"
	"Search for HPLMN when on any other network\n"
	"Search for HPLMN when in a different country")
{
	struct osmocom_ms *ms = vty->index;

	switch (argv[0][0]) {
	case 'e':
		ms->settings.test_always = 1;
		break;
	case 'f':
		ms->settings.test_always = 0;
		break;
	}

	return CMD_SUCCESS;
}

int ms_vty_init(void)
{
	cmd_init(1);
	vty_init();

	install_element(ENABLE_NODE, &show_ms_cmd);
	install_element(VIEW_NODE, &show_ms_cmd);
	install_element(ENABLE_NODE, &show_subscr_cmd);
	install_element(VIEW_NODE, &show_subscr_cmd);
	install_element(ENABLE_NODE, &show_support_cmd);
	install_element(VIEW_NODE, &show_support_cmd);
	install_element(ENABLE_NODE, &show_states_cmd);
	install_element(VIEW_NODE, &show_states_cmd);
	install_element(ENABLE_NODE, &show_cell_cmd);
	install_element(VIEW_NODE, &show_cell_cmd);
	install_element(ENABLE_NODE, &show_cell_si_cmd);
	install_element(VIEW_NODE, &show_cell_si_cmd);
	install_element(ENABLE_NODE, &show_ba_cmd);
	install_element(VIEW_NODE, &show_ba_cmd);
	install_element(ENABLE_NODE, &show_forb_la_cmd);
	install_element(VIEW_NODE, &show_forb_la_cmd);
	install_element(ENABLE_NODE, &show_forb_plmn_cmd);
	install_element(VIEW_NODE, &show_forb_plmn_cmd);

	install_element(ENABLE_NODE, &insert_test_cmd);
	install_element(ENABLE_NODE, &remove_sim_cmd);
	install_element(ENABLE_NODE, &network_search_cmd);
	install_element(ENABLE_NODE, &network_show_cmd);
	install_element(ENABLE_NODE, &network_select_cmd);
	install_element(ENABLE_NODE, &call_cmd);

	install_element(CONFIG_NODE, &cfg_ms_cmd);
	install_node(&ms_node, config_write_ms);
	install_default(MS_NODE);
	install_element(MS_NODE, &cfg_ms_mode_cmd);
	install_element(MS_NODE, &cfg_ms_imei_cmd);
	install_element(MS_NODE, &cfg_ms_imei_fixed_cmd);
	install_element(MS_NODE, &cfg_ms_imei_random_cmd);
	install_element(MS_NODE, &cfg_ms_emerg_imsi_cmd);
	install_element(MS_NODE, &cfg_ms_sim_cmd);

	install_element(MS_NODE, &cfg_testsim_cmd);
	install_node(&testsim_node, config_write_dummy);
	install_default(TESTSIM_NODE);
	install_element(TESTSIM_NODE, &cfg_test_imsi_cmd);
	install_element(TESTSIM_NODE, &cfg_test_barr_cmd);
	install_element(TESTSIM_NODE, &cfg_test_no_barr_cmd);
	install_element(TESTSIM_NODE, &cfg_test_no_rplmn_cmd);
	install_element(TESTSIM_NODE, &cfg_test_rplmn_cmd);
	install_element(TESTSIM_NODE, &cfg_test_hplmn_cmd);

	return 0;
}

