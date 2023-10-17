/* Handle VGCS/VBCS calls. (Voice Group/Broadcast Call Service). */
/*
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * Author: Andreas Eversberg
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Notes on the state machine:
 *
 * The state machine is different from the diagram depicted in the specs.
 * This is because there are some messages missing and some state transitions
 * are different or not shown.
 *
 * A call that has no channel is answered without joining the group channel.
 * If it comes available, the establishment is performed and the U4 is entered.
 *
 * Uplink control is not described in the diagram. Talking/listening is
 * requested by user and can be rejected by MM layer, if talking is not
 * allowed.
 *
 * We can be sure that there is no other MM connection while doing VGCS call
 * establishment: MMxx-EST-REQ is only accpepted, if there is no MM connection.
 * We block calls from user, if there is some other transaction, which is not
 * in state U3. Also we accept incoming indications any time and create
 * transactions that go to state U3.
 *
 * If the upper layer or lower layer requests another call/SMS/SS while VGCS
 * call is ongoing, this may cause undefined behaviour.
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
#include <osmocom/gsm/protocol/gsm_44_068.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/fsm.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/transaction.h>
#include <osmocom/bb/mobile/gsm44068_gcc_bcc.h>
#include <osmocom/bb/mobile/voice.h>
#include <osmocom/bb/mobile/vty.h>
#include <l1ctl_proto.h>

#define S(x)	(1 << (x))

#define LOG_GCC(trans, level, fmt, args...) \
	LOGP(((trans)->protocol == GSM48_PDISC_GROUP_CC) ? DGCC : DBCC, level, \
	     ((trans)->protocol == GSM48_PDISC_GROUP_CC) ? ("VGCS callref %u: " fmt) : ("VBS callref %u: " fmt), \
	     (trans)->callref, ##args)
#define LOG_GCC_PR(protocol, ref, level, fmt, args...) \
	LOGP((protocol == GSM48_PDISC_GROUP_CC) ? DGCC : DBCC, level, \
	     (protocol == GSM48_PDISC_GROUP_CC) ? ("VGCS callref %u: " fmt) : ("VBS callref %u: " fmt), \
	     ref, ##args)

/*
 * init
 */

int gsm44068_gcc_init(struct osmocom_ms *ms)
{
	LOGP(DGCC, LOGL_INFO, "init GCC/BCC\n");

	return 0;
}

int gsm44068_gcc_exit(struct osmocom_ms *ms)
{
	struct gsm_trans *trans, *trans2;

	LOGP(DGCC, LOGL_INFO, "exit GCC/BCC processes for %s\n", ms->name);

	llist_for_each_entry_safe(trans, trans2, &ms->trans_list, entry) {
		if (trans->protocol == GSM48_PDISC_GROUP_CC || trans->protocol == GSM48_PDISC_BCAST_CC) {
			LOG_GCC(trans, LOGL_NOTICE, "Free pendig CC-transaction.\n");
			trans_free(trans);
		}
	}

	return 0;
}


/*
 * messages
 */

/* TS 44.068 Chapter 6.1.2.1 */
enum vgcs_gcc_fsm_states {
	VGCS_GCC_ST_U0_NULL = 0,
	VGCS_GCC_ST_U0p_MM_CONNECTION_PENDING,
	VGCS_GCC_ST_U1_GROUP_CALL_INITIATED,
	VGCS_GCC_ST_U2sl_GROUP_CALL_ACTIVE,	/* sepeate link */
	VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE,	/* wait for receive mode */
	VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE,	/* receive mode / U6 @ BCC */
	VGCS_GCC_ST_U2ws_GROUP_CALL_ACTIVE,	/* wait for send and receive mode */
	VGCS_GCC_ST_U2sr_GROUP_CALL_ACTIVE,	/* send and receive mode */
	VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE,	/* no channel */
	VGCS_GCC_ST_U3_GROUP_CALL_PRESENT,
	VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST,
	VGCS_GCC_ST_U5_TERMINATION_REQUESTED,
};

/* TS 44.068 Figure 6.1 (additional events added) */
enum vgcs_gcc_fsm_event {
	VGCS_GCC_EV_SETUP_REQ,			/* calling user initiates call */
	VGCS_GCC_EV_TERM_REQ,			/* calling user requests termination */
	VGCS_GCC_EV_MM_EST_CNF,			/* MM connection established */
	VGCS_GCC_EV_MM_EST_REJ,			/* MM connection failed */
	VGCS_GCC_EV_DI_TERMINATION,		/* network acknowledges termination */
	VGCS_GCC_EV_DI_TERM_REJECT,		/* network rejects termination */
	VGCS_GCC_EV_DI_CONNECT,			/* network indicates connect */
	VGCS_GCC_EV_TIMEOUT,			/* several timeout events */
	VGCS_GCC_EV_SETUP_IND,			/* notification of ongoing call received */
	VGCS_GCC_EV_JOIN_GC_REQ,		/* user wants to join ongoing call */
	VGCS_GCC_EV_JOIN_GC_CNF,		/* MM confirms joining ongoing call */
	VGCS_GCC_EV_ABORT_REQ,			/* user rejects or leaves call */
	VGCS_GCC_EV_ABORT_IND,			/* MM indicates call gone / channel released or failed */
	VGCS_GCC_EV_TALK_REQ,			/* user wants to talk */
	VGCS_GCC_EV_TALK_CNF,			/* MM confirms talk */
	VGCS_GCC_EV_TALK_REJ,			/* MM rejects talk */
	VGCS_GCC_EV_LISTEN_REQ,			/* user wants to listen */
	VGCS_GCC_EV_LISTEN_CNF,			/* MM confirms listen */
	VGCS_GCC_EV_MM_IDLE,			/* MM layer becomes ready for new channel */
	VGCS_GCC_EV_UPLINK_FREE,		/* MM layer indicates free uplink in group receive mode */
	VGCS_GCC_EV_UPLINK_BUSY,		/* MM layer indicates busy uplink in group receive mode */
};

static const struct value_string vgcs_gcc_fsm_event_names[] = {
	OSMO_VALUE_STRING(VGCS_GCC_EV_SETUP_REQ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_TERM_REQ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_MM_EST_CNF),
	OSMO_VALUE_STRING(VGCS_GCC_EV_MM_EST_REJ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_DI_TERMINATION),
	OSMO_VALUE_STRING(VGCS_GCC_EV_DI_TERM_REJECT),
	OSMO_VALUE_STRING(VGCS_GCC_EV_DI_CONNECT),
	OSMO_VALUE_STRING(VGCS_GCC_EV_TIMEOUT),
	OSMO_VALUE_STRING(VGCS_GCC_EV_SETUP_IND),
	OSMO_VALUE_STRING(VGCS_GCC_EV_JOIN_GC_REQ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_JOIN_GC_CNF),
	OSMO_VALUE_STRING(VGCS_GCC_EV_ABORT_REQ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_ABORT_IND),
	OSMO_VALUE_STRING(VGCS_GCC_EV_TALK_REQ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_TALK_CNF),
	OSMO_VALUE_STRING(VGCS_GCC_EV_TALK_REJ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_LISTEN_REQ),
	OSMO_VALUE_STRING(VGCS_GCC_EV_LISTEN_CNF),
	OSMO_VALUE_STRING(VGCS_GCC_EV_MM_IDLE),
	OSMO_VALUE_STRING(VGCS_GCC_EV_UPLINK_FREE),
	OSMO_VALUE_STRING(VGCS_GCC_EV_UPLINK_BUSY),
	{ }
};

/*! return string representation of GCC/BCC Message Type */
static const char *gsm44068_gcc_msg_name(uint8_t msg_type)
{
	return get_value_string(osmo_gsm44068_msg_type_names, msg_type);
}

#define TFU(param) ((param < 0) ? "unchanged" : ((param) ? "T" : "F"))

/* Set state attributes and check if they are consistent with the current state. */
static int set_state_attributes(struct gsm_trans *trans, int d_att, int u_att, int comm, int orig, int call_state)
{
	bool orig_t = false, comm_t = false;

	LOG_GCC(trans, LOGL_DEBUG, "Setting state attributes: D-ATT = %s, U-ATT = %s, COMM = %s, ORIG = %s.\n",
		TFU(d_att), TFU(u_att), TFU(comm), TFU(orig));

	/* Control Speaker. */
	if (d_att >= 0 && trans->gcc.d_att != d_att) {
		LOG_GCC(trans, LOGL_DEBUG, "Switching Speaker to %d\n", d_att);
		gsm48_rr_audio_mode(trans->ms, AUDIO_TX_MICROPHONE | (d_att * AUDIO_RX_SPEAKER));
	}

	if (d_att >= 0)
		trans->gcc.d_att = d_att;
	if (u_att >= 0)
		trans->gcc.u_att = u_att;
	if (comm >= 0)
		trans->gcc.comm = comm;
	if (orig >= 0)
		trans->gcc.orig = orig;
	if (call_state >= 0)
		trans->gcc.call_state = call_state;

	switch (trans->gcc.fi->state) {
	case VGCS_GCC_ST_U3_GROUP_CALL_PRESENT:
	case VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST:
		orig_t = orig;
		comm_t = comm;
		break;
	case VGCS_GCC_ST_U0_NULL:
	case VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE:
	case VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE:
		comm_t = comm;
		break;
	}

	if (orig_t)
		LOG_GCC(trans, LOGL_ERROR, "ORIG = T is inconsistent with states U3 and U4. Please fix!");

	if (comm_t)
		LOG_GCC(trans, LOGL_ERROR,
			"COMM = T is inconsistent with states U0, U3, U4, U2nc and U2r. Please fix!");

	return (orig_t || comm_t) ? -EINVAL : 0;
}

