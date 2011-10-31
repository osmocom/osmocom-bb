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

#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/core/talloc.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/gsm48_cc.h>
#include <osmocom/bb/mobile/voice.h>
#include <l1ctl_proto.h>

extern void *l23_ctx;

static int gsm48_cc_tx_release(struct gsm_trans *trans, void *arg);
static int gsm48_rel_null_free(struct gsm_trans *trans);
int mncc_release_ind(struct osmocom_ms *ms, struct gsm_trans *trans,
		     uint32_t callref, int location, int value);
static int gsm48_cc_tx_disconnect(struct gsm_trans *trans, void *arg);
static int gsm48_cc_tx_connect_ack(struct gsm_trans *trans, void *arg);

/*
 * init
 */

int gsm48_cc_init(struct osmocom_ms *ms)
{
	struct gsm48_cclayer *cc = &ms->cclayer;

	cc->ms = ms;

	if (!cc->mncc_upqueue.next == 0)
		return 0;

	LOGP(DCC, LOGL_INFO, "init Call Control\n");

	INIT_LLIST_HEAD(&cc->mncc_upqueue);

	return 0;
}

int gsm48_cc_exit(struct osmocom_ms *ms)
{
	struct gsm48_cclayer *cc = &ms->cclayer;
	struct gsm_trans *trans, *trans2;
	struct msgb *msg;

	LOGP(DCC, LOGL_INFO, "exit Call Control processes for %s\n", ms->name);

	llist_for_each_entry_safe(trans, trans2, &ms->trans_list, entry) {
		if (trans->protocol == GSM48_PDISC_CC) {
			LOGP(DCC, LOGL_NOTICE, "Free pendig CC-transaction.\n");
			trans_free(trans);
		}
	}

	while ((msg = msgb_dequeue(&cc->mncc_upqueue)))
		msgb_free(msg);

	return 0;
}

/*
 * messages
 */

/* names of MNCC-SAP */
static const struct value_string gsm_mncc_names[] = {
	{ MNCC_SETUP_REQ,	"MNCC_SETUP_REQ" },
	{ MNCC_SETUP_IND,	"MNCC_SETUP_IND" },
	{ MNCC_SETUP_RSP,	"MNCC_SETUP_RSP" },
	{ MNCC_SETUP_CNF,	"MNCC_SETUP_CNF" },
	{ MNCC_SETUP_COMPL_REQ,	"MNCC_SETUP_COMPL_REQ" },
	{ MNCC_SETUP_COMPL_IND,	"MNCC_SETUP_COMPL_IND" },
	{ MNCC_CALL_CONF_IND,	"MNCC_CALL_CONF_IND" },
	{ MNCC_CALL_PROC_REQ,	"MNCC_CALL_PROC_REQ" },
	{ MNCC_PROGRESS_REQ,	"MNCC_PROGRESS_REQ" },
	{ MNCC_ALERT_REQ,	"MNCC_ALERT_REQ" },
	{ MNCC_ALERT_IND,	"MNCC_ALERT_IND" },
	{ MNCC_NOTIFY_REQ,	"MNCC_NOTIFY_REQ" },
	{ MNCC_NOTIFY_IND,	"MNCC_NOTIFY_IND" },
	{ MNCC_DISC_REQ,	"MNCC_DISC_REQ" },
	{ MNCC_DISC_IND,	"MNCC_DISC_IND" },
	{ MNCC_REL_REQ,		"MNCC_REL_REQ" },
	{ MNCC_REL_IND,		"MNCC_REL_IND" },
	{ MNCC_REL_CNF,		"MNCC_REL_CNF" },
	{ MNCC_FACILITY_REQ,	"MNCC_FACILITY_REQ" },
	{ MNCC_FACILITY_IND,	"MNCC_FACILITY_IND" },
	{ MNCC_START_DTMF_IND,	"MNCC_START_DTMF_IND" },
	{ MNCC_START_DTMF_RSP,	"MNCC_START_DTMF_RSP" },
	{ MNCC_START_DTMF_REJ,	"MNCC_START_DTMF_REJ" },
	{ MNCC_STOP_DTMF_IND,	"MNCC_STOP_DTMF_IND" },
	{ MNCC_STOP_DTMF_RSP,	"MNCC_STOP_DTMF_RSP" },
	{ MNCC_MODIFY_REQ,	"MNCC_MODIFY_REQ" },
	{ MNCC_MODIFY_IND,	"MNCC_MODIFY_IND" },
	{ MNCC_MODIFY_RSP,	"MNCC_MODIFY_RSP" },
	{ MNCC_MODIFY_CNF,	"MNCC_MODIFY_CNF" },
	{ MNCC_MODIFY_REJ,	"MNCC_MODIFY_REJ" },
	{ MNCC_HOLD_IND,	"MNCC_HOLD_IND" },
	{ MNCC_HOLD_CNF,	"MNCC_HOLD_CNF" },
	{ MNCC_HOLD_REJ,	"MNCC_HOLD_REJ" },
	{ MNCC_RETRIEVE_IND,	"MNCC_RETRIEVE_IND" },
	{ MNCC_RETRIEVE_CNF,	"MNCC_RETRIEVE_CNF" },
	{ MNCC_RETRIEVE_REJ,	"MNCC_RETRIEVE_REJ" },
	{ MNCC_USERINFO_REQ,	"MNCC_USERINFO_REQ" },
	{ MNCC_USERINFO_IND,	"MNCC_USERINFO_IND" },
	{ MNCC_REJ_REQ,		"MNCC_REJ_REQ" },
	{ MNCC_REJ_IND,		"MNCC_REJ_IND" },
	{ MNCC_PROGRESS_IND,	"MNCC_PROGRESS_IND" },
	{ MNCC_CALL_PROC_IND,	"MNCC_CALL_PROC_IND" },
	{ MNCC_CALL_CONF_REQ,	"MNCC_CALL_CONF_REQ" },
	{ MNCC_START_DTMF_REQ,	"MNCC_START_DTMF_REQ" },
	{ MNCC_STOP_DTMF_REQ,	"MNCC_STOP_DTMF_REQ" },
	{ MNCC_HOLD_REQ,	"MNCC_HOLD_REQ " },
	{ MNCC_RETRIEVE_REQ,	"MNCC_RETRIEVE_REQ" },
	{ MNCC_FRAME_RECV,      "MNCC_FRAME_RECV" },
	{ MNCC_FRAME_DROP,      "MNCC_FRAME_DROP" },
	{ MNCC_LCHAN_MODIFY,    "MNCC_LCHAN_MODIFY" },
	{ 0,			NULL }
};

const char *get_mncc_name(int value)
{
	return get_value_string(gsm_mncc_names, value);
}

/* push MMCC header and send to MM */
static int gsm48_cc_to_mm(struct msgb *msg, struct gsm_trans *trans,
	int msg_type)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_mmxx_hdr *mmh;
	int emergency = 0;

	/* Add protocol type and transaction ID */
	gh->proto_discr = trans->protocol | (trans->transaction_id << 4);

	/* indicate emergency setup to MM layer */
	if (gh->msg_type == GSM48_MT_CC_EMERG_SETUP)
		emergency = 1;

	/* push RR header */
	msgb_push(msg, sizeof(struct gsm48_mmxx_hdr));
	mmh = (struct gsm48_mmxx_hdr *)msg->data;
	mmh->msg_type = msg_type;
	mmh->ref = trans->callref;
	mmh->transaction_id = trans->transaction_id;
	mmh->sapi = 0;
	mmh->emergency = emergency;

	/* send message to MM */
	LOGP(DCC, LOGL_INFO, "Sending '%s' using %s (callref=%x, "
		"transaction_id=%d)\n", gsm48_cc_msg_name(gh->msg_type),
		get_mmxx_name(msg_type), trans->callref, trans->transaction_id);
	return gsm48_mmxx_downmsg(trans->ms, msg);
}

/* enqueue message to application (MNCC-SAP) */
static int mncc_recvmsg(struct osmocom_ms *ms, struct gsm_trans *trans,
       	int msg_type, struct gsm_mncc *mncc)
{
	struct gsm48_cclayer *cc = &ms->cclayer;
	struct msgb *msg;

	if (trans)
		LOGP(DCC, LOGL_INFO, "(ms %s ti %x) Sending '%s' to MNCC.\n",
			ms->name, trans->transaction_id,
			get_mncc_name(msg_type));
	else
		LOGP(DCC, LOGL_INFO, "(ms %s ti -) Sending '%s' to MNCC.\n",
			ms->name, get_mncc_name(msg_type));

	mncc->msg_type = msg_type;

	msg = msgb_alloc(sizeof(struct gsm_mncc), "MNCC");
	if (!msg)
		return -ENOMEM;
	memcpy(msg->data, mncc, sizeof(struct gsm_mncc));
	msgb_enqueue(&cc->mncc_upqueue, msg);

	return 0;
}

