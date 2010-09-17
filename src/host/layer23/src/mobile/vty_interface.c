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

#include <osmocore/utils.h>
#include <osmocore/gsm48.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/gps.h>
#include <osmocom/vty/telnet_interface.h>

int mncc_call(struct osmocom_ms *ms, char *number);
int mncc_hangup(struct osmocom_ms *ms);
int mncc_answer(struct osmocom_ms *ms);
int mncc_hold(struct osmocom_ms *ms);
int mncc_retrieve(struct osmocom_ms *ms, int number);

extern struct llist_head ms_list;
extern struct llist_head active_connections;

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

DEFUN(monitor_network, monitor_network_cmd, "monitor network MS_NAME",
	"Monitor...\nMonitor network information\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	gsm48_rr_start_monitor(ms);

	return CMD_SUCCESS;
}

DEFUN(no_monitor_network, no_monitor_network_cmd, "no monitor network MS_NAME",
	NO_STR "Monitor...\nDeactivate monitor of network information\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	gsm48_rr_stop_monitor(ms);

	return CMD_SUCCESS;
}

DEFUN(sim_test, sim_test_cmd, "sim testcard MS_NAME [mcc] [mnc]",
	"SIM actions\nInsert test card\nName of MS (see \"show ms\")\n"
	"Mobile Country Code of RPLMN\nMobile Network Code of RPLMN")
{
	struct osmocom_ms *ms;
	uint16_t mcc = 0x001, mnc = 0x01f;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already presend, remove first!%s",
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

	gsm_subscr_testcard(ms, mcc, mnc);

	return CMD_SUCCESS;
}

DEFUN(sim_reader, sim_reader_cmd, "sim reader MS_NAME",
	"SIM actions\nSelect SIM from reader\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already presend, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_simcard(ms);

	return CMD_SUCCESS;
}

DEFUN(sim_remove, sim_remove_cmd, "sim remove MS_NAME",
	"SIM actions\nRemove SIM card\nName of MS (see \"show ms\")")
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

DEFUN(sim_pin, sim_pin_cmd, "sim pin MS_NAME PIN",
	"SIM actions\nEnter PIN for SIM card\nName of MS (see \"show ms\")\n"
	"PIN number")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (!ms->subscr.sim_pin_required) {
		vty_out(vty, "No PIN is required at this time!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_sim_pin(ms, (char *)argv[1]);

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

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_CHOOSE_PLMN);
	if (!nmsg)
		return CMD_WARNING;
	ngm = (struct gsm322_msg *) nmsg->data;
	ngm->mcc = mcc;
	ngm->mnc = mnc;
	gsm322_plmn_sendmsg(ms, nmsg);

	return CMD_SUCCESS;
}

DEFUN(call, call_cmd, "call MS_NAME (NUMBER|emergency|answer|hangup|hold)",
	"Make a call\nName of MS (see \"show ms\")\nPhone number to call\n"
	"Make an emergency call\nAnswer an incomming call\nHangup a call\n"
	"Hold current active call\n")
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
		if (argv[1][1] == 'a')
			mncc_hangup(ms);
		else
			mncc_hold(ms);
		break;
	default:
		mncc_call(ms, (char *)argv[1]);
	}

	return CMD_SUCCESS;
}

DEFUN(call_retr, call_retr_cmd, "call MS_NAME retrieve [number]",
	"Make a call\nName of MS (see \"show ms\")\n"
	"Retrieve call on hold\nNumber of call to retrieve")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	mncc_retrieve(ms, (argc > 1) ? atoi(argv[1]) : 0);

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