static void vgcs_vty_notify(struct gsm_trans *trans, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

static void vgcs_vty_notify(struct gsm_trans *trans, const char *fmt, ...)
{
	struct osmocom_ms *ms = trans->ms;
	char buffer[1000];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
	buffer[sizeof(buffer) - 1] = '\0';
	va_end(args);

	l23_vty_ms_notify(ms, NULL);
	l23_vty_ms_notify(ms, "%s call %d: %s", (trans->protocol == GSM48_PDISC_GROUP_CC) ? "Group" : "Broadcast",
			  trans->callref, buffer);
}

/*
 * messages
 */

/* Send MMxx-GROUP-REQ to MM. */
static int vgcs_group_req(struct gsm_trans *trans)
{
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;
	uint16_t msg_type = (trans->protocol == GSM48_PDISC_GROUP_CC) ? GSM48_MMGCC_GROUP_REQ : GSM48_MMBCC_GROUP_REQ;

	LOG_GCC(trans, LOGL_INFO, "Sending %s.\n", get_mmxx_name(msg_type));

	OSMO_ASSERT(trans->gcc.ch_desc_present);

	nmsg = gsm48_mmxx_msgb_alloc(msg_type, trans->callref, trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;
	nmmh = (struct gsm48_mmxx_hdr *) nmsg->data;
	nmmh->ch_desc_present = trans->gcc.ch_desc_present;
	memcpy(&nmmh->ch_desc, &trans->gcc.ch_desc, sizeof(nmmh->ch_desc));

	return gsm48_mmxx_downmsg(trans->ms, nmsg);
}

/* Send MMxx-EST-REQ to MM. */
static int vgcs_est_req(struct gsm_trans *trans)
{
	struct msgb *nmsg;
	uint16_t msg_type = (trans->protocol == GSM48_PDISC_GROUP_CC) ? GSM48_MMGCC_EST_REQ : GSM48_MMBCC_EST_REQ;

	LOG_GCC(trans, LOGL_INFO, "Sending %s.\n", get_mmxx_name(msg_type));

	nmsg = gsm48_mmxx_msgb_alloc(msg_type, trans->callref, trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;

	return gsm48_mmxx_downmsg(trans->ms, nmsg);
}

/* Push message and send MMxx-DATA-REQ to MM. */
static int vgcs_data_req(struct gsm_trans *trans, struct msgb *msg)
{
	struct gsm48_mmxx_hdr *mmh;
	uint16_t msg_type = (trans->protocol == GSM48_PDISC_GROUP_CC) ? GSM48_MMGCC_DATA_REQ : GSM48_MMBCC_DATA_REQ;

	/* push RR header */
	msg->l3h = msg->data;
	msgb_push(msg, sizeof(struct gsm48_mmxx_hdr));
	mmh = (struct gsm48_mmxx_hdr *)msg->data;
	mmh->msg_type = msg_type;
	mmh->ref = trans->callref;
	mmh->transaction_id = trans->transaction_id;
	mmh->sapi = 0;

	/* send message to MM */
	return gsm48_mmxx_downmsg(trans->ms, msg);
}

/* Send MMxx-REL-REQ to MM. */
static int vgcs_rel_req(struct gsm_trans *trans)
{
	struct msgb *nmsg;
	uint16_t msg_type = (trans->protocol == GSM48_PDISC_GROUP_CC) ? GSM48_MMGCC_REL_REQ : GSM48_MMBCC_REL_REQ;

	LOG_GCC(trans, LOGL_INFO, "Sending %s.\n", get_mmxx_name(msg_type));

	nmsg = gsm48_mmxx_msgb_alloc(msg_type, trans->callref, trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;

	return gsm48_mmxx_downmsg(trans->ms, nmsg);
}

/* Send MMxx-UPLINK-REQ to MM. */
static int vgcs_uplink_req(struct gsm_trans *trans)
{
	struct msgb *nmsg;
	uint16_t msg_type = (trans->protocol == GSM48_PDISC_GROUP_CC) ? GSM48_MMGCC_UPLINK_REQ : GSM48_MMBCC_UPLINK_REQ;

	LOG_GCC(trans, LOGL_INFO, "Sending %s.\n", get_mmxx_name(msg_type));

	nmsg = gsm48_mmxx_msgb_alloc(msg_type, trans->callref, trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;

	return gsm48_mmxx_downmsg(trans->ms, nmsg);
}

/* Send MMxx-UPLINK-REL-REQ to MM. */
static int vgcs_uplink_rel_req(struct gsm_trans *trans)
{
	struct msgb *nmsg;
	uint16_t msg_type = (trans->protocol == GSM48_PDISC_GROUP_CC) ? GSM48_MMGCC_UPLINK_REL_REQ
								      : GSM48_MMBCC_UPLINK_REL_REQ;

	LOG_GCC(trans, LOGL_INFO, "Sending %s.\n", get_mmxx_name(msg_type));

	nmsg = gsm48_mmxx_msgb_alloc(msg_type, trans->callref, trans->transaction_id, 0);
	if (!nmsg)
		return -ENOMEM;

	return gsm48_mmxx_downmsg(trans->ms, nmsg);
}

static void _add_callref_ie(struct msgb *msg, uint32_t callref, bool with_prio, uint8_t prio)
{
	uint32_t ie;

	ie = callref << 5;
	if (with_prio)
		ie |= 0x10 | (prio << 1);
	msgb_put_u32(msg, ie);
}

static void _add_user_user_ie(struct msgb *msg, uint8_t user_pdisc, uint8_t *user, uint8_t user_len)
{
	uint8_t *ie;

	ie = msgb_put(msg, user_len + 2);
	*ie++ = GSM48_IE_USER_USER;
	*ie++ = user_len;
	memcpy(ie, user, user_len);
}

#define GSM44068_IE_CALL_STATE	0xA0
#define GSM44068_IE_STATE_ATTRS	0xB0
#define GSM44068_IE_TALKER_PRIO	0xc0

static void _add_call_state_ie(struct msgb *msg, uint8_t call_state)
{
	msgb_put_u8(msg, GSM44068_IE_CALL_STATE | call_state);
}

static void _add_state_attrs_ie(struct msgb *msg, uint8_t da, uint8_t ua, uint8_t comm, uint8_t oi)
{
	msgb_put_u8(msg, GSM44068_IE_STATE_ATTRS | (da << 3) | (ua << 2) | (comm << 1) | oi);
}

static void _add_talker_prio_ie(struct msgb *msg, uint8_t talker_prio)
{
	msgb_put_u8(msg, GSM44068_IE_TALKER_PRIO | talker_prio);
}

static void _add_cause_ie(struct msgb *msg, uint8_t cause, uint8_t *diag, uint8_t diag_len)
{
	uint8_t *ie;

	ie = msgb_put(msg, diag_len + 2);
	*ie++ = diag_len + 1;
	*ie++ = 0x80 | cause;
	if (diag_len && diag)
		memcpy(ie, diag, diag_len);
}

static int _msg_too_short(struct gsm_trans *trans)
{
	LOG_GCC(trans, LOGL_ERROR, "MSG too short.\n");
	return -EINVAL;
}

static int _ie_invalid(struct gsm_trans *trans)
{
	LOG_GCC(trans, LOGL_ERROR, "IE invalid.\n");
	return -EINVAL;
}

/* 3GPP TS 44.068 Clause 8.4 */
static int gsm44068_rx_set_parameter(struct gsm_trans *trans, struct msgb *msg,
				     uint8_t *da, uint8_t *ua, uint8_t *comm, uint8_t *oi)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int remaining_len = msgb_l3len(msg) - sizeof(*gh);
	uint8_t *ie = gh->data;

	/* State attributes */
	if (remaining_len < 1)
		return _msg_too_short(trans);
	if (da)
		*da = (ie[0] >> 3) & 0x1;
	if (ua)
		*ua = (ie[0] >> 2) & 0x1;
	if (comm)
		*comm = (ie[0] >> 1) & 0x1;
	if (oi)
		*oi = ie[0] & 0x1;
	ie += 1;

	return 0;
}

/* 3GPP TS 44.068 Clause 8.5 */
static int gsm44068_tx_setup(struct gsm_trans *trans, uint32_t callref, bool with_prio, uint8_t prio,
			     uint8_t user_pdisc, uint8_t *user, uint8_t user_len,
			     bool with_talker_prio, uint8_t talker_prio)
{
	struct msgb *msg = gsm44068_msgb_alloc_name("GSM 44.068 TX SETUP");
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));

	LOG_GCC(trans, LOGL_INFO, "Sending SETUP.\n");

	gh->proto_discr = trans->protocol | (trans->transaction_id << 4);
	gh->msg_type = OSMO_GSM44068_MSGT_SETUP;
	_add_callref_ie(msg, callref, with_prio, prio);
	if (user_len && user)
		_add_user_user_ie(msg, user_pdisc, user, user_len);
	if (with_talker_prio)
		_add_talker_prio_ie(msg, talker_prio);

	return vgcs_data_req(trans, msg);
}

/* 3GPP TS 44.068 Clause 8.6 */
static int gsm44068_tx_status(struct gsm_trans *trans, uint8_t cause, uint8_t *diag, uint8_t diag_len,
			      bool with_call_state, uint8_t call_state, bool with_state_attrs,
			      uint8_t da, uint8_t ua, uint8_t comm, uint8_t oi)
{
	struct msgb *msg = gsm44068_msgb_alloc_name("GSM 44.068 TX STATUS");
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));

	LOG_GCC(trans, LOGL_INFO, "Sending STATUS.\n");

	gh->proto_discr = trans->protocol | (trans->transaction_id << 4);
	gh->msg_type = OSMO_GSM44068_MSGT_STATUS;
	_add_cause_ie(msg, cause, diag, diag_len);
	if (with_call_state)
		_add_call_state_ie(msg, call_state);
	if (with_state_attrs)
		_add_state_attrs_ie(msg, da, ua, comm, oi);

	return vgcs_data_req(trans, msg);
}