/* dequeue messages to layer 4 */
int mncc_dequeue(struct osmocom_ms *ms)
{
	struct gsm48_cclayer *cc = &ms->cclayer;
	struct gsm_mncc *mncc;
	struct msgb *msg;
	int work = 0;

	while ((msg = msgb_dequeue(&cc->mncc_upqueue))) {
		mncc = (struct gsm_mncc *)msg->data;
		if (ms->mncc_entity.mncc_recv)
			ms->mncc_entity.mncc_recv(ms, mncc->msg_type, mncc);
		work = 1; /* work done */
		msgb_free(msg);
	}

	return work;
}


/*
 * state transition
 */

static void new_cc_state(struct gsm_trans *trans, int state)
{
	if (state > 31 || state < 0)
		return;

	DEBUGP(DCC, "new state %s -> %s\n",
		gsm48_cc_state_name(trans->cc.state),
		gsm48_cc_state_name(state));

	trans->cc.state = state;
}

/*
 * timers
 */

/* timeout events of all timers */
static void gsm48_cc_timeout(void *arg)
{
	struct gsm_trans *trans = arg;
	int disconnect = 0, release = 0, abort = 1;
	int mo_cause = GSM48_CC_CAUSE_RECOVERY_TIMER;
	int mo_location = GSM48_CAUSE_LOC_PRN_S_LU;
	int l4_cause = GSM48_CC_CAUSE_NORMAL_UNSPEC;
	int l4_location = GSM48_CAUSE_LOC_PRN_S_LU;
	struct gsm_mncc mo_rel, l4_rel;

	memset(&mo_rel, 0, sizeof(struct gsm_mncc));
	mo_rel.callref = trans->callref;
	memset(&l4_rel, 0, sizeof(struct gsm_mncc));
	l4_rel.callref = trans->callref;

	LOGP(DCC, LOGL_INFO, "Timer T%x has fired.\n", trans->cc.Tcurrent);

	switch(trans->cc.Tcurrent) {
	case 0x303:
		/* abort if connection is not already esablished */
		if (trans->cc.state == GSM_CSTATE_MM_CONNECTION_PEND)
			abort = 1;
		else
			release = 1;
		l4_cause = GSM48_CC_CAUSE_USER_NOTRESPOND;
		break;
	case 0x305:
		release = 1;
		mo_cause = trans->cc.msg.cause.value;
		mo_location = trans->cc.msg.cause.location;
		break;
	case 0x308:
		if (!trans->cc.T308_second) {
			/* restart T308 a second time */
			gsm48_cc_tx_release(trans, &trans->cc.msg);
			trans->cc.T308_second = 1;
			break; /* stay in release state */
		}
		/* release MM conn, got NULL state, free trans */
		gsm48_rel_null_free(trans);

		return;
	case 0x310:
		disconnect = 1;
		l4_cause = GSM48_CC_CAUSE_USER_NOTRESPOND;
		break;
	case 0x313:
		disconnect = 1;
		/* unknown, did not find it in the specs */
		break;
	default:
		release = 1;
	}

	if ((release || abort) && trans->callref) {
		/* process release towards layer 4 */
		mncc_release_ind(trans->ms, trans, trans->callref,
				 l4_location, l4_cause);
	}

	if (disconnect && trans->callref) {
		/* process disconnect towards layer 4 */
		mncc_set_cause(&l4_rel, l4_location, l4_cause);
		mncc_recvmsg(trans->ms, trans, MNCC_DISC_IND, &l4_rel);
	}

	/* process disconnect towards mobile station */
	if (disconnect || release || abort) {
		mncc_set_cause(&mo_rel, mo_location, mo_cause);
		mo_rel.cause.diag[0] =
			((trans->cc.Tcurrent & 0xf00) >> 8) + '0';
		mo_rel.cause.diag[1] =
			((trans->cc.Tcurrent & 0x0f0) >> 4) + '0';
		mo_rel.cause.diag[2] = (trans->cc.Tcurrent & 0x00f) + '0';
		mo_rel.cause.diag_len = 3;

		if (disconnect)
			gsm48_cc_tx_disconnect(trans, &mo_rel);
		if (release)
			gsm48_cc_tx_release(trans, &mo_rel);
		if (abort) {
			/* release MM conn, got NULL state, free trans */
			gsm48_rel_null_free(trans);
		}
	}
}

/* start various timers */
static void gsm48_start_cc_timer(struct gsm_trans *trans, int current,
				 int sec, int micro)
{
	LOGP(DCC, LOGL_INFO, "starting timer T%x with %d seconds\n", current,
		sec);
	trans->cc.timer.cb = gsm48_cc_timeout;
	trans->cc.timer.data = trans;
	osmo_timer_schedule(&trans->cc.timer, sec, micro);
	trans->cc.Tcurrent = current;
}

/* stop various timers */
static void gsm48_stop_cc_timer(struct gsm_trans *trans)
{
	if (osmo_timer_pending(&trans->cc.timer)) {
		LOGP(DCC, LOGL_INFO, "stopping pending timer T%x\n",
			trans->cc.Tcurrent);
		osmo_timer_del(&trans->cc.timer);
		trans->cc.Tcurrent = 0;
	}
}

/*
 * process handlers (misc)
 */

/* Call Control Specific transaction release.
 * gets called by trans_free, DO NOT CALL YOURSELF!
 */
void _gsm48_cc_trans_free(struct gsm_trans *trans)
{
	gsm48_stop_cc_timer(trans);

	/* disable audio distribution */
	if (trans->ms->mncc_entity.ref == trans->callref)
		trans->ms->mncc_entity.ref = 0;

	/* send release to L4, if callref still exists */
	if (trans->callref) {
		/* Ressource unavailable */
		mncc_release_ind(trans->ms, trans, trans->callref,
			GSM48_CAUSE_LOC_PRN_S_LU,
			GSM48_CC_CAUSE_RESOURCE_UNAVAIL);
	}
	if (trans->cc.state != GSM_CSTATE_NULL)
		new_cc_state(trans, GSM_CSTATE_NULL);
}

/* release MM connection, go NULL state, free transaction */
static int gsm48_rel_null_free(struct gsm_trans *trans)
{
	struct msgb *nmsg;

	/* release MM connection */
	nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_REL_REQ, trans->callref,
					trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;
	LOGP(DCC, LOGL_INFO, "Sending MMCC_REL_REQ\n");
	gsm48_mmxx_downmsg(trans->ms, nmsg);

	new_cc_state(trans, GSM_CSTATE_NULL);

	trans->callref = 0;
	trans_free(trans);

	return 0;
}

void mncc_set_cause(struct gsm_mncc *data, int loc, int val)
{
        data->fields |= MNCC_F_CAUSE;
	data->cause.coding = 0x3;
	data->cause.location = loc;
	data->cause.value = val;
}

/* send release indication to upper layer */
int mncc_release_ind(struct osmocom_ms *ms, struct gsm_trans *trans,
		     uint32_t callref, int location, int value)
{
	struct gsm_mncc rel;

	memset(&rel, 0, sizeof(rel));
	rel.callref = callref;
	mncc_set_cause(&rel, location, value);
	return mncc_recvmsg(ms, trans, MNCC_REL_IND, &rel);
}

/* sending status message in response to unknown message */
static int gsm48_cc_tx_status(struct gsm_trans *trans, int cause)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	uint8_t *cause_ie, *call_state_ie;

	LOGP(DCC, LOGL_INFO, "sending STATUS (cause %d)\n", cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_STATUS;

	cause_ie = msgb_put(nmsg, 3);
	cause_ie[0] = 2;
	cause_ie[1] = GSM48_CAUSE_CS_GSM | GSM48_CAUSE_LOC_PRN_S_LU;
	cause_ie[2] = 0x80 | cause;

	call_state_ie = msgb_put(nmsg, 1);
	call_state_ie[0] = 0xc0 | trans->cc.state;

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* reply status enquiry */
static int gsm48_cc_rx_status_enq(struct gsm_trans *trans, struct msgb *msg)
{
	LOGP(DCC, LOGL_INFO, "received STATUS ENQUIREY\n");

	return gsm48_cc_tx_status(trans, GSM48_CC_CAUSE_RESP_STATUS_INQ);
}

static int gsm48_cc_rx_status(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc_cause cause;

	if (payload_len < 1 || payload_len < gh->data[0] + 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of status message "
			"error.\n");
		return -EINVAL;
	}
	gsm48_decode_cause(&cause, gh->data);

	LOGP(DCC, LOGL_INFO, "received STATUS (cause %d)\n", cause.value);

	return 0;
}

/*
 * process handlers (mobile originating call establish)
 */

