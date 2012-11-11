/* Point-to-Point (PP) Short Message Service (SMS)
 * Support on Mobile Radio Interface
 * 3GPP TS 04.11 version 7.1.0 Release 1998 / ETSI TS 100 942 V7.1.0 */

/* (C) 2008 by Daniel Willmann <daniel@totalueberwachung.de>
 * (C) 2009 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2010 by On-Waves
 * (C) 2011 by Andreas Eversberg <jolly@eversberg.eu>
 *
 * All Rights Reserved
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
 *
 */

/* Notes on msg:
 *
 * Messages from lower layer are freed by lower layer.
 *
 * Messages to upper layer are freed after upper layer call returns, so upper
 * layer cannot use data after returning. Upper layer must not free the msg.
 *
 * This implies: Lower layer messages can be forwarded to upper layer.
 *
 * Upper layer messages are freed by lower layer, so they must not be freed
 * after calling lower layer.
 *
 *
 * Notes on release:
 *
 * Sending Abort/Release (MNSMS-ABORT-REQ or MNSMS-REL-REQ) may cause the
 * lower layer to become IDLE. Then it is allowed to destroy this instance,
 * so sending this this MUST be the last thing that is done.
 *
 */


#include <string.h>
#include <errno.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/timer.h>
#include <osmocom/gsm/tlv.h>

#include <osmocom/gsm/gsm0411_utils.h>
#include <osmocom/gsm/gsm0411_smc.h>
#include <osmocom/gsm/gsm0411_smr.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

static void rp_timer_expired(void *data);

/* init a new instance */
void gsm411_smr_init(struct gsm411_smr_inst *inst, int network,
	int (*rl_recv) (struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg),
	int (*mn_send) (struct gsm411_smr_inst *inst, int msg_type,
			struct msgb *msg))
{
	memset(inst, 0, sizeof(*inst));
	inst->network = network;
	inst->rp_state = GSM411_RPS_IDLE;
	inst->rl_recv = rl_recv;
	inst->mn_send = mn_send;
	inst->rp_timer.data = inst;
	inst->rp_timer.cb = rp_timer_expired;

	LOGP(DLSMS, LOGL_INFO, "New SMR instance created\n");
}

/* clear instance */
void gsm411_smr_clear(struct gsm411_smr_inst *inst)
{
	LOGP(DLSMS, LOGL_INFO, "Clear SMR instance\n");

	osmo_timer_del(&inst->rp_timer);
}

const char *smr_state_names[] = {
	"IDLE",
	"WAIT_FOR_RP_ACK",
	"illegal state 2"
	"WAIT_TO_TX_RP_ACK",
	"WAIT_FOR_RETRANS_T",
};

