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

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <osmocore/talloc.h>

#include <osmocom/logging.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/mncc.h>

void *l23_ctx;
static int new_callref = 1;
static LLIST_HEAD(call_list);

/*
 * support functions
 */

void mncc_set_cause(struct gsm_mncc *data, int loc, int val);

static void free_call(struct gsm_call *call)
{
	llist_del(&call->entry);
	DEBUGP(DMNCC, "(call %x) Call removed.\n", call->callref);
	talloc_free(call);
}


struct gsm_call *get_call_ref(uint32_t callref)
{
	struct gsm_call *callt;

	llist_for_each_entry(callt, &call_list, entry) {
		if (callt->callref == callref)
			return callt;
	}
	return NULL;
}

/*
 * MNCCms dummy application
 */

/* this is a minimal implementation as required by GSM 04.08 */
int mncc_recv_dummy(struct osmocom_ms *ms, int msg_type, void *arg)
{
	struct gsm_mncc *data = arg;
	uint32_t callref = data->callref;
	struct gsm_mncc rel;

	if (msg_type == MNCC_REL_IND || msg_type == MNCC_REL_CNF)
		return 0;

	LOGP(DMNCC, LOGL_INFO, "Rejecting incomming call\n");

	/* reject, as we don't support Calls */
	memset(&rel, 0, sizeof(struct gsm_mncc));
       	rel.callref = callref;
	mncc_set_cause(&rel, GSM48_CAUSE_LOC_USER,
		GSM48_CC_CAUSE_INCOMPAT_DEST);

	return mncc_send(ms, MNCC_REL_REQ, &rel);
}

/*
 * MNCCms basic call application
 */

int mncc_recv_mobile(struct osmocom_ms *ms, int msg_type, void *arg)
{
	struct gsm_mncc *data = arg;
	struct gsm_call *call = get_call_ref(data->callref);
	struct gsm_mncc mncc;
	uint8_t cause;

	/* call does not exist */
	if (!call && msg_type != MNCC_SETUP_IND) {
		LOGP(DMNCC, LOGL_INFO, "Rejecting incomming call\n");
		if (msg_type == MNCC_REL_IND || msg_type == MNCC_REL_CNF)
			return 0;
		cause = GSM48_CC_CAUSE_INCOMPAT_DEST;

		release:
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = data->callref;
		mncc_set_cause(&mncc, GSM48_CAUSE_LOC_USER, cause);
		return mncc_send(ms, MNCC_REL_REQ, &mncc);
	}

	/* setup without call */
	if (!call) {
		call = talloc_zero(l23_ctx, struct gsm_call);
		if (!call)
			return -ENOMEM;
		call->callref = new_callref++;
		llist_add_tail(&call->entry, &call_list);
	}

	switch (msg_type) {
	case MNCC_DISC_IND:
		LOGP(DMNCC, LOGL_INFO, "Call has been disconnected\n");
		if ((data->fields & MNCC_F_PROGRESS)
		 && data->progress.descr == 8)
		 	break;
		free_call(call);
		cause = GSM48_CC_CAUSE_NORM_CALL_CLEAR;
		goto release;
	case MNCC_REL_IND:
	case MNCC_REL_CNF:
		LOGP(DMNCC, LOGL_INFO, "Call has been released\n");
		free_call(call);
		break;
	case MNCC_CALL_PROC_IND:
		LOGP(DMNCC, LOGL_INFO, "Call is proceeding\n");
		break;
	case MNCC_ALERT_IND:
		LOGP(DMNCC, LOGL_INFO, "Call is alerting\n");
		break;
	case MNCC_SETUP_CNF:
		LOGP(DMNCC, LOGL_INFO, "Call is answered\n");
		break;
	case MNCC_SETUP_IND:
		if (!llist_empty(&call_list)) {
			LOGP(DMNCC, LOGL_INFO, "Incomming call but busy\n");
			cause = GSM48_CC_CAUSE_NORM_CALL_CLEAR;
			goto release;
		}
		LOGP(DMNCC, LOGL_INFO, "Incomming call\n");
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = call->callref;
		mncc_send(ms, MNCC_CALL_CONF_REQ, &mncc);
		break;
		LOGP(DMNCC, LOGL_INFO, "Ring!\n");
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = call->callref;
		mncc_send(ms, MNCC_ALERT_REQ, &mncc);
		break;
	default:
		LOGP(DMNCC, LOGL_INFO, "Message 0x%02x unsupported\n",
			msg_type);
	}
}

int mncc_call(struct osmocom_ms *ms, char *number)
{
	struct gsm_call *call;
	struct gsm_mncc setup;

	if (!llist_empty(&call_list)) {
		LOGP(DMNCC, LOGL_INFO, "Cannot make a call, busy!\n");
		return -EBUSY;
	}

	call = talloc_zero(l23_ctx, struct gsm_call);
	if (!call)
		return -ENOMEM;
	call->callref = new_callref++;
	llist_add_tail(&call->entry, &call_list);

	memset(&setup, 0, sizeof(struct gsm_mncc));
       	setup.callref = call->callref;

	if (!strncasecmp(number, "emerg", 5)) {
		LOGP(DMNCC, LOGL_INFO, "Make emergency call\n");
		/* emergency */
		setup.emergency = 1;
	} else {
		LOGP(DMNCC, LOGL_INFO, "Make call to %s\n", number);
		/* called number */
		setup.fields |= MNCC_F_CALLED;
		strncpy(setup.called.number, number,
			sizeof(setup.called.number) - 1);
		
		/* bearer capability (mandatory) */
		setup.fields |= MNCC_F_BEARER_CAP;
		setup.bearer_cap.coding = 0;
		setup.bearer_cap.radio = 1;
		setup.bearer_cap.speech_ctm = 0;
		setup.bearer_cap.speech_ver[0] = 0;
		setup.bearer_cap.speech_ver[1] = -1; /* end of list */
		setup.bearer_cap.transfer = 0;
		setup.bearer_cap.mode = 0;
	}

	return mncc_send(ms, MNCC_SETUP_REQ, &setup);
}

int mncc_hangup(struct osmocom_ms *ms)
{
	struct gsm_call *call;
	struct gsm_mncc disc;

	if (llist_empty(&call_list)) {
		LOGP(DMNCC, LOGL_INFO, "No active call to hangup\n");
		return -EBUSY;
	}
	call = llist_entry(call_list.next, struct gsm_call, entry);

	memset(&disc, 0, sizeof(struct gsm_mncc));
	disc.callref = call->callref;
	mncc_set_cause(&disc, GSM48_CAUSE_LOC_USER,
		GSM48_CC_CAUSE_NORM_CALL_CLEAR);
	return mncc_send(ms, MNCC_DISC_REQ, &disc);
}

int mncc_answer(struct osmocom_ms *ms)
{
	struct gsm_call *call;
	struct gsm_mncc rsp;

	if (llist_empty(&call_list)) {
		LOGP(DMNCC, LOGL_INFO, "No call to answer\n");
		return -EBUSY;
	}
	call = llist_entry(call_list.next, struct gsm_call, entry);

	memset(&rsp, 0, sizeof(struct gsm_mncc));
	rsp.callref = call->callref;
	mncc_set_cause(&rsp, GSM48_CAUSE_LOC_USER,
		GSM48_CC_CAUSE_NORM_CALL_CLEAR);
	return mncc_send(ms, MNCC_SETUP_RSP, &rsp);
}