/* on SETUP request from L4, init MM connection */
static int gsm48_cc_init_mm(struct gsm_trans *trans, void *arg)
{
	struct msgb *nmsg;
	struct gsm_mncc *data = arg;
	struct gsm48_mmxx_hdr *nmmh;

	/* store setup message */
	memcpy(&trans->cc.msg, data, sizeof(struct gsm_mncc));

	new_cc_state(trans, GSM_CSTATE_MM_CONNECTION_PEND);

	/* establish MM connection */
	nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_EST_REQ, trans->callref,
					trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;
	nmmh = (struct gsm48_mmxx_hdr *) nmsg->data;
	if (data->emergency)
		nmmh->emergency = 1;
	LOGP(DCC, LOGL_INFO, "Sending MMCC_EST_REQ\n");
	return gsm48_mmxx_downmsg(trans->ms, nmsg);
}

/* abort connection prior SETUP */
static int gsm48_cc_abort_mm(struct gsm_trans *trans, void *arg)
{
	struct msgb *nmsg;

	/* abort MM connection */
	nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_REL_REQ, trans->callref,
			trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;
	LOGP(DCC, LOGL_INFO, "Sending MMCC_REL_REQ\n");
	gsm48_mmxx_downmsg(trans->ms, nmsg);

	new_cc_state(trans, GSM_CSTATE_NULL);

	trans->callref = 0;
	trans_free(trans);

	return 0;
}