const struct value_string gsm411_rp_cause_strs[] = {
	{ GSM411_RP_CAUSE_MO_NUM_UNASSIGNED, "(MO) Number not assigned" },
	{ GSM411_RP_CAUSE_MO_OP_DET_BARR, "(MO) Operator determined barring" },
	{ GSM411_RP_CAUSE_MO_CALL_BARRED, "(MO) Call barred" },
	{ GSM411_RP_CAUSE_MO_SMS_REJECTED, "(MO) SMS rejected" },
	{ GSM411_RP_CAUSE_MO_DEST_OUT_OF_ORDER, "(MO) Destination out of order" },
	{ GSM411_RP_CAUSE_MO_UNIDENTIFIED_SUBSCR, "(MO) Unidentified subscriber" },
	{ GSM411_RP_CAUSE_MO_FACILITY_REJ, "(MO) Facility reject" },
	{ GSM411_RP_CAUSE_MO_UNKNOWN_SUBSCR, "(MO) Unknown subscriber" },
	{ GSM411_RP_CAUSE_MO_NET_OUT_OF_ORDER, "(MO) Network out of order" },
	{ GSM411_RP_CAUSE_MO_TEMP_FAIL, "(MO) Temporary failure" },
	{ GSM411_RP_CAUSE_MO_CONGESTION, "(MO) Congestion" },
	{ GSM411_RP_CAUSE_MO_RES_UNAVAIL, "(MO) Resource unavailable" },
	{ GSM411_RP_CAUSE_MO_REQ_FAC_NOTSUBSCR, "(MO) Requested facility not subscribed" },
	{ GSM411_RP_CAUSE_MO_REQ_FAC_NOTIMPL, "(MO) Requested facility not implemented" },
	{ GSM411_RP_CAUSE_MO_INTERWORKING, "(MO) Interworking" },
	/* valid only for MT */
	{ GSM411_RP_CAUSE_MT_MEM_EXCEEDED, "(MT) Memory Exceeded" },
	/* valid for both directions */
	{ GSM411_RP_CAUSE_INV_TRANS_REF, "Invalid Transaction Reference" },
	{ GSM411_RP_CAUSE_SEMANT_INC_MSG, "Semantically Incorrect Message" },
	{ GSM411_RP_CAUSE_INV_MAND_INF, "Invalid Mandatory Information" },
	{ GSM411_RP_CAUSE_MSGTYPE_NOTEXIST, "Message Type non-existant" },
	{ GSM411_RP_CAUSE_MSG_INCOMP_STATE, "Message incompatible with protocol state" },
	{ GSM411_RP_CAUSE_IE_NOTEXIST, "Information Element not existing" },
	{ GSM411_RP_CAUSE_PROTOCOL_ERR, "Protocol Error" },
	{ 0, NULL }
};

static void new_rp_state(struct gsm411_smr_inst *inst,
	enum gsm411_rp_state state)
{
	LOGP(DLSMS, LOGL_INFO, "New RP state %s -> %s\n",
		smr_state_names[inst->rp_state], smr_state_names[state]);
	inst->rp_state = state;

	/* stop timer when going idle */
	if (state == GSM411_RPS_IDLE)
		osmo_timer_del(&inst->rp_timer);
}

/* Prefix msg with a RP-DATA header and send as CP-DATA */
static int gsm411_rp_sendmsg(struct gsm411_smr_inst *inst, struct msgb *msg,
			     uint8_t rp_msg_type, uint8_t rp_msg_ref,
			     int mnsms_msg_type)
{
	struct gsm411_rp_hdr *rp;
	uint8_t len = msg->len;

	/* GSM 04.11 RP-DATA header */
	rp = (struct gsm411_rp_hdr *)msgb_push(msg, sizeof(*rp));
	rp->len = len + 2;
	rp->msg_type = rp_msg_type;
	rp->msg_ref = rp_msg_ref; /* FIXME: Choose randomly */

	return inst->mn_send(inst, mnsms_msg_type, msg);
}

static int gsm411_send_rp_error(struct gsm411_smr_inst *inst,
				uint8_t msg_ref, uint8_t cause)
{
	struct msgb *msg = gsm411_msgb_alloc();

	msgb_tv_put(msg, 1, cause);

	LOGP(DLSMS, LOGL_NOTICE, "TX: SMS RP ERROR, cause %d (%s)\n", cause,
		get_value_string(gsm411_rp_cause_strs, cause));

	return gsm411_rp_sendmsg(inst, msg,
		(inst->network) ? GSM411_MT_RP_ERROR_MT : GSM411_MT_RP_ERROR_MO,
		msg_ref, GSM411_MNSMS_DATA_REQ);
}

static int gsm411_send_release(struct gsm411_smr_inst *inst)
{
	struct msgb *msg = gsm411_msgb_alloc();

	LOGP(DLSMS, LOGL_DEBUG, "TX: MNSMS-REL-REQ\n");

	return inst->mn_send(inst, GSM411_MNSMS_REL_REQ, msg);
}