/* 3GPP TS 44.068 Clause 8.7 and 8.8 */
static int gsm44068_rx_termination(struct gsm_trans *trans, struct msgb *msg, uint8_t *cause, uint8_t *diag, uint8_t *diag_len)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int remaining_len = msgb_l3len(msg) - sizeof(*gh);
	uint8_t *ie = gh->data;
	uint8_t ie_len;

	/* Cause */
	if (remaining_len < 2 || ie[0] < remaining_len - 2)
		return _msg_too_short(trans);
	ie_len = ie[0];
	if (remaining_len < ie_len + 1)
		return _msg_too_short(trans);
	if (ie_len < 1)
		return _ie_invalid(trans);
	if (cause)
		*cause = ie[1] & 0x7f;
	if (diag && diag_len) {
		*diag_len = ie_len - 1;
		if (*diag_len)
			memcpy(diag, ie + 2, ie_len - 1);
	}
	remaining_len -= ie_len + 1;
	ie += ie_len + 1;

	return 0;
}

/* 3GPP TS 44.068 Clause 8.9 */
static int gsm44068_tx_termination_request(struct gsm_trans *trans, uint32_t callref, bool with_prio, uint8_t prio,
					   bool with_talker_prio, uint8_t talker_prio)
{
	struct msgb *msg = gsm44068_msgb_alloc_name("GSM 44.068 TX TERMINATION REQUEST");
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));

	LOG_GCC(trans, LOGL_INFO, "Sending TERMINATION REQUEST.\n");

	gh->proto_discr = trans->protocol | (trans->transaction_id << 4);
	gh->msg_type = OSMO_GSM44068_MSGT_TERMINATION_REQUEST;
	_add_callref_ie(msg, callref, with_prio, prio);
	if (with_talker_prio)
		_add_talker_prio_ie(msg, talker_prio);

	return vgcs_data_req(trans, msg);
}

/* 3GPP TS 44.018 Clause 9.1.48 */
static int gsm44068_tx_uplink_release(struct gsm_trans *trans, uint8_t cause)
{
	struct msgb *msg = gsm44068_msgb_alloc_name("GSM 44.068 TX UPLINK RELEASE");
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm48_uplink_release *ur = (struct gsm48_uplink_release *) msgb_put(msg, sizeof(*ur));

	LOG_GCC(trans, LOGL_INFO, "UPLINK RELEASE (cause #%d)\n", cause);

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_UPLINK_RELEASE;
	ur->rr_cause = cause;

	return vgcs_data_req(trans, msg);
}

/*
 * GCC/BCC state machine
 *
 * For reference see Figure 6.1 of TS 44.068.
 *
 * Note: There are some events that are not depicted in the state diagram:
 *
 * "L: ABORT-IND" indicates closing/failing of radio channel.
 * "H: TALK-REQ" request talking on the channel.
 * "L: TALK-CNF" confirms talker.
 * "L: TALK-REJ" rejects talker.
 * "H: LISTEN-REQ" request listening.
 * "L: LISTEN-CNF" confirms listening.
 *
 */

/* Table 6.1 of TS 44.068 */
#define T_NO_CHANNEL	3
#define T_MM_EST	7
#define T_TERM		10
#define T_CONN_REQ	10

static void vgcs_gcc_fsm_u0_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 0, 0, 0, 0, OSMO_GSM44068_CSTATE_U0);
}

static void vgcs_gcc_fsm_u0_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;

	switch (event) {
	case VGCS_GCC_EV_SETUP_REQ:
		/* The calling user initiates a new call. */
		LOG_GCC(trans, LOGL_INFO, "Received call from user.\n");
		/* Change to MM CONNECTION PENDING state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0p_MM_CONNECTION_PENDING, 0, 0);
		/* Send EST-REQ to MM layer. */
		vgcs_est_req(trans);
		break;
	case VGCS_GCC_EV_SETUP_IND:
		/* New call notification. */
		LOG_GCC(trans, LOGL_INFO, "Received call from network.\n");
		/* Change to GROUP CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U3_GROUP_CALL_PRESENT, 0, 0);
		/* Notify call at VTY. */
		vgcs_vty_notify(trans, "Incoming call\n");
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u0p_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 0, 0, 0, 1, OSMO_GSM44068_CSTATE_U0p);

	/* Start timer */
	osmo_timer_schedule(&fi->timer, T_MM_EST, 0);
}