/* setup message from upper layer */
static int gsm48_cc_tx_setup(struct gsm_trans *trans)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm_mncc *setup = &trans->cc.msg;
	int rc, transaction_id;
	uint8_t *ie;

	LOGP(DCC, LOGL_INFO, "sending SETUP\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	/* transaction id must not be assigned */
	if (trans->transaction_id != 0xff) { /* unasssigned */
		LOGP(DCC, LOGL_NOTICE, "TX Setup with assigned transaction. "
			"This is not allowed!\n");
		/* Temporarily out of order */
		rc = mncc_release_ind(trans->ms, trans, trans->callref,
				      GSM48_CAUSE_LOC_PRN_S_LU,
				      GSM48_CC_CAUSE_NORMAL_UNSPEC);
		trans->callref = 0;
		trans_free(trans);
		return rc;
	}

	/* Get free transaction_id */
	transaction_id = trans_assign_trans_id(trans->ms, GSM48_PDISC_CC, 0);
	if (transaction_id < 0) {
		/* no free transaction ID */
		rc = mncc_release_ind(trans->ms, trans, trans->callref,
				      GSM48_CAUSE_LOC_PRN_S_LU,
				      GSM48_CC_CAUSE_RESOURCE_UNAVAIL);
		trans->callref = 0;
		trans_free(trans);
		return rc;
	}
	trans->transaction_id = transaction_id;

	gh->msg_type = (setup->emergency) ? GSM48_MT_CC_EMERG_SETUP :
						GSM48_MT_CC_SETUP;

	/* actually we have to start it when CM SERVICE REQUEST has been sent,
	 * but there is no primitive for that defined. i think it is ok to
	 * do it here rather than inventing MMCC-NOTIFY-IND.
	 */
	gsm48_start_cc_timer(trans, 0x303, GSM48_T303_MS);

	/* bearer capability (optional for emergency calls only) */
	if (setup->fields & MNCC_F_BEARER_CAP)
		gsm48_encode_bearer_cap(nmsg, 0, &setup->bearer_cap);
	if (!setup->emergency) {
		/* facility */
		if (setup->fields & MNCC_F_FACILITY)
			gsm48_encode_facility(nmsg, 0, &setup->facility);
		/* called party BCD number */
		if (setup->fields & MNCC_F_CALLED)
			gsm48_encode_called(nmsg, &setup->called);
		/* user-user */
		if (setup->fields & MNCC_F_USERUSER)
			gsm48_encode_useruser(nmsg, 0, &setup->useruser);
		/* ss version */
		if (setup->fields & MNCC_F_SSVERSION)
			gsm48_encode_ssversion(nmsg, &setup->ssversion);
		/* CLIR suppression */
		if (setup->clir.sup) {
			ie = msgb_put(nmsg, 1);
			ie[0] = GSM48_IE_CLIR_SUPP;
		}
		/* CLIR invocation */
		if (setup->clir.inv) {
			ie = msgb_put(nmsg, 1);
			ie[0] = GSM48_IE_CLIR_INVOC;
		}
		/* cc cap */
		if (setup->fields & MNCC_F_CCCAP)
			gsm48_encode_cccap(nmsg, &setup->cccap);
	}

	/* actually MM CONNECTION PENDING */
	new_cc_state(trans, GSM_CSTATE_INITIATED);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* progress is received from lower layer */
static int gsm48_cc_rx_progress(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc progress;

	LOGP(DCC, LOGL_INFO, "received PROGRESS\n");

	memset(&progress, 0, sizeof(struct gsm_mncc));
	progress.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len,
		GSM48_IE_PROGR_IND, 0);
	/* progress */
	if (TLVP_PRESENT(&tp, GSM48_IE_PROGR_IND)) {
		progress.fields |= MNCC_F_PROGRESS;
		gsm48_decode_progress(&progress.progress,
				TLVP_VAL(&tp, GSM48_IE_PROGR_IND)-1);
		/* store last progress indicator */
		trans->cc.prog_ind = progress.progress.descr;
	}
	/* user-user */
	if (TLVP_PRESENT(&tp, GSM48_IE_USER_USER)) {
		progress.fields |= MNCC_F_USERUSER;
		gsm48_decode_useruser(&progress.useruser,
				TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	}

	return mncc_recvmsg(trans->ms, trans, MNCC_PROGRESS_IND, &progress);
}

/* call proceeding is received from lower layer */
static int gsm48_cc_rx_call_proceeding(struct gsm_trans *trans,
	struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc call_proc;

	LOGP(DCC, LOGL_INFO, "sending CALL PROCEEDING\n");

	gsm48_stop_cc_timer(trans);

	memset(&call_proc, 0, sizeof(struct gsm_mncc));
	call_proc.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
#if 0
	/* repeat */
	if (TLVP_PRESENT(&tp, GSM48_IE_REPEAT_CIR))
		call_conf.repeat = 1;
	if (TLVP_PRESENT(&tp, GSM48_IE_REPEAT_SEQ))
		call_conf.repeat = 2;
#endif
	/* bearer capability */
	if (TLVP_PRESENT(&tp, GSM48_IE_BEARER_CAP)) {
		call_proc.fields |= MNCC_F_BEARER_CAP;
		gsm48_decode_bearer_cap(&call_proc.bearer_cap,
				  TLVP_VAL(&tp, GSM48_IE_BEARER_CAP)-1);
	}
	/* facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		call_proc.fields |= MNCC_F_FACILITY;
		gsm48_decode_facility(&call_proc.facility,
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}

	/* progress */
	if (TLVP_PRESENT(&tp, GSM48_IE_PROGR_IND)) {
		call_proc.fields |= MNCC_F_PROGRESS;
		gsm48_decode_progress(&call_proc.progress,
				TLVP_VAL(&tp, GSM48_IE_PROGR_IND)-1);
		/* store last progress indicator */
		trans->cc.prog_ind = call_proc.progress.descr;
	}

	/* start T310, if last progress indicator was 1 or 2 or 64 */
	if (trans->cc.prog_ind == 1
	 || trans->cc.prog_ind == 2
	 || trans->cc.prog_ind == 64)
		gsm48_start_cc_timer(trans, 0x310, GSM48_T310_MS);

	new_cc_state(trans, GSM_CSTATE_MO_CALL_PROC);

	return mncc_recvmsg(trans->ms, trans, MNCC_CALL_PROC_IND,
			    &call_proc);
}

/* alerting is received by the lower layer */
static int gsm48_cc_rx_alerting(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc alerting;

	LOGP(DCC, LOGL_INFO, "received ALERTING\n");

	gsm48_stop_cc_timer(trans);
	/* no T301 in MS call control */

	memset(&alerting, 0, sizeof(struct gsm_mncc));
	alerting.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
	/* facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		alerting.fields |= MNCC_F_FACILITY;
		gsm48_decode_facility(&alerting.facility,
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* progress */
	if (TLVP_PRESENT(&tp, GSM48_IE_PROGR_IND)) {
		alerting.fields |= MNCC_F_PROGRESS;
		gsm48_decode_progress(&alerting.progress,
				TLVP_VAL(&tp, GSM48_IE_PROGR_IND)-1);
	}
	/* user-user */
	if (TLVP_PRESENT(&tp, GSM48_IE_USER_USER)) {
		alerting.fields |= MNCC_F_USERUSER;
		gsm48_decode_useruser(&alerting.useruser,
				TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	}

	new_cc_state(trans, GSM_CSTATE_CALL_DELIVERED);

	return mncc_recvmsg(trans->ms, trans, MNCC_ALERT_IND,
			    &alerting);
}

/* connect is received from lower layer */
static int gsm48_cc_rx_connect(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc connect;

	LOGP(DCC, LOGL_INFO, "received CONNECT\n");

	gsm48_stop_cc_timer(trans);

	memset(&connect, 0, sizeof(struct gsm_mncc));
	connect.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
	/* facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		connect.fields |= MNCC_F_FACILITY;
		gsm48_decode_facility(&connect.facility,
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* connected */
	if (TLVP_PRESENT(&tp, GSM48_IE_CONN_BCD)) {
		connect.fields |= MNCC_F_CONNECTED;
		gsm48_decode_connected(&connect.connected,
				TLVP_VAL(&tp, GSM48_IE_CONN_BCD)-1);
	}
	/* progress */
	if (TLVP_PRESENT(&tp, GSM48_IE_PROGR_IND)) {
		connect.fields |= MNCC_F_PROGRESS;
		gsm48_decode_progress(&connect.progress,
				TLVP_VAL(&tp, GSM48_IE_PROGR_IND)-1);
	}
	/* user-user */
	if (TLVP_PRESENT(&tp, GSM48_IE_USER_USER)) {
		connect.fields |= MNCC_F_USERUSER;
		gsm48_decode_useruser(&connect.useruser,
				TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	}

	/* ACTIVE state is set during this: */
	gsm48_cc_tx_connect_ack(trans, NULL);

	return mncc_recvmsg(trans->ms, trans, MNCC_SETUP_CNF, &connect);
}

/* connect ack message from upper layer */
static int gsm48_cc_tx_connect_ack(struct gsm_trans *trans, void *arg)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending CONNECT ACKNOWLEDGE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_CONNECT_ACK;

	new_cc_state(trans, GSM_CSTATE_ACTIVE);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/*
 * process handlers (mobile terminating call establish)
 */

/* setup is received from lower layer */
static int gsm48_cc_rx_setup(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc setup;

	LOGP(DCC, LOGL_INFO, "received SETUP\n");

	memset(&setup, 0, sizeof(struct gsm_mncc));
	setup.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);

	/* bearer capability */
	if (TLVP_PRESENT(&tp, GSM48_IE_BEARER_CAP)) {
		setup.fields |= MNCC_F_BEARER_CAP;
		gsm48_decode_bearer_cap(&setup.bearer_cap,
				  TLVP_VAL(&tp, GSM48_IE_BEARER_CAP)-1);
	}
	/* facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		setup.fields |= MNCC_F_FACILITY;
		gsm48_decode_facility(&setup.facility,
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* progress */
	if (TLVP_PRESENT(&tp, GSM48_IE_PROGR_IND)) {
		setup.fields |= MNCC_F_PROGRESS;
		gsm48_decode_progress(&setup.progress,
				TLVP_VAL(&tp, GSM48_IE_PROGR_IND)-1);
	}
	/* signal */
	if (TLVP_PRESENT(&tp, GSM48_IE_SIGNAL)) {
		setup.fields |= MNCC_F_SIGNAL;
		gsm48_decode_signal(&setup.signal,
				TLVP_VAL(&tp, GSM48_IE_SIGNAL)-1);
	}
	/* calling party bcd number */
	if (TLVP_PRESENT(&tp, GSM48_IE_CALLING_BCD)) {
		setup.fields |= MNCC_F_CALLING;
		gsm48_decode_calling(&setup.calling,
			      TLVP_VAL(&tp, GSM48_IE_CALLING_BCD)-1);
	}
	/* called party bcd number */
	if (TLVP_PRESENT(&tp, GSM48_IE_CALLED_BCD)) {
		setup.fields |= MNCC_F_CALLED;
		gsm48_decode_called(&setup.called,
			      TLVP_VAL(&tp, GSM48_IE_CALLED_BCD)-1);
	}
	/* redirecting party bcd number */
	if (TLVP_PRESENT(&tp, GSM48_IE_REDIR_BCD)) {
		setup.fields |= MNCC_F_REDIRECTING;
		gsm48_decode_redirecting(&setup.redirecting,
			      TLVP_VAL(&tp, GSM48_IE_REDIR_BCD)-1);
	}
	/* user-user */
	if (TLVP_PRESENT(&tp, GSM48_IE_USER_USER)) {
		setup.fields |= MNCC_F_USERUSER;
		gsm48_decode_useruser(&setup.useruser,
				TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	}

	new_cc_state(trans, GSM_CSTATE_CALL_PRESENT);

	/* indicate setup to MNCC */
	mncc_recvmsg(trans->ms, trans, MNCC_SETUP_IND, &setup);

	return 0;
}

/* call conf message from upper layer */
static int gsm48_cc_tx_call_conf(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *confirm = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending CALL CONFIRMED (proceeding)\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_CALL_CONF;

	new_cc_state(trans, GSM_CSTATE_MO_TERM_CALL_CONF);

	/* bearer capability */
	if (confirm->fields & MNCC_F_BEARER_CAP)
		gsm48_encode_bearer_cap(nmsg, 0, &confirm->bearer_cap);
	/* cause */
	if (confirm->fields & MNCC_F_CAUSE)
		gsm48_encode_cause(nmsg, 0, &confirm->cause);
	/* cc cap */
	if (confirm->fields & MNCC_F_CCCAP)
		gsm48_encode_cccap(nmsg, &confirm->cccap);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* alerting message from upper layer */
static int gsm48_cc_tx_alerting(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *alerting = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending ALERTING\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_ALERTING;

	/* facility */
	if (alerting->fields & MNCC_F_FACILITY)
		gsm48_encode_facility(nmsg, 0, &alerting->facility);
	/* user-user */
	if (alerting->fields & MNCC_F_USERUSER)
		gsm48_encode_useruser(nmsg, 0, &alerting->useruser);
	/* ss version */
	if (alerting->fields & MNCC_F_SSVERSION)
		gsm48_encode_ssversion(nmsg, &alerting->ssversion);

	new_cc_state(trans, GSM_CSTATE_CALL_RECEIVED);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* connect message from upper layer */
static int gsm48_cc_tx_connect(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *connect = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending CONNECT\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_CONNECT;

	gsm48_stop_cc_timer(trans);
	gsm48_start_cc_timer(trans, 0x313, GSM48_T313_MS);

	/* facility */
	if (connect->fields & MNCC_F_FACILITY)
		gsm48_encode_facility(nmsg, 0, &connect->facility);
	/* user-user */
	if (connect->fields & MNCC_F_USERUSER)
		gsm48_encode_useruser(nmsg, 0, &connect->useruser);
	/* ss version */
	if (connect->fields & MNCC_F_SSVERSION)
		gsm48_encode_ssversion(nmsg, &connect->ssversion);

	new_cc_state(trans, GSM_CSTATE_CONNECT_REQUEST);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* connect ack is received from lower layer */
static int gsm48_cc_rx_connect_ack(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm_mncc connect_ack;

	LOGP(DCC, LOGL_INFO, "received CONNECT ACKNOWLEDGE\n");

	gsm48_stop_cc_timer(trans);

	new_cc_state(trans, GSM_CSTATE_ACTIVE);

	memset(&connect_ack, 0, sizeof(struct gsm_mncc));
	connect_ack.callref = trans->callref;
	return mncc_recvmsg(trans->ms, trans, MNCC_SETUP_COMPL_IND,
			    &connect_ack);
}

/*
 * process handlers (during active state)
 */

/* notify message from upper layer */
static int gsm48_cc_tx_notify(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *notify = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending NOTIFY\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_NOTIFY;

	/* notify */
	gsm48_encode_notify(nmsg, notify->notify);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* notify is received from lower layer */
static int gsm48_cc_rx_notify(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc notify;

	LOGP(DCC, LOGL_INFO, "received NOTIFY\n");

	memset(&notify, 0, sizeof(struct gsm_mncc));
	notify.callref = trans->callref;
	/* notify */
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of notify message error.\n");
		return -EINVAL;
	}
	gsm48_decode_notify(&notify.notify, gh->data);

	return mncc_recvmsg(trans->ms, trans, MNCC_NOTIFY_IND, &notify);
}

/* start dtmf message from upper layer */
static int gsm48_cc_tx_start_dtmf(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *dtmf = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending START DTMF\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_START_DTMF;

	/* keypad */
	gsm48_encode_keypad(nmsg, dtmf->keypad);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* start dtmf ack is received from lower layer */
static int gsm48_cc_rx_start_dtmf_ack(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc dtmf;

	LOGP(DCC, LOGL_INFO, "received START DTMF ACKNOWLEDGE\n");

	memset(&dtmf, 0, sizeof(struct gsm_mncc));
	dtmf.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
	/* keypad facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_KPD_FACILITY)) {
		dtmf.fields |= MNCC_F_KEYPAD;
		gsm48_decode_keypad(&dtmf.keypad,
			      TLVP_VAL(&tp, GSM48_IE_KPD_FACILITY)-1);
	}

	return mncc_recvmsg(trans->ms, trans, MNCC_START_DTMF_RSP, &dtmf);
}

/* start dtmf rej is received from lower layer */
static int gsm48_cc_rx_start_dtmf_rej(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc dtmf;

	LOGP(DCC, LOGL_INFO, "received START DTMF REJECT\n");

	memset(&dtmf, 0, sizeof(struct gsm_mncc));
	dtmf.callref = trans->callref;
	/* cause */
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of dtmf reject message "
			"error.\n");
		return -EINVAL;
	}
	gsm48_decode_cause(&dtmf.cause, gh->data);

	return mncc_recvmsg(trans->ms, trans, MNCC_START_DTMF_REJ, &dtmf);
}

/* stop dtmf message from upper layer */
static int gsm48_cc_tx_stop_dtmf(struct gsm_trans *trans, void *arg)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending STOP DTMF\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_STOP_DTMF;

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* stop dtmf ack is received from lower layer */
static int gsm48_cc_rx_stop_dtmf_ack(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc dtmf;

	LOGP(DCC, LOGL_INFO, "received STOP DTMF ACKNOWLEDGE\n");

	memset(&dtmf, 0, sizeof(struct gsm_mncc));
	dtmf.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);

	return mncc_recvmsg(trans->ms, trans, MNCC_STOP_DTMF_RSP, &dtmf);
}

/* hold message from upper layer */
static int gsm48_cc_tx_hold(struct gsm_trans *trans, void *arg)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending HOLD\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_HOLD;

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* hold ack is received from lower layer */
static int gsm48_cc_rx_hold_ack(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm_mncc hold;

	LOGP(DCC, LOGL_INFO, "received HOLD ACKNOWLEDGE\n");

	memset(&hold, 0, sizeof(struct gsm_mncc));
	hold.callref = trans->callref;

	return mncc_recvmsg(trans->ms, trans, MNCC_HOLD_CNF, &hold);
}

/* hold rej is received from lower layer */
static int gsm48_cc_rx_hold_rej(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc hold;

	LOGP(DCC, LOGL_INFO, "received HOLD REJECT\n");

	memset(&hold, 0, sizeof(struct gsm_mncc));
	hold.callref = trans->callref;
	/* cause */
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of hold reject message "
			"error.\n");
		return -EINVAL;
	}
	gsm48_decode_cause(&hold.cause, gh->data);

	return mncc_recvmsg(trans->ms, trans, MNCC_HOLD_REJ, &hold);
}

/* retrieve message from upper layer */
static int gsm48_cc_tx_retrieve(struct gsm_trans *trans, void *arg)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending RETRIEVE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_RETR;

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* retrieve ack is received from lower layer */
static int gsm48_cc_rx_retrieve_ack(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm_mncc retrieve;

	LOGP(DCC, LOGL_INFO, "received RETRIEVE ACKNOWLEDGE\n");

	memset(&retrieve, 0, sizeof(struct gsm_mncc));
	retrieve.callref = trans->callref;

	return mncc_recvmsg(trans->ms, trans, MNCC_RETRIEVE_CNF, &retrieve);
}

/* retrieve rej is received from lower layer */
static int gsm48_cc_rx_retrieve_rej(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc retrieve;

	LOGP(DCC, LOGL_INFO, "received RETRIEVE REJECT\n");

	memset(&retrieve, 0, sizeof(struct gsm_mncc));
	retrieve.callref = trans->callref;
	/* cause */
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of retrieve reject message "
			"error.\n");
		return -EINVAL;
	}
	gsm48_decode_cause(&retrieve.cause, gh->data);

	return mncc_recvmsg(trans->ms, trans, MNCC_RETRIEVE_REJ, &retrieve);
}

/* facility message from upper layer or from timer event */
static int gsm48_cc_tx_facility(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *fac = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending FACILITY\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_FACILITY;

	/* facility */
	gsm48_encode_facility(nmsg, 1, &fac->facility);
	/* ss version */
	if (fac->fields & MNCC_F_SSVERSION)
		gsm48_encode_ssversion(nmsg, &fac->ssversion);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* facility is received from lower layer */
static int gsm48_cc_rx_facility(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc fac;

	LOGP(DCC, LOGL_INFO, "received FACILITY\n");

	memset(&fac, 0, sizeof(struct gsm_mncc));
	fac.callref = trans->callref;
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of facility message "
			"error.\n");
		return -EINVAL;
	}
	/* facility */
	gsm48_decode_facility(&fac.facility, gh->data);

	return mncc_recvmsg(trans->ms, trans, MNCC_FACILITY_IND, &fac);
}