static int gsm411_send_abort(struct gsm411_smr_inst *inst)
{
	struct msgb *msg = gsm411_msgb_alloc();

	msgb_tv_put(msg, 1, 111); //FIXME: better idea ? */
	LOGP(DLSMS, LOGL_DEBUG, "TX: MNSMS-ABORT-REQ\n");

	return inst->mn_send(inst, GSM411_MNSMS_ABORT_REQ, msg);
}

static int gsm411_send_report(struct gsm411_smr_inst *inst)
{
	struct msgb *msg = gsm411_msgb_alloc();

	LOGP(DLSMS, LOGL_DEBUG, "Sending empty SM_RL_REPORT_IND\n");

	return inst->rl_recv(inst, GSM411_SM_RL_REPORT_IND, msg);
}

static int gsm411_rl_data_req(struct gsm411_smr_inst *inst, struct msgb *msg)
{
	LOGP(DLSMS, LOGL_DEBUG,  "TX SMS RP-DATA\n");
	/* start TR1N and enter 'wait for RP-ACK state' */
	osmo_timer_schedule(&inst->rp_timer, GSM411_TMR_TR1M);
	new_rp_state(inst, GSM411_RPS_WAIT_FOR_RP_ACK);

	return inst->mn_send(inst, GSM411_MNSMS_EST_REQ, msg);
}

static int gsm411_rl_report_req(struct gsm411_smr_inst *inst, struct msgb *msg)
{
	LOGP(DLSMS, LOGL_DEBUG,  "TX SMS REPORT\n");
	new_rp_state(inst, GSM411_RPS_IDLE);

	inst->mn_send(inst, GSM411_MNSMS_DATA_REQ, msg);
	gsm411_send_release(inst);
	return 0;
}