static void vgcs_gcc_fsm_u0p_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;
	int rc;

	switch (event) {
	case VGCS_GCC_EV_TERM_REQ:
		/* The user terminates the call. */
		LOG_GCC(trans, LOGL_INFO, "User terminates the call.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Free transaction. MM confirmation/rejection is handled without transaction also. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_MM_EST_REJ:
		/* The MM layer rejects the call. */
		LOG_GCC(trans, LOGL_INFO, "Call was rejected by MM layer.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify reject at VTY. */
		vgcs_vty_notify(trans, "Rejected (cause %d)\n", *cause);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_MM_EST_CNF:
		/* The MM connection was confirmed. */
		LOG_GCC(trans, LOGL_INFO, "Call was confirmed by MM layer.\n");
		/* Change to GROUP CALL INITIATED state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U1_GROUP_CALL_INITIATED, 0, 0);
		/* Choose transaction ID. */
		rc = trans_assign_trans_id(trans->ms, trans->protocol, 0);
		if (rc < 0) {
			/* No free transaction ID. */
			trans_free(trans);
			return;
		}
		trans->transaction_id = rc;
		/* Send SETUP towards network. */
		gsm44068_tx_setup(trans, trans->callref, false, 0, false, NULL, 0, false, 0);
		break;
	case VGCS_GCC_EV_TIMEOUT:
		/* Establishment of MM layer timed out. */
		LOG_GCC(trans, LOGL_INFO, "MM layer timed out.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Free transaction. MM confirmation/rejection is handled without transaction also. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u1_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 0, 0, 1, 1, OSMO_GSM44068_CSTATE_U1);
}

static void vgcs_gcc_fsm_u1_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;

	switch (event) {
	case VGCS_GCC_EV_DI_CONNECT:
		/* Received CONNECT from network. */
		LOG_GCC(trans, LOGL_INFO, "Call was accepted by network.\n");
		/* Change to GROUP CALL ACTIVE (separate link) state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2sl_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify connect at VTY. */
		vgcs_vty_notify(trans, "Connect\n");
		break;
	case VGCS_GCC_EV_DI_TERMINATION:
		/* Received TERMINATION from network. */
		LOG_GCC(trans, LOGL_INFO, "Call was terminated by network (cause %d).\n", *cause);
		/* Chane to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Terminated (cause %d)\n", *cause);
		/* Release MM connection. */
		vgcs_rel_req(trans);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_TERM_REQ:
		/* The user terminates the call. */
		LOG_GCC(trans, LOGL_INFO, "User terminates the call.\n");
		/* Change to TERMINATION REQUESTED state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U5_TERMINATION_REQUESTED, 0, 0);
		/* Send TERMINATION REQUEST towards network. */
		gsm44068_tx_termination_request(trans, trans->callref, false, 0, false, 0);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* Radio link was released or failed. */
		LOG_GCC(trans, LOGL_INFO, "Got release from MM layer.\n");
		/* Chane to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2sl_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released (cause %d)\n", *cause);
		/* Release MM connection. */
		vgcs_rel_req(trans);
		/* Free transaction. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u2sl_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 1, 1, 1, 1, OSMO_GSM44068_CSTATE_U2sl_U2);
}

static void vgcs_gcc_fsm_u2sl_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;

	switch (event) {
	case VGCS_GCC_EV_DI_TERMINATION:
		/* Received TERMINATION from network. */
		LOG_GCC(trans, LOGL_INFO, "Call was terminated by network (cause %d).\n", *cause);
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Terminated (cause %d)\n", *cause);
		/* Don't release MM connection, this is done from network side using CHANNEL RELEASE. */
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_TERM_REQ:
		/* The user terminates the call. */
		LOG_GCC(trans, LOGL_INFO, "User terminates the call.\n");
		/* Change to TERMINATION REQUESTED state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U5_TERMINATION_REQUESTED, 0, 0);
		/* Send TERMINATION REQUEST towards network. */
		gsm44068_tx_termination_request(trans, trans->callref, false, 0, false, 0);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* Radio link was released or failed. */
		LOG_GCC(trans, LOGL_INFO, "Got release from MM layer.\n");
		/* Chane to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released (cause %d)\n", *cause);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_LISTEN_REQ:
		/* The user wants to release dedicated link and join the group channel as listener. */
		LOG_GCC(trans, LOGL_INFO, "User releases uplink on dedicated channel.\n");
		/* Change state to ACTIVE (wait receive). */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE, 0, 0);
		/* Set flag that we change to group receive mode after separate link. */
		trans->gcc.receive_after_sl = true;
		/* Request release of the uplink. */
		gsm44068_tx_uplink_release(trans, GSM48_RR_CAUSE_LEAVE_GROUP_CA);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u2wr_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 1, 0, 1, -1, OSMO_GSM44068_CSTATE_U2wr_U6);
}

static void vgcs_gcc_fsm_u2wr_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;

	switch (event) {
	case VGCS_GCC_EV_LISTEN_CNF:
		/* The MM layer confirms uplink release. */
		LOG_GCC(trans, LOGL_INFO, "Uplink is now released.\n");
		/* Change to CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify answer at VTY. */
		vgcs_vty_notify(trans, "Listening\n");
		break;
	case VGCS_GCC_EV_TERM_REQ:
		/* The calling subscriber wants to terminate the call. */
		LOG_GCC(trans, LOGL_INFO, "User wants to terminate. Setting termination flag.\n");
		trans->gcc.termination = true;
		break;
	case VGCS_GCC_EV_ABORT_REQ:
		/* User aborts group channel. */
		LOG_GCC(trans, LOGL_INFO, "User leaves the group channel.\n");
		/* Change to GROUP CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U3_GROUP_CALL_PRESENT, 0, 0);
		/* Reset flag after we changed to group receive mode after separate link. */
		trans->gcc.receive_after_sl = false;
		/* Notify leaving at VTY. */
		vgcs_vty_notify(trans, "Call left\n");
		/* Release MM connection. (Leave call.) */
		vgcs_rel_req(trans);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The MM layer released the group channel. */
		LOG_GCC(trans, LOGL_INFO, "Release/failure received from MM layer.\n");
		if (trans->gcc.receive_after_sl) {
			LOG_GCC(trans, LOGL_INFO, "Ignoring release, because we released separate link.\n");
			break;
		}
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released (cause %d)\n", *cause);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_MM_IDLE:
		if (!trans->gcc.receive_after_sl)
			break;
		/* If no channel is available (no notification received), enter the U2nc state. */
		if (!trans->gcc.ch_desc_present) {
			/* Change state to ACTIVE (no channel). */
			osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE, 0, 0);
			/* Notify answer at VTY. */
			vgcs_vty_notify(trans, "Listen (no channel yet)\n");
			break;
		}
		/* The MM layer indicates that the phone is ready to request group channel. */
		LOG_GCC(trans, LOGL_INFO, "MM is now idle, we can request group channel.\n");
		/* Send GROUP-REQ to MM layer. */
		vgcs_group_req(trans);
		break;
	case VGCS_GCC_EV_JOIN_GC_CNF:
		/* Reset flag after we changed to group receive mode after separate link. */
		trans->gcc.receive_after_sl = false;
		/* The MM layer confirms group channel. */
		LOG_GCC(trans, LOGL_INFO, "Joined group call after releasing separate link.\n");
		/* Change to CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify answer at VTY. */
		vgcs_vty_notify(trans, "Listening\n");
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u2r_u6_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 1, 0, 0, -1, (trans->protocol == GSM48_PDISC_GROUP_CC) ?
			     OSMO_GSM44068_CSTATE_U2r : OSMO_GSM44068_CSTATE_U2wr_U6);

	/* There is a pending termination request, request uplink. */
	if (trans->gcc.termination)
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_TALK_REQ, NULL);
}

static void vgcs_gcc_fsm_u2r_u6_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;

	switch (event) {
	case VGCS_GCC_EV_TERM_REQ:
		/* The calling subscriber wants to terminate the call. */
		LOG_GCC(trans, LOGL_INFO, "User wants to terminate. Setting termination flag.\n");
		trans->gcc.termination = true;
		/* fall-thru */
	case VGCS_GCC_EV_TALK_REQ:
		/* The user wants to talk. */
		LOG_GCC(trans, LOGL_INFO, "User wants to talk on the uplink.\n");
		/* Change to GROUP CALL ACTIVE (wait send) state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2ws_GROUP_CALL_ACTIVE, 0, 0);
		/* Request group transmit mode from MM layer. */
		vgcs_uplink_req(trans);
		break;
	case VGCS_GCC_EV_ABORT_REQ:
		/* User aborts group channel. */
		LOG_GCC(trans, LOGL_INFO, "User leaves the group channel.\n");
		/* Change to GROUP CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U3_GROUP_CALL_PRESENT, 0, 0);
		/* Notify leaving at VTY. */
		vgcs_vty_notify(trans, "Call left\n");
		/* Release MM connection. (Leave call.) */
		vgcs_rel_req(trans);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The MM layer released the group channel. */
		LOG_GCC(trans, LOGL_INFO, "Release/failure received from MM layer.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released (cause %d)\n", *cause);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_UPLINK_FREE:
		/* The MM layer indicates that the uplink is free. */
		LOG_GCC(trans, LOGL_INFO, "Uplink free indication received from MM layer.\n");
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Uplink free => You may talk\n");
		break;
	case VGCS_GCC_EV_UPLINK_BUSY:
		/* The MM layer indicates that the uplink is busy. */
		LOG_GCC(trans, LOGL_INFO, "Uplink busy indication received from MM layer.\n");
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Uplink busy => You cannot talk\n");
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u2ws_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 1, 1, 0, -1, OSMO_GSM44068_CSTATE_U2ws);
}

static void vgcs_gcc_fsm_u2ws_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;

	switch (event) {
	case VGCS_GCC_EV_TALK_CNF:
		/* Uplink was granted. */
		LOG_GCC(trans, LOGL_INFO, "Uplink established, user can talk now.\n");
		/* Change to GROUP CALL ACTIVE (send and receive) state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2sr_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Talking\n");
		break;
	case VGCS_GCC_EV_TALK_REJ:
		/* Uplink was rejected. */
		LOG_GCC(trans, LOGL_INFO, "Uplink rejected, user cannot talk.\n");
		/* Clear termination flag, if set. */
		trans->gcc.termination = false;
		/* Change to GROUP CALL ACTIVE (receive) state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Talking rejected (cause %d)\n", *cause);
		break;
	case VGCS_GCC_EV_TERM_REQ:
		/* The calling subscriber wants to terminate the call. */
		LOG_GCC(trans, LOGL_INFO, "User wants to terminate. Setting termination flag.\n");
		trans->gcc.termination = true;
		break;
	case VGCS_GCC_EV_ABORT_REQ:
		/* User aborts group channel. */
		LOG_GCC(trans, LOGL_INFO, "User leaves the group channel.\n");
		/* Change to GROUP CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U3_GROUP_CALL_PRESENT, 0, 0);
		/* Notify leaving at VTY. */
		vgcs_vty_notify(trans, "Call left\n");
		/* Release MM connection. (Leave call.) */
		vgcs_rel_req(trans);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The MM layer released the group channel. */
		LOG_GCC(trans, LOGL_INFO, "Release/failure received from MM layer.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released (cause %d)\n", *cause);
		/* Free transaction. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u2sr_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 1, 1, 1, -1, OSMO_GSM44068_CSTATE_U2sr);

	/* There is a pending termination request, request uplink. */
	if (trans->gcc.termination)
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_TERM_REQ, NULL);
}