/* user info message from upper layer or from timer event */
static int gsm48_cc_tx_userinfo(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *user = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending USERINFO\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_USER_INFO;

	/* user-user */
	if (user->fields & MNCC_F_USERUSER)
		gsm48_encode_useruser(nmsg, 1, &user->useruser);
	/* more data */
	if (user->more)
		gsm48_encode_more(nmsg);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* user info is received from lower layer */
static int gsm48_cc_rx_userinfo(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc user;

	LOGP(DCC, LOGL_INFO, "received USERINFO\n");

	memset(&user, 0, sizeof(struct gsm_mncc));
	user.callref = trans->callref;
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of userinfo message "
			"error.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len,
		GSM48_IE_USER_USER, 0);
	/* user-user */
	gsm48_decode_useruser(&user.useruser,
			TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	/* more data */
	if (TLVP_PRESENT(&tp, GSM48_IE_MORE_DATA))
		user.more = 1;

	return mncc_recvmsg(trans->ms, trans, MNCC_USERINFO_IND, &user);
}

/* modify message from upper layer or from timer event */
static int gsm48_cc_tx_modify(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *modify = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending MODIFY\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_MODIFY;

	gsm48_start_cc_timer(trans, 0x323, GSM48_T323_MS);

	/* bearer capability */
	gsm48_encode_bearer_cap(nmsg, 1, &modify->bearer_cap);

	new_cc_state(trans, GSM_CSTATE_MO_TERM_MODIFY);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* modify complete is received from lower layer */
static int gsm48_cc_rx_modify_complete(struct gsm_trans *trans,
	struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc modify;

	LOGP(DCC, LOGL_INFO, "received MODIFY COMPLETE\n");

	gsm48_stop_cc_timer(trans);

	memset(&modify, 0, sizeof(struct gsm_mncc));
	modify.callref = trans->callref;
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of modify complete message "
			"error.\n");
		return -EINVAL;
	}
	/* bearer capability */
	gsm48_decode_bearer_cap(&modify.bearer_cap, gh->data);

	new_cc_state(trans, GSM_CSTATE_ACTIVE);

	return mncc_recvmsg(trans->ms, trans, MNCC_MODIFY_CNF, &modify);
}

/* modify reject is received from lower layer */
static int gsm48_cc_rx_modify_reject(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc modify;

	LOGP(DCC, LOGL_INFO, "received MODIFY REJECT\n");

	gsm48_stop_cc_timer(trans);

	memset(&modify, 0, sizeof(struct gsm_mncc));
	modify.callref = trans->callref;
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of modify reject message "
			"error.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len,
		GSM48_IE_BEARER_CAP, GSM48_IE_CAUSE);
	/* bearer capability */
	if (TLVP_PRESENT(&tp, GSM48_IE_BEARER_CAP)) {
		modify.fields |= MNCC_F_BEARER_CAP;
		gsm48_decode_bearer_cap(&modify.bearer_cap,
				  TLVP_VAL(&tp, GSM48_IE_BEARER_CAP)-1);
	}
	/* cause */
	if (TLVP_PRESENT(&tp, GSM48_IE_CAUSE)) {
		modify.fields |= MNCC_F_CAUSE;
		gsm48_decode_cause(&modify.cause,
			     TLVP_VAL(&tp, GSM48_IE_CAUSE)-1);
	}

	new_cc_state(trans, GSM_CSTATE_ACTIVE);

	return mncc_recvmsg(trans->ms, trans, MNCC_MODIFY_REJ, &modify);
}

/* modify is received from lower layer */
static int gsm48_cc_rx_modify(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm_mncc modify;

	LOGP(DCC, LOGL_INFO, "received MODIFY\n");

	memset(&modify, 0, sizeof(struct gsm_mncc));
	modify.callref = trans->callref;
	if (payload_len < 1) {
		LOGP(DCC, LOGL_NOTICE, "Short read of modify message error.\n");
		return -EINVAL;
	}
	/* bearer capability */
	gsm48_decode_bearer_cap(&modify.bearer_cap, gh->data);

	new_cc_state(trans, GSM_CSTATE_MO_ORIG_MODIFY);

	return mncc_recvmsg(trans->ms, trans, MNCC_MODIFY_IND, &modify);
}

