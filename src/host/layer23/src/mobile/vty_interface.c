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

#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/signal.h>
#include <osmocom/crypt/auth.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/gps.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/vty.h>
#include <osmocom/bb/mobile/app_mobile.h>
#include <osmocom/bb/mobile/gsm480_ss.h>
#include <osmocom/bb/mobile/gsm411_sms.h>
#include <osmocom/vty/telnet_interface.h>

void *l23_ctx;

int mncc_call(struct osmocom_ms *ms, char *number);
int mncc_hangup(struct osmocom_ms *ms);
int mncc_answer(struct osmocom_ms *ms);
int mncc_hold(struct osmocom_ms *ms);
int mncc_retrieve(struct osmocom_ms *ms, int number);
int mncc_dtmf(struct osmocom_ms *ms, char *dtmf);

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

struct cmd_node support_node = {
	SUPPORT_NODE,
	"%s(support)#",
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

int vty_check_number(struct vty *vty, const char *number)
{
	int i;

	for (i = 0; i < strlen(number); i++) {
		/* allow international notation with + */
		if (i == 0 && number[i] == '+')
			continue;
		if (!(number[i] >= '0' && number[i] <= '9')
		 && number[i] != '*'
		 && number[i] != '#'
		 && !(number[i] >= 'a' && number[i] <= 'c')) {
			vty_out(vty, "Invalid digit '%c' of number!%s",
				number[i], VTY_NEWLINE);
			return -EINVAL;
		}
	}
	if (number[0] == '\0' || (number[0] == '+' && number[1] == '\0')) {
		vty_out(vty, "Given number has no digits!%s", VTY_NEWLINE);
		return -EINVAL;
	}

	return 0;
}

int vty_reading = 0;
static int hide_default = 0;

static void vty_restart(struct vty *vty, struct osmocom_ms *ms)
{
	if (vty_reading)
		return;
	if (ms->shutdown != 0)
		return;
	vty_out(vty, "You must restart MS '%s' ('shutdown / no shutdown') for "
		"change to take effect!%s", ms->name, VTY_NEWLINE);
}

static void vty_restart_if_started(struct vty *vty, struct osmocom_ms *ms)
{
	if (!ms->started)
		return;
	vty_restart(vty, ms);
}

static struct osmocom_ms *get_ms(const char *name, struct vty *vty)
{
	struct osmocom_ms *ms;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, name)) {
			if (ms->shutdown) {
				vty_out(vty, "MS '%s' is admin down.%s", name,
					VTY_NEWLINE);
				return NULL;
			}
			return ms;
		}
	}
	vty_out(vty, "MS name '%s' does not exits.%s", name, VTY_NEWLINE);

	return NULL;
}

static void gsm_ms_dump(struct osmocom_ms *ms, struct vty *vty)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_trans *trans;
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
		(ms->shutdown) ? "administratively " : "",
		(ms->shutdown || !ms->started) ? "down" : "up",
		(!ms->shutdown) ? service : "",
		VTY_NEWLINE);
	vty_out(vty, "  IMEI: %s%s", set->imei, VTY_NEWLINE);
	vty_out(vty, "     IMEISV: %s%s", set->imeisv, VTY_NEWLINE);
	if (set->imei_random)
		vty_out(vty, "     IMEI generation: random (%d trailing "
			"digits)%s", set->imei_random, VTY_NEWLINE);
	else
		vty_out(vty, "     IMEI generation: fixed%s", VTY_NEWLINE);

	if (ms->shutdown)
		return;

	if (set->plmn_mode == PLMN_MODE_AUTO)
		vty_out(vty, "  automatic network selection state: %s%s",
			get_a_state_name(ms->plmn.state), VTY_NEWLINE);
	else
		vty_out(vty, "  manual network selection state   : %s%s",
			get_m_state_name(ms->plmn.state), VTY_NEWLINE);
	if (ms->plmn.mcc)
		vty_out(vty, "                                     MCC=%s "
			"MNC=%s (%s, %s)%s", gsm_print_mcc(ms->plmn.mcc),
			gsm_print_mnc(ms->plmn.mnc), gsm_get_mcc(ms->plmn.mcc),
			gsm_get_mnc(ms->plmn.mcc, ms->plmn.mnc), VTY_NEWLINE);
	vty_out(vty, "  cell selection state: %s%s",
		get_cs_state_name(ms->cellsel.state), VTY_NEWLINE);
	if (ms->cellsel.sel_mcc) {
		vty_out(vty, "                        ARFCN=%s MCC=%s MNC=%s "
			"LAC=0x%04x CELLID=0x%04x%s",
			gsm_print_arfcn(ms->cellsel.sel_arfcn),
			gsm_print_mcc(ms->cellsel.sel_mcc),
			gsm_print_mnc(ms->cellsel.sel_mnc),
			ms->cellsel.sel_lac, ms->cellsel.sel_id, VTY_NEWLINE);
		vty_out(vty, "                        (%s, %s)%s",
			gsm_get_mcc(ms->cellsel.sel_mcc),
			gsm_get_mnc(ms->cellsel.sel_mcc, ms->cellsel.sel_mnc),
			VTY_NEWLINE);
	}
	vty_out(vty, "  radio ressource layer state: %s%s",
		gsm48_rr_state_names[ms->rrlayer.state], VTY_NEWLINE);
	vty_out(vty, "  mobility management layer state: %s",
		gsm48_mm_state_names[ms->mmlayer.state]);
	if (ms->mmlayer.state == GSM48_MM_ST_MM_IDLE)
		vty_out(vty, ", %s",
			gsm48_mm_substate_names[ms->mmlayer.substate]);
	vty_out(vty, "%s", VTY_NEWLINE);
	llist_for_each_entry(trans, &ms->trans_list, entry) {
		vty_out(vty, "  call control state: %s%s",
			gsm48_cc_state_name(trans->cc.state), VTY_NEWLINE);
	}
}


DEFUN(show_ms, show_ms_cmd, "show ms [MS_NAME]",
	SHOW_STR "Display available MS entities\n")
{
	struct osmocom_ms *ms;

	if (argc) {
		llist_for_each_entry(ms, &ms_list, entity) {
			if (!strcmp(ms->name, argv[0])) {
				gsm_ms_dump(ms, vty);
				return CMD_SUCCESS;
			}
		}
		vty_out(vty, "MS name '%s' does not exits.%s", argv[0],
		VTY_NEWLINE);
		return CMD_WARNING;
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			gsm_ms_dump(ms, vty);
			vty_out(vty, "%s", VTY_NEWLINE);
		}
	}

	return CMD_SUCCESS;
}

