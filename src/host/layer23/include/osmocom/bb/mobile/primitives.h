#pragma once

#include <osmocom/core/prim.h>

struct mobile_prim;

/**
 * Mobile Script<->App primitives. Application script will receive
 * indications and will send primitives to the lower layers. Here
 * we will convert from internal state/events to the primitives. In
 * the future the indications might be generated at lower levels
 * directly.
 */
enum mobile_prims {
	PRIM_MOB_TIMER,
	PRIM_MOB_TIMER_CANCEL,
};

struct mobile_prim_intf {
	struct osmocom_ms *ms;
	void (*indication)(struct mobile_prim_intf *, struct mobile_prim *prim);

	/* Internal state */
	struct llist_head timers;
};

/**
 * Primitive to create timers and get indication once they have
 * expired. Currently there is no way to cancel timers.
 */
struct mobile_timer_param {
	uint64_t timer_id;	  	/*!< Unique Id identifying the timer */
	int seconds;			/*!< Seconds the timer should fire in */
};

struct mobile_prim {
	struct osmo_prim_hdr hdr;	/*!< Primitive base class */
	union {
		struct mobile_timer_param timer;
	} u;
};


struct mobile_prim_intf *mobile_prim_intf_alloc(struct osmocom_ms *ms);
int mobile_prim_intf_req(struct mobile_prim_intf *intf, struct mobile_prim *hdr);
void mobile_prim_intf_free(struct mobile_prim_intf *intf);

struct mobile_prim *mobile_prim_alloc(unsigned int primitive, enum osmo_prim_operation op);
