#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/utils.h>

/*! \defgroup fsm Finite State Machine abstraction
 *  @{
 */

/*! \file fsm.h
 *  \brief Finite State Machine
 */

struct osmo_fsm_inst;

enum osmo_fsm_term_cause {
	/*! \brief terminate because parent terminated */
	OSMO_FSM_TERM_PARENT,
	/*! \brief terminate on explicit user request */
	OSMO_FSM_TERM_REQUEST,
	/*! \brief regular termination of process */
	OSMO_FSM_TERM_REGULAR,
	/*! \brief erroneous termination of process */
	OSMO_FSM_TERM_ERROR,
	/*! \brief termination due to time-out */
	OSMO_FSM_TERM_TIMEOUT,
};

/*! \brief description of a rule in the FSM */
struct osmo_fsm_state {
	/*! \brief bit-mask of permitted input events for this state */
	uint32_t in_event_mask;
	/*! \brief bit-mask to which other states this state may transiton */
	uint32_t out_state_mask;
	/*! \brief human-readable name of this state */
	const char *name;
	/*! \brief function to be called for events arriving in this state */
	void (*action)(struct osmo_fsm_inst *fi, uint32_t event, void *data);
	/*! \brief function to be called just after entering the state */
	void (*onenter)(struct osmo_fsm_inst *fi, uint32_t prev_state);
	/*! \brief function to be called just before leaving the state */
	void (*onleave)(struct osmo_fsm_inst *fi, uint32_t next_state);
};

/*! \brief a description of an osmocom finite state machine */
struct osmo_fsm {
	/*! \brief global list */
	struct llist_head list;
	/*! \brief list of instances of this FSM */
	struct llist_head instances;
	/*! \brief human readable name */
	const char *name;
	/*! \brief table of state transition rules */
	const struct osmo_fsm_state *states;
	/*! \brief number of entries in \ref states */
	unsigned int num_states;
	/*! \brief bit-mask of events permitted in all states */
	uint32_t allstate_event_mask;
	/*! \brief function pointer to be called for allstate events */
	void (*allstate_action)(struct osmo_fsm_inst *fi, uint32_t event, void *data);
	/*! \breif clean-up function, called during termination */
	void (*cleanup)(struct osmo_fsm_inst *fi, enum osmo_fsm_term_cause cause);
	/*! \brief timer call-back for states with time-out.
	 * \returns 1 to request termination, 0 to keep running. */
	int (*timer_cb)(struct osmo_fsm_inst *fi);
	/*! \brief logging sub-system for this FSM */
	int log_subsys;
	/*! \brief human-readable names of events */
	const struct value_string *event_names;
};

/*! \brief a single instanceof an osmocom finite state machine */
struct osmo_fsm_inst {
	/*! \brief member in the fsm->instances list */
	struct llist_head list;
	/*! \brief back-pointer to the FSM of which we are an instance */
	struct osmo_fsm *fsm;
	/*! \brief human readable identifier */
	const char *id;
	/*! \brief human readable fully-qualified name */
	const char *name;
	/*! \brief some private data of this instance */
	void *priv;
	/*! \brief logging level for this FSM */
	int log_level;
	/*! \brief current state of the FSM */
	uint32_t state;

	/*! \brief timer number for states with time-out */
	int T;
	/*! \brief timer back-end for states with time-out */
	struct osmo_timer_list timer;

	/*! \brief support for fsm-based procedures */
	struct {
		/*! \brief the parent FSM that has created us */
		struct osmo_fsm_inst *parent;
		/*! \brief the event we should send upon termination */
		uint32_t parent_term_event;
		/*! \brief a list of children processes */
		struct llist_head children;
		/*! \brief \ref llist_head linked to parent->proc.children */
		struct llist_head child;
	} proc;
};

void osmo_fsm_log_addr(bool log_addr);

#define LOGPFSML(fi, level, fmt, args...) \
		LOGP((fi)->fsm->log_subsys, level, "%s{%s}: " fmt, \
			osmo_fsm_inst_name(fi),				    \
			osmo_fsm_state_name((fi)->fsm, (fi)->state), ## args)

#define LOGPFSM(fi, fmt, args...) \
		LOGPFSML(fi, (fi)->log_level, fmt, ## args)

int osmo_fsm_register(struct osmo_fsm *fsm);
void osmo_fsm_unregister(struct osmo_fsm *fsm);
struct osmo_fsm_inst *osmo_fsm_inst_alloc(struct osmo_fsm *fsm, void *ctx, void *priv,
					  int log_level, const char *id);
struct osmo_fsm_inst *osmo_fsm_inst_alloc_child(struct osmo_fsm *fsm,
						struct osmo_fsm_inst *parent,
						uint32_t parent_term_event);
void osmo_fsm_inst_free(struct osmo_fsm_inst *fi);

const char *osmo_fsm_event_name(struct osmo_fsm *fsm, uint32_t event);
const char *osmo_fsm_inst_name(struct osmo_fsm_inst *fi);
const char *osmo_fsm_state_name(struct osmo_fsm *fsm, uint32_t state);

/*! \brief perform a state change of the given FSM instance
 *
 *  This is a macro that calls _osmo_fsm_inst_state_chg() with the given
 *  parameters as well as the caller's source file and line number for logging
 *  purposes. See there for documentation.
 */
#define osmo_fsm_inst_state_chg(fi, new_state, timeout_secs, T) \
	_osmo_fsm_inst_state_chg(fi, new_state, timeout_secs, T, \
				 __BASE_FILE__, __LINE__)
int _osmo_fsm_inst_state_chg(struct osmo_fsm_inst *fi, uint32_t new_state,
			     unsigned long timeout_secs, int T,
			     const char *file, int line);

/*! \brief dispatch an event to an osmocom finite state machine instance
 *
 *  This is a macro that calls _osmo_fsm_inst_dispatch() with the given
 *  parameters as well as the caller's source file and line number for logging
 *  purposes. See there for documentation.
 */
#define osmo_fsm_inst_dispatch(fi, event, data) \
	_osmo_fsm_inst_dispatch(fi, event, data, __BASE_FILE__, __LINE__)
int _osmo_fsm_inst_dispatch(struct osmo_fsm_inst *fi, uint32_t event, void *data,
			    const char *file, int line);

/*! \brief Terminate FSM instance with given cause
 *
 *  This is a macro that calls _osmo_fsm_inst_term() with the given parameters
 *  as well as the caller's source file and line number for logging purposes.
 *  See there for documentation.
 */
#define osmo_fsm_inst_term(fi, cause, data) \
	_osmo_fsm_inst_term(fi, cause, data, __BASE_FILE__, __LINE__)
void _osmo_fsm_inst_term(struct osmo_fsm_inst *fi,
			 enum osmo_fsm_term_cause cause, void *data,
			 const char *file, int line);

/*! @} */
