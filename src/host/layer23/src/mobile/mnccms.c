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

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/vty.h>

void *l23_ctx;
static uint32_t new_callref = 1;
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
	int first_call = 0;

	/* call does not exist */
	if (!call && msg_type != MNCC_SETUP_IND) {
		LOGP(DMNCC, LOGL_INFO, "Rejecting incomming call "
			"(callref %x)\n", data->callref);
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
		if (llist_empty(&call_list))
			first_call = 1;
		call = talloc_zero(l23_ctx, struct gsm_call);
		if (!call)
			return -ENOMEM;
		call->callref = data->callref;
		llist_add_tail(&call->entry, &call_list);
	}

	switch (msg_type) {
	case MNCC_DISC_IND:
		vty_notify(ms, NULL);
		switch (data->cause.value) {
		case GSM48_CC_CAUSE_UNASSIGNED_NR:
			vty_notify(ms, "Call: Number not assigned\n");
			break;
		case GSM48_CC_CAUSE_NO_ROUTE:
			vty_notify(ms, "Call: Destination unreachable\n");
			break;
		case GSM48_CC_CAUSE_NORM_CALL_CLEAR:
			vty_notify(ms, "Call: Remote hangs up\n");
			break;
		case GSM48_CC_CAUSE_USER_BUSY:
			vty_notify(ms, "Call: Remote busy\n");
			break;
		case GSM48_CC_CAUSE_USER_NOTRESPOND:
			vty_notify(ms, "Call: Remote not responding\n");
			break;
		case GSM48_CC_CAUSE_USER_ALERTING_NA:
			vty_notify(ms, "Call: Remote not answering\n");
			break;
		case GSM48_CC_CAUSE_CALL_REJECTED:
			vty_notify(ms, "Call has been rejected\n");
			break;
		case GSM48_CC_CAUSE_NUMBER_CHANGED:
			vty_notify(ms, "Call: Number changed\n");
			break;
		case GSM48_CC_CAUSE_PRE_EMPTION:
			vty_notify(ms, "Call: Cleared due to pre-emption\n");
			break;
		case GSM48_CC_CAUSE_DEST_OOO:
			vty_notify(ms, "Call: Remote out of order\n");
			break;
		case GSM48_CC_CAUSE_INV_NR_FORMAT:
			vty_notify(ms, "Call: Number invalid or imcomplete\n");
			break;
		case GSM48_CC_CAUSE_NO_CIRCUIT_CHAN:
			vty_notify(ms, "Call: No channel available\n");
			break;
		case GSM48_CC_CAUSE_NETWORK_OOO:
			vty_notify(ms, "Call: Network out of order\n");
			break;
		case GSM48_CC_CAUSE_TEMP_FAILURE:
			vty_notify(ms, "Call: Temporary failure\n");
			break;
		case GSM48_CC_CAUSE_SWITCH_CONG:
			vty_notify(ms, "Congestion\n");
			break;
		default:
			vty_notify(ms, "Call has been disconnected\n");
		}
		LOGP(DMNCC, LOGL_INFO, "Call has been disconnected "
			"(cause %d)\n", data->cause.value);
		if ((data->fields & MNCC_F_PROGRESS)
		 && data->progress.descr == 8) {
			vty_notify(ms, "Please hang up!\n");
		 	break;
		}
		free_call(call);
		cause = GSM48_CC_CAUSE_NORM_CALL_CLEAR;
		goto release;
	case MNCC_REL_IND:
	case MNCC_REL_CNF:
		vty_notify(ms, NULL);
		if (data->cause.value == GSM48_CC_CAUSE_CALL_REJECTED)
			vty_notify(ms, "Call has been rejected\n");
		else
			vty_notify(ms, "Call has been released\n");
		LOGP(DMNCC, LOGL_INFO, "Call has been released (cause %d)\n",
			data->cause.value);
		free_call(call);
		break;
	case MNCC_CALL_PROC_IND:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call is proceeding\n");
		LOGP(DMNCC, LOGL_INFO, "Call is proceeding\n");
		break;
	case MNCC_ALERT_IND:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call is aleriting\n");
		LOGP(DMNCC, LOGL_INFO, "Call is alerting\n");
		break;
	case MNCC_SETUP_CNF:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call is answered\n");
		LOGP(DMNCC, LOGL_INFO, "Call is answered\n");
		break;
	case MNCC_SETUP_IND:
		vty_notify(ms, NULL);
		if (!first_call && !ms->settings.cw) {
			vty_notify(ms, "Incomming call rejected while busy\n");
			LOGP(DMNCC, LOGL_INFO, "Incomming call but busy\n");
			cause = GSM48_CC_CAUSE_USER_BUSY;
			goto release;
		}
		/* presentation allowed if present == 0 */
		if (data->calling.present || !data->calling.number[0])
			vty_notify(ms, "Incomming call (anonymous)\n");
		else if (data->calling.type == 1)
			vty_notify(ms, "Incomming call (from +%s)\n",
				data->calling.number);
		else if (data->calling.type == 2)
			vty_notify(ms, "Incomming call (from 0-%s)\n",
				data->calling.number);
		else
			vty_notify(ms, "Incomming call (from %s)\n",
				data->calling.number);
		LOGP(DMNCC, LOGL_INFO, "Incomming call (from %s callref %x)\n",
			data->calling.number, call->callref);
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = call->callref;
		mncc_send(ms, MNCC_CALL_CONF_REQ, &mncc);
		if (first_call)
			LOGP(DMNCC, LOGL_INFO, "Ring!\n");
		else {
			LOGP(DMNCC, LOGL_INFO, "Knock!\n");
			call->hold = 1;
		}
		call->ring = 1;
		memset(&mncc, 0, sizeof(struct gsm_mncc));
		mncc.callref = call->callref;
		mncc_send(ms, MNCC_ALERT_REQ, &mncc);
		break;
	case MNCC_SETUP_COMPL_IND:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call is connected\n");
		LOGP(DMNCC, LOGL_INFO, "Call is connected\n");
		break;
	case MNCC_HOLD_CNF:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call is on hold\n");
		LOGP(DMNCC, LOGL_INFO, "Call is on hold\n");
		call->hold = 1;
		break;
	case MNCC_HOLD_REJ:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call hold was rejected\n");
		LOGP(DMNCC, LOGL_INFO, "Call hold was rejected\n");
		break;
	case MNCC_RETRIEVE_CNF:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call is retrieved\n");
		LOGP(DMNCC, LOGL_INFO, "Call is retrieved\n");
		call->hold = 0;
		break;
	case MNCC_RETRIEVE_REJ:
		vty_notify(ms, NULL);
		vty_notify(ms, "Call retrieve was rejected\n");
		LOGP(DMNCC, LOGL_INFO, "Call retrieve was rejected\n");
		break;
	default:
		LOGP(DMNCC, LOGL_INFO, "Message 0x%02x unsupported\n",
			msg_type);
		return -EINVAL;
	}

	return 0;
}