DEFUN(cfg_gps_enable, cfg_gps_enable_cmd, "gps enable",
	"GPS receiver")
{
	if (gps_open()) {
		gps.enable = 1;
		vty_out(vty, "Failed to open GPS device!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	gps.enable = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_gps_enable, cfg_no_gps_enable_cmd, "no gps enable",
	NO_STR "Disable GPS receiver")
{
	if (gps.enable)
		gps_close();
	gps.enable = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_gps_device, cfg_gps_device_cmd, "gps device DEVICE",
	"GPS receiver\nSelect serial device\n"
	"Full path of serial device including /dev/")
{
	strncpy(gps.device, argv[0], sizeof(gps.device));
	gps.device[sizeof(gps.device) - 1] = '\0';
	if (gps.enable) {
		gps_close();
		if (gps_open()) {
			vty_out(vty, "Failed to open GPS device!%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_gps_baud, cfg_gps_baud_cmd, "gps baudrate "
	"(default|4800|""9600|19200|38400|57600|115200)",
	"GPS receiver\nSelect baud rate\nDefault, don't modify\n\n\n\n\n\n")
{
	if (argv[0][0] == 'd')
		gps.baud = 0;
	else
		gps.baud = atoi(argv[0]);
	if (gps.enable) {
		gps_close();
		if (gps_open()) {
			gps.enable = 0;
			vty_out(vty, "Failed to open GPS device!%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

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
	switch(set->sim_type) {
		case GSM_SIM_TYPE_NONE:
		vty_out(vty, " sim none%s", VTY_NEWLINE);
		break;
		case GSM_SIM_TYPE_READER:
		vty_out(vty, " sim reader%s", VTY_NEWLINE);
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
	if (set->emergency_imsi[0])
		vty_out(vty, " emergency-imsi %s%s", set->emergency_imsi,
			VTY_NEWLINE);
	else
		vty_out(vty, " no emergency-imsi%s", VTY_NEWLINE);
	vty_out(vty, " %scall-waiting%s", (set->cw) ? "" : "no ", VTY_NEWLINE);
	vty_out(vty, " %sclip%s", (set->clip) ? "" : "no ", VTY_NEWLINE);
	vty_out(vty, " %sclir%s", (set->clir) ? "" : "no ", VTY_NEWLINE);
	vty_out(vty, " test-sim%s", VTY_NEWLINE);
	vty_out(vty, "  imsi %s%s", set->test_imsi, VTY_NEWLINE);
	switch (set->test_ki_type) {
	case GSM_SIM_KEY_XOR:
		vty_out(vty, "  ki xor %s%s", hexdump(set->test_ki, 12),
			VTY_NEWLINE);
		break;
	case GSM_SIM_KEY_COMP128:
		vty_out(vty, "  ki comp128 %s%s", hexdump(set->test_ki, 16),
			VTY_NEWLINE);
		break;
	}
	vty_out(vty, "  %sbarred-access%s", (set->test_barr) ? "" : "no ",
		VTY_NEWLINE);
	if (set->test_rplmn_valid)
		vty_out(vty, "  rplmn %s %s%s",
			gsm_print_mcc(set->test_rplmn_mcc),
			gsm_print_mnc(set->test_rplmn_mnc),
			VTY_NEWLINE);
	else
		vty_out(vty, "  no rplmn%s", VTY_NEWLINE);
	vty_out(vty, "  hplmn-search %s%s", (set->test_always) ? "everywhere"
			: "foreign-country", VTY_NEWLINE);
	vty_out(vty, " exit%s", VTY_NEWLINE);
	if (set->alter_tx_power)
		if (set->alter_tx_power_value)
			vty_out(vty, " tx-power %d%s",
				set->alter_tx_power_value, VTY_NEWLINE);
		else
			vty_out(vty, " tx-power full%s", VTY_NEWLINE);
	else
		vty_out(vty, " tx-power auto%s", VTY_NEWLINE);
	if (set->alter_delay)
		vty_out(vty, " simulated-delay %d%s", set->alter_delay,
			VTY_NEWLINE);
	else
		vty_out(vty, " no simulated-delay%s", VTY_NEWLINE);
	if (set->stick)
		vty_out(vty, " stick %d%s", set->stick_arfcn,
			VTY_NEWLINE);
	else
		vty_out(vty, " no stick%s", VTY_NEWLINE);
	if (set->no_lupd)
		vty_out(vty, " no location-updating%s", VTY_NEWLINE);
	else
		vty_out(vty, " location-updating%s", VTY_NEWLINE);
	vty_out(vty, "exit%s", VTY_NEWLINE);
	vty_out(vty, "!%s", VTY_NEWLINE);
}

static int config_write_ms(struct vty *vty)
{
	struct osmocom_ms *ms;

	vty_out(vty, "gps device %s%s", gps.device, VTY_NEWLINE);
	if (gps.baud)
		vty_out(vty, "gps baudrate %d%s", gps.baud, VTY_NEWLINE);
	else
		vty_out(vty, "gps baudrate default%s", VTY_NEWLINE);
	vty_out(vty, "%sgps enable%s", (gps.enable) ? "" : "no ", VTY_NEWLINE);
	vty_out(vty, "!%s", VTY_NEWLINE);

	llist_for_each_entry(ms, &ms_list, entity)
		config_write_ms_single(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sim, cfg_ms_sim_cmd, "sim (none|reader|test)",
	"Set SIM card type when powering on\nNo SIM interted\n"
	"Use SIM from reader\nTest SIM inserted")
{
	struct osmocom_ms *ms = vty->index;

	switch (argv[0][0]) {
	case 'n':
		ms->settings.sim_type = GSM_SIM_TYPE_NONE;
		break;
	case 'r':
		ms->settings.sim_type = GSM_SIM_TYPE_READER;
		break;
	case 't':
		ms->settings.sim_type = GSM_SIM_TYPE_TEST;
		break;
	default:
		vty_out(vty, "unknown SIM type%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

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

DEFUN(cfg_ms_emerg_imsi, cfg_ms_emerg_imsi_cmd, "emergency-imsi IMSI",
	"Use special IMSI for emergency calls\n15 digits IMSI")
{
	struct osmocom_ms *ms = vty->index;
	char *error;

	error = gsm_check_imsi(argv[0]);
	if (error) {
		vty_out(vty, "%s%s", error, VTY_NEWLINE);
		return CMD_WARNING;
	}
	strcpy(ms->settings.emergency_imsi, argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_emerg_imsi, cfg_ms_no_emerg_imsi_cmd, "no emergency-imsi",
	NO_STR "Use IMSI of SIM or IMEI for emergency calls")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.emergency_imsi[0] = '\0';

	return CMD_SUCCESS;
}

DEFUN(cfg_no_cw, cfg_ms_no_cw_cmd, "no call-waiting",
	NO_STR "Disallow waiting calls")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.cw = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_cw, cfg_ms_cw_cmd, "call-waiting",
	"Allow waiting calls")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.cw = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_clip, cfg_ms_clip_cmd, "clip",
	"Force caller ID presentation")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.clip = 1;
	ms->settings.clir = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_clir, cfg_ms_clir_cmd, "clir",
	"Force caller ID restriction")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.clip = 0;
	ms->settings.clir = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_clip, cfg_ms_no_clip_cmd, "no clip",
	NO_STR "Disable forcing of caller ID presentation")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.clip = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_clir, cfg_ms_no_clir_cmd, "no clir",
	NO_STR "Disable forcing of caller ID restriction")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.clir = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_tx_power, cfg_ms_tx_power_cmd, "tx-power (auto|full)",
	"Set the way to choose transmit power\nControlled by BTS\n"
	"Always full power\nFixed GSM power value if supported")
{
	struct osmocom_ms *ms = vty->index;

	switch (argv[0][0]) {
	case 'a':
		ms->settings.alter_tx_power = 0;
		break;
	case 'f':
		ms->settings.alter_tx_power = 1;
		ms->settings.alter_tx_power_value = 0;
		break;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_tx_power_val, cfg_ms_tx_power_val_cmd, "tx-power <0-31>",
	"Set the way to choose transmit power\n"
	"Fixed GSM power value if supported")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.alter_tx_power = 1;
	ms->settings.alter_tx_power_value = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sim_delay, cfg_ms_sim_delay_cmd, "simulated-delay <-128-127>",
	"Simulate a lower or higher distance from the BTS\n"
	"Delay in half bits (distance in 553.85 meter steps)")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.alter_delay = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_sim_delay, cfg_ms_no_sim_delay_cmd, "no simulated-delay",
	NO_STR "Do not simulate a lower or higher distance from the BTS")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.alter_delay = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_stick, cfg_ms_stick_cmd, "stick <0-1023>",
	"Stick to the given cell\nARFCN of the cell to stick to")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.stick = 1;
	ms->settings.stick_arfcn = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_stick, cfg_ms_no_stick_cmd, "no stick",
	NO_STR "Do not stick to any cell")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.stick = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_lupd, cfg_ms_lupd_cmd, "location-updating",
	"Allow location updating")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.no_lupd = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_lupd, cfg_ms_no_lupd_cmd, "no location-updating",
	NO_STR "Do not allow location updating")
{
	struct osmocom_ms *ms = vty->index;

	ms->settings.no_lupd = 1;

	return CMD_SUCCESS;
}

/* per testsim config */
DEFUN(cfg_ms_testsim, cfg_ms_testsim_cmd, "test-sim",
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

#define HEX_STR "\nByte as two digits hexadecimal"
DEFUN(cfg_test_ki_xor, cfg_test_ki_xor_cmd, "ki xor HEX HEX HEX HEX HEX HEX "
	"HEX HEX HEX HEX HEX HEX",
	"Set Key (Kc) on test card\nUse XOR algorithm" HEX_STR HEX_STR HEX_STR
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

	set->test_ki_type = GSM_SIM_KEY_XOR;
	memcpy(set->test_ki, ki, 12);
	return CMD_SUCCESS;
}

DEFUN(cfg_test_ki_comp128, cfg_test_ki_comp128_cmd, "ki comp128 HEX HEX HEX "
	"HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX HEX",
	"Set Key (Kc) on test card\nUse XOR algorithm" HEX_STR HEX_STR HEX_STR
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

	set->test_ki_type = GSM_SIM_KEY_COMP128;
	memcpy(set->test_ki, ki, 16);
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

enum node_type ms_vty_go_parent(struct vty *vty)
{
	switch (vty->node) {
	case MS_NODE:
		vty->node = CONFIG_NODE;
		vty->index = NULL;
		break;
	case TESTSIM_NODE:
		vty->node = MS_NODE;
		break;
	default:
		vty->node = CONFIG_NODE;
	}

	return vty->node;
}

/* Down vty node level. */
gDEFUN(ournode_exit,
       ournode_exit_cmd, "exit", "Exit current mode and down to previous mode\n")
{
	switch (vty->node) {
	case MS_NODE:
		vty->node = CONFIG_NODE;
		vty->index = NULL;
		break;
	case TESTSIM_NODE:
		vty->node = MS_NODE;
		break;
	default:
		break;
	}
	return CMD_SUCCESS;
}

/* End of configuration. */
gDEFUN(ournode_end,
       ournode_end_cmd, "end", "End current mode and change to enable mode.")
{
	switch (vty->node) {
	case VIEW_NODE:
	case ENABLE_NODE:
		/* Nothing to do. */
		break;
	case CONFIG_NODE:
	case VTY_NODE:
	case MS_NODE:
	case TESTSIM_NODE:
		vty_config_unlock(vty);
		vty->node = ENABLE_NODE;
		vty->index = NULL;
		vty->index_sub = NULL;
		break;
	default:
		break;
	}
	return CMD_SUCCESS;
}

int ms_vty_init(void)
{
	install_element_ve(&show_ms_cmd);
	install_element_ve(&show_subscr_cmd);
	install_element_ve(&show_support_cmd);
	install_element_ve(&show_states_cmd);
	install_element_ve(&show_cell_cmd);
	install_element_ve(&show_cell_si_cmd);
	install_element_ve(&show_ba_cmd);
	install_element_ve(&show_forb_la_cmd);
	install_element_ve(&show_forb_plmn_cmd);
	install_element_ve(&monitor_network_cmd);
	install_element_ve(&no_monitor_network_cmd);

	install_element(ENABLE_NODE, &sim_test_cmd);
	install_element(ENABLE_NODE, &sim_reader_cmd);
	install_element(ENABLE_NODE, &sim_remove_cmd);
	install_element(ENABLE_NODE, &sim_pin_cmd);
	install_element(ENABLE_NODE, &network_search_cmd);
	install_element(ENABLE_NODE, &network_show_cmd);
	install_element(ENABLE_NODE, &network_select_cmd);
	install_element(ENABLE_NODE, &call_cmd);
	install_element(ENABLE_NODE, &call_retr_cmd);

	install_element(CONFIG_NODE, &cfg_gps_device_cmd);
	install_element(CONFIG_NODE, &cfg_gps_baud_cmd);
	install_element(CONFIG_NODE, &cfg_gps_enable_cmd);
	install_element(CONFIG_NODE, &cfg_no_gps_enable_cmd);

	install_element(CONFIG_NODE, &cfg_ms_cmd);
	install_element(CONFIG_NODE, &ournode_end_cmd);
	install_node(&ms_node, config_write_ms);
	install_default(MS_NODE);
	install_element(MS_NODE, &ournode_exit_cmd);
	install_element(MS_NODE, &ournode_end_cmd);
	install_element(MS_NODE, &cfg_ms_sim_cmd);
	install_element(MS_NODE, &cfg_ms_mode_cmd);
	install_element(MS_NODE, &cfg_ms_imei_cmd);
	install_element(MS_NODE, &cfg_ms_imei_fixed_cmd);
	install_element(MS_NODE, &cfg_ms_imei_random_cmd);
	install_element(MS_NODE, &cfg_ms_no_emerg_imsi_cmd);
	install_element(MS_NODE, &cfg_ms_emerg_imsi_cmd);
	install_element(MS_NODE, &cfg_ms_cw_cmd);
	install_element(MS_NODE, &cfg_ms_no_cw_cmd);
	install_element(MS_NODE, &cfg_ms_clip_cmd);
	install_element(MS_NODE, &cfg_ms_clir_cmd);
	install_element(MS_NODE, &cfg_ms_no_clip_cmd);
	install_element(MS_NODE, &cfg_ms_no_clir_cmd);
	install_element(MS_NODE, &cfg_ms_testsim_cmd);
	install_element(MS_NODE, &cfg_ms_tx_power_cmd);
	install_element(MS_NODE, &cfg_ms_tx_power_val_cmd);
	install_element(MS_NODE, &cfg_ms_sim_delay_cmd);
	install_element(MS_NODE, &cfg_ms_no_sim_delay_cmd);
	install_element(MS_NODE, &cfg_ms_stick_cmd);
	install_element(MS_NODE, &cfg_ms_no_stick_cmd);
	install_element(MS_NODE, &cfg_ms_lupd_cmd);
	install_element(MS_NODE, &cfg_ms_no_lupd_cmd);

	install_node(&testsim_node, config_write_dummy);
	install_default(TESTSIM_NODE);
	install_element(TESTSIM_NODE, &ournode_exit_cmd);
	install_element(TESTSIM_NODE, &ournode_end_cmd);
	install_element(TESTSIM_NODE, &cfg_test_imsi_cmd);
	install_element(TESTSIM_NODE, &cfg_test_ki_xor_cmd);
	install_element(TESTSIM_NODE, &cfg_test_ki_comp128_cmd);
	install_element(TESTSIM_NODE, &cfg_test_barr_cmd);
	install_element(TESTSIM_NODE, &cfg_test_no_barr_cmd);
	install_element(TESTSIM_NODE, &cfg_test_no_rplmn_cmd);
	install_element(TESTSIM_NODE, &cfg_test_rplmn_cmd);
	install_element(TESTSIM_NODE, &cfg_test_hplmn_cmd);

	return 0;
}

void vty_notify(struct osmocom_ms *ms, const char *fmt, ...)
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

