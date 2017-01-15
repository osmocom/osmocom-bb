/* Osmocom generic Finite State Machine implementation
 *
 * (C) 2016 by Harald Welte <laforge@gnumonks.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301, USA.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>

/*! \addtogroup fsm
 *  @{
 */

/*! \file fsm.c
 *  \brief Finite State Machine abstraction
 *
 *  This is a generic C-language abstraction for implementing finite
 *  state machines within the Osmocom framework.  It is intended to
 *  replace existing hand-coded or even only implicitly existing FSMs
 *  all over the existing code base.
 *
 *  An libosmocore FSM is described by its \ref osmo_fsm description,
 *  which in turn refers to an array of \ref osmo_fsm_state descriptor,
 *  each describing a single state in the FSM.
 *
 *  The general idea is that all actions performed within one state are
 *  located at one position in the code (the state's action function),
 *  as opposed to the 'message-centric' view of e.g. the existing
 *  state machines of the LAPD(m) coe, where there is one message for
 *  eahc possible event (primitive), and the function then needs to
 *  concern itself on how to handle that event over all possible states.
 *
 *  For each state, there is a bit-mask of permitted input events for
 *  this state, as well as a bit-mask of permitted new output states to
 *  which the state can change.  Furthermore, there is a function
 *  pointer implementing the actual handling of the input events
 *  occurring whilst in thta state.
 *
 *  Furthermore, each state offers a function pointer that can be
 *  executed just before leaving a state, and another one just after
 *  entering a state.
 *
 *  When transitioning into a new state, an optional timer number and
 *  time-out can be passed along.  The timer is started just after
 *  entering the new state, and will call the \ref osmo_fsm timer_cb
 *  function once it expires.  This is intended to be used in telecom
 *  state machines where a given timer (identified by a certain number)
 *  is started to terminate the fsm or terminate the fsm once expected
 *  events are not happening before timeout expiration.
 *
 *  As there can often be many concurrent FSMs of one given class, we
 *  introduce the concept of \ref osmo_fsm_inst, i.e. an FSM instance.
 *  The instance keeps the actual state, while the \ref osmo_fsm
 *  descriptor contains the static/const descriptor of the FSM's states
 *  and possible transitions.
 *
 *  osmo_fsm are integrated with the libosmocore logging system.  The
 *  logging sub-system is determined by the FSM descriptor, as we assume
 *  one FSM (let's say one related to a location update procedure) is
 *  inevitably always tied to a sub-system.  The logging level however
 *  is configurable for each FSM instance, to ensure that e.g. DEBUG
 *  logging can be used for the LU procedure of one subscriber, while
 *  NOTICE level is used for all other subscribers.
 *
 *  In order to attach private state to the \ref osmo_fsm_inst, it
 *  offers an opaque priv pointer.
 *
 */

LLIST_HEAD(osmo_g_fsms);
static bool fsm_log_addr = true;

/*! \brief specify if FSM instance addresses should be logged or not
 *
 *  By default, the FSM name includes the pointer address of the \ref
 *  osmo_fsm_inst.  This behavior can be disabled (and re-enabled)
 *  using this function.
 *
 *  \param[in] log_addr Indicate if FSM instance address shall be logged
 */
void osmo_fsm_log_addr(bool log_addr)
{
	fsm_log_addr = log_addr;
}

struct osmo_fsm *osmo_fsm_find_by_name(const char *name)
{
	struct osmo_fsm *fsm;
	llist_for_each_entry(fsm, &osmo_g_fsms, list) {
		if (!strcmp(name, fsm->name))
			return fsm;
	}
	return NULL;
}

/*! \brief register a FSM with the core
 *
 *  A FSM descriptor needs to be registered with the core before any
 *  instances can be created for it.
 *
 *  \param[in] fsm Descriptor of Finite State Machine to be registered
 *  \returns 0 on success; negative on error
 */
int osmo_fsm_register(struct osmo_fsm *fsm)
{
	if (osmo_fsm_find_by_name(fsm->name))
		return -EEXIST;
	llist_add_tail(&fsm->list, &osmo_g_fsms);
	INIT_LLIST_HEAD(&fsm->instances);

	return 0;
}

/*! \brief unregister a FSM from the core
 *
 *  Once the FSM descriptor is unregistered, active instances can still
 *  use it, but no new instances may be created for it.
 *
 *  \param[in] fsm Descriptor of Finite State Machine to be removed
 */
void osmo_fsm_unregister(struct osmo_fsm *fsm)
{
	llist_del(&fsm->list);
}