int mncc_call(struct osmocom_ms *ms, char *number)
{
	struct gsm_call *call;
	struct gsm_mncc setup;

	llist_for_each_entry(call, &call_list, entry) {
		if (!call->hold) {
			vty_notify(ms, NULL);
			vty_notify(ms, "Please put active call on hold "
				"first!\n");
			LOGP(DMNCC, LOGL_INFO, "Cannot make a call, busy!\n");
			return -EBUSY;
		}
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
		if (ms->settings.clir)
			setup.clir.sup = 1;
		else if (ms->settings.clip)
			setup.clir.inv = 1;
	}

	return mncc_send(ms, MNCC_SETUP_REQ, &setup);
}

int mncc_hangup(struct osmocom_ms *ms)
{
	struct gsm_call *call, *found = NULL;
	struct gsm_mncc disc;

	llist_for_each_entry(call, &call_list, entry) {
		if (!call->hold) {
			found = call;
			break;
		}
	}
	if (!found) {
		LOGP(DMNCC, LOGL_INFO, "No active call to hangup\n");
		vty_notify(ms, NULL);
		vty_notify(ms, "No active call\n");
		return -EINVAL;
	}

	memset(&disc, 0, sizeof(struct gsm_mncc));
	disc.callref = found->callref;
	mncc_set_cause(&disc, GSM48_CAUSE_LOC_USER,
		GSM48_CC_CAUSE_NORM_CALL_CLEAR);
	return mncc_send(ms, MNCC_DISC_REQ, &disc);
}

