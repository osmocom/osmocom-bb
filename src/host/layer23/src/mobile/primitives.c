/* (C) 2017 by Holger Hans Peter Freyther
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

#include <inttypes.h>

#include <osmocom/bb/mobile/gsm322.h>
#include <osmocom/bb/mobile/primitives.h>
#include <osmocom/bb/common/logging.h>

#include <osmocom/core/timer.h>
#include <osmocom/core/talloc.h>

static LLIST_HEAD(s_prims);

struct timer_closure {
	struct llist_head entry;
	struct mobile_prim_intf *intf;
	struct osmo_timer_list timer;
	uint64_t id;
};

struct mobile_prim_intf *mobile_prim_intf_alloc(struct osmocom_ms *ms)
{
	struct mobile_prim_intf *intf;

	intf = talloc_zero(ms, struct mobile_prim_intf);
	intf->ms = ms;

	INIT_LLIST_HEAD(&intf->timers);
	llist_add_tail(&intf->entry, &s_prims);
	return intf;
}

void mobile_prim_intf_free(struct mobile_prim_intf *intf)
{
	struct timer_closure *timer, *tmp;

	llist_for_each_entry_safe(timer, tmp, &intf->timers, entry) {
		osmo_timer_del(&timer->timer);
		llist_del(&timer->entry);
		talloc_free(timer);
	}
	llist_del(&intf->entry);
	talloc_free(intf);
}

struct mobile_prim *mobile_prim_alloc(unsigned int primitive, enum osmo_prim_operation op)
{
	struct msgb *msg = msgb_alloc(1024, "Mobile Primitive");
	struct mobile_prim *prim = (struct mobile_prim *) msgb_put(msg, sizeof(*prim));
	osmo_prim_init(&prim->hdr, 0, primitive, op, msg);
	msg->l2h = msg->tail;
	return prim;
}

static void timer_expired_cb(void *_closure)
{
	struct timer_closure *closure = _closure;
	struct mobile_prim_intf *intf;
	struct mobile_prim *prim;

	prim = mobile_prim_alloc(PRIM_MOB_TIMER, PRIM_OP_INDICATION);
	intf = closure->intf;
	prim->u.timer.timer_id = closure->id;

	llist_del(&closure->entry);
	talloc_free(closure);

	intf->indication(intf, prim);
}

static int create_timer(struct mobile_prim_intf *intf, struct mobile_timer_param *param)
{
	struct timer_closure *closure;

	LOGP(DPRIM, LOGL_DEBUG, "Creating timer with reference: "
		"%" PRIu64 "\n", param->timer_id);

	closure = talloc_zero(intf, struct timer_closure);
	closure->intf = intf;
	closure->id = param->timer_id;
	closure->timer.cb = timer_expired_cb;
	closure->timer.data = closure;
	llist_add_tail(&closure->entry, &intf->timers);
	osmo_timer_schedule(&closure->timer, param->seconds, 0);
	return 0;
}

static void dispatch(struct osmocom_ms *ms, struct mobile_prim *prim)
{
	struct mobile_prim_intf *intf, *tmp;

	llist_for_each_entry_safe(intf, tmp, &s_prims, entry) {
		if (intf->ms == ms)
			intf->indication(intf, prim);
	}
	msgb_free(prim->hdr.msg);
}

void mobile_prim_ntfy_started(struct osmocom_ms *ms, bool started)
{
	struct mobile_prim *prim = mobile_prim_alloc(PRIM_MOB_STARTED, PRIM_OP_INDICATION);

	prim->u.started.started = started;
	dispatch(ms, prim);
}

void mobile_prim_ntfy_shutdown(struct osmocom_ms *ms, int old_state, int new_state)
{
	struct mobile_prim *prim = mobile_prim_alloc(PRIM_MOB_SHUTDOWN, PRIM_OP_INDICATION);

	prim->u.shutdown.old_state = old_state;
	prim->u.shutdown.new_state = new_state;
	dispatch(ms, prim);
}

void mobile_prim_ntfy_sms_new(struct osmocom_ms *ms, struct gsm_sms *sms)
{
	struct mobile_prim *prim = mobile_prim_alloc(PRIM_MOB_SMS, PRIM_OP_INDICATION);

	prim->u.sms.sms = *sms;
	dispatch(ms, prim);
}

void mobile_prim_ntfy_sms_status(struct osmocom_ms *ms, struct gsm_sms *sms, uint8_t cause)
{
	struct mobile_prim *prim = mobile_prim_alloc(PRIM_MOB_SMS, PRIM_OP_INDICATION);

	prim->u.sms.sms = *sms;
	prim->u.sms.cause_valid = true;
	prim->u.sms.cause = cause;
	dispatch(ms, prim);
}

void mobile_prim_ntfy_mm_status(struct osmocom_ms *ms, int state, int substate, int mr_substate)
{
	struct mobile_prim *prim = mobile_prim_alloc(PRIM_MOB_MM, PRIM_OP_INDICATION);

	prim->u.mm.state = state;
	prim->u.mm.substate = substate;
	prim->u.mm.prev_substate = mr_substate;
	dispatch(ms, prim);
}

static int cancel_timer(struct mobile_prim_intf *intf, struct mobile_timer_param *param)
{
	struct timer_closure *closure;


	llist_for_each_entry(closure, &intf->timers, entry) {
		if (closure->id != param->timer_id)
			continue;

		LOGP(DPRIM, LOGL_DEBUG, "Canceling timer with reference: "
			"%" PRIu64 "\n", param->timer_id);

		osmo_timer_del(&closure->timer);
		llist_del(&closure->entry);
		talloc_free(closure);
		return 0;
	}
	return -1;
}

static int send_sms(struct mobile_prim_intf *intf, struct mobile_sms_param *param)
{
	struct gsm_sms *sms;

	sms = sms_alloc();
	*sms = param->sms;

	/* Force a NUL at the end of the string */
	param->sca[sizeof(param->sca) - 1] = '\0';

	return gsm411_tx_sms_submit(intf->ms, param->sca, sms);
}

static int network_reselect(struct mobile_prim_intf *intf)
{
	struct msgb *nmsg;

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_USER_RESEL);
	if (!nmsg)
		return -1;
	gsm322_plmn_sendmsg(intf->ms, nmsg);
	return 0;
}

int mobile_prim_intf_req(struct mobile_prim_intf *intf, struct mobile_prim *prim)
{
	int rc = 0;

	switch (OSMO_PRIM_HDR(&prim->hdr)) {
	case OSMO_PRIM(PRIM_MOB_TIMER, PRIM_OP_REQUEST):
		rc = create_timer(intf, &prim->u.timer);
		break;
	case OSMO_PRIM(PRIM_MOB_TIMER_CANCEL, PRIM_OP_REQUEST):
		rc = cancel_timer(intf, &prim->u.timer);
		break;
	case OSMO_PRIM(PRIM_MOB_SMS, PRIM_OP_REQUEST):
		rc = send_sms(intf, &prim->u.sms);
		break;
	case OSMO_PRIM(PRIM_MOB_NETWORK_RESELECT, PRIM_OP_REQUEST):
		rc = network_reselect(intf);
		break;
	default:
		LOGP(DPRIM, LOGL_ERROR, "Unknown primitive: %d\n", OSMO_PRIM_HDR(&prim->hdr));
		break;
	}

	msgb_free(prim->hdr.msg);
	return rc;
}