DEFUN(show_support, show_support_cmd, "show support [MS_NAME]",
	SHOW_STR "Display information about MS support\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	if (argc) {
		ms = get_ms(argv[0], vty);
		if (!ms)
			return CMD_WARNING;
		gsm_support_dump(ms, print_vty, vty);
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			gsm_support_dump(ms, print_vty, vty);
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
		ms = get_ms(argv[0], vty);
		if (!ms)
			return CMD_WARNING;
		gsm_subscr_dump(&ms->subscr, print_vty, vty);
	} else {
		llist_for_each_entry(ms, &ms_list, entity) {
			if (!ms->shutdown) {
				gsm_subscr_dump(&ms->subscr, print_vty, vty);
				vty_out(vty, "%s", VTY_NEWLINE);
			}
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

	gsm322_dump_cs_list(&ms->cellsel, GSM322_CS_FLAG_SUPPORT, print_vty,
		vty);

	return CMD_SUCCESS;
}

DEFUN(show_cell_si, show_cell_si_cmd, "show cell MS_NAME <0-1023> [pcs]",
	SHOW_STR "Display information about received cell\n"
	"Name of MS (see \"show ms\")\nRadio frequency number\n"
	"Given frequency is PCS band (1900) rather than DCS band.")
{
	struct osmocom_ms *ms;
	struct gsm48_sysinfo *s;
	uint16_t arfcn = atoi(argv[1]);

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (argc > 2) {
		if (arfcn < 512 || arfcn > 810) {
			vty_out(vty, "Given ARFCN not in PCS band%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
		arfcn |= ARFCN_PCS;
	}

	s = ms->cellsel.list[arfcn2index(arfcn)].sysinfo;
	if (!s) {
		vty_out(vty, "Given ARFCN '%s' has no sysinfo available%s",
			argv[1], VTY_NEWLINE);
		return CMD_SUCCESS;
	}

	gsm48_sysinfo_dump(s, arfcn, print_vty, vty, ms->settings.freq_map);

	return CMD_SUCCESS;
}

DEFUN(show_nbcells, show_nbcells_cmd, "show neighbour-cells MS_NAME",
	SHOW_STR "Display information about current neighbour cells\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	gsm322_dump_nb_list(&ms->cellsel, print_vty, vty);

	return CMD_SUCCESS;
}

DEFUN(show_ba, show_ba_cmd, "show ba MS_NAME [MCC] [MNC]",
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
		if (mcc == GSM_INPUT_INVALID) {
			vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (mnc == GSM_INPUT_INVALID) {
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

static int _sim_test_cmd(struct vty *vty, int argc, const char *argv[],
	int attached)
{
	struct osmocom_ms *ms;
	uint16_t mcc = 0x001, mnc = 0x01f, lac = 0x0000;
	uint32_t tmsi = 0xffffffff;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already attached, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (argc == 2) {
		vty_out(vty, "Give MNC together with MCC%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (argc >= 3) {
		mcc = gsm_input_mcc((char *)argv[1]);
		mnc = gsm_input_mnc((char *)argv[2]);
		if (mcc == GSM_INPUT_INVALID) {
			vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (mnc == GSM_INPUT_INVALID) {
			vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	if (argc >= 4)
		lac = strtoul(argv[3], NULL, 16);

	if (argc >= 5)
		tmsi = strtoul(argv[4], NULL, 16);

	gsm_subscr_testcard(ms, mcc, mnc, lac, tmsi, attached);

	return CMD_SUCCESS;
}

DEFUN(sim_test, sim_test_cmd,
	"sim testcard MS_NAME [MCC] [MNC] [LAC] [TMSI]",
	"SIM actions\nAttach bulit in test SIM\nName of MS (see \"show ms\")\n"
	"Optionally set mobile Country Code of RPLMN\n"
	"Optionally set mobile Network Code of RPLMN\n"
	"Optionally set location area code of RPLMN\n"
	"Optionally set current assigned TMSI")
{
	return _sim_test_cmd(vty, argc, argv, 0);
}

DEFUN(sim_test_att, sim_test_att_cmd,
	"sim testcard MS_NAME MCC MNC LAC TMSI attached",
	"SIM actions\nAttach bulit in test SIM\nName of MS (see \"show ms\")\n"
	"Set mobile Country Code of RPLMN\nSet mobile Network Code of RPLMN\n"
	"Set location area code\nSet current assigned TMSI\n"
	"Indicate to MM that card is already attached")
{
	return _sim_test_cmd(vty, argc, argv, 1);
}

DEFUN(sim_sap, sim_sap_cmd, "sim sap MS_NAME",
	"SIM actions\nAttach SIM over SAP interface\n"
	"Name of MS (see \"show ms\")\n")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already attached, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (gsm_subscr_sapcard(ms) != 0) {
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(sim_reader, sim_reader_cmd, "sim reader MS_NAME",
	"SIM actions\nAttach SIM from reader\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (ms->subscr.sim_valid) {
		vty_out(vty, "SIM already attached, remove first!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_simcard(ms);

	return CMD_SUCCESS;
}

DEFUN(sim_remove, sim_remove_cmd, "sim remove MS_NAME",
	"SIM actions\nDetach SIM card\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (!ms->subscr.sim_valid) {
		vty_out(vty, "No SIM attached!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (ms->subscr.sim_type == GSM_SIM_TYPE_SAP) {
		gsm_subscr_remove_sapcard(ms);
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

	ms = get_ms(argv[0], vty);
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

	ms = get_ms(argv[0], vty);
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

	ms = get_ms(argv[0], vty);
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

	ms = get_ms(argv[0], vty);
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
	uint16_t mcc = gsm_input_mcc((char *)argv[1]),
		 mnc = gsm_input_mnc((char *)argv[2]),
		 lac = strtoul(argv[3], NULL, 16);

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (mcc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (mnc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	ms->subscr.mcc = mcc;
	ms->subscr.mnc = mnc;
	ms->subscr.lac = lac;
	ms->subscr.tmsi = 0xffffffff;

	gsm_subscr_write_loci(ms);

	return CMD_SUCCESS;
}

DEFUN(network_select, network_select_cmd,
	"network select MS_NAME MCC MNC [force]",
	"Select ...\nSelect Network\nName of MS (see \"show ms\")\n"
	"Mobile Country Code\nMobile Network Code\n"
	"Force selecting a network that is not in the list")
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

	if (ms->settings.plmn_mode != PLMN_MODE_MANUAL) {
		vty_out(vty, "Not in manual network selection mode%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (mcc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (mnc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (argc < 4) {
		llist_for_each_entry(temp, &plmn->sorted_plmn, entry)
			if (temp->mcc == mcc &&  temp->mnc == mnc)
				found = 1;
		if (!found) {
			vty_out(vty, "Network not in list!%s", VTY_NEWLINE);
			vty_out(vty, "To force selecting this network, use "
				"'force' keyword%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
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
	"Make a call\nName of MS (see \"show ms\")\nPhone number to call "
	"(Use digits '0123456789*#abc', and '+' to dial international)\n"
	"Make an emergency call\nAnswer an incomming call\nHangup a call\n"
	"Hold current active call\n")
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;
	struct gsm_settings_abbrev *abbrev;
	char *number;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;
	set = &ms->settings;

	if (set->ch_cap == GSM_CAP_SDCCH) {
		vty_out(vty, "Basic call is not supported for SDCCH only "
			"mobile%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	number = (char *)argv[1];
	if (!strcmp(number, "emergency"))
		mncc_call(ms, number);
	else if (!strcmp(number, "answer"))
		mncc_answer(ms);
	else if (!strcmp(number, "hangup"))
		mncc_hangup(ms);
	else if (!strcmp(number, "hold"))
		mncc_hold(ms);
	else {
		llist_for_each_entry(abbrev, &set->abbrev, list) {
			if (!strcmp(number, abbrev->abbrev)) {
				number = abbrev->number;
				vty_out(vty, "Dialing number '%s'%s", number,
					VTY_NEWLINE);
				break;
			}
		}
		if (vty_check_number(vty, number))
			return CMD_WARNING;
		mncc_call(ms, number);
	}

	return CMD_SUCCESS;
}

DEFUN(call_retr, call_retr_cmd, "call MS_NAME retrieve [NUMBER]",
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

DEFUN(call_dtmf, call_dtmf_cmd, "call MS_NAME dtmf DIGITS",
	"Make a call\nName of MS (see \"show ms\")\n"
	"One or more DTMF digits to transmit")
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;
	set = &ms->settings;

	if (!set->cc_dtmf) {
		vty_out(vty, "DTMF not supported, please enable!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	mncc_dtmf(ms, (char *)argv[1]);

	return CMD_SUCCESS;
}

DEFUN(sms, sms_cmd, "sms MS_NAME NUMBER .LINE",
	"Send an SMS\nName of MS (see \"show ms\")\nPhone number to send SMS "
	"(Use digits '0123456789*#abc', and '+' to dial international)\n"
	"SMS text\n")
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;
	struct gsm_settings_abbrev *abbrev;
	char *number, *sms_sca = NULL;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;
	set = &ms->settings;

	if (!set->sms_ptp) {
		vty_out(vty, "SMS not supported by this mobile, please enable "
			"SMS support%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (ms->subscr.sms_sca[0])
		sms_sca = ms->subscr.sms_sca;
	else if (set->sms_sca[0])
		sms_sca = set->sms_sca;

	if (!sms_sca) {
		vty_out(vty, "SMS sms-service-center not defined on SIM card, "
			"please define one at settings.%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	number = (char *)argv[1];
	llist_for_each_entry(abbrev, &set->abbrev, list) {
		if (!strcmp(number, abbrev->abbrev)) {
			number = abbrev->number;
			vty_out(vty, "Using number '%s'%s", number,
				VTY_NEWLINE);
			break;
		}
	}
	if (vty_check_number(vty, number))
		return CMD_WARNING;

	sms_send(ms, sms_sca, number, argv_concat(argv, argc, 2));

	return CMD_SUCCESS;
}

DEFUN(service, service_cmd, "service MS_NAME (*#06#|*#21#|*#67#|*#61#|*#62#"
	"|*#002#|*#004#|*xx*number#|*xx#|#xx#|##xx#|STRING|hangup)",
	"Send a Supplementary Service request\nName of MS (see \"show ms\")\n"
	"Query IMSI\n"
	"Query Call Forwarding Unconditional (CFU)\n"
	"Query Call Forwarding when Busy (CFB)\n"
	"Query Call Forwarding when No Response (CFNR)\n"
	"Query Call Forwarding when Not Reachable\n"
	"Query all Call Forwardings\n"
	"Query all conditional Call Forwardings\n"
	"Set and activate Call Forwarding (xx = Service Code, see above)\n"
	"Activate Call Forwarding (xx = Service Code, see above)\n"
	"Deactivate Call Forwarding (xx = Service Code, see above)\n"
	"Erase and deactivate Call Forwarding (xx = Service Code, see above)\n"
	"Service string "
	"(Example: '*100#' requests account balace on some networks.)\n"
	"Hangup existing service connection")
{
	struct osmocom_ms *ms;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	ss_send(ms, argv[1], 0);

	return CMD_SUCCESS;
}

DEFUN(test_reselection, test_reselection_cmd, "test re-selection NAME",
	"Manually trigger cell re-selection\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;
	struct msgb *nmsg;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;
	set = &ms->settings;

	if (set->stick) {
		vty_out(vty, "Cannot trigger cell re-selection, because we "
			"stick to a cell!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_CELL_RESEL);
	if (!nmsg)
		return CMD_WARNING;
	gsm322_c_event(ms, nmsg);


	return CMD_SUCCESS;
}

DEFUN(delete_forbidden_plmn, delete_forbidden_plmn_cmd,
	"delete forbidden plmn NAME MCC MNC",
	"Delete\nForbidden\nplmn\nName of MS (see \"show ms\")\n"
	"Mobile Country Code\nMobile Network Code")
{
	struct osmocom_ms *ms;
	uint16_t mcc = gsm_input_mcc((char *)argv[1]),
		 mnc = gsm_input_mnc((char *)argv[2]);

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;

	if (mcc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (mnc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	gsm_subscr_del_forbidden_plmn(&ms->subscr, mcc, mnc);

	return CMD_SUCCESS;
}

DEFUN(network_show, network_show_cmd, "network show MS_NAME",
	"Network ...\nShow results of network search (again)\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;
	struct gsm_settings *set;
	struct gsm322_plmn *plmn;
	struct gsm322_plmn_list *temp;

	ms = get_ms(argv[0], vty);
	if (!ms)
		return CMD_WARNING;
	set = &ms->settings;
	plmn = &ms->plmn;

	if (set->plmn_mode != PLMN_MODE_AUTO
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
	if (osmo_gps_open()) {
		g.enable = 1;
		vty_out(vty, "Failed to open GPS device!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	g.enable = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_gps_enable, cfg_no_gps_enable_cmd, "no gps enable",
	NO_STR "Disable GPS receiver")
{
	if (g.enable)
		osmo_gps_close();
	g.enable = 0;

	return CMD_SUCCESS;
}

#ifdef _HAVE_GPSD
DEFUN(cfg_gps_host, cfg_gps_host_cmd, "gps host HOST:PORT",
	"GPS receiver\nSelect gpsd host and port\n"
	"IP and port (optional) of the host running gpsd")
{
	char* colon = strstr(argv[0], ":");
	if (colon != NULL) {
		memcpy(g.gpsd_host, argv[0], colon - argv[0]);
		g.gpsd_host[colon - argv[0]] = '\0';
		memcpy(g.gpsd_port, colon+1, strlen(colon+1));
		g.gpsd_port[strlen(colon+1)] = '\0';
	} else {
		snprintf(g.gpsd_host, ARRAY_SIZE(g.gpsd_host), "%s", argv[0]);
		g.gpsd_host[ARRAY_SIZE(g.gpsd_host) - 1] = '\0';
		snprintf(g.gpsd_port, ARRAY_SIZE(g.gpsd_port), "2947");
		g.gpsd_port[ARRAY_SIZE(g.gpsd_port) - 1] = '\0';
	}
	g.gps_type = GPS_TYPE_GPSD;
	if (g.enable) {
		osmo_gps_close();
		if (osmo_gps_open()) {
			vty_out(vty, "Failed to connect to gpsd host!%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	return CMD_SUCCESS;
}
#endif

DEFUN(cfg_gps_device, cfg_gps_device_cmd, "gps device DEVICE",
	"GPS receiver\nSelect serial device\n"
	"Full path of serial device including /dev/")
{
	strncpy(g.device, argv[0], sizeof(g.device));
	g.device[sizeof(g.device) - 1] = '\0';
	g.gps_type = GPS_TYPE_SERIAL;
	if (g.enable) {
		osmo_gps_close();
		if (osmo_gps_open()) {
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
		g.baud = 0;
	else
		g.baud = atoi(argv[0]);
	if (g.enable) {
		osmo_gps_close();
		if (osmo_gps_open()) {
			g.enable = 0;
			vty_out(vty, "Failed to open GPS device!%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_hide_default, cfg_hide_default_cmd, "hide-default",
	"Hide most default values in config to make it more compact")
{
	hide_default = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_hide_default, cfg_no_hide_default_cmd, "no hide-default",
	NO_STR "Show default values in config")
{
	hide_default = 0;

	return CMD_SUCCESS;
}

/* per MS config */
DEFUN(cfg_ms, cfg_ms_cmd, "ms MS_NAME",
	"Select a mobile station to configure\nName of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;
	int found = 0;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, argv[0])) {
			found = 1;
			break;
		}
	}

	if (!found) {
		if (!vty_reading) {
			vty_out(vty, "MS name '%s' does not exits, try "
				"'ms %s create'%s", argv[0], argv[0],
				VTY_NEWLINE);
			return CMD_WARNING;
		}
		ms = mobile_new((char *)argv[0]);
		if (!ms) {
			vty_out(vty, "Failed to add MS name '%s'%s", argv[0],
				VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	vty->index = ms;
	vty->node = MS_NODE;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_create, cfg_ms_create_cmd, "ms MS_NAME create",
	"Select a mobile station to configure\nName of MS (see \"show ms\")\n"
	"Create if MS does not exists")
{
	struct osmocom_ms *ms;
	int found = 0;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, argv[0])) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ms = mobile_new((char *)argv[0]);
		if (!ms) {
			vty_out(vty, "Failed to add MS name '%s'%s", argv[0],
				VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	vty->index = ms;
	vty->node = MS_NODE;

	vty_out(vty, "MS '%s' created, after configuration, do 'no shutdown'%s",
		argv[0], VTY_NEWLINE);
	return CMD_SUCCESS;
}

DEFUN(cfg_ms_rename, cfg_ms_rename_cmd, "ms MS_NAME rename MS_NAME",
	"Select a mobile station to configure\nName of MS (see \"show ms\")\n"
	"Rename MS\nNew name of MS")
{
	struct osmocom_ms *ms;
	int found = 0;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, argv[0])) {
			found = 1;
			break;
		}
	}

	if (!found) {
		vty_out(vty, "MS name '%s' does not exist%s", argv[0],
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	strncpy(ms->name, argv[1], sizeof(ms->name) - 1);

	return CMD_SUCCESS;
}

DEFUN(cfg_no_ms, cfg_no_ms_cmd, "no ms MS_NAME",
	NO_STR "Select a mobile station to remove\n"
	"Name of MS (see \"show ms\")")
{
	struct osmocom_ms *ms;
	int found = 0;

	llist_for_each_entry(ms, &ms_list, entity) {
		if (!strcmp(ms->name, argv[0])) {
			found = 1;
			break;
		}
	}

	if (!found) {
		vty_out(vty, "MS name '%s' does not exist%s", argv[0],
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	mobile_delete(ms, 1);

	return CMD_SUCCESS;
}

#define SUP_WRITE(item, cmd) \
	if (sup->item) \
		if (!hide_default || !set->item) \
			vty_out(vty, "  %s%s%s", (set->item) ? "" : "no ", \
			cmd, VTY_NEWLINE);

static void config_write_ms(struct vty *vty, struct osmocom_ms *ms)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;
	struct gsm_settings_abbrev *abbrev;

	vty_out(vty, "ms %s%s", ms->name, VTY_NEWLINE);
	vty_out(vty, " layer2-socket %s%s", set->layer2_socket_path,
		VTY_NEWLINE);
	vty_out(vty, " sap-socket %s%s", set->sap_socket_path, VTY_NEWLINE);
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
		case GSM_SIM_TYPE_SAP:
		vty_out(vty, " sim sap%s", VTY_NEWLINE);
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
		if (!hide_default)
			vty_out(vty, " imei-fixed%s", VTY_NEWLINE);
	if (set->emergency_imsi[0])
		vty_out(vty, " emergency-imsi %s%s", set->emergency_imsi,
			VTY_NEWLINE);
	else
		if (!hide_default)
			vty_out(vty, " no emergency-imsi%s", VTY_NEWLINE);
	if (set->sms_sca[0])
		vty_out(vty, " sms-service-center %s%s", set->sms_sca,
			VTY_NEWLINE);
	else
		if (!hide_default)
			vty_out(vty, " no sms-service-center%s", VTY_NEWLINE);
	if (!hide_default || set->cw)
		vty_out(vty, " %scall-waiting%s", (set->cw) ? "" : "no ",
			VTY_NEWLINE);
	if (!hide_default || set->auto_answer)
		vty_out(vty, " %sauto-answer%s",
			(set->auto_answer) ? "" : "no ", VTY_NEWLINE);
	if (!hide_default || set->force_rekey)
		vty_out(vty, " %sforce-rekey%s",
			(set->force_rekey) ? "" : "no ", VTY_NEWLINE);
	if (!hide_default || set->clip)
		vty_out(vty, " %sclip%s", (set->clip) ? "" : "no ",
			VTY_NEWLINE);
	if (!hide_default || set->clir)
		vty_out(vty, " %sclir%s", (set->clir) ? "" : "no ",
			VTY_NEWLINE);
	if (set->alter_tx_power)
		if (set->alter_tx_power_value)
			vty_out(vty, " tx-power %d%s",
				set->alter_tx_power_value, VTY_NEWLINE);
		else
			vty_out(vty, " tx-power full%s", VTY_NEWLINE);
	else
		if (!hide_default)
			vty_out(vty, " tx-power auto%s", VTY_NEWLINE);
	if (set->alter_delay)
		vty_out(vty, " simulated-delay %d%s", set->alter_delay,
			VTY_NEWLINE);
	else
		if (!hide_default)
			vty_out(vty, " no simulated-delay%s", VTY_NEWLINE);
	if (set->stick)
		vty_out(vty, " stick %d%s%s", set->stick_arfcn & 1023,
			(set->stick_arfcn & ARFCN_PCS) ? " pcs" : "",
			VTY_NEWLINE);
	else
		if (!hide_default)
			vty_out(vty, " no stick%s", VTY_NEWLINE);
	if (!hide_default || set->no_lupd)
		vty_out(vty, " %slocation-updating%s",
			(set->no_lupd) ? "no " : "", VTY_NEWLINE);
	if (!hide_default || set->no_neighbour)
		vty_out(vty, " %sneighbour-measurement%s",
			(set->no_neighbour) ? "no " : "", VTY_NEWLINE);
	if (set->full_v1 || set->full_v2 || set->full_v3) {
		/* mandatory anyway */
		vty_out(vty, " codec full-speed%s%s",
			(!set->half_prefer) ? " prefer" : "",
			VTY_NEWLINE);
	}
	if (set->half_v1 || set->half_v3) {
		if (set->half)
			vty_out(vty, " codec half-speed%s%s",
				(set->half_prefer) ? " prefer" : "",
				VTY_NEWLINE);
		else
			vty_out(vty, " no codec half-speed%s", VTY_NEWLINE);
	}
	if (llist_empty(&set->abbrev)) {
		if (!hide_default)
			vty_out(vty, " no abbrev%s", VTY_NEWLINE);
	} else {
		llist_for_each_entry(abbrev, &set->abbrev, list)
			vty_out(vty, " abbrev %s %s%s%s%s", abbrev->abbrev,
				abbrev->number, (abbrev->name[0]) ? " " : "",
				abbrev->name, VTY_NEWLINE);
	}
	vty_out(vty, " support%s", VTY_NEWLINE);
	SUP_WRITE(sms_ptp, "sms");
	SUP_WRITE(a5_1, "a5/1");
	SUP_WRITE(a5_2, "a5/2");
	SUP_WRITE(a5_3, "a5/3");
	SUP_WRITE(a5_4, "a5/4");
	SUP_WRITE(a5_5, "a5/5");
	SUP_WRITE(a5_6, "a5/6");
	SUP_WRITE(a5_7, "a5/7");
	SUP_WRITE(p_gsm, "p-gsm");
	SUP_WRITE(e_gsm, "e-gsm");
	SUP_WRITE(r_gsm, "r-gsm");
	SUP_WRITE(pcs, "gsm-850");
	SUP_WRITE(gsm_480, "gsm-480");
	SUP_WRITE(gsm_450, "gsm-450");
	SUP_WRITE(dcs, "dcs");
	SUP_WRITE(pcs, "pcs");
	if (sup->r_gsm || sup->e_gsm || sup->p_gsm)
		if (!hide_default || sup->class_900 != set->class_900)
			vty_out(vty, "  class-900 %d%s", set->class_900,
				VTY_NEWLINE);
	if (sup->gsm_850)
		if (!hide_default || sup->class_850 != set->class_850)
			vty_out(vty, "  class-850 %d%s", set->class_850,
				VTY_NEWLINE);
	if (sup->gsm_480 || sup->gsm_450)
		if (!hide_default || sup->class_400 != set->class_400)
			vty_out(vty, "  class-400 %d%s", set->class_400,
				VTY_NEWLINE);
	if (sup->dcs)
		if (!hide_default || sup->class_dcs != set->class_dcs)
			vty_out(vty, "  class-dcs %d%s", set->class_dcs,
				VTY_NEWLINE);
	if (sup->pcs)
		if (!hide_default || sup->class_pcs != set->class_pcs)
			vty_out(vty, "  class-pcs %d%s", set->class_pcs,
				VTY_NEWLINE);
	if (!hide_default || sup->ch_cap != set->ch_cap) {
		switch (set->ch_cap) {
		case GSM_CAP_SDCCH:
			vty_out(vty, "  channel-capability sdcch%s",
				VTY_NEWLINE);
			break;
		case GSM_CAP_SDCCH_TCHF:
			vty_out(vty, "  channel-capability sdcch+tchf%s",
				VTY_NEWLINE);
			break;
		case GSM_CAP_SDCCH_TCHF_TCHH:
			vty_out(vty, "  channel-capability sdcch+tchf+tchh%s",
				VTY_NEWLINE);
			break;
		}
	}
	SUP_WRITE(full_v1, "full-speech-v1");
	SUP_WRITE(full_v2, "full-speech-v2");
	SUP_WRITE(full_v3, "full-speech-v3");
	SUP_WRITE(half_v1, "half-speech-v1");
	SUP_WRITE(half_v3, "half-speech-v3");
	if (!hide_default || sup->min_rxlev_dbm != set->min_rxlev_dbm)
		vty_out(vty, "  min-rxlev %d%s", set->min_rxlev_dbm,
			VTY_NEWLINE);
	if (!hide_default || sup->dsc_max != set->dsc_max)
		vty_out(vty, "  dsc-max %d%s", set->dsc_max, VTY_NEWLINE);
	if (!hide_default || set->skip_max_per_band)
		vty_out(vty, "  %sskip-max-per-band%s",
			(set->skip_max_per_band) ? "" : "no ", VTY_NEWLINE);
	vty_out(vty, " exit%s", VTY_NEWLINE);
	vty_out(vty, " test-sim%s", VTY_NEWLINE);
	vty_out(vty, "  imsi %s%s", set->test_imsi, VTY_NEWLINE);
	switch (set->test_ki_type) {
	case OSMO_AUTH_ALG_XOR:
		vty_out(vty, "  ki xor %s%s",
			osmo_hexdump(set->test_ki, 12), VTY_NEWLINE);
		break;
	case OSMO_AUTH_ALG_COMP128v1:
		vty_out(vty, "  ki comp128 %s%s",
			osmo_hexdump(set->test_ki, 16), VTY_NEWLINE);
		break;
	}
	if (!hide_default || set->test_barr)
		vty_out(vty, "  %sbarred-access%s",
			(set->test_barr) ? "" : "no ", VTY_NEWLINE);
	if (set->test_rplmn_valid) {
		vty_out(vty, "  rplmn %s %s",
			gsm_print_mcc(set->test_rplmn_mcc),
			gsm_print_mnc(set->test_rplmn_mnc));
		if (set->test_lac > 0x0000 && set->test_lac < 0xfffe) {
			vty_out(vty, " 0x%04x", set->test_lac);
			if (set->test_tmsi != 0xffffffff) {
				vty_out(vty, " 0x%08x", set->test_tmsi);
				if (set->test_imsi_attached)
					vty_out(vty, " attached");
			}
		}
		vty_out(vty, "%s", VTY_NEWLINE);
	} else
		if (!hide_default)
			vty_out(vty, "  no rplmn%s", VTY_NEWLINE);
	if (!hide_default || set->test_always)
		vty_out(vty, "  hplmn-search %s%s",
			(set->test_always) ? "everywhere" : "foreign-country",
			VTY_NEWLINE);
	vty_out(vty, " exit%s", VTY_NEWLINE);
	/* no shutdown must be written to config, because shutdown is default */
	vty_out(vty, " %sshutdown%s", (ms->shutdown) ? "" : "no ",
		VTY_NEWLINE);
	vty_out(vty, "exit%s", VTY_NEWLINE);
	vty_out(vty, "!%s", VTY_NEWLINE);
}

static int config_write(struct vty *vty)
{
	struct osmocom_ms *ms;

#ifdef _HAVE_GPSD
	vty_out(vty, "gps host %s:%s%s", g.gpsd_host, g.gpsd_port, VTY_NEWLINE);
#endif
	vty_out(vty, "gps device %s%s", g.device, VTY_NEWLINE);
	if (g.baud)
		vty_out(vty, "gps baudrate %d%s", g.baud, VTY_NEWLINE);
	else
		vty_out(vty, "gps baudrate default%s", VTY_NEWLINE);
	vty_out(vty, "%sgps enable%s", (g.enable) ? "" : "no ", VTY_NEWLINE);
	vty_out(vty, "!%s", VTY_NEWLINE);

	vty_out(vty, "%shide-default%s", (hide_default) ? "": "no ",
		VTY_NEWLINE);
	vty_out(vty, "!%s", VTY_NEWLINE);

	llist_for_each_entry(ms, &ms_list, entity)
		config_write_ms(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_show_this, cfg_ms_show_this_cmd, "show this",
	SHOW_STR "Show config of this MS")
{
	struct osmocom_ms *ms = vty->index;

	config_write_ms(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_layer2, cfg_ms_layer2_cmd, "layer2-socket PATH",
	"Define socket path to connect between layer 2 and layer 1\n"
	"Unix socket, default '/tmp/osmocom_l2'")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	strncpy(set->layer2_socket_path, argv[0],
		sizeof(set->layer2_socket_path) - 1);

	vty_restart(vty, ms);
	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sap, cfg_ms_sap_cmd, "sap-socket PATH",
	"Define socket path to connect to SIM reader\n"
	"Unix socket, default '/tmp/osmocom_sap'")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	strncpy(set->sap_socket_path, argv[0],
		sizeof(set->sap_socket_path) - 1);

	vty_restart(vty, ms);
	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sim, cfg_ms_sim_cmd, "sim (none|reader|test|sap)",
	"Set SIM card to attach when powering on\nAttach no SIM\n"
	"Attach SIM from reader\nAttach bulit in test SIM\n"
	"Attach SIM over SAP interface")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	switch (argv[0][0]) {
	case 'n':
		set->sim_type = GSM_SIM_TYPE_NONE;
		break;
	case 'r':
		set->sim_type = GSM_SIM_TYPE_READER;
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

	vty_restart_if_started(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_mode, cfg_ms_mode_cmd, "network-selection-mode (auto|manual)",
	"Set network selection mode\nAutomatic network selection\n"
	"Manual network selection")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct msgb *nmsg;

	if (!ms->started) {
		if (argv[0][0] == 'a')
			set->plmn_mode = PLMN_MODE_AUTO;
		else
			set->plmn_mode = PLMN_MODE_MANUAL;
	} else {
		if (argv[0][0] == 'a')
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_SEL_AUTO);
		else
			nmsg = gsm322_msgb_alloc(GSM322_EVENT_SEL_MANUAL);
		if (!nmsg)
			return CMD_WARNING;
		gsm322_plmn_sendmsg(ms, nmsg);
	}

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

	strcpy(set->imei, argv[0]);
	strcpy(set->imeisv, argv[0]);
	strcpy(set->imeisv + 15, sv);

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

DEFUN(cfg_ms_emerg_imsi, cfg_ms_emerg_imsi_cmd, "emergency-imsi IMSI",
	"Use special IMSI for emergency calls\n15 digits IMSI")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	char *error;

	error = gsm_check_imsi(argv[0]);
	if (error) {
		vty_out(vty, "%s%s", error, VTY_NEWLINE);
		return CMD_WARNING;
	}
	strcpy(set->emergency_imsi, argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_emerg_imsi, cfg_ms_no_emerg_imsi_cmd, "no emergency-imsi",
	NO_STR "Use IMSI of SIM or IMEI for emergency calls")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->emergency_imsi[0] = '\0';

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sms_sca, cfg_ms_sms_sca_cmd, "sms-service-center NUMBER",
	"Use Service center address for outgoing SMS\nNumber of service center "
	"(Use digits '0123456789*#abc', and '+' to dial international)")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	const char *number = argv[0];

	if ((strlen(number) > 20 && number[0] != '+') || strlen(number) > 21) {
		vty_out(vty, "Number too long%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (vty_check_number(vty, number))
		return CMD_WARNING;

	strcpy(set->sms_sca, number);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_sms_sca, cfg_ms_no_sms_sca_cmd, "no sms-service-center",
	NO_STR "Use Service center address for outgoing SMS")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->sms_sca[0] = '\0';

	return CMD_SUCCESS;
}

DEFUN(cfg_no_cw, cfg_ms_no_cw_cmd, "no call-waiting",
	NO_STR "Disallow waiting calls")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->cw = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_cw, cfg_ms_cw_cmd, "call-waiting",
	"Allow waiting calls")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->cw = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_auto_answer, cfg_ms_no_auto_answer_cmd, "no auto-answer",
	NO_STR "Disable auto-answering calls")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->auto_answer = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_auto_answer, cfg_ms_auto_answer_cmd, "auto-answer",
	"Enable auto-answering calls")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->auto_answer = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_force_rekey, cfg_ms_no_force_rekey_cmd, "no force-rekey",
	NO_STR "Disable key renew forcing after every event")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->force_rekey = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_force_rekey, cfg_ms_force_rekey_cmd, "force-rekey",
	"Enable key renew forcing after every event")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->force_rekey = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_clip, cfg_ms_clip_cmd, "clip",
	"Force caller ID presentation")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->clip = 1;
	set->clir = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_clir, cfg_ms_clir_cmd, "clir",
	"Force caller ID restriction")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->clip = 0;
	set->clir = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_clip, cfg_ms_no_clip_cmd, "no clip",
	NO_STR "Disable forcing of caller ID presentation")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->clip = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_clir, cfg_ms_no_clir_cmd, "no clir",
	NO_STR "Disable forcing of caller ID restriction")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->clir = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_tx_power, cfg_ms_tx_power_cmd, "tx-power (auto|full)",
	"Set the way to choose transmit power\nControlled by BTS\n"
	"Always full power\nFixed GSM power value if supported")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	switch (argv[0][0]) {
	case 'a':
		set->alter_tx_power = 0;
		break;
	case 'f':
		set->alter_tx_power = 1;
		set->alter_tx_power_value = 0;
		break;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_tx_power_val, cfg_ms_tx_power_val_cmd, "tx-power <0-31>",
	"Set the way to choose transmit power\n"
	"Fixed GSM power value if supported")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->alter_tx_power = 1;
	set->alter_tx_power_value = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sim_delay, cfg_ms_sim_delay_cmd, "simulated-delay <-128-127>",
	"Simulate a lower or higher distance from the BTS\n"
	"Delay in half bits (distance in 553.85 meter steps)")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->alter_delay = atoi(argv[0]);
	gsm48_rr_alter_delay(ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_sim_delay, cfg_ms_no_sim_delay_cmd, "no simulated-delay",
	NO_STR "Do not simulate a lower or higher distance from the BTS")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->alter_delay = 0;
	gsm48_rr_alter_delay(ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_stick, cfg_ms_stick_cmd, "stick <0-1023> [pcs]",
	"Stick to the given cell\nARFCN of the cell to stick to\n"
	"Given frequency is PCS band (1900) rather than DCS band.")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	uint16_t arfcn = atoi(argv[0]);

	if (argc > 1) {
		if (arfcn < 512 || arfcn > 810) {
			vty_out(vty, "Given ARFCN not in PCS band%s",
				VTY_NEWLINE);
			return CMD_WARNING;
		}
		arfcn |= ARFCN_PCS;
	}
	set->stick = 1;
	set->stick_arfcn = arfcn;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_stick, cfg_ms_no_stick_cmd, "no stick",
	NO_STR "Do not stick to any cell")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->stick = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_lupd, cfg_ms_lupd_cmd, "location-updating",
	"Allow location updating")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->no_lupd = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_lupd, cfg_ms_no_lupd_cmd, "no location-updating",
	NO_STR "Do not allow location updating")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->no_lupd = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_codec_full, cfg_ms_codec_full_cmd, "codec full-speed",
	"Enable codec\nFull speed speech codec")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	if (!set->full_v1 && !set->full_v2 && !set->full_v3) {
		vty_out(vty, "Full-rate codec not supported%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_codec_full_pref, cfg_ms_codec_full_pref_cmd, "codec full-speed "
	"prefer",
	"Enable codec\nFull speed speech codec\nPrefer this codec")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	if (!set->full_v1 && !set->full_v2 && !set->full_v3) {
		vty_out(vty, "Full-rate codec not supported%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	set->half_prefer = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_codec_half, cfg_ms_codec_half_cmd, "codec half-speed",
	"Enable codec\nHalf speed speech codec")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	if (!set->half_v1 && !set->half_v3) {
		vty_out(vty, "Half-rate codec not supported%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	set->half = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_codec_half_pref, cfg_ms_codec_half_pref_cmd, "codec half-speed "
	"prefer",
	"Enable codec\nHalf speed speech codec\nPrefer this codec")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	if (!set->half_v1 && !set->half_v3) {
		vty_out(vty, "Half-rate codec not supported%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	set->half = 1;
	set->half_prefer = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_no_codec_half, cfg_ms_no_codec_half_cmd, "no codec half-speed",
	NO_STR "Disable codec\nHalf speed speech codec")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	if (!set->half_v1 && !set->half_v3) {
		vty_out(vty, "Half-rate codec not supported%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	set->half = 0;
	set->half_prefer = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_abbrev, cfg_ms_abbrev_cmd, "abbrev ABBREVIATION NUMBER [NAME]",
	"Store given abbreviation number\n1-3 digits abbreviation\n"
	"Number to store for the abbreviation "
	"(Use digits '0123456789*#abc', and '+' to dial international)\n"
	"Name of the abbreviation")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_settings_abbrev *abbrev;
	int i;

	llist_for_each_entry(abbrev, &set->abbrev, list) {
		if (!strcmp(argv[0], abbrev->abbrev)) {
			vty_out(vty, "Given abbreviation '%s' already stored, "
				"delete first!%s", argv[0], VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	if (strlen(argv[0]) >= sizeof(abbrev->abbrev)) {
		vty_out(vty, "Given abbreviation too long%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	for (i = 0; i < strlen(argv[0]); i++) {
		if (argv[0][i] < '0' || argv[0][i] > '9') {
			vty_out(vty, "Given abbreviation must have digits "
				"0..9 only!%s", VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	if (vty_check_number(vty, argv[1]))
		return CMD_WARNING;

	abbrev = talloc_zero(l23_ctx, struct gsm_settings_abbrev);
	if (!abbrev) {
		vty_out(vty, "No Memory!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	llist_add_tail(&abbrev->list, &set->abbrev);
	strncpy(abbrev->abbrev, argv[0], sizeof(abbrev->abbrev) - 1);
	strncpy(abbrev->number, argv[1], sizeof(abbrev->number) - 1);
	if (argc >= 3)
		strncpy(abbrev->name, argv[2], sizeof(abbrev->name) - 1);

	return CMD_SUCCESS;
}

DEFUN(cfg_no_abbrev, cfg_ms_no_abbrev_cmd, "no abbrev [ABBREVIATION]",
	NO_STR "Remove given abbreviation number or all numbers\n"
	"Abbreviation number to remove")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_settings_abbrev *abbrev, *abbrev2;
	uint8_t deleted = 0;

	llist_for_each_entry_safe(abbrev, abbrev2, &set->abbrev, list) {
		if (argc < 1 || !strcmp(argv[0], abbrev->abbrev)) {
			llist_del(&abbrev->list);
			deleted = 1;
		}
	}

	if (argc >= 1 && !deleted) {
		vty_out(vty, "Given abbreviation '%s' not found!%s",
			argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_neighbour, cfg_ms_neighbour_cmd, "neighbour-measurement",
	"Allow neighbour cell measurement in idle mode")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->no_neighbour = 0;

	vty_restart_if_started(vty, ms);


	return CMD_SUCCESS;
}

DEFUN(cfg_ms_no_neighbour, cfg_ms_no_neighbour_cmd, "no neighbour-measurement",
	NO_STR "Do not allow neighbour cell measurement in idle mode")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->no_neighbour = 1;

	vty_restart_if_started(vty, ms);

	return CMD_SUCCESS;
}

static int config_write_dummy(struct vty *vty)
{
	return CMD_SUCCESS;
}

/* per support config */
DEFUN(cfg_ms_support, cfg_ms_support_cmd, "support",
	"Define supported features")
{
	vty->node = SUPPORT_NODE;

	return CMD_SUCCESS;
}

#define SUP_EN(cfg, cfg_cmd, item, cmd, desc, restart) \
DEFUN(cfg, cfg_cmd, cmd, "Enable " desc "support") \
{ \
	struct osmocom_ms *ms = vty->index; \
	struct gsm_settings *set = &ms->settings; \
	struct gsm_support *sup = &ms->support; \
	if (!sup->item) { \
		vty_out(vty, desc " not supported%s", VTY_NEWLINE); \
		if (vty_reading) \
			return CMD_SUCCESS; \
		return CMD_WARNING; \
	} \
	if (restart) \
		vty_restart(vty, ms); \
	set->item = 1; \
	return CMD_SUCCESS; \
}

#define SUP_DI(cfg, cfg_cmd, item, cmd, desc, restart) \
DEFUN(cfg, cfg_cmd, "no " cmd, NO_STR "Disable " desc " support") \
{ \
	struct osmocom_ms *ms = vty->index; \
	struct gsm_settings *set = &ms->settings; \
	struct gsm_support *sup = &ms->support; \
	if (!sup->item) { \
		vty_out(vty, desc " not supported%s", VTY_NEWLINE); \
		if (vty_reading) \
			return CMD_SUCCESS; \
		return CMD_WARNING; \
	} \
	if (restart) \
		vty_restart(vty, ms); \
	set->item = 0; \
	return CMD_SUCCESS; \
}

#define SET_EN(cfg, cfg_cmd, item, cmd, desc, restart) \
DEFUN(cfg, cfg_cmd, cmd, "Enable " desc "support") \
{ \
	struct osmocom_ms *ms = vty->index; \
	struct gsm_settings *set = &ms->settings; \
	if (restart) \
		vty_restart(vty, ms); \
	set->item = 1; \
	return CMD_SUCCESS; \
}

#define SET_DI(cfg, cfg_cmd, item, cmd, desc, restart) \
DEFUN(cfg, cfg_cmd, "no " cmd, NO_STR "Disable " desc " support") \
{ \
	struct osmocom_ms *ms = vty->index; \
	struct gsm_settings *set = &ms->settings; \
	if (restart) \
		vty_restart(vty, ms); \
	set->item = 0; \
	return CMD_SUCCESS; \
}

SET_EN(cfg_ms_sup_dtmf, cfg_ms_sup_dtmf_cmd, cc_dtmf, "dtmf", "DTMF", 0);
SET_DI(cfg_ms_sup_no_dtmf, cfg_ms_sup_no_dtmf_cmd, cc_dtmf, "dtmf", "DTMF", 0);
SUP_EN(cfg_ms_sup_sms, cfg_ms_sup_sms_cmd, sms_ptp, "sms", "SMS", 0);
SUP_DI(cfg_ms_sup_no_sms, cfg_ms_sup_no_sms_cmd, sms_ptp, "sms", "SMS", 0);
SUP_EN(cfg_ms_sup_a5_1, cfg_ms_sup_a5_1_cmd, a5_1, "a5/1", "A5/1", 0);
SUP_DI(cfg_ms_sup_no_a5_1, cfg_ms_sup_no_a5_1_cmd, a5_1, "a5/1", "A5/1", 0);
SUP_EN(cfg_ms_sup_a5_2, cfg_ms_sup_a5_2_cmd, a5_2, "a5/2", "A5/2", 0);
SUP_DI(cfg_ms_sup_no_a5_2, cfg_ms_sup_no_a5_2_cmd, a5_2, "a5/2", "A5/2", 0);
SUP_EN(cfg_ms_sup_a5_3, cfg_ms_sup_a5_3_cmd, a5_3, "a5/3", "A5/3", 0);
SUP_DI(cfg_ms_sup_no_a5_3, cfg_ms_sup_no_a5_3_cmd, a5_3, "a5/3", "A5/3", 0);
SUP_EN(cfg_ms_sup_a5_4, cfg_ms_sup_a5_4_cmd, a5_4, "a5/4", "A5/4", 0);
SUP_DI(cfg_ms_sup_no_a5_4, cfg_ms_sup_no_a5_4_cmd, a5_4, "a5/4", "A5/4", 0);
SUP_EN(cfg_ms_sup_a5_5, cfg_ms_sup_a5_5_cmd, a5_5, "a5/5", "A5/5", 0);
SUP_DI(cfg_ms_sup_no_a5_5, cfg_ms_sup_no_a5_5_cmd, a5_5, "a5/5", "A5/5", 0);
SUP_EN(cfg_ms_sup_a5_6, cfg_ms_sup_a5_6_cmd, a5_6, "a5/6", "A5/6", 0);
SUP_DI(cfg_ms_sup_no_a5_6, cfg_ms_sup_no_a5_6_cmd, a5_6, "a5/6", "A5/6", 0);
SUP_EN(cfg_ms_sup_a5_7, cfg_ms_sup_a5_7_cmd, a5_7, "a5/7", "A5/7", 0);
SUP_DI(cfg_ms_sup_no_a5_7, cfg_ms_sup_no_a5_7_cmd, a5_7, "a5/7", "A5/7", 0);
SUP_EN(cfg_ms_sup_p_gsm, cfg_ms_sup_p_gsm_cmd, p_gsm, "p-gsm", "P-GSM (900)",
	1);
SUP_DI(cfg_ms_sup_no_p_gsm, cfg_ms_sup_no_p_gsm_cmd, p_gsm, "p-gsm",
	"P-GSM (900)", 1);
SUP_EN(cfg_ms_sup_e_gsm, cfg_ms_sup_e_gsm_cmd, e_gsm, "e-gsm", "E-GSM (850)",
	1);
SUP_DI(cfg_ms_sup_no_e_gsm, cfg_ms_sup_no_e_gsm_cmd, e_gsm, "e-gsm",
	"E-GSM (850)", 1);
SUP_EN(cfg_ms_sup_r_gsm, cfg_ms_sup_r_gsm_cmd, r_gsm, "r-gsm", "R-GSM (850)",
	1);
SUP_DI(cfg_ms_sup_no_r_gsm, cfg_ms_sup_no_r_gsm_cmd, r_gsm, "r-gsm",
	"R-GSM (850)", 1);
SUP_EN(cfg_ms_sup_dcs, cfg_ms_sup_dcs_cmd, dcs, "dcs", "DCS (1800)", 1);
SUP_DI(cfg_ms_sup_no_dcs, cfg_ms_sup_no_dcs_cmd, dcs, "dcs", "DCS (1800)", 1);
SUP_EN(cfg_ms_sup_gsm_850, cfg_ms_sup_gsm_850_cmd, gsm_850, "gsm-850",
	"GSM 850", 1);
SUP_DI(cfg_ms_sup_no_gsm_850, cfg_ms_sup_no_gsm_850_cmd, gsm_850, "gsm-850",
	"GSM 850", 1);
SUP_EN(cfg_ms_sup_pcs, cfg_ms_sup_pcs_cmd, pcs, "pcs", "PCS (1900)", 1);
SUP_DI(cfg_ms_sup_no_pcs, cfg_ms_sup_no_pcs_cmd, pcs, "pcs", "PCS (1900)", 1);
SUP_EN(cfg_ms_sup_gsm_480, cfg_ms_sup_gsm_480_cmd, gsm_480, "gsm-480",
	"GSM 480", 1);
SUP_DI(cfg_ms_sup_no_gsm_480, cfg_ms_sup_no_gsm_480_cmd, gsm_480, "gsm-480",
	"GSM 480", 1);
SUP_EN(cfg_ms_sup_gsm_450, cfg_ms_sup_gsm_450_cmd, gsm_450, "gsm-450",
	"GSM 450", 1);
SUP_DI(cfg_ms_sup_no_gsm_450, cfg_ms_sup_no_gsm_450_cmd, gsm_450, "gsm-450",
	"GSM 450", 1);

DEFUN(cfg_ms_sup_class_900, cfg_ms_sup_class_900_cmd, "class-900 (1|2|3|4|5)",
	"Select power class for GSM 900\n"
	"20 Watts\n"
	"8 Watts\n"
	"5 Watts\n"
	"2 Watts\n"
	"0.8 Watts")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	set->class_900 = atoi(argv[0]);

	if (set->class_900 < sup->class_900 && !vty_reading)
		vty_out(vty, "Note: You selected a higher class than supported "
			" by hardware!%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_class_850, cfg_ms_sup_class_850_cmd, "class-850 (1|2|3|4|5)",
	"Select power class for GSM 850\n"
	"20 Watts\n"
	"8 Watts\n"
	"5 Watts\n"
	"2 Watts\n"
	"0.8 Watts")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	set->class_850 = atoi(argv[0]);

	if (set->class_850 < sup->class_850 && !vty_reading)
		vty_out(vty, "Note: You selected a higher class than supported "
			" by hardware!%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_class_400, cfg_ms_sup_class_400_cmd, "class-400 (1|2|3|4|5)",
	"Select power class for GSM 400 (480 and 450)\n"
	"20 Watts\n"
	"8 Watts\n"
	"5 Watts\n"
	"2 Watts\n"
	"0.8 Watts")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	set->class_400 = atoi(argv[0]);

	if (set->class_400 < sup->class_400 && !vty_reading)
		vty_out(vty, "Note: You selected a higher class than supported "
			" by hardware!%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_class_dcs, cfg_ms_sup_class_dcs_cmd, "class-dcs (1|2|3)",
	"Select power class for DCS 1800\n"
	"1 Watt\n"
	"0.25 Watts\n"
	"4 Watts")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	set->class_dcs = atoi(argv[0]);

	if (((set->class_dcs + 1) & 3) < ((sup->class_dcs + 1) & 3)
	 && !vty_reading)
		vty_out(vty, "Note: You selected a higher class than supported "
			" by hardware!%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_class_pcs, cfg_ms_sup_class_pcs_cmd, "class-pcs (1|2|3)",
	"Select power class for PCS 1900\n"
	"1 Watt\n"
	"0.25 Watts\n"
	"2 Watts")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;

	set->class_pcs = atoi(argv[0]);

	if (((set->class_pcs + 1) & 3) < ((sup->class_pcs + 1) & 3)
	 && !vty_reading)
		vty_out(vty, "Note: You selected a higher class than supported "
			" by hardware!%s", VTY_NEWLINE);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_ch_cap, cfg_ms_sup_ch_cap_cmd, "channel-capability "
	"(sdcch|sdcch+tchf|sdcch+tchf+tchh)",
	"Select channel capability\nSDCCH only\nSDCCH + TCH/F\nSDCCH + TCH/H")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	struct gsm_support *sup = &ms->support;
	uint8_t ch_cap;

	if (!strcmp(argv[0], "sdcch+tchf+tchh"))
		ch_cap = GSM_CAP_SDCCH_TCHF_TCHH;
	else if (!strcmp(argv[0], "sdcch+tchf"))
		ch_cap = GSM_CAP_SDCCH_TCHF;
	else
		ch_cap = GSM_CAP_SDCCH;

	if (ch_cap > sup->ch_cap && !vty_reading) {
		vty_out(vty, "You selected an higher capability than supported "
			" by hardware!%s", VTY_NEWLINE);
		return CMD_WARNING;
	}

	if (ms->started && ch_cap != set->ch_cap
	 && (ch_cap == GSM_CAP_SDCCH || set->ch_cap == GSM_CAP_SDCCH))
		vty_restart_if_started(vty, ms);

	set->ch_cap = ch_cap;

	return CMD_SUCCESS;
}

SUP_EN(cfg_ms_sup_full_v1, cfg_ms_sup_full_v1_cmd, full_v1, "full-speech-v1",
	"Full rate speech V1", 0);
SUP_DI(cfg_ms_sup_no_full_v1, cfg_ms_sup_no_full_v1_cmd, full_v1,
	"full-speech-v1", "Full rate speech V1", 0);
SUP_EN(cfg_ms_sup_full_v2, cfg_ms_sup_full_v2_cmd, full_v2, "full-speech-v2",
	"Full rate speech V2 (EFR)", 0);
SUP_DI(cfg_ms_sup_no_full_v2, cfg_ms_sup_no_full_v2_cmd, full_v2,
	"full-speech-v2", "Full rate speech V2 (EFR)", 0);
SUP_EN(cfg_ms_sup_full_v3, cfg_ms_sup_full_v3_cmd, full_v3, "full-speech-v3",
	"Full rate speech V3 (AMR)", 0);
SUP_DI(cfg_ms_sup_no_full_v3, cfg_ms_sup_no_full_v3_cmd, full_v3,
	"full-speech-v3", "Full rate speech V3 (AMR)", 0);
SUP_EN(cfg_ms_sup_half_v1, cfg_ms_sup_half_v1_cmd, half_v1, "half-speech-v1",
	"Half rate speech V1", 0);
SUP_DI(cfg_ms_sup_no_half_v1, cfg_ms_sup_no_half_v1_cmd, half_v1,
	"half-speech-v1", "Half rate speech V1", 0);
SUP_EN(cfg_ms_sup_half_v3, cfg_ms_sup_half_v3_cmd, half_v3, "half-speech-v3",
	"Half rate speech V3 (AMR)", 0);
SUP_DI(cfg_ms_sup_no_half_v3, cfg_ms_sup_no_half_v3_cmd, half_v3,
	"half-speech-v3", "Half rate speech V3 (AMR)", 0);

DEFUN(cfg_ms_sup_min_rxlev, cfg_ms_sup_min_rxlev_cmd, "min-rxlev <-110--47>",
	"Set the minimum receive level to select a cell\n"
	"Minimum receive level from -110 dBm to -47 dBm")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->min_rxlev_dbm = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_dsc_max, cfg_ms_sup_dsc_max_cmd, "dsc-max <90-500>",
	"Set the maximum DSC value. Standard is 90. Increase to make mobile "
	"more reliable against bad RX signal. This increase the propability "
	"of missing a paging requests\n"
	"DSC initial and maximum value (standard is 90)")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->dsc_max = atoi(argv[0]);

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_skip_max_per_band, cfg_ms_sup_skip_max_per_band_cmd,
	"skip-max-per-band",
	"Scan all frequencies per band, not only a maximum number")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->skip_max_per_band = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_ms_sup_no_skip_max_per_band, cfg_ms_sup_no_skip_max_per_band_cmd,
	"no skip-max-per-band",
	NO_STR "Scan only a maximum number of frequencies per band")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->skip_max_per_band = 0;

	return CMD_SUCCESS;
}

/* per testsim config */
DEFUN(cfg_ms_testsim, cfg_ms_testsim_cmd, "test-sim",
	"Configure test SIM emulation")
{
	vty->node = TESTSIM_NODE;

	return CMD_SUCCESS;
}

DEFUN(cfg_test_imsi, cfg_test_imsi_cmd, "imsi IMSI",
	"Set IMSI on test card\n15 digits IMSI")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	char *error = gsm_check_imsi(argv[0]);

	if (error) {
		vty_out(vty, "%s%s", error, VTY_NEWLINE);
		return CMD_WARNING;
	}

	strcpy(set->test_imsi, argv[0]);

	vty_restart_if_started(vty, ms);

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

	set->test_ki_type = OSMO_AUTH_ALG_XOR;
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

	set->test_ki_type = OSMO_AUTH_ALG_COMP128v1;
	memcpy(set->test_ki, ki, 16);
	return CMD_SUCCESS;
}

DEFUN(cfg_test_barr, cfg_test_barr_cmd, "barred-access",
	"Allow access to barred cells")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->test_barr = 1;

	return CMD_SUCCESS;
}

DEFUN(cfg_test_no_barr, cfg_test_no_barr_cmd, "no barred-access",
	NO_STR "Deny access to barred cells")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->test_barr = 0;

	return CMD_SUCCESS;
}

DEFUN(cfg_test_no_rplmn, cfg_test_no_rplmn_cmd, "no rplmn",
	NO_STR "Unset Registered PLMN")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	set->test_rplmn_valid = 0;

	vty_restart_if_started(vty, ms);

	return CMD_SUCCESS;
}

static int _test_rplmn_cmd(struct vty *vty, int argc, const char *argv[],
	int attached)
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;
	uint16_t mcc = gsm_input_mcc((char *)argv[0]),
		 mnc = gsm_input_mnc((char *)argv[1]);

	if (mcc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MCC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	if (mnc == GSM_INPUT_INVALID) {
		vty_out(vty, "Given MNC invalid%s", VTY_NEWLINE);
		return CMD_WARNING;
	}
	set->test_rplmn_valid = 1;
	set->test_rplmn_mcc = mcc;
	set->test_rplmn_mnc = mnc;

	if (argc >= 3)
		set->test_lac = strtoul(argv[2], NULL, 16);
	else
		set->test_lac = 0xfffe;

	if (argc >= 4)
		set->test_tmsi = strtoul(argv[3], NULL, 16);
	else
		set->test_tmsi = 0xffffffff;

	if (attached)
		set->test_imsi_attached = 1;
	else
		set->test_imsi_attached = 0;

	vty_restart_if_started(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_test_rplmn, cfg_test_rplmn_cmd,
	"rplmn MCC MNC [LAC] [TMSI]",
	"Set Registered PLMN\nMobile Country Code\nMobile Network Code\n"
	"Optionally set location area code\n"
	"Optionally set current assigned TMSI")
{
	return _test_rplmn_cmd(vty, argc, argv, 0);
}

DEFUN(cfg_test_rplmn_att, cfg_test_rplmn_att_cmd,
	"rplmn MCC MNC LAC TMSI attached",
	"Set Registered PLMN\nMobile Country Code\nMobile Network Code\n"
	"Set location area code\nSet current assigned TMSI\n"
	"Indicate to MM that card is already attached")
{
	return _test_rplmn_cmd(vty, argc, argv, 1);
}

DEFUN(cfg_test_hplmn, cfg_test_hplmn_cmd, "hplmn-search (everywhere|foreign-country)",
	"Set Home PLMN search mode\n"
	"Search for HPLMN when on any other network\n"
	"Search for HPLMN when in a different country")
{
	struct osmocom_ms *ms = vty->index;
	struct gsm_settings *set = &ms->settings;

	switch (argv[0][0]) {
	case 'e':
		set->test_always = 1;
		break;
	case 'f':
		set->test_always = 0;
		break;
	}

	vty_restart_if_started(vty, ms);

	return CMD_SUCCESS;
}

DEFUN(cfg_no_shutdown, cfg_ms_no_shutdown_cmd, "no shutdown",
	NO_STR "Activate and run MS")
{
	struct osmocom_ms *ms = vty->index, *tmp;
	int rc;

	if (ms->shutdown != 3)
		return CMD_SUCCESS;

	llist_for_each_entry(tmp, &ms_list, entity) {
		if (tmp->shutdown == 3)
			continue;
		if (!strcmp(ms->settings.layer2_socket_path,
				tmp->settings.layer2_socket_path)) {
			vty_out(vty, "Cannot start MS '%s', because MS '%s' "
				"use the same layer2-socket.%sPlease shutdown "
				"MS '%s' first.%s", ms->name, tmp->name,
				VTY_NEWLINE, tmp->name, VTY_NEWLINE);
			return CMD_WARNING;
		}
		if (!strcmp(ms->settings.sap_socket_path,
				tmp->settings.sap_socket_path)) {
			vty_out(vty, "Cannot start MS '%s', because MS '%s' "
				"use the same sap-socket.%sPlease shutdown "
				"MS '%s' first.%s", ms->name, tmp->name,
				VTY_NEWLINE, tmp->name, VTY_NEWLINE);
			return CMD_WARNING;
		}
	}

	rc = mobile_init(ms);
	if (rc < 0) {
		vty_out(vty, "Connection to layer 1 failed!%s",
			VTY_NEWLINE);
		return CMD_WARNING;
	}

	return CMD_SUCCESS;
}

DEFUN(cfg_shutdown, cfg_ms_shutdown_cmd, "shutdown",
	"Shut down and deactivate MS")
{
	struct osmocom_ms *ms = vty->index;

	if (ms->shutdown == 0)
		mobile_exit(ms, 0);

	return CMD_SUCCESS;
}

DEFUN(cfg_shutdown_force, cfg_ms_shutdown_force_cmd, "shutdown force",
	"Shut down and deactivate MS\nDo not perform IMSI detach")
{
	struct osmocom_ms *ms = vty->index;

	if (ms->shutdown <= 1)
		mobile_exit(ms, 1);

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
	case SUPPORT_NODE:
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
	case SUPPORT_NODE:
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
	case SUPPORT_NODE:
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

DEFUN(off, off_cmd, "off",
	"Turn mobiles off (shutdown) and exit")
{
	osmo_signal_dispatch(SS_GLOBAL, S_GLOBAL_SHUTDOWN, NULL);

	return CMD_SUCCESS;
}

#define SUP_NODE(item) \
	install_element(SUPPORT_NODE, &cfg_ms_sup_item_cmd);

int ms_vty_init(void)
{
	install_element_ve(&show_ms_cmd);
	install_element_ve(&show_subscr_cmd);
	install_element_ve(&show_support_cmd);
	install_element_ve(&show_cell_cmd);
	install_element_ve(&show_cell_si_cmd);
	install_element_ve(&show_nbcells_cmd);
	install_element_ve(&show_ba_cmd);
	install_element_ve(&show_forb_la_cmd);
	install_element_ve(&show_forb_plmn_cmd);
	install_element_ve(&monitor_network_cmd);
	install_element_ve(&no_monitor_network_cmd);
	install_element(ENABLE_NODE, &off_cmd);

	install_element(ENABLE_NODE, &sim_test_cmd);
	install_element(ENABLE_NODE, &sim_test_att_cmd);
	install_element(ENABLE_NODE, &sim_sap_cmd);
	install_element(ENABLE_NODE, &sim_reader_cmd);
	install_element(ENABLE_NODE, &sim_remove_cmd);
	install_element(ENABLE_NODE, &sim_pin_cmd);
	install_element(ENABLE_NODE, &sim_disable_pin_cmd);
	install_element(ENABLE_NODE, &sim_enable_pin_cmd);
	install_element(ENABLE_NODE, &sim_change_pin_cmd);
	install_element(ENABLE_NODE, &sim_unblock_pin_cmd);
	install_element(ENABLE_NODE, &sim_lai_cmd);
	install_element(ENABLE_NODE, &network_search_cmd);
	install_element(ENABLE_NODE, &network_show_cmd);
	install_element(ENABLE_NODE, &network_select_cmd);
	install_element(ENABLE_NODE, &call_cmd);
	install_element(ENABLE_NODE, &call_retr_cmd);
	install_element(ENABLE_NODE, &call_dtmf_cmd);
	install_element(ENABLE_NODE, &sms_cmd);
	install_element(ENABLE_NODE, &service_cmd);
	install_element(ENABLE_NODE, &test_reselection_cmd);
	install_element(ENABLE_NODE, &delete_forbidden_plmn_cmd);

#ifdef _HAVE_GPSD
	install_element(CONFIG_NODE, &cfg_gps_host_cmd);
#endif
	install_element(CONFIG_NODE, &cfg_gps_device_cmd);
	install_element(CONFIG_NODE, &cfg_gps_baud_cmd);
	install_element(CONFIG_NODE, &cfg_gps_enable_cmd);
	install_element(CONFIG_NODE, &cfg_no_gps_enable_cmd);

	install_element(CONFIG_NODE, &cfg_hide_default_cmd);
	install_element(CONFIG_NODE, &cfg_no_hide_default_cmd);

	install_element(CONFIG_NODE, &cfg_ms_cmd);
	install_element(CONFIG_NODE, &cfg_ms_create_cmd);
	install_element(CONFIG_NODE, &cfg_ms_rename_cmd);
	install_element(CONFIG_NODE, &cfg_no_ms_cmd);
	install_element(CONFIG_NODE, &ournode_end_cmd);
	install_node(&ms_node, config_write);
	install_default(MS_NODE);
	install_element(MS_NODE, &ournode_exit_cmd);
	install_element(MS_NODE, &ournode_end_cmd);
	install_element(MS_NODE, &cfg_ms_show_this_cmd);
	install_element(MS_NODE, &cfg_ms_layer2_cmd);
	install_element(MS_NODE, &cfg_ms_sap_cmd);
	install_element(MS_NODE, &cfg_ms_sim_cmd);
	install_element(MS_NODE, &cfg_ms_mode_cmd);
	install_element(MS_NODE, &cfg_ms_imei_cmd);
	install_element(MS_NODE, &cfg_ms_imei_fixed_cmd);
	install_element(MS_NODE, &cfg_ms_imei_random_cmd);
	install_element(MS_NODE, &cfg_ms_no_emerg_imsi_cmd);
	install_element(MS_NODE, &cfg_ms_emerg_imsi_cmd);
	install_element(MS_NODE, &cfg_ms_no_sms_sca_cmd);
	install_element(MS_NODE, &cfg_ms_sms_sca_cmd);
	install_element(MS_NODE, &cfg_ms_cw_cmd);
	install_element(MS_NODE, &cfg_ms_no_cw_cmd);
	install_element(MS_NODE, &cfg_ms_auto_answer_cmd);
	install_element(MS_NODE, &cfg_ms_no_auto_answer_cmd);
	install_element(MS_NODE, &cfg_ms_force_rekey_cmd);
	install_element(MS_NODE, &cfg_ms_no_force_rekey_cmd);
	install_element(MS_NODE, &cfg_ms_clip_cmd);
	install_element(MS_NODE, &cfg_ms_clir_cmd);
	install_element(MS_NODE, &cfg_ms_no_clip_cmd);
	install_element(MS_NODE, &cfg_ms_no_clir_cmd);
	install_element(MS_NODE, &cfg_ms_tx_power_cmd);
	install_element(MS_NODE, &cfg_ms_tx_power_val_cmd);
	install_element(MS_NODE, &cfg_ms_sim_delay_cmd);
	install_element(MS_NODE, &cfg_ms_no_sim_delay_cmd);
	install_element(MS_NODE, &cfg_ms_stick_cmd);
	install_element(MS_NODE, &cfg_ms_no_stick_cmd);
	install_element(MS_NODE, &cfg_ms_lupd_cmd);
	install_element(MS_NODE, &cfg_ms_no_lupd_cmd);
	install_element(MS_NODE, &cfg_ms_codec_full_cmd);
	install_element(MS_NODE, &cfg_ms_codec_full_pref_cmd);
	install_element(MS_NODE, &cfg_ms_codec_half_cmd);
	install_element(MS_NODE, &cfg_ms_codec_half_pref_cmd);
	install_element(MS_NODE, &cfg_ms_no_codec_half_cmd);
	install_element(MS_NODE, &cfg_ms_abbrev_cmd);
	install_element(MS_NODE, &cfg_ms_no_abbrev_cmd);
	install_element(MS_NODE, &cfg_ms_testsim_cmd);
	install_element(MS_NODE, &cfg_ms_neighbour_cmd);
	install_element(MS_NODE, &cfg_ms_no_neighbour_cmd);
	install_element(MS_NODE, &cfg_ms_support_cmd);
	install_node(&support_node, config_write_dummy);
	install_default(SUPPORT_NODE);
	install_element(SUPPORT_NODE, &ournode_exit_cmd);
	install_element(SUPPORT_NODE, &ournode_end_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_dtmf_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_dtmf_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_sms_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_sms_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_a5_1_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_a5_1_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_a5_2_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_a5_2_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_a5_3_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_a5_3_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_a5_4_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_a5_4_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_a5_5_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_a5_5_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_a5_6_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_a5_6_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_a5_7_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_a5_7_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_p_gsm_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_p_gsm_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_e_gsm_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_e_gsm_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_r_gsm_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_r_gsm_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_dcs_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_dcs_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_gsm_850_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_gsm_850_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_pcs_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_pcs_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_gsm_480_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_gsm_480_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_gsm_450_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_gsm_450_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_class_900_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_class_dcs_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_class_850_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_class_pcs_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_class_400_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_ch_cap_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_full_v1_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_full_v1_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_full_v2_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_full_v2_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_full_v3_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_full_v3_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_half_v1_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_half_v1_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_half_v3_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_half_v3_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_min_rxlev_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_dsc_max_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_skip_max_per_band_cmd);
	install_element(SUPPORT_NODE, &cfg_ms_sup_no_skip_max_per_band_cmd);
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
	install_element(TESTSIM_NODE, &cfg_test_rplmn_att_cmd);
	install_element(TESTSIM_NODE, &cfg_test_hplmn_cmd);
	install_element(MS_NODE, &cfg_ms_shutdown_cmd);
	install_element(MS_NODE, &cfg_ms_shutdown_force_cmd);
	install_element(MS_NODE, &cfg_ms_no_shutdown_cmd);

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