static void vgcs_gcc_fsm_u2sr_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;

	switch (event) {
	case VGCS_GCC_EV_DI_TERMINATION:
		/* Received TERMINATION from network. */
		LOG_GCC(trans, LOGL_INFO, "Call was terminated by network (cause %d).\n", *cause);
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Terminated (cause %d)\n", *cause);
		/* Don't release MM connection, this is done from network side using CHANNEL RELEASE. */
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_TALK_REJ:
		/* Uplink was rejected by network. (Reject after granting access.) */
		LOG_GCC(trans, LOGL_INFO, "Uplink rejected, user cannot talk.\n");
		/* Change to GROUP CALL ACTIVE (receive) state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Talking rejected (cause %d)\n", *cause);
		break;
	case VGCS_GCC_EV_LISTEN_REQ:
		/* The user wants to release the uplink and become a listener. */
		LOG_GCC(trans, LOGL_INFO, "User wants to release the uplink.\n");
		/* Change to GROUP CALL ACTIVE (wait receive) state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE, 0, 0);
		/* Request group receive mode from MM layer. */
		vgcs_uplink_rel_req(trans);
		break;
	case VGCS_GCC_EV_TERM_REQ:
		/* The user terminates the call. */
		LOG_GCC(trans, LOGL_INFO, "User terminates the call.\n");
		/* Change to TERMINATION REQUESTED state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U5_TERMINATION_REQUESTED, 0, 0);
		/* Send TERMINATION REQUEST towards network. */
		gsm44068_tx_termination_request(trans, trans->callref, false, 0, false, 0);
		break;
	case VGCS_GCC_EV_ABORT_REQ:
		/* User aborts group channel. */
		LOG_GCC(trans, LOGL_INFO, "User leaves the group channel.\n");
		/* Change to GROUP CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U3_GROUP_CALL_PRESENT, 0, 0);
		/* Notify leaving at VTY. */
		vgcs_vty_notify(trans, "Call left\n");
		/* Release MM connection. (Leave call.) */
		vgcs_rel_req(trans);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The MM layer released the group channel. */
		LOG_GCC(trans, LOGL_INFO, "Release/failure received from MM layer.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released (cause %d)\n", *cause);
		/* Free transaction. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u2nc_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 1, 1, 0, -1, OSMO_GSM44068_CSTATE_U2nc);

	/* Start timer */
	osmo_timer_schedule(&fi->timer, T_NO_CHANNEL, 0);
}

static void vgcs_gcc_fsm_u2nc_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;

	switch (event) {
	case VGCS_GCC_EV_SETUP_IND:
		/* Channel becomes available, now join the group call. */
		LOG_GCC(trans, LOGL_INFO, "Radio channel becomes available.\n");
		/* Change to CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST, 0, 0);
		/* Send GROUP-REQ to MM layer. */
		vgcs_group_req(trans);
		break;
	case VGCS_GCC_EV_TERM_REQ:
		/* The calling subscriber wants to terminate the call. */
		LOG_GCC(trans, LOGL_INFO, "User wants to terminate. Setting termination flag.\n");
		trans->gcc.termination = true;
		break;
	case VGCS_GCC_EV_ABORT_REQ:
		/* User aborts group channel. */
		LOG_GCC(trans, LOGL_INFO, "User leaves the group channel, that is not yet established.\n");
		/* Change to GROUP CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U3_GROUP_CALL_PRESENT, 0, 0);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The MM layer indicates that group channel is gone. */
		LOG_GCC(trans, LOGL_INFO, "Group call notification is gone.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released\n");
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_TIMEOUT:
		/* Group channel did not become availblet. */
		LOG_GCC(trans, LOGL_INFO, "Timeout waiting for group channel.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Timeout\n");
		/* Free transaction. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u3_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 0, 0, 0, 0, OSMO_GSM44068_CSTATE_U3);
}

static void vgcs_gcc_fsm_u3_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;

	switch (event) {
	case VGCS_GCC_EV_JOIN_GC_REQ:
		/* Join (answer) incoming group call. */
		LOG_GCC(trans, LOGL_INFO, "Join call.\n");
		/* If no channel is available, enter the U2nc state. */
		if (!trans->gcc.ch_desc_present) {
			/* Change state to ACTIVE (no channel). */
			osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE, 0, 0);
			/* Notify answer at VTY. */
			vgcs_vty_notify(trans, "Answer (no channel yet)\n");
			break;
		}
		/* Change to CALL PRESENT state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST, 0, 0);
		/* Send GROUP-REQ to MM layer. */
		vgcs_group_req(trans);
		break;
	case VGCS_GCC_EV_ABORT_REQ:
		/* User rejects group call. */
		LOG_GCC(trans, LOGL_INFO, "User rejects group call.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The notified call is gone. */
		LOG_GCC(trans, LOGL_INFO, "Received call from network is gone.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. (No cause, because notification is gone.) */
		vgcs_vty_notify(trans, "Released\n");
		/* Free transaction. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u4_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, 0, 0, 0, 0, OSMO_GSM44068_CSTATE_U4);

	/* Start timer */
	osmo_timer_schedule(&fi->timer, T_CONN_REQ, 0);
}

static void vgcs_gcc_fsm_u4_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;

	switch (event) {
	case VGCS_GCC_EV_JOIN_GC_CNF:
		/* The MM layer confirms the group receive mode. (We have a channel.) */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE, 0, 0);
		/* Notify answer at VTY. */
		vgcs_vty_notify(trans, "Answer\n");
		break;
	case VGCS_GCC_EV_ABORT_REQ:
		/* User aborts group channel. */
		LOG_GCC(trans, LOGL_INFO, "User aborts group channel.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The notified call is gone. */
		LOG_GCC(trans, LOGL_INFO, "Received call from network is gone.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. (No cause, because notification is gone.) */
		vgcs_vty_notify(trans, "Released\n");
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_TIMEOUT:
		/* Group channel timed out. */
		LOG_GCC(trans, LOGL_INFO, "Timeout waiting for group channel.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Timeout\n");
		/* Free transaction. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void vgcs_gcc_fsm_u5_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct gsm_trans *trans = fi->priv;

	set_state_attributes(trans, -1, -1, 1, 1, OSMO_GSM44068_CSTATE_U5);

	/* Start timer */
	osmo_timer_schedule(&fi->timer, T_TERM, 0);
}

static void vgcs_gcc_fsm_u5_action(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct gsm_trans *trans = fi->priv;
	uint8_t *cause = data;

	switch (event) {
	case VGCS_GCC_EV_DI_TERMINATION:
		/* The network confirm the termination. */
		LOG_GCC(trans, LOGL_INFO, "Termination confirmend by network.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Terminated (cause %d)\n", *cause);
		/* Don't release MM connection, this is done from network side using CHANNEL RELEASE. */
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_DI_TERM_REJECT:
		/* Termination was rejected. */
		LOG_GCC(trans, LOGL_INFO, "Termination rejected (cause %d), releasing MM connection.\n", *cause);
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Termination rejected (cause %d), Call left\n", *cause);
		/* Release MM connection. (Leave call.)
		 * We must release here, because we can have a dedicated MM connection or joined a group channel.
		 * We cannot go back, because we don't know what state we had before U5 state was entered.
		 * Do we really need to go back or just leave the call? */
		vgcs_rel_req(trans);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_ABORT_IND:
		/* The notified call is gone. */
		LOG_GCC(trans, LOGL_INFO, "Received call from network is gone.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Released (cause %d)\n", *cause);
		/* Free transaction. */
		trans_free(trans);
		break;
	case VGCS_GCC_EV_TIMEOUT:
		/* Termination timed out. */
		LOG_GCC(trans, LOGL_INFO, "Timeout waiting for termination.\n");
		/* Change to NULL state. */
		osmo_fsm_inst_state_chg(fi, VGCS_GCC_ST_U0_NULL, 0, 0);
		/* Notify termination at VTY. */
		vgcs_vty_notify(trans, "Timeout\n");
		/* Release MM connection. (Leave call.) */
		vgcs_rel_req(trans);
		/* Free transaction. */
		trans_free(trans);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static int vgcs_gcc_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	return osmo_fsm_inst_dispatch(fi, VGCS_GCC_EV_TIMEOUT, NULL);
}

static const struct osmo_fsm_state vgcs_gcc_fsm_states[] = {
	[VGCS_GCC_ST_U0_NULL] = {
		.name = "NULL (U0)",
		.in_event_mask = S(VGCS_GCC_EV_SETUP_REQ) |
				 S(VGCS_GCC_EV_SETUP_IND),
		.out_state_mask = S(VGCS_GCC_ST_U0p_MM_CONNECTION_PENDING) |
				  S(VGCS_GCC_ST_U1_GROUP_CALL_INITIATED) |
				  S(VGCS_GCC_ST_U3_GROUP_CALL_PRESENT),
		.onenter = vgcs_gcc_fsm_u0_onenter,
		.action = vgcs_gcc_fsm_u0_action,
	},
	[VGCS_GCC_ST_U0p_MM_CONNECTION_PENDING] = {
		.name = "MM CONNECTION PENDING (U0.p)",
		.in_event_mask = S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_MM_EST_REJ) |
				 S(VGCS_GCC_EV_MM_EST_CNF) |
				 S(VGCS_GCC_EV_TIMEOUT),
		.out_state_mask = S(VGCS_GCC_ST_U0_NULL) |
				  S(VGCS_GCC_ST_U1_GROUP_CALL_INITIATED),
		.onenter = vgcs_gcc_fsm_u0p_onenter,
		.action = vgcs_gcc_fsm_u0p_action,
	},
	[VGCS_GCC_ST_U1_GROUP_CALL_INITIATED] = {
		.name = "GROUP CALL INITIATED (U1)",
		.in_event_mask = S(VGCS_GCC_EV_DI_CONNECT) |
				 S(VGCS_GCC_EV_DI_TERMINATION) |
				 S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND),
		.out_state_mask = S(VGCS_GCC_ST_U2sl_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U5_TERMINATION_REQUESTED) |
				  S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u1_onenter,
		.action = vgcs_gcc_fsm_u1_action,
	},
	[VGCS_GCC_ST_U2sl_GROUP_CALL_ACTIVE] = {
		.name = "GROUP CALL ACTIVE separate link (U2sl)",
		.in_event_mask = S(VGCS_GCC_EV_DI_TERMINATION) |
				 S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND) |
				 S(VGCS_GCC_EV_LISTEN_REQ),
		.out_state_mask = S(VGCS_GCC_ST_U0_NULL) |
				  S(VGCS_GCC_ST_U5_TERMINATION_REQUESTED) |
				  S(VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE),
		.onenter = vgcs_gcc_fsm_u2sl_onenter,
		.action = vgcs_gcc_fsm_u2sl_action,
	},
	[VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE] = {
		.name = "GROUP CALL ACTIVE wait for receive mode (U2wr)",
		.in_event_mask = S(VGCS_GCC_EV_LISTEN_CNF) |
				 S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_ABORT_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND) |
				 S(VGCS_GCC_EV_MM_IDLE) |
				 S(VGCS_GCC_EV_JOIN_GC_CNF),
		.out_state_mask = S(VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U3_GROUP_CALL_PRESENT) |
				  S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u2wr_onenter,
		.action = vgcs_gcc_fsm_u2wr_action,
	},
	[VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE] = {
		.name = "GROUP CALL ACTIVE receive mode (U2r or U6 @ BCC)",
		.in_event_mask = S(VGCS_GCC_EV_TALK_REQ) |
				 S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_ABORT_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND) |
				 S(VGCS_GCC_EV_UPLINK_FREE) |
				 S(VGCS_GCC_EV_UPLINK_BUSY),
		.out_state_mask = S(VGCS_GCC_ST_U2ws_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U3_GROUP_CALL_PRESENT) |
				  S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u2r_u6_onenter,
		.action = vgcs_gcc_fsm_u2r_u6_action,
	},
	[VGCS_GCC_ST_U2ws_GROUP_CALL_ACTIVE] = {
		.name = "GROUP CALL ACTIVE wait for send+receive mode (U2ws)",
		.in_event_mask = S(VGCS_GCC_EV_TALK_CNF) |
				 S(VGCS_GCC_EV_TALK_REJ) |
				 S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_ABORT_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND),
		.out_state_mask = S(VGCS_GCC_ST_U2sr_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U3_GROUP_CALL_PRESENT) |
				  S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u2ws_onenter,
		.action = vgcs_gcc_fsm_u2ws_action,
	},
	[VGCS_GCC_ST_U2sr_GROUP_CALL_ACTIVE] = {
		.name = "GROUP CALL ACTIVE send+receive mode (U2sr)",
		.in_event_mask = S(VGCS_GCC_EV_DI_TERMINATION) |
				 S(VGCS_GCC_EV_LISTEN_REQ) |
				 S(VGCS_GCC_EV_TALK_REJ) |
				 S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_ABORT_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND),
		.out_state_mask = S(VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U3_GROUP_CALL_PRESENT) |
				  S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u2sr_onenter,
		.action = vgcs_gcc_fsm_u2sr_action,
	},
	[VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE] = {
		.name = "GROUP CALL ACTIVE no channel (U2nc)",
		.in_event_mask = S(VGCS_GCC_EV_SETUP_IND) |
				 S(VGCS_GCC_EV_TERM_REQ) |
				 S(VGCS_GCC_EV_ABORT_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND) |
				 S(VGCS_GCC_EV_TIMEOUT),
		.out_state_mask = S(VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST) |
				  S(VGCS_GCC_ST_U3_GROUP_CALL_PRESENT) |
				  S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u2nc_onenter,
		.action = vgcs_gcc_fsm_u2nc_action,
	},
	[VGCS_GCC_ST_U3_GROUP_CALL_PRESENT] = {
		.name = "GROUP CALL PRESENT (U3)",
		.in_event_mask = S(VGCS_GCC_EV_JOIN_GC_REQ) |
				 S(VGCS_GCC_EV_ABORT_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND),
		.out_state_mask = S(VGCS_GCC_ST_U0_NULL) |
				  S(VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST) |
				  S(VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE),
		.onenter = vgcs_gcc_fsm_u3_onenter,
		.action = vgcs_gcc_fsm_u3_action,
	},
	[VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST] = {
		.name = "GROUP CALL CONNECTION REQUEST (U4)",
		.in_event_mask = S(VGCS_GCC_EV_JOIN_GC_CNF) |
				 S(VGCS_GCC_EV_ABORT_REQ) |
				 S(VGCS_GCC_EV_ABORT_IND) |
				 S(VGCS_GCC_EV_TIMEOUT),
		.out_state_mask = S(VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE) |
				  S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u4_onenter,
		.action = vgcs_gcc_fsm_u4_action,
	},
	[VGCS_GCC_ST_U5_TERMINATION_REQUESTED] = {
		.name = "TERMINATION REQUESTED (U5)",
		.in_event_mask = S(VGCS_GCC_EV_DI_TERMINATION) |
				 S(VGCS_GCC_EV_DI_TERM_REJECT) |
				 S(VGCS_GCC_EV_ABORT_IND) |
				 S(VGCS_GCC_EV_TIMEOUT),
		.out_state_mask = S(VGCS_GCC_ST_U0_NULL),
		.onenter = vgcs_gcc_fsm_u5_onenter,
		.action = vgcs_gcc_fsm_u5_action,
	},
};