/* modify complete message from upper layer or from timer event */
static int gsm48_cc_tx_modify_complete(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *modify = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending MODIFY COMPLETE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_MODIFY_COMPL;

	/* bearer capability */
	gsm48_encode_bearer_cap(nmsg, 1, &modify->bearer_cap);

	new_cc_state(trans, GSM_CSTATE_ACTIVE);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* modify reject message from upper layer or from timer event */
static int gsm48_cc_tx_modify_reject(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *modify = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending MODIFY REJECT\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_MODIFY_REJECT;

	/* bearer capability */
	gsm48_encode_bearer_cap(nmsg, 1, &modify->bearer_cap);
	/* cause */
	gsm48_encode_cause(nmsg, 1, &modify->cause);

	new_cc_state(trans, GSM_CSTATE_ACTIVE);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/*
 * process handlers (call clearing)
 */

static struct gsm_mncc_cause default_cause = {
	.location       = GSM48_CAUSE_LOC_PRN_S_LU,
	.coding         = 0,
	.rec            = 0,
	.rec_val        = 0,
	.value          = GSM48_CC_CAUSE_NORMAL_UNSPEC,
	.diag_len       = 0,
	.diag           = { 0 },
};

/* disconnect message from upper layer or from timer event */
static int gsm48_cc_tx_disconnect(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *disc = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending DISCONNECT\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_DISCONNECT;

	gsm48_stop_cc_timer(trans);
	gsm48_start_cc_timer(trans, 0x305, GSM48_T305_MS);

	/* cause */
	if (disc->fields & MNCC_F_CAUSE)
		gsm48_encode_cause(nmsg, 1, &disc->cause);
	else
		gsm48_encode_cause(nmsg, 1, &default_cause);

	/* facility */
	if (disc->fields & MNCC_F_FACILITY)
		gsm48_encode_facility(nmsg, 0, &disc->facility);
	/* progress */
	if (disc->fields & MNCC_F_PROGRESS)
		gsm48_encode_progress(nmsg, 0, &disc->progress);
	/* user-user */
	if (disc->fields & MNCC_F_USERUSER)
		gsm48_encode_useruser(nmsg, 0, &disc->useruser);
	/* ss version */
	if (disc->fields & MNCC_F_SSVERSION)
		gsm48_encode_ssversion(nmsg, &disc->ssversion);

	new_cc_state(trans, GSM_CSTATE_DISCONNECT_REQ);

	return gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);
}

/* release message from upper layer or from timer event */
static int gsm48_cc_tx_release(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *rel = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending RELEASE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_RELEASE;

	gsm48_stop_cc_timer(trans);
	gsm48_start_cc_timer(trans, 0x308, GSM48_T308_MS);

	/* cause */
	if (rel->fields & MNCC_F_CAUSE)
		gsm48_encode_cause(nmsg, 0, &rel->cause);
	/* facility */
	if (rel->fields & MNCC_F_FACILITY)
		gsm48_encode_facility(nmsg, 0, &rel->facility);
	/* user-user */
	if (rel->fields & MNCC_F_USERUSER)
		gsm48_encode_useruser(nmsg, 0, &rel->useruser);
	/* ss version */
	if (rel->fields & MNCC_F_SSVERSION)
		gsm48_encode_ssversion(nmsg, &rel->ssversion);

	trans->cc.T308_second = 0;
	memcpy(&trans->cc.msg, rel, sizeof(struct gsm_mncc));

	if (trans->cc.state != GSM_CSTATE_RELEASE_REQ)
		new_cc_state(trans, GSM_CSTATE_RELEASE_REQ);

	gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);

#if 0
	/* release without sending MMCC_REL_REQ */
	new_cc_state(trans, GSM_CSTATE_NULL);
	trans->callref = 0;
	trans_free(trans);
#endif

	return 0;
}