/* small wrapper function around timer expiration (for logging) */
static void fsm_tmr_cb(void *data)
{
	struct osmo_fsm_inst *fi = data;
	struct osmo_fsm *fsm = fi->fsm;
	uint32_t T = fi->T;

	LOGPFSM(fi, "Timeout of T%u\n", fi->T);

	if (fsm->timer_cb) {
		int rc = fsm->timer_cb(fi);
		if (rc != 1)
			return;
		LOGPFSM(fi, "timer_cb requested termination\n");
	} else
		LOGPFSM(fi, "No timer_cb, automatic termination\n");

	/* if timer_cb returns 1 or there is no timer_cb */
	osmo_fsm_inst_term(fi, OSMO_FSM_TERM_TIMEOUT, &T);
}

/*! \brief allocate a new instance of a specified FSM
 *  \param[in] fsm Descriptor of the FSM
 *  \param[in] ctx talloc context from which to allocate memory
 *  \param[in] priv private data reference store in fsm instance
 *  \param[in] log_level The log level for events of this FSM
 *  \returns newly-allocated, initialized and registered FSM instance
 */
struct osmo_fsm_inst *osmo_fsm_inst_alloc(struct osmo_fsm *fsm, void *ctx, void *priv,
					  int log_level, const char *id)
{
	struct osmo_fsm_inst *fi = talloc_zero(ctx, struct osmo_fsm_inst);

	fi->fsm = fsm;
	fi->priv = priv;
	fi->log_level = log_level;
	fi->timer.data = fi;
	fi->timer.cb = fsm_tmr_cb;
	if (id)
		fi->id = talloc_strdup(fi, id);

	if (!fsm_log_addr) {
		if (id)
			fi->name = talloc_asprintf(fi, "%s(%s)", fsm->name, id);
		else
			fi->name = talloc_asprintf(fi, "%s", fsm->name);
	} else {
		if (id)
			fi->name = talloc_asprintf(fi, "%s(%s)[%p]", fsm->name,
						   id, fi);
		else
			fi->name = talloc_asprintf(fi, "%s[%p]", fsm->name, fi);
	}

	INIT_LLIST_HEAD(&fi->proc.children);
	INIT_LLIST_HEAD(&fi->proc.child);
	llist_add(&fi->list, &fsm->instances);

	LOGPFSM(fi, "Allocated\n");

	return fi;
}

/*! \brief allocate a new instance of a specified FSM as child of
 *  other FSM instance
 *
 *  This is like \ref osmo_fsm_inst_alloc but using the parent FSM as
 *  talloc context, and inheriting the log level of the parent.
 *
 *  \param[in] fsm Descriptor of the to-be-allocated FSM
 *  \param[in] parent Parent FSM instance
 *  \param[in] parent_term_event Event to be sent to parent when terminating
 *  \returns newly-allocated, initialized and registered FSM instance
 */
struct osmo_fsm_inst *osmo_fsm_inst_alloc_child(struct osmo_fsm *fsm,
						struct osmo_fsm_inst *parent,
						uint32_t parent_term_event)
{
	struct osmo_fsm_inst *fi;

	fi = osmo_fsm_inst_alloc(fsm, parent, NULL, parent->log_level,
				 parent->id);
	if (!fi) {
		/* indicate immediate termination to caller */
		osmo_fsm_inst_dispatch(parent, parent_term_event, NULL);
		return NULL;
	}

	LOGPFSM(fi, "is child of %s\n", osmo_fsm_inst_name(parent));

	fi->proc.parent = parent;
	fi->proc.parent_term_event = parent_term_event;
	llist_add(&fi->proc.child, &parent->proc.children);

	return fi;
}

/*! \brief delete a given instance of a FSM
 *  \param[in] fsm The FSM to be un-registered and deleted
 */
void osmo_fsm_inst_free(struct osmo_fsm_inst *fi)
{
	LOGPFSM(fi, "Deallocated\n");
	osmo_timer_del(&fi->timer);
	llist_del(&fi->list);
	talloc_free(fi);
}

/*! \brief get human-readable name of FSM event
 *  \param[in] fsm FSM descriptor of event
 *  \param[in] event Event integer value
 *  \returns string rendering of the event
 */
const char *osmo_fsm_event_name(struct osmo_fsm *fsm, uint32_t event)
{
	static char buf[32];
	if (!fsm->event_names) {
		snprintf(buf, sizeof(buf), "%u", event);
		return buf;
	} else
		return get_value_string(fsm->event_names, event);
}

/*! \brief get human-readable name of FSM instance
 *  \param[in] fi FSM instance
 *  \returns string rendering of the FSM identity
 */
const char *osmo_fsm_inst_name(struct osmo_fsm_inst *fi)
{
	if (!fi)
		return "NULL";

	if (fi->name)
		return fi->name;
	else
		return fi->fsm->name;
}