static struct osmo_fsm vgcs_gcc_fsm = {
	.name = "gcc",
	.states = vgcs_gcc_fsm_states,
	.num_states = ARRAY_SIZE(vgcs_gcc_fsm_states),
	.log_subsys = DGCC,
	.event_names = vgcs_gcc_fsm_event_names,
	.timer_cb = vgcs_gcc_fsm_timer_cb,
};

static struct osmo_fsm vgcs_bcc_fsm = {
	.name = "bcc",
	.states = vgcs_gcc_fsm_states,
	.num_states = ARRAY_SIZE(vgcs_gcc_fsm_states),
	.log_subsys = DBCC,
	.event_names = vgcs_gcc_fsm_event_names,
	.timer_cb = vgcs_gcc_fsm_timer_cb,
};

static __attribute__((constructor)) void on_dso_load(void)
{
	OSMO_ASSERT(osmo_fsm_register(&vgcs_gcc_fsm) == 0);
	OSMO_ASSERT(osmo_fsm_register(&vgcs_bcc_fsm) == 0);
}

static const char *gsm44068_gcc_state_name(struct osmo_fsm_inst *fi)
{
	return vgcs_gcc_fsm_states[fi->state].name;
}

/*
 * transaction
 */

/* Create transaction together with state machine and set initail states. */
static struct gsm_trans *trans_alloc_vgcs(struct osmocom_ms *ms, uint8_t pdisc, uint8_t transaction_id,
					  uint32_t callref)
{
	struct gsm_trans *trans;

	trans = trans_alloc(ms, pdisc, 0xff, callref);
	if (!trans)
		return NULL;
	trans->gcc.fi = osmo_fsm_inst_alloc((pdisc == GSM48_PDISC_GROUP_CC) ? &vgcs_gcc_fsm : &vgcs_bcc_fsm,
					    trans, trans, LOGL_DEBUG, NULL);
	if (!trans->gcc.fi) {
		trans_free(trans);
		return NULL;
	}

	LOG_GCC(trans, LOGL_DEBUG, "Created transaction for callref %d, pdisc %d, transaction_id 0x%x\n",
	     callref, pdisc, transaction_id);

	set_state_attributes(trans, 0, 0, 0, 0, OSMO_GSM44068_CSTATE_U0);

	return trans;
}

/* Call Control Specific transaction release.
 * gets called by trans_free, DO NOT CALL YOURSELF!
 */