int mncc_answer(struct osmocom_ms *ms)
{
	struct gsm_call *call, *alerting = NULL;
	struct gsm_mncc rsp;
	int active = 0;

	llist_for_each_entry(call, &call_list, entry) {
		if (call->ring)
			alerting = call;
		else if (!call->hold)
			active = 1;
	}
	if (!alerting) {
		LOGP(DMNCC, LOGL_INFO, "No call alerting\n");
		vty_notify(ms, NULL);
		vty_notify(ms, "No alerting call\n");
		return -EBUSY;
	}
	if (active) {
		LOGP(DMNCC, LOGL_INFO, "Answer but we have an active call\n");
		vty_notify(ms, NULL);
		vty_notify(ms, "Please put active call on hold first!\n");
		return -EBUSY;
	}
	alerting->ring = 0;
	alerting->hold = 0;

	memset(&rsp, 0, sizeof(struct gsm_mncc));
	rsp.callref = alerting->callref;
	return mncc_send(ms, MNCC_SETUP_RSP, &rsp);
}

int mncc_hold(struct osmocom_ms *ms)
{
	struct gsm_call *call, *found = NULL;
	struct gsm_mncc hold;

	llist_for_each_entry(call, &call_list, entry) {
		if (!call->hold) {
			found = call;
			break;
		}
	}
	if (!found) {
		LOGP(DMNCC, LOGL_INFO, "No active call to hold\n");
		vty_notify(ms, NULL);
		vty_notify(ms, "No active call\n");
		return -EINVAL;
	}

	memset(&hold, 0, sizeof(struct gsm_mncc));
	hold.callref = found->callref;
	return mncc_send(ms, MNCC_HOLD_REQ, &hold);
}

int mncc_retrieve(struct osmocom_ms *ms, int number)
{
	struct gsm_call *call;
	struct gsm_mncc retr;
	int holdnum = 0, active = 0, i = 0;

	llist_for_each_entry(call, &call_list, entry) {
		if (call->hold)
			holdnum++;
		if (!call->hold)
			active = 1;
	}
	if (active) {
		LOGP(DMNCC, LOGL_INFO, "Cannot retrieve during active call\n");
		vty_notify(ms, NULL);
		vty_notify(ms, "Hold active call first!\n");
		return -EINVAL;
	}
	if (holdnum == 0) {
		vty_notify(ms, NULL);
		vty_notify(ms, "No call on hold!\n");
		return -EINVAL;
	}
	if (holdnum > 1 && number <= 0) {
		vty_notify(ms, NULL);
		vty_notify(ms, "Select call 1..%d\n", holdnum);
		return -EINVAL;
	}
	if (holdnum == 1 && number <= 0)
		number = 1;
	if (number > holdnum) {
		vty_notify(ms, NULL);
		vty_notify(ms, "Given number %d out of range!\n", number);
		vty_notify(ms, "Select call 1..%d\n", holdnum);
		return -EINVAL;
	}

	llist_for_each_entry(call, &call_list, entry) {
		i++;
		if (i == number)
			break;
	}

	memset(&retr, 0, sizeof(struct gsm_mncc));
	retr.callref = call->callref;
	return mncc_send(ms, MNCC_RETRIEVE_REQ, &retr);
}