/*! \brief get human-readable name of FSM instance
 *  \param[in] fsm FSM descriptor
 *  \param[in] state FSM state number
 *  \returns string rendering of the FSM state
 */
const char *osmo_fsm_state_name(struct osmo_fsm *fsm, uint32_t state)
{
	static char buf[32];
	if (state >= fsm->num_states) {
		snprintf(buf, sizeof(buf), "unknown %u", state);
		return buf;
	} else
		return fsm->states[state].name;
}

/*! \brief perform a state change of the given FSM instance
 *
 *  Best invoke via the osmo_fsm_inst_state_chg() macro which logs the source
 *  file where the state change was effected. Alternatively, you may pass \a
 *  file as NULL to use the normal file/line indication instead.
 *
 *  All changes to the FSM instance state must be made via this
 *  function.  It verifies that the existing state actually permits a
 *  transiiton to new_state.
 *
 *  timeout_secs and T are optional parameters, and only have any effect
 *  if timeout_secs is not 0.  If the timeout function is used, then the
 *  new_state is entered, and the FSM instances timer is set to expire
 *  in timeout_secs functions.   At that time, the FSM's timer_cb
 *  function will be called for handling of the timeout by the user.
 *
 *  \param[in] fi FSM instance whose state is to change
 *  \param[in] new_state The new state into which we should change
 *  \param[in] timeout_secs Timeout in seconds (if !=0)
 *  \param[in] T Timer number (if \ref timeout_secs != 0)
 *  \param[in] file Calling source file (from osmo_fsm_inst_state_chg macro)
 *  \param[in] line Calling source line (from osmo_fsm_inst_state_chg macro)
 *  \returns 0 on success; negative on error
 */
int _osmo_fsm_inst_state_chg(struct osmo_fsm_inst *fi, uint32_t new_state,
			     unsigned long timeout_secs, int T,
			     const char *file, int line)
{
	struct osmo_fsm *fsm = fi->fsm;
	uint32_t old_state = fi->state;
	const struct osmo_fsm_state *st = &fsm->states[fi->state];

	/* validate if new_state is a valid state */
	if (!(st->out_state_mask & (1 << new_state))) {
		LOGPFSMLSRC(fi, LOGL_ERROR, file, line,
			    "transition to state %s not permitted!\n",
			    osmo_fsm_state_name(fsm, new_state));
		return -EPERM;
	}

	/* delete the old timer */
	osmo_timer_del(&fi->timer);

	if (st->onleave)
		st->onleave(fi, new_state);

	LOGPFSMSRC(fi, file, line, "state_chg to %s\n",
		   osmo_fsm_state_name(fsm, new_state));
	fi->state = new_state;
	st = &fsm->states[new_state];

	if (timeout_secs) {
		fi->T = T;
		osmo_timer_schedule(&fi->timer, timeout_secs, 0);
	}

	/* Call 'onenter' last, user might terminate FSM from there */
	if (st->onenter)
		st->onenter(fi, old_state);

	return 0;
}

/*! \brief dispatch an event to an osmocom finite state machine instance
 *
 *  Best invoke via the osmo_fsm_inst_dispatch() macro which logs the source
 *  file where the event was effected. Alternatively, you may pass \a file as
 *  NULL to use the normal file/line indication instead.
 *
 *  Any incoming events to \ref osmo_fsm instances must be dispatched to
 *  them via this function.  It verifies, whether the event is permitted
 *  based on the current state of the FSM.  If not, -1 is returned.
 *
 *  \param[in] fi FSM instance
 *  \param[in] event Event to send to FSM instance
 *  \param[in] data Data to pass along with the event
 *  \param[in] file Calling source file (from osmo_fsm_inst_dispatch macro)
 *  \param[in] line Calling source line (from osmo_fsm_inst_dispatch macro)
 *  \returns 0 in case of success; negative on error
 */
int _osmo_fsm_inst_dispatch(struct osmo_fsm_inst *fi, uint32_t event, void *data,
			    const char *file, int line)
{
	struct osmo_fsm *fsm;
	const struct osmo_fsm_state *fs;

	if (!fi) {
		LOGPSRC(DLGLOBAL, LOGL_ERROR, file, line,
			"Trying to dispatch event %u to non-existent"
			" FSM instance!\n", event);
		osmo_log_backtrace(DLGLOBAL, LOGL_ERROR);
		return -ENODEV;
	}

	fsm = fi->fsm;
	OSMO_ASSERT(fi->state < fsm->num_states);
	fs = &fi->fsm->states[fi->state];

	LOGPFSMSRC(fi, file, line,
		   "Received Event %s\n", osmo_fsm_event_name(fsm, event));

	if (((1 << event) & fsm->allstate_event_mask) && fsm->allstate_action) {
		fsm->allstate_action(fi, event, data);
		return 0;
	}

	if (!((1 << event) & fs->in_event_mask)) {
		LOGPFSMLSRC(fi, LOGL_ERROR, file, line,
			    "Event %s not permitted\n",
			    osmo_fsm_event_name(fsm, event));
		return -1;
	}
	fs->action(fi, event, data);

	return 0;
}