/* reject message from upper layer */
static int gsm48_cc_tx_release_compl(struct gsm_trans *trans, void *arg)
{
	struct gsm_mncc *rel = arg;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;

	LOGP(DCC, LOGL_INFO, "sending RELEASE COMPLETE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->msg_type = GSM48_MT_CC_RELEASE_COMPL;

	gsm48_stop_cc_timer(trans);

	/* cause */
	if (rel->fields & MNCC_F_CAUSE)
		gsm48_encode_cause(nmsg, 0, &rel->cause);
	/* facility */
	if (rel->fields & MNCC_F_FACILITY)
		gsm48_encode_facility(nmsg, 0, &rel->facility);
	/* user-user */
	if (rel->fields & MNCC_F_USERUSER)
		gsm48_encode_useruser(nmsg, 0, &rel->useruser);
	/* ss version */
	if (rel->fields & MNCC_F_SSVERSION)
		gsm48_encode_ssversion(nmsg, &rel->ssversion);

	/* release without sending MMCC_REL_REQ */
	new_cc_state(trans, GSM_CSTATE_NULL);
	trans->callref = 0;
	trans_free(trans);

	return 0;
}

/* disconnect is received from lower layer */
static int gsm48_cc_rx_disconnect(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc disc;

	LOGP(DCC, LOGL_INFO, "received DISCONNECT\n");

	gsm48_stop_cc_timer(trans);

	new_cc_state(trans, GSM_CSTATE_DISCONNECT_IND);

	memset(&disc, 0, sizeof(struct gsm_mncc));
	disc.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len,
		GSM48_IE_CAUSE, 0);
	/* cause */
	if (TLVP_PRESENT(&tp, GSM48_IE_CAUSE)) {
		disc.fields |= MNCC_F_CAUSE;
		gsm48_decode_cause(&disc.cause,
			     TLVP_VAL(&tp, GSM48_IE_CAUSE)-1);
	}
	/* facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		disc.fields |= MNCC_F_FACILITY;
		gsm48_decode_facility(&disc.facility,
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* progress */
	if (TLVP_PRESENT(&tp, GSM48_IE_PROGR_IND)) {
		disc.fields |= MNCC_F_PROGRESS;
		gsm48_decode_progress(&disc.progress,
				TLVP_VAL(&tp, GSM48_IE_PROGR_IND)-1);
	}
	/* user-user */
	if (TLVP_PRESENT(&tp, GSM48_IE_USER_USER)) {
		disc.fields |= MNCC_F_USERUSER;
		gsm48_decode_useruser(&disc.useruser,
				TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	}

	/* store disconnect cause for T305 expiry */
	memcpy(&trans->cc.msg, &disc, sizeof(struct gsm_mncc));

	return mncc_recvmsg(trans->ms, trans, MNCC_DISC_IND, &disc);
}

/* release is received from lower layer */
static int gsm48_cc_rx_release(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc rel;

	LOGP(DCC, LOGL_INFO, "received RELEASE\n");

	gsm48_stop_cc_timer(trans);

	memset(&rel, 0, sizeof(struct gsm_mncc));
	rel.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
	/* cause */
	if (TLVP_PRESENT(&tp, GSM48_IE_CAUSE)) {
		rel.fields |= MNCC_F_CAUSE;
		gsm48_decode_cause(&rel.cause,
			     TLVP_VAL(&tp, GSM48_IE_CAUSE)-1);
	}
	/* facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		rel.fields |= MNCC_F_FACILITY;
		gsm48_decode_facility(&rel.facility,
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* user-user */
	if (TLVP_PRESENT(&tp, GSM48_IE_USER_USER)) {
		rel.fields |= MNCC_F_USERUSER;
		gsm48_decode_useruser(&rel.useruser,
				TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	}

	/* in case we receive a relase, when we are already in NULL state */
	if (trans->cc.state == GSM_CSTATE_NULL) {
		LOGP(DCC, LOGL_INFO, "ignoring RELEASE in NULL state\n");
		/* release MM conn, free trans */
		return gsm48_rel_null_free(trans);
	}
	if (trans->cc.state == GSM_CSTATE_RELEASE_REQ) {
		/* release collision 5.4.5 */
		mncc_recvmsg(trans->ms, trans, MNCC_REL_CNF, &rel);
	} else {
		struct msgb *nmsg;

		/* forward cause only */
		LOGP(DCC, LOGL_INFO, "sending RELEASE COMPLETE\n");

		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

		gh->msg_type = GSM48_MT_CC_RELEASE_COMPL;

		if (rel.fields & MNCC_F_CAUSE)
			gsm48_encode_cause(nmsg, 0, &rel.cause);

		gsm48_cc_to_mm(nmsg, trans, GSM48_MMCC_DATA_REQ);

		/* release indication */
		mncc_recvmsg(trans->ms, trans, MNCC_REL_IND, &rel);
	}

	/* release MM conn, got NULL state, free trans */
	return gsm48_rel_null_free(trans);
}

/* release complete is received from lower layer */
static int gsm48_cc_rx_release_compl(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;
	struct gsm_mncc rel;

	LOGP(DCC, LOGL_INFO, "received RELEASE COMPLETE\n");

	gsm48_stop_cc_timer(trans);

	memset(&rel, 0, sizeof(struct gsm_mncc));
	rel.callref = trans->callref;
	tlv_parse(&tp, &gsm48_att_tlvdef, gh->data, payload_len, 0, 0);
	/* cause */
	if (TLVP_PRESENT(&tp, GSM48_IE_CAUSE)) {
		rel.fields |= MNCC_F_CAUSE;
		gsm48_decode_cause(&rel.cause,
			     TLVP_VAL(&tp, GSM48_IE_CAUSE)-1);
	}
	/* facility */
	if (TLVP_PRESENT(&tp, GSM48_IE_FACILITY)) {
		rel.fields |= MNCC_F_FACILITY;
		gsm48_decode_facility(&rel.facility,
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* user-user */
	if (TLVP_PRESENT(&tp, GSM48_IE_USER_USER)) {
		rel.fields |= MNCC_F_USERUSER;
		gsm48_decode_useruser(&rel.useruser,
				TLVP_VAL(&tp, GSM48_IE_USER_USER)-1);
	}

	if (trans->callref) {
		switch (trans->cc.state) {
		case GSM_CSTATE_CALL_PRESENT:
			mncc_recvmsg(trans->ms, trans,
					  MNCC_REJ_IND, &rel);
			break;
		case GSM_CSTATE_RELEASE_REQ:
			mncc_recvmsg(trans->ms, trans,
					  MNCC_REL_CNF, &rel);
			break;
		default:
			mncc_recvmsg(trans->ms, trans,
					  MNCC_REL_IND, &rel);
		}
	}

	/* release MM conn, got NULL state, free trans */
	return gsm48_rel_null_free(trans);
}

/*
 * state machines
 */

/* state trasitions for MNCC messages (upper layer) */
static struct downstate {
	uint32_t	states;
	int		type;
	int		(*rout) (struct gsm_trans *trans, void *arg);
} downstatelist[] = {
	/* mobile originating call establishment */
	{SBIT(GSM_CSTATE_NULL), /* 5.2.1 */
	 MNCC_SETUP_REQ, gsm48_cc_init_mm},

	{SBIT(GSM_CSTATE_MM_CONNECTION_PEND), /* 5.2.1 */
	 MNCC_REL_REQ, gsm48_cc_abort_mm},

	/* mobile terminating call establishment */
	{SBIT(GSM_CSTATE_CALL_PRESENT), /* 5.2.2.3.1 */
	 MNCC_CALL_CONF_REQ, gsm48_cc_tx_call_conf},

	{SBIT(GSM_CSTATE_MO_TERM_CALL_CONF), /* 5.2.2.3.2 */
	 MNCC_ALERT_REQ, gsm48_cc_tx_alerting},

	{SBIT(GSM_CSTATE_MO_TERM_CALL_CONF) |
	 SBIT(GSM_CSTATE_CALL_RECEIVED), /* 5.2.2.5 */
	 MNCC_SETUP_RSP, gsm48_cc_tx_connect},

	 /* signalling during call */
	{SBIT(GSM_CSTATE_ACTIVE), /* 5.3.1 */
	 MNCC_NOTIFY_REQ, gsm48_cc_tx_notify},

	{ALL_STATES, /* 5.5.7.1 */
	 MNCC_START_DTMF_REQ, gsm48_cc_tx_start_dtmf},

	{ALL_STATES, /* 5.5.7.3 */
	 MNCC_STOP_DTMF_REQ, gsm48_cc_tx_stop_dtmf},

	{SBIT(GSM_CSTATE_ACTIVE),
	 MNCC_HOLD_REQ, gsm48_cc_tx_hold},

	{SBIT(GSM_CSTATE_ACTIVE),
	 MNCC_RETRIEVE_REQ, gsm48_cc_tx_retrieve},

	{ALL_STATES - SBIT(GSM_CSTATE_NULL) - SBIT(GSM_CSTATE_RELEASE_REQ),
	 MNCC_FACILITY_REQ, gsm48_cc_tx_facility},

	{SBIT(GSM_CSTATE_ACTIVE),
	 MNCC_USERINFO_REQ, gsm48_cc_tx_userinfo},

	/* clearing */
	{ALL_STATES - SBIT(GSM_CSTATE_NULL) - SBIT(GSM_CSTATE_DISCONNECT_IND) -
	 SBIT(GSM_CSTATE_RELEASE_REQ) -
	 SBIT(GSM_CSTATE_DISCONNECT_REQ), /* 5.4.3.1 */
	 MNCC_DISC_REQ, gsm48_cc_tx_disconnect},

	{SBIT(GSM_CSTATE_INITIATED),
	 MNCC_REJ_REQ, gsm48_cc_tx_release_compl},

	{ALL_STATES - SBIT(GSM_CSTATE_NULL) -
	 SBIT(GSM_CSTATE_RELEASE_REQ), /* ??? */
	 MNCC_REL_REQ, gsm48_cc_tx_release},

	/* modify */
	{SBIT(GSM_CSTATE_ACTIVE),
	 MNCC_MODIFY_REQ, gsm48_cc_tx_modify},

	{SBIT(GSM_CSTATE_MO_ORIG_MODIFY),
	 MNCC_MODIFY_RSP, gsm48_cc_tx_modify_complete},

	{SBIT(GSM_CSTATE_MO_ORIG_MODIFY),
	 MNCC_MODIFY_REJ, gsm48_cc_tx_modify_reject},
};

#define DOWNSLLEN \
	(sizeof(downstatelist) / sizeof(struct downstate))

int mncc_tx_to_cc(void *inst, int msg_type, void *arg)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) inst;
	struct gsm_mncc *data = arg;
	struct gsm_trans *trans;
	int i, rc;

	if (!ms->started || ms->shutdown) {
		LOGP(DCC, LOGL_NOTICE, "Phone is down!\n");
		if (ms->mncc_entity.mncc_recv && msg_type != MNCC_REL_REQ) {
			struct gsm_mncc rel;

			memset(&rel, 0, sizeof(rel));
			rel.callref = data->callref;
			mncc_set_cause(&rel, GSM48_CAUSE_LOC_PRN_S_LU,
			GSM48_CC_CAUSE_DEST_OOO);
			ms->mncc_entity.mncc_recv(ms, MNCC_REL_IND, &rel);
		}
		return -EBUSY;
	}

	data->msg_type = msg_type;

	/* Find callref */
	trans = trans_find_by_callref(ms, data->callref);

	if (!trans) {
		/* check for SETUP message */
		if (msg_type != MNCC_SETUP_REQ) {
			/* Invalid call reference */
			LOGP(DCC, LOGL_NOTICE, "transaction not found\n");
			return mncc_release_ind(ms, NULL, data->callref,
				GSM48_CAUSE_LOC_PRN_S_LU,
				GSM48_CC_CAUSE_INVAL_TRANS_ID);
		}
		if (data->callref >= 0x40000000) {
			LOGP(DCC, LOGL_FATAL, "MNCC ref wrong.\n");
			return mncc_release_ind(ms, NULL, data->callref,
				GSM48_CAUSE_LOC_PRN_S_LU,
				GSM48_CC_CAUSE_INVAL_TRANS_ID);
		}

		/* Create transaction */
		trans = trans_alloc(ms, GSM48_PDISC_CC, 0xff, data->callref);
		if (!trans) {
			/* No memory or whatever */
			return mncc_release_ind(ms, NULL, data->callref,
				GSM48_CAUSE_LOC_PRN_S_LU,
				GSM48_CC_CAUSE_RESOURCE_UNAVAIL);
		}
	}

	switch (msg_type) {
	case GSM_TCHF_FRAME:
		return gsm_send_voice(ms, arg);
	case MNCC_LCHAN_MODIFY:
		return 0;
	case MNCC_FRAME_RECV:
		ms->mncc_entity.ref = trans->callref;
		gsm48_rr_audio_mode(ms,
			AUDIO_TX_TRAFFIC_REQ | AUDIO_RX_TRAFFIC_IND);
		return 0;
	case MNCC_FRAME_DROP:
		if (ms->mncc_entity.ref == trans->callref)
			ms->mncc_entity.ref = 0;
		gsm48_rr_audio_mode(ms, AUDIO_TX_MICROPHONE | AUDIO_RX_SPEAKER);
		return 0;
	}

	/* Find function for current state and message */
	for (i = 0; i < DOWNSLLEN; i++)
		if ((msg_type == downstatelist[i].type)
		 && ((1 << trans->cc.state) & downstatelist[i].states))
			break;
	if (i == DOWNSLLEN) {
		LOGP(DCC, LOGL_NOTICE, "Message %d unhandled at state %d\n",
			msg_type, trans->cc.state);
		return 0;
	}

	rc = downstatelist[i].rout(trans, arg);

	return rc;
}

/* state trasitions for call control messages (lower layer) */
static struct datastate {
	uint32_t	states;
	int		type;
	int		(*rout) (struct gsm_trans *trans, struct msgb *msg);
} datastatelist[] = {
	/* mobile originating call establishment */
	{SBIT(GSM_CSTATE_INITIATED), /* 5.2.1.3 */
	 GSM48_MT_CC_CALL_PROC, gsm48_cc_rx_call_proceeding},

	{SBIT(GSM_CSTATE_INITIATED) | SBIT(GSM_CSTATE_MO_CALL_PROC) |
	 SBIT(GSM_CSTATE_CALL_DELIVERED), /* 5.2.1.4.1 */
	 GSM48_MT_CC_PROGRESS, gsm48_cc_rx_progress},

	{SBIT(GSM_CSTATE_INITIATED) |
	 SBIT(GSM_CSTATE_MO_CALL_PROC), /* 5.2.1.5 */
	 GSM48_MT_CC_ALERTING, gsm48_cc_rx_alerting},

	{SBIT(GSM_CSTATE_INITIATED) | SBIT(GSM_CSTATE_MO_CALL_PROC) |
	 SBIT(GSM_CSTATE_CALL_DELIVERED), /* 5.2.1.6 */
	 GSM48_MT_CC_CONNECT, gsm48_cc_rx_connect},

	/* mobile terminating call establishment */
	{SBIT(GSM_CSTATE_NULL), /* 5.2.2.1 */
	 GSM48_MT_CC_SETUP, gsm48_cc_rx_setup},

	{SBIT(GSM_CSTATE_CONNECT_REQUEST), /* 5.2.2.6 */
	 GSM48_MT_CC_CONNECT_ACK, gsm48_cc_rx_connect_ack},

	 /* signalling during call */
	{SBIT(GSM_CSTATE_ACTIVE), /* 5.3.1 */
	 GSM48_MT_CC_NOTIFY, gsm48_cc_rx_notify},

	{ALL_STATES, /* 8.4 */
	 GSM48_MT_CC_STATUS_ENQ, gsm48_cc_rx_status_enq},

	{ALL_STATES,
	 GSM48_MT_CC_STATUS, gsm48_cc_rx_status},

	{ALL_STATES, /* 5.5.7.2 */
	 GSM48_MT_CC_START_DTMF_ACK, gsm48_cc_rx_start_dtmf_ack},

	{ALL_STATES, /* 5.5.7.2 */
	 GSM48_MT_CC_START_DTMF_REJ, gsm48_cc_rx_start_dtmf_rej},

	{ALL_STATES, /* 5.5.7.4 */
	 GSM48_MT_CC_STOP_DTMF_ACK, gsm48_cc_rx_stop_dtmf_ack},

	{SBIT(GSM_CSTATE_ACTIVE),
	 GSM48_MT_CC_HOLD_ACK, gsm48_cc_rx_hold_ack},

	{SBIT(GSM_CSTATE_ACTIVE),
	 GSM48_MT_CC_HOLD_REJ, gsm48_cc_rx_hold_rej},

	{SBIT(GSM_CSTATE_ACTIVE),
	 GSM48_MT_CC_RETR_ACK, gsm48_cc_rx_retrieve_ack},

	{SBIT(GSM_CSTATE_ACTIVE),
	 GSM48_MT_CC_RETR_REJ, gsm48_cc_rx_retrieve_rej},

	{ALL_STATES - SBIT(GSM_CSTATE_NULL),
	 GSM48_MT_CC_FACILITY, gsm48_cc_rx_facility},

	{SBIT(GSM_CSTATE_ACTIVE),
	 GSM48_MT_CC_USER_INFO, gsm48_cc_rx_userinfo},

	/* clearing */
	{ALL_STATES - SBIT(GSM_CSTATE_NULL) - SBIT(GSM_CSTATE_RELEASE_REQ) -
	 SBIT(GSM_CSTATE_DISCONNECT_IND), /* 5.4.4.1.1 */
	 GSM48_MT_CC_DISCONNECT, gsm48_cc_rx_disconnect},

	{ALL_STATES, /* 5.4.3.3 & 5.4.5!!!*/
	 GSM48_MT_CC_RELEASE, gsm48_cc_rx_release},

	{ALL_STATES, /* 5.4.4.1.3 */
	 GSM48_MT_CC_RELEASE_COMPL, gsm48_cc_rx_release_compl},

	/* modify */
	{SBIT(GSM_CSTATE_ACTIVE),
	 GSM48_MT_CC_MODIFY, gsm48_cc_rx_modify},

	{SBIT(GSM_CSTATE_MO_TERM_MODIFY),
	 GSM48_MT_CC_MODIFY_COMPL, gsm48_cc_rx_modify_complete},

	{SBIT(GSM_CSTATE_MO_TERM_MODIFY),
	 GSM48_MT_CC_MODIFY_REJECT, gsm48_cc_rx_modify_reject},
};

#define DATASLLEN \
	(sizeof(datastatelist) / sizeof(struct datastate))

static int gsm48_cc_data_ind(struct gsm_trans *trans, struct msgb *msg)
{
	struct osmocom_ms *ms = trans->ms;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int msg_type = gh->msg_type & 0xbf;
	uint8_t transaction_id = ((gh->proto_discr & 0xf0) ^ 0x80) >> 4;
		/* flip */
	int msg_supported = 0; /* determine, if message is supported at all */
	int i, rc;

	/* set transaction ID, if not already */
	trans->transaction_id = transaction_id;

	/* pull the MMCC header */
	msgb_pull(msg, sizeof(struct gsm48_mmxx_hdr));

	LOGP(DCC, LOGL_INFO, "(ms %s) Received '%s' in CC state %s\n", ms->name,
		gsm48_cc_msg_name(msg_type),
		gsm48_cc_state_name(trans->cc.state));

	/* find function for current state and message */
	for (i = 0; i < DATASLLEN; i++) {
		if (msg_type == datastatelist[i].type)
			msg_supported = 1;
		if ((msg_type == datastatelist[i].type)
		 && ((1 << trans->cc.state) & datastatelist[i].states))
			break;
	}
	if (i == DATASLLEN) {
		if (msg_supported) {
			LOGP(DCC, LOGL_NOTICE, "Message unhandled at this "
				"state.\n");
			return gsm48_cc_tx_status(trans,
				GSM48_REJECT_MSG_TYPE_NOT_COMPATIBLE);
		} else {
			LOGP(DCC, LOGL_NOTICE, "Message not supported.\n");
			return gsm48_cc_tx_status(trans,
				GSM48_REJECT_MSG_TYPE_NOT_IMPLEMENTED);
		}
	}

	rc = datastatelist[i].rout(trans, msg);

	return rc;
}

/* receive message from MM layer */
int gsm48_rcv_cc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	struct gsm_trans *trans;
	int rc = 0;

	trans = trans_find_by_callref(ms, mmh->ref);
	if (!trans) {
		trans = trans_alloc(ms, GSM48_PDISC_CC, mmh->transaction_id,
					mmh->ref);
		if (!trans)
			return -ENOMEM;
	}

	LOGP(DCC, LOGL_INFO, "(ms %s) Received '%s' in CC state %s\n", ms->name,
		get_mmxx_name(msg_type),
		gsm48_cc_state_name(trans->cc.state));

	switch (msg_type) {
	case GSM48_MMCC_EST_IND:
		/* data included */
		rc = gsm48_cc_data_ind(trans, msg);
		break;
	case GSM48_MMCC_EST_CNF:
		/* send setup after confirm */
		if (trans->cc.state == GSM_CSTATE_MM_CONNECTION_PEND)
			rc = gsm48_cc_tx_setup(trans);
		else
			LOGP(DCC, LOGL_ERROR, "Oops, MMCC-EST-CONF in state "
				"%d?\n", trans->cc.state);
		break;
	case GSM48_MMCC_ERR_IND: /* no supporting re-establishment */
	case GSM48_MMCC_REL_IND:
		/* release L4, release transaction */
		mncc_release_ind(trans->ms, trans, trans->callref,
			 GSM48_CAUSE_LOC_PRN_S_LU, mmh->cause);
		/* release without sending MMCC_REL_REQ */
		new_cc_state(trans, GSM_CSTATE_NULL);
		trans->callref = 0;
		trans_free(trans);
		break;
	case GSM48_MMCC_DATA_IND:
		rc = gsm48_cc_data_ind(trans, msg);
		break;
	case GSM48_MMCC_UNIT_DATA_IND:
		break;
	case GSM48_MMCC_SYNC_IND:
		break;
	default:
		LOGP(DCC, LOGL_NOTICE, "Message unhandled.\n");
		rc = -ENOTSUP;
	}

	return rc;
}

int mncc_clear_trans(void *inst, uint8_t protocol)
{
	struct osmocom_ms *ms = (struct osmocom_ms *) inst;
	struct gsm_mncc rel;
	struct gsm_trans *trans, *trans2;

	memset(&rel, 0, sizeof(struct gsm_mncc));

	/* safe, in case the release process will destroy transaction node */
	llist_for_each_entry_safe(trans, trans2, &ms->trans_list, entry) {
		if (trans->protocol == protocol) {
			LOGP(DCC, LOGL_NOTICE, "Release CC-transaction.\n");
			rel.callref = trans->callref;
			mncc_set_cause(&rel, GSM48_CAUSE_LOC_PRN_S_LU,
				GSM48_CC_CAUSE_TEMP_FAILURE);
			mncc_tx_to_cc(ms, MNCC_REL_REQ, &rel);
		}
	}

	return 0;

}

