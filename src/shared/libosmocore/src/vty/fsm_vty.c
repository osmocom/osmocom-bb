/* Osmocom FSM introspection via VTY */
/* (C) 2016 by Harald Welte <laforge@gnumonks.org>
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
#include <osmocom/vty/misc.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/linuxlist.h>

/* we don't want to add this to a public header file; this is simply
 * exported by libosmocore and used by libmsomvty but not for public
 * consumption. */
extern struct llist_head osmo_g_fsms;

/*! \brief Print information about a FSM [class] to the given VTY
 *  \param vty The VTY to which to print
 *  \param[in] fsm The FSM class to print
 */
void vty_out_fsm(struct vty *vty, struct osmo_fsm *fsm)
{
	unsigned int i;
	const struct value_string *evt_name;

	vty_out(vty, "FSM Name: '%s', Log Subsys: '%s'%s", fsm->name,
		log_category_name(fsm->log_subsys), VTY_NEWLINE);
	/* list the events */
	for (evt_name = fsm->event_names; evt_name->str != NULL; evt_name++) {
		vty_out(vty, " Event %02u (0x%08x): '%s'%s", evt_name->value,
			(1 << evt_name->value), evt_name->str, VTY_NEWLINE);
	}
	/* list the states */
	vty_out(vty, " Number of States: %u%s", fsm->num_states, VTY_NEWLINE);
	for (i = 0; i < fsm->num_states; i++) {
		const struct osmo_fsm_state *state = &fsm->states[i];
		vty_out(vty, "  State %-20s InEvtMask: 0x%08x, OutStateMask: 0x%08x%s",
			state->name, state->in_event_mask, state->out_state_mask,
			VTY_NEWLINE);
	}
}

/*! \brief Print a FSM instance to the given VTY
 *  \param vty The VTY to which to print
 *  \param[in] fsmi The FSM instance to print
 */
void vty_out_fsm_inst(struct vty *vty, struct osmo_fsm_inst *fsmi)
{
	struct osmo_fsm_inst *child;

	vty_out(vty, "FSM Instance Name: '%s', ID: '%s'%s",
		fsmi->name, fsmi->id, VTY_NEWLINE);
	vty_out(vty, " Log-Level: '%s', State: '%s'%s",
		log_level_str(fsmi->log_level),
		osmo_fsm_state_name(fsmi->fsm, fsmi->state),
		VTY_NEWLINE);
	if (fsmi->T)
		vty_out(vty, " Timer: %u%s", fsmi->T, VTY_NEWLINE);
	if (fsmi->proc.parent) {
		vty_out(vty, " Parent: '%s', Term-Event: '%s'%s",
			fsmi->proc.parent->name,
			osmo_fsm_event_name(fsmi->proc.parent->fsm,
					    fsmi->proc.parent_term_event),
			VTY_NEWLINE);
	}
	llist_for_each_entry(child, &fsmi->proc.children, list) {
		vty_out(vty, " Child: '%s'%s", child->name, VTY_NEWLINE);
	}
}

#define SH_FSM_STR	SHOW_STR "Show information about finite state machines\n"
#define SH_FSMI_STR	SHOW_STR "Show information about finite state machine instances\n"

DEFUN(show_fsms, show_fsms_cmd,
	"show fsm all",
	SH_FSM_STR
	"Display a list of all registered finite state machines\n")
{
	struct osmo_fsm *fsm;

	llist_for_each_entry(fsm, &osmo_g_fsms, list)
		vty_out_fsm(vty, fsm);

	return CMD_SUCCESS;
}

DEFUN(show_fsm, show_fsm_cmd,
	"show fsm NAME",
	SH_FSM_STR
	"Display information about a single named finite state machine\n")
{
	struct osmo_fsm *fsm;

	fsm = osmo_fsm_find_by_name(argv[0]);
	if (!fsm) {
		vty_out(vty, "Error: FSM with name '%s' doesn't exist!%s",
			argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	vty_out_fsm(vty, fsm);

	return CMD_SUCCESS;
}

DEFUN(show_fsm_insts, show_fsm_insts_cmd,
	"show fsm-instances all",
	SH_FSMI_STR
	"Display a list of all FSM instances of all finite state machine")
{
	struct osmo_fsm *fsm;

	llist_for_each_entry(fsm, &osmo_g_fsms, list) {
		struct osmo_fsm_inst *fsmi;
		llist_for_each_entry(fsmi, &fsm->instances, list)
			vty_out_fsm_inst(vty, fsmi);
	}

	return CMD_SUCCESS;
}

DEFUN(show_fsm_inst, show_fsm_inst_cmd,
	"show fsm-instances NAME",
	SH_FSMI_STR
	"Display a list of all FSM instances of the named finite state machine")
{
	struct osmo_fsm *fsm;
	struct osmo_fsm_inst *fsmi;

	fsm = osmo_fsm_find_by_name(argv[0]);
	if (!fsm) {
		vty_out(vty, "Error: FSM with name '%s' doesn't exist!%s",
			argv[0], VTY_NEWLINE);
		return CMD_WARNING;
	}

	llist_for_each_entry(fsmi, &fsm->instances, list)
		vty_out_fsm_inst(vty, fsmi);

	return CMD_SUCCESS;
}

/*! \brief Install VTY commands for FSM introspection
 *  This installs a couple of VTY commands for introspection of FSM
 *  classes as well as FSM instances. Call this once from your
 *  application if you want to support those commands. */
void osmo_fsm_vty_add_cmds(void)
{
	install_element_ve(&show_fsm_cmd);
	install_element_ve(&show_fsms_cmd);
	install_element_ve(&show_fsm_inst_cmd);
	install_element_ve(&show_fsm_insts_cmd);
}