void _gsm44068_gcc_bcc_trans_free(struct gsm_trans *trans)
{
	if (!trans->gcc.fi)
		return;

	/* Free state machine. */
	osmo_fsm_inst_free(trans->gcc.fi);
}

/* Find ongoing call. (The call must not be present.) */
struct gsm_trans *trans_find_ongoing_gcc_bcc(struct osmocom_ms *ms)
{
	struct gsm_trans *trans;

	llist_for_each_entry(trans, &ms->trans_list, entry) {
		if (trans->protocol != GSM48_PDISC_GROUP_CC && trans->protocol != GSM48_PDISC_BCAST_CC)
			continue;
		if (trans->gcc.fi->state != VGCS_GCC_ST_U3_GROUP_CALL_PRESENT)
			return trans;
	}

	return NULL;
}

/*
 * messages from upper/lower layers
 */

static int gsm44068_gcc_data_ind(struct gsm_trans *trans, struct msgb *msg, uint8_t transaction_id)
{
	struct osmocom_ms *ms = trans->ms;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int msg_type = gh->msg_type & 0xbf;
	int rc = 0;
	uint8_t cause = 0;
	uint8_t d_att, u_att, comm, orig;

	/* pull the MMCC header */
	msgb_pull(msg, sizeof(struct gsm48_mmxx_hdr));

	/* Use transaction ID from message. If we join a group call, we don't have it until now. */
	trans->transaction_id = transaction_id;

	LOG_GCC(trans, LOGL_INFO, "(ms %s) Received '%s' in state %s\n", ms->name, gsm44068_gcc_msg_name(msg_type),
		gsm44068_gcc_state_name(trans->gcc.fi));

	switch (msg_type) {
	case OSMO_GSM44068_MSGT_CONNECT:
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_DI_CONNECT, NULL);
		break;
	case OSMO_GSM44068_MSGT_TERMINATION:
		rc = gsm44068_rx_termination(trans, msg, &cause, NULL, NULL);
		if (rc < 0)
			break;
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_DI_TERMINATION, &cause);
		break;
	case OSMO_GSM44068_MSGT_TERMINATION_REJECT:
		rc = gsm44068_rx_termination(trans, msg, &cause, NULL, NULL);
		if (rc < 0)
			break;
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_DI_TERM_REJECT, &cause);
		break;
	case OSMO_GSM44068_MSGT_GET_STATUS:
		/* Note: The message via UNIT DATA is not supported. */
		gsm44068_tx_status(trans, OSMO_GSM44068_CAUSE_RESPONSE_TO_GET_STATUS, NULL, 0,
				   true, trans->gcc.call_state,
				   true, trans->gcc.d_att, trans->gcc.u_att, trans->gcc.comm, trans->gcc.orig);
		break;
	case OSMO_GSM44068_MSGT_SET_PARAMETER:
		rc = gsm44068_rx_set_parameter(trans, msg, &d_att, &u_att, &comm, &orig);
		if (rc >= 0)
			set_state_attributes(trans, d_att, u_att, comm, orig, -1);
		break;
	default:
		LOG_GCC(trans, LOGL_NOTICE, "Message '%s' not supported.\n", gsm44068_gcc_msg_name(msg_type));
		rc = -EINVAL;
	}

	return rc;
}

/* receive message from MM layer */
int gsm44068_rcv_gcc_bcc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_settings *set = &ms->settings;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	uint8_t pdisc;
	struct gsm_trans *trans;
	int rc = 0;
	uint8_t cause;

	/* Check for message class and get protocol type. */
	switch ((msg_type & GSM48_MMXX_MASK)) {
	case GSM48_MMGCC_CLASS:
		pdisc = GSM48_PDISC_GROUP_CC;
		if (!set->vgcs) {
			LOGP(DGCC, LOGL_ERROR, "Ignoring message '%s', because VGCS is not supported!\n",
			     get_mmxx_name(msg_type));
			return -ENOTSUP;
		}
		break;
	case GSM48_MMBCC_CLASS:
		pdisc = GSM48_PDISC_BCAST_CC;
		if (!set->vbs) {
			LOGP(DGCC, LOGL_ERROR, "Ignoring message '%s', because VBS is not supported!\n",
			     get_mmxx_name(msg_type));
			return -ENOTSUP;
		}
		break;
	default:
		LOGP(DGCC, LOGL_ERROR, "Message class not allowed, please fix!\n");
		return -EINVAL;
	}

	trans = trans_find_by_callref(ms, pdisc, mmh->ref);
	if (!trans) {
		LOG_GCC_PR(pdisc, mmh->ref, LOGL_INFO, "(ms %s) Received '%s' without transaction.\n",
			   ms->name, get_mmxx_name(msg_type));

		if (msg_type == GSM48_MMGCC_REL_IND || msg_type == GSM48_MMBCC_REL_IND) {
			/* If we got the MMxx-EST-REJ after we aborted the call, we ignore. */
			return 0;
		}
		if (msg_type == GSM48_MMGCC_EST_CNF || msg_type == GSM48_MMBCC_EST_CNF ||
		    msg_type == GSM48_MMGCC_GROUP_CNF || msg_type == GSM48_MMBCC_GROUP_CNF) {
			struct msgb *nmsg;
			LOG_GCC_PR(pdisc, mmh->ref, LOGL_ERROR, "Received confirm for GCC/BCC call, "
				   "but there is no transaction, releasing.\n");
			/* If we got the MMxx-EST-CNF after we aborted the call, we release. */
			msg_type = (pdisc == GSM48_PDISC_GROUP_CC) ? GSM48_MMGCC_REL_REQ : GSM48_MMBCC_REL_REQ;
			nmsg = gsm48_mmxx_msgb_alloc(msg_type, mmh->ref, mmh->transaction_id, 0);
			if (!nmsg)
				return -ENOMEM;
			LOG_GCC_PR(pdisc, mmh->ref, LOGL_INFO, "Sending %s\n", get_mmxx_name(msg_type));
			gsm48_mmxx_downmsg(ms, nmsg);
			return 0;

		} else if (msg_type == GSM48_MMGCC_NOTIF_IND || msg_type == GSM48_MMBCC_NOTIF_IND) {
			/* Notification gone but no transaction. */
			if (mmh->notify != MMXX_NOTIFY_SETUP)
				return 0;
			/* Incoming notification, creation transaction. */
			trans = trans_alloc_vgcs(ms, pdisc, 0xff, mmh->ref);
			if (!trans)
				return -ENOMEM;
		} else {
			LOG_GCC_PR(pdisc, mmh->ref, LOGL_ERROR, "Received GCC/BCC message for unknown transaction.\n");
			return -ENOENT;
		}
	}

	LOG_GCC(trans, LOGL_INFO, "(ms %s) Received '%s' in state %s\n", ms->name, get_mmxx_name(msg_type),
		gsm44068_gcc_state_name(trans->gcc.fi));

	switch (msg_type) {
	case GSM48_MMGCC_EST_CNF:
	case GSM48_MMBCC_EST_CNF:
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_MM_EST_CNF, NULL);
		break;
	case GSM48_MMGCC_ERR_IND:
	case GSM48_MMBCC_ERR_IND:
	case GSM48_MMGCC_REL_IND:
	case GSM48_MMBCC_REL_IND:
		/* If MM fails or is rejected during U0.p state, this is a MM-EST-REJ. */
		if (trans->gcc.fi->state == VGCS_GCC_ST_U0p_MM_CONNECTION_PENDING)
			osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_MM_EST_REJ, &mmh->cause);
		else
			osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_ABORT_IND, &mmh->cause);
		break;
	case GSM48_MMGCC_DATA_IND:
	case GSM48_MMBCC_DATA_IND:
		rc = gsm44068_gcc_data_ind(trans, msg, mmh->transaction_id);
		break;
	case GSM48_MMGCC_NOTIF_IND:
	case GSM48_MMBCC_NOTIF_IND:
		switch (mmh->notify) {
		case MMXX_NOTIFY_SETUP:
			/* Store channel description. */
			trans->gcc.ch_desc_present = mmh->ch_desc_present;
			memcpy(&trans->gcc.ch_desc, &mmh->ch_desc, sizeof(trans->gcc.ch_desc));
			if (trans->gcc.fi->state == VGCS_GCC_ST_U0_NULL) {
				/* In null state this indication is a SETUP-IND. */
				osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_SETUP_IND, NULL);
			} else if (trans->gcc.fi->state == VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE && mmh->ch_desc_present) {
				/* In U2nc state with a channel info, is a SETUP-IND (with updated mode). */
				osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_SETUP_IND, NULL);
			}
			break;
		case MMXX_NOTIFY_RELEASE:
			if (trans->gcc.fi->state == VGCS_GCC_ST_U3_GROUP_CALL_PRESENT ||
			    trans->gcc.fi->state == VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST ||
			    trans->gcc.fi->state == VGCS_GCC_ST_U2nc_GROUP_CALL_ACTIVE) {
				/* If notification is gone, abort pending received call. */
				cause = GSM48_CC_CAUSE_NORM_CALL_CLEAR;
				osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_ABORT_IND, &cause);
			}
			break;
		}
		break;
	case GSM48_MMGCC_GROUP_CNF:
	case GSM48_MMBCC_GROUP_CNF:
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_JOIN_GC_CNF, NULL);
		break;
	case GSM48_MMGCC_UPLINK_CNF:
	case GSM48_MMBCC_UPLINK_CNF:
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_TALK_CNF, NULL);
		break;
	case GSM48_MMGCC_UPLINK_REL_IND:
	case GSM48_MMBCC_UPLINK_REL_IND:
		if (trans->gcc.fi->state == VGCS_GCC_ST_U2ws_GROUP_CALL_ACTIVE ||
		    trans->gcc.fi->state == VGCS_GCC_ST_U2sr_GROUP_CALL_ACTIVE) {
			/* We are waiting to send, so this is a reject. */
			osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_TALK_REJ, &mmh->cause);
		} else if (trans->gcc.fi->state == VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE) {
			/* We are waiting to receive, so this is a confirm. */
			osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_LISTEN_CNF, &mmh->cause);
		}
		break;
	case GSM48_MMGCC_UPLINK_FREE_IND:
	case GSM48_MMBCC_UPLINK_FREE_IND:
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_UPLINK_FREE, NULL);
		break;
	case GSM48_MMGCC_UPLINK_BUSY_IND:
	case GSM48_MMBCC_UPLINK_BUSY_IND:
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_UPLINK_BUSY, NULL);
		break;
	default:
		LOG_GCC(trans, LOGL_NOTICE, "Message '%s' unhandled.\n", get_mmxx_name(msg_type));
		rc = -ENOTSUP;
	}

	return rc;
}