static int gsm411_mnsms_est_ind(struct gsm411_smr_inst *inst, struct msgb *msg)
{
	struct gsm48_hdr *gh = (struct gsm48_hdr*)msg->l3h;
	struct gsm411_rp_hdr *rp_data = (struct gsm411_rp_hdr*)&gh->data;
	uint8_t msg_type =  rp_data->msg_type & 0x07;
	int rc;

	/* check direction */
	if (inst->network == (msg_type & 1)) {
		LOGP(DLSMS, LOGL_NOTICE, "Invalid RP type 0x%02x\n", msg_type);
		gsm411_send_rp_error(inst, rp_data->msg_ref,
					  GSM411_RP_CAUSE_MSG_INCOMP_STATE);
		new_rp_state(inst, GSM411_RPS_IDLE);
		gsm411_send_release(inst);
		return -EINVAL;
	}

	switch (msg_type) {
	case GSM411_MT_RP_DATA_MT:
	case GSM411_MT_RP_DATA_MO:
		LOGP(DLSMS, LOGL_DEBUG,  "RX SMS RP-DATA\n");
		/* start TR2N and enter 'wait to send RP-ACK state' */
		osmo_timer_schedule(&inst->rp_timer, GSM411_TMR_TR2M);
		new_rp_state(inst, GSM411_RPS_WAIT_TO_TX_RP_ACK);
		rc = inst->rl_recv(inst, GSM411_SM_RL_DATA_IND, msg);
		break;
	case GSM411_MT_RP_SMMA_MO:
		LOGP(DLSMS, LOGL_DEBUG,  "RX SMS RP-SMMA\n");
		/* start TR2N and enter 'wait to send RP-ACK state' */
		osmo_timer_schedule(&inst->rp_timer, GSM411_TMR_TR2M);
		new_rp_state(inst, GSM411_RPS_WAIT_TO_TX_RP_ACK);
		rc = inst->rl_recv(inst, GSM411_SM_RL_DATA_IND, msg);
		break;
	default:
		LOGP(DLSMS, LOGL_NOTICE, "Invalid RP type 0x%02x\n", msg_type);
		gsm411_send_rp_error(inst, rp_data->msg_ref,
					  GSM411_RP_CAUSE_MSGTYPE_NOTEXIST);
		new_rp_state(inst, GSM411_RPS_IDLE);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int gsm411_mnsms_data_ind_tx(struct gsm411_smr_inst *inst,
	struct msgb *msg)
{
	struct gsm48_hdr *gh = (struct gsm48_hdr*)msg->l3h;
	struct gsm411_rp_hdr *rp_data = (struct gsm411_rp_hdr*)&gh->data;
	uint8_t msg_type =  rp_data->msg_type & 0x07;
	int rc;

	/* check direction */
	if (inst->network == (msg_type & 1)) {
		LOGP(DLSMS, LOGL_NOTICE, "Invalid RP type 0x%02x\n", msg_type);
		gsm411_send_rp_error(inst, rp_data->msg_ref,
					  GSM411_RP_CAUSE_MSG_INCOMP_STATE);
		new_rp_state(inst, GSM411_RPS_IDLE);
		gsm411_send_release(inst);
		return -EINVAL;
	}

	switch (msg_type) {
	case GSM411_MT_RP_ACK_MO:
	case GSM411_MT_RP_ACK_MT:
		LOGP(DLSMS, LOGL_DEBUG, "RX SMS RP-ACK\n");
		new_rp_state(inst, GSM411_RPS_IDLE);
		inst->rl_recv(inst, GSM411_SM_RL_REPORT_IND, msg);
		gsm411_send_release(inst);
		return 0;
	case GSM411_MT_RP_ERROR_MO:
	case GSM411_MT_RP_ERROR_MT:
		LOGP(DLSMS, LOGL_DEBUG, "RX SMS RP-ERROR\n");
		new_rp_state(inst, GSM411_RPS_IDLE);
		inst->rl_recv(inst, GSM411_SM_RL_REPORT_IND, msg);
		gsm411_send_release(inst);
		return 0;
	default:
		LOGP(DLSMS, LOGL_NOTICE, "Invalid RP type 0x%02x\n", msg_type);
		gsm411_send_rp_error(inst, rp_data->msg_ref,
					  GSM411_RP_CAUSE_MSGTYPE_NOTEXIST);
		new_rp_state(inst, GSM411_RPS_IDLE);
		gsm411_send_release(inst);
		return -EINVAL;
	}

	return rc;
}

static int gsm411_mnsms_error_ind_tx(struct gsm411_smr_inst *inst,
	struct msgb *msg)
{
	LOGP(DLSMS, LOGL_DEBUG, "RX SMS MNSMS-ERROR-IND\n");
	new_rp_state(inst, GSM411_RPS_IDLE);
	inst->rl_recv(inst, GSM411_SM_RL_REPORT_IND, msg);
	gsm411_send_release(inst);
	return 0;
}

static int gsm411_mnsms_error_ind_rx(struct gsm411_smr_inst *inst,
	struct msgb *msg)
{
	LOGP(DLSMS, LOGL_DEBUG, "RX SMS MNSMS-ERROR-IND\n");
	new_rp_state(inst, GSM411_RPS_IDLE);
	return inst->rl_recv(inst, GSM411_SM_RL_REPORT_IND, msg);
}

/* SMR TR1* is expired */
static void rp_timer_expired(void *data)
{
	struct gsm411_smr_inst *inst = data;

	if (inst->rp_state == GSM411_RPS_WAIT_TO_TX_RP_ACK)
		LOGP(DLSMS, LOGL_DEBUG, "TR2N\n");
	else
		LOGP(DLSMS, LOGL_DEBUG, "TR1N\n");
	gsm411_send_report(inst);
	gsm411_send_abort(inst);
}

/* statefull handling for SM-RL SAP messages */
static struct smrdownstate {
	uint32_t	states;
	int		type;
	const char 	*name;
	int		(*rout) (struct gsm411_smr_inst *inst,
					struct msgb *msg);
} smrdownstatelist[] = {
	/* data request */
	{SBIT(GSM411_RPS_IDLE),
	GSM411_SM_RL_DATA_REQ,
	 "SM-RL-DATA_REQ", gsm411_rl_data_req},

	/* report request */
	{SBIT(GSM411_RPS_WAIT_TO_TX_RP_ACK),
	GSM411_SM_RL_REPORT_REQ,
	 "SM-RL-REPORT_REQ", gsm411_rl_report_req},
};

#define SMRDOWNSLLEN \
	(sizeof(smrdownstatelist) / sizeof(struct smrdownstate))

/* message from upper layer */
int gsm411_smr_send(struct gsm411_smr_inst *inst, int msg_type,
	struct msgb *msg)
{
	int i, rc;

	/* find function for current state and message */
	for (i = 0; i < SMRDOWNSLLEN; i++) {
		if ((msg_type == smrdownstatelist[i].type)
		  && (SBIT(inst->rp_state) & smrdownstatelist[i].states))
				break;
	}
	if (i == SMRDOWNSLLEN) {
		LOGP(DLSMS, LOGL_NOTICE, "Message %u unhandled at this state "
			"%s.\n", msg_type, smr_state_names[inst->rp_state]);
		msgb_free(msg);
		return 0;
	}

	LOGP(DLSMS, LOGL_INFO, "Message %s received in state %s\n",
		smrdownstatelist[i].name, smr_state_names[inst->rp_state]);

	rc = smrdownstatelist[i].rout(inst, msg);

	return rc;
}

/* statefull handling for MMSMS SAP messages */
static struct smrdatastate {
	uint32_t	states;
	int		type;
	const char 	*name;
	int		(*rout) (struct gsm411_smr_inst *inst,
					struct msgb *msg);
} smrdatastatelist[] = {
	/* establish indication */
	{SBIT(GSM411_RPS_IDLE),
	 GSM411_MNSMS_EST_IND,
	 "MNSMS-EST-IND", gsm411_mnsms_est_ind},

	/* data indication */
	{SBIT(GSM411_RPS_WAIT_FOR_RP_ACK),
	 GSM411_MNSMS_DATA_IND,
	 "MNSMS-DATA-IND", gsm411_mnsms_data_ind_tx},

	/* error indication */
	{SBIT(GSM411_RPS_WAIT_FOR_RP_ACK),
	 GSM411_MNSMS_ERROR_IND,
	 "MNSMS-ERROR-IND", gsm411_mnsms_error_ind_tx},

	/* error indication */
	{SBIT(GSM411_RPS_WAIT_TO_TX_RP_ACK),
	 GSM411_MNSMS_ERROR_IND,
	 "MNSMS-ERROR-IND", gsm411_mnsms_error_ind_rx},

};

#define SMRDATASLLEN \
	(sizeof(smrdatastatelist) / sizeof(struct smrdatastate))

/* message from lower layer
 * WARNING: We must not free msg, since it will be performed by the
 * lower layer. */
int gsm411_smr_recv(struct gsm411_smr_inst *inst, int msg_type,
	struct msgb *msg)
{
	int i, rc;

	/* find function for current state and message */
	for (i = 0; i < SMRDATASLLEN; i++) {
		/* state must machtch, MM message must match
		 * CP msg must match only in case of MMSMS_DATA_IND
		 */
		if ((msg_type == smrdatastatelist[i].type)
		  && (SBIT(inst->rp_state) & smrdatastatelist[i].states))
			break;
	}
	if (i == SMRDATASLLEN) {
		LOGP(DLSMS, LOGL_NOTICE, "Message %u unhandled at this state "
			"%s.\n", msg_type, smr_state_names[inst->rp_state]);
		return 0;
	}

	LOGP(DLSMS, LOGL_INFO, "Message %s received in state %s\n",
		smrdatastatelist[i].name, smr_state_names[inst->rp_state]);

	rc = smrdatastatelist[i].rout(inst, msg);

	return rc;
}