/*! \brief Terminate FSM instance with given cause
 *
 *  This safely terminates the given FSM instance by first iterating
 *  over all children and sending them a termination event.  Next, it
 *  calls the FSM descriptors cleanup function (if any), followed by
 *  releasing any memory associated with the FSM instance.
 *
 *  Finally, the parent FSM instance (if any) is notified using the
 *  parent termination event configured at time of FSM instance start.
 *
 *  \param[in] fi FSM instance to be terminated
 *  \param[in] cause Cause / reason for termination
 *  \param[in] data Opaque event data to be passed with the parent term event
 *  \param[in] file Calling source file (from osmo_fsm_inst_term macro)
 *  \param[in] line Calling source line (from osmo_fsm_inst_term macro)
 */
void _osmo_fsm_inst_term(struct osmo_fsm_inst *fi,
			 enum osmo_fsm_term_cause cause, void *data,
			 const char *file, int line)
{
	struct osmo_fsm_inst *parent;
	uint32_t parent_term_event = fi->proc.parent_term_event;

	LOGPFSMSRC(fi, file, line, "Terminating (cause = %s)\n",
		   osmo_fsm_term_cause_name(cause));

	_osmo_fsm_inst_term_children(fi, OSMO_FSM_TERM_PARENT, NULL,
				     file, line);

	/* delete ourselves from the parent */
	parent = fi->proc.parent;
	if (parent)
		LOGPFSMSRC(fi, file, line, "Removing from parent %s\n",
			   osmo_fsm_inst_name(parent));
	llist_del(&fi->proc.child);

	/* call destructor / clean-up function */
	if (fi->fsm->cleanup)
		fi->fsm->cleanup(fi, cause);

	LOGPFSMSRC(fi, file, line, "Freeing instance\n");
	/* Fetch parent again in case it has changed. */
	parent = fi->proc.parent;
	osmo_fsm_inst_free(fi);

	/* indicate our termination to the parent */
	if (parent && cause != OSMO_FSM_TERM_PARENT)
		_osmo_fsm_inst_dispatch(parent, parent_term_event, data,
					file, line);
}

/*! \brief Terminate all child FSM instances of an FSM instance.
 *
 *  Iterate over all children and send them a termination event, with the given
 *  cause. Pass OSMO_FSM_TERM_PARENT to avoid dispatching events from the
 *  terminated child FSMs.
 *
 *  \param[in] fi FSM instance that should be cleared of child FSMs
 *  \param[in] cause Cause / reason for termination (OSMO_FSM_TERM_PARENT)
 *  \param[in] data Opaque event data to be passed with the parent term events
 *  \param[in] file Calling source file (from osmo_fsm_inst_term_children macro)
 *  \param[in] line Calling source line (from osmo_fsm_inst_term_children macro)
 */
void _osmo_fsm_inst_term_children(struct osmo_fsm_inst *fi,
				  enum osmo_fsm_term_cause cause,
				  void *data,
				  const char *file, int line)
{
	struct osmo_fsm_inst *first_child, *last_seen_first_child;

	/* iterate over all children, starting from the beginning every time:
	 * terminating an FSM may emit events that cause other FSMs to also
	 * terminate and remove themselves from this list. */
	last_seen_first_child = NULL;
	while (!llist_empty(&fi->proc.children)) {
		first_child = llist_entry(fi->proc.children.next,
					  typeof(*first_child),
					  proc.child);

		/* paranoia: do not loop forever */
		if (first_child == last_seen_first_child) {
			LOGPFSMLSRC(fi, LOGL_ERROR, file, line,
				    "Internal error while terminating child"
				    " FSMs: a child FSM is stuck\n");
			break;
		}
		last_seen_first_child = first_child;

		/* terminate child */
		_osmo_fsm_inst_term(first_child, cause, data,
				    file, line);
	}
}

const struct value_string osmo_fsm_term_cause_names[] = {
	OSMO_VALUE_STRING(OSMO_FSM_TERM_PARENT),
	OSMO_VALUE_STRING(OSMO_FSM_TERM_REQUEST),
	OSMO_VALUE_STRING(OSMO_FSM_TERM_REGULAR),
	OSMO_VALUE_STRING(OSMO_FSM_TERM_ERROR),
	OSMO_VALUE_STRING(OSMO_FSM_TERM_TIMEOUT),
	{ 0, NULL }
};

/*! @} */