/* Special function to receive IDLE state of MM layer. This is required to request a channel after dedicated mode. */
int gsm44068_rcv_mm_idle(struct osmocom_ms *ms)
{
	struct gsm_trans *trans;

	trans = trans_find_ongoing_gcc_bcc(ms);
	if (!trans)
		return -ENOENT;
	if (trans->gcc.fi->state == VGCS_GCC_ST_U2wr_GROUP_CALL_ACTIVE && trans->gcc.receive_after_sl) {
		/* Finally the MM layer is IDLE. */
		osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_MM_IDLE, NULL);
		return 0;
	}
	return -EINVAL;
}

/* Setup or join a VGC/VBC. */
int gcc_bcc_call(struct osmocom_ms *ms, uint8_t protocol, const char *number)
{
	uint32_t callref = strtoul(number, NULL, 0);
	struct gsm_trans *trans;
	int i;

	if (strlen(number) > 8) {
inval:
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Invalid group '%s'\n", number);
		return -EINVAL;
	}

	for (i = 0; i < strlen(number); i++) {
		if (number[i] < '0' || number[i] > '9')
			goto inval;
	}

	if (callref == 0)
		goto inval;

	/* Reject if there is already an ongoing call. */
	trans = trans_find_ongoing_gcc_bcc(ms);
	if (trans) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Cannot call/join, we are busy\n");
		return -EBUSY;
	}

	/* Find call that matches the given protocol+cellref. */
	trans = trans_find_by_callref(ms, protocol, callref);
	if (trans) {
		/* Answer incoming call. */
		if (trans->gcc.fi->state == VGCS_GCC_ST_U3_GROUP_CALL_PRESENT) {
			osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_JOIN_GC_REQ, NULL);
			return 0;
		}
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "Call already established\n");
		return -EEXIST;
	}

	/* Create new transaction. ORIG will be set when entering U2sl state. */
	trans = trans_alloc_vgcs(ms, protocol, 0xff, callref);
	if (!trans)
		return -ENOMEM;

	/* Setup new call. */
	return osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_SETUP_REQ, NULL);
}

/* Leave a VGC. (If we are the originator, we do not terminate.) */
int gcc_leave(struct osmocom_ms *ms)
{
	struct gsm_trans *trans;

	/* Reject if there is no call. */
	trans = trans_find_ongoing_gcc_bcc(ms);
	if (!trans) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No Call\n");
		return -EINVAL;
	}

	if (trans->gcc.fi->state == VGCS_GCC_ST_U0p_MM_CONNECTION_PENDING ||
	    trans->gcc.fi->state == VGCS_GCC_ST_U1_GROUP_CALL_INITIATED ||
	    trans->gcc.fi->state == VGCS_GCC_ST_U2sl_GROUP_CALL_ACTIVE ||
	    trans->gcc.fi->state == VGCS_GCC_ST_U5_TERMINATION_REQUESTED) {
		LOG_GCC(trans, LOGL_NOTICE, "Cannot leave (abort), in this state.\n");
		return -EINVAL;
	}

	/* Send ABORT-REQ to leave the call. */
	return osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_ABORT_REQ, NULL);
}

/* Hangup VGC/VBC. (If we are the originator, we terminate the call.)  */
int gcc_bcc_hangup(struct osmocom_ms *ms)
{
	struct gsm_trans *trans;

	/* Reject if there is no call. */
	trans = trans_find_ongoing_gcc_bcc(ms);
	if (!trans) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No Call\n");
		return -EINVAL;
	}

	if (trans->gcc.orig) {
		if (trans->gcc.fi->state == VGCS_GCC_ST_U3_GROUP_CALL_PRESENT ||
		    trans->gcc.fi->state == VGCS_GCC_ST_U4_GROUP_CALL_CONN_REQUEST ||
		    trans->gcc.fi->state == VGCS_GCC_ST_U5_TERMINATION_REQUESTED) {
			LOG_GCC(trans, LOGL_NOTICE, "Cannot hangup (terminate) in this state.\n");
			return -EINVAL;
		}
		/* Send TERM-REQ to leave the call. */
		return osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_TERM_REQ, NULL);
	}

	if (trans->gcc.fi->state == VGCS_GCC_ST_U5_TERMINATION_REQUESTED) {
		LOG_GCC(trans, LOGL_NOTICE, "Cannot hangup (abort) in this state.\n");
		return -EINVAL;
	}
	/* Send ABORT-REQ to leave the call. */
	return osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_ABORT_REQ, NULL);
}

int gcc_talk(struct osmocom_ms *ms)
{
	struct gsm_trans *trans;

	/* Reject if there is no call. */
	trans = trans_find_ongoing_gcc_bcc(ms);
	if (!trans) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No Call\n");
		return -EINVAL;
	}

	if (trans->protocol != GSM48_PDISC_GROUP_CC) {
		LOG_GCC(trans, LOGL_NOTICE, "Cannot talk, the ongoing call is not a group call.\n");
		vgcs_vty_notify(trans, "Not a group call\n");
		return -EIO;
	}

	if (trans->gcc.fi->state != VGCS_GCC_ST_U2r_U6_GROUP_CALL_ACTIVE) {
		LOG_GCC(trans, LOGL_NOTICE, "Cannot talk, we are not listening.\n");
		return -EINVAL;
	}

	/* Send TALK-REQ to become talker. */
	return osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_TALK_REQ, NULL);
}

int gcc_listen(struct osmocom_ms *ms)
{
	struct gsm_trans *trans;

	/* Reject if there is no call. */
	trans = trans_find_ongoing_gcc_bcc(ms);
	if (!trans) {
		l23_vty_ms_notify(ms, NULL);
		l23_vty_ms_notify(ms, "No Call\n");
		return -EINVAL;
	}

	if (trans->protocol != GSM48_PDISC_GROUP_CC) {
		LOG_GCC(trans, LOGL_NOTICE, "Cannot become listener, the ongoing call is not a group call.\n");
		vgcs_vty_notify(trans, "Not a group call\n");
		return -EIO;
	}

	if (trans->gcc.fi->state != VGCS_GCC_ST_U2sl_GROUP_CALL_ACTIVE &&
	    trans->gcc.fi->state != VGCS_GCC_ST_U2sr_GROUP_CALL_ACTIVE) {
		LOG_GCC(trans, LOGL_NOTICE, "Cannot listen, we are not talking.\n");
		return -EINVAL;
	}

	/* Send LISTEN-REQ to become listener. */
	return osmo_fsm_inst_dispatch(trans->gcc.fi, VGCS_GCC_EV_LISTEN_REQ, NULL);
}

int gsm44068_dump_calls(struct osmocom_ms *ms, void (*print)(void *, const char *, ...), void *priv)
{
	struct gsm_trans *trans;

	llist_for_each_entry(trans, &ms->trans_list, entry) {
		if (trans->protocol != GSM48_PDISC_GROUP_CC && trans->protocol != GSM48_PDISC_BCAST_CC)
			continue;
		print(priv, "Voice %s call '%d' is %s.\n",
		      (trans->protocol == GSM48_PDISC_GROUP_CC) ? "group" : "broadcast", trans->callref,
		      (trans->gcc.fi->state == VGCS_GCC_ST_U3_GROUP_CALL_PRESENT) ? "incoming" : "active");
	}

	return 0;
}
