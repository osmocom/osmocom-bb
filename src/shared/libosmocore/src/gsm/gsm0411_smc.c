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
 * Whenever the process returns to IDLE, the MM connection is released using
 * MMSMS-REL-REQ. It is allowed to destroy this process while processing
 * this message.
 *
 * There is expeption, if MMSMS-REL-IND is received from lower layer, the
 * process returns to IDLE without sending MMSMS-REL-REQ.
 *
 */

#include <string.h>
#include <errno.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/logging.h>
#include <osmocom/core/timer.h>

#include <osmocom/gsm/gsm0411_utils.h>
#include <osmocom/gsm/gsm0411_smc.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

static void cp_timer_expired(void *data);

#define MAX_SMS_RETRY 2

/* init a new instance */
void gsm411_smc_init(struct gsm411_smc_inst *inst, int network,
	int (*mn_recv) (struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg),
	int (*mm_send) (struct gsm411_smc_inst *inst, int msg_type,
			struct msgb *msg, int cp_msg_type))
{
	memset(inst, 0, sizeof(*inst));
	inst->network = network;
	inst->cp_max_retr = MAX_SMS_RETRY;
	inst->cp_tc1 = GSM411_TMR_TC1A_SEC / (inst->cp_max_retr + 1);
	inst->cp_state = GSM411_CPS_IDLE;
	inst->mn_recv = mn_recv;
	inst->mm_send = mm_send;

	LOGP(DLSMS, LOGL_INFO, "New SMC instance created\n");
}

/* clear instance */
void gsm411_smc_clear(struct gsm411_smc_inst *inst)
{
	LOGP(DLSMS, LOGL_INFO, "Clear SMC instance\n");

	osmo_timer_del(&inst->cp_timer);

	/* free stored msg */
	if (inst->cp_msg) {
		LOGP(DLSMS, LOGL_INFO, "Dropping pending message\n");
		msgb_free(inst->cp_msg);
		inst->cp_msg = NULL;
	}
}

const char *smc_state_names[] = {
	"IDLE",
	"MM_CONN_PENDING",
	"WAIT_CP_ACK",
	"MM_ESTABLISHED",
};

const struct value_string gsm411_cp_cause_strs[] = {
	{ GSM411_CP_CAUSE_NET_FAIL,	"Network Failure" },
	{ GSM411_CP_CAUSE_CONGESTION,	"Congestion" },
	{ GSM411_CP_CAUSE_INV_TRANS_ID,	"Invalid Transaction ID" },
	{ GSM411_CP_CAUSE_SEMANT_INC_MSG, "Semantically Incorrect Message" },
	{ GSM411_CP_CAUSE_INV_MAND_INF,	"Invalid Mandatory Information" },
	{ GSM411_CP_CAUSE_MSGTYPE_NOTEXIST, "Message Type doesn't exist" },
	{ GSM411_CP_CAUSE_MSG_INCOMP_STATE,
				"Message incompatible with protocol state" },
	{ GSM411_CP_CAUSE_IE_NOTEXIST,	"IE does not exist" },
	{ GSM411_CP_CAUSE_PROTOCOL_ERR,	"Protocol Error" },
	{ 0, 0 }
};

static void new_cp_state(struct gsm411_smc_inst *inst,
	enum gsm411_cp_state state)
{
	LOGP(DLSMS, LOGL_INFO, "New CP state %s -> %s\n",
		smc_state_names[inst->cp_state], smc_state_names[state]);
	inst->cp_state = state;
}

static int gsm411_tx_cp_error(struct gsm411_smc_inst *inst, uint8_t cause)
{
	struct msgb *nmsg = gsm411_msgb_alloc();
	uint8_t *causep;

	LOGP(DLSMS, LOGL_NOTICE, "TX CP-ERROR, cause %d (%s)\n", cause,
		get_value_string(gsm411_cp_cause_strs, cause));

	causep = msgb_put(nmsg, 1);
	*causep = cause;

	return inst->mm_send(inst, GSM411_MMSMS_DATA_REQ, nmsg,
		GSM411_MT_CP_ERROR);
}

/* establish SMC connection */
static int gsm411_mnsms_est_req(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	struct msgb *nmsg;

	if (inst->cp_msg) {
		LOGP(DLSMS, LOGL_FATAL, "EST REQ, but we already have an "
			"cp_msg. This should never happen, please fix!\n");
		msgb_free(inst->cp_msg);
	}

	inst->cp_msg = msg;
	new_cp_state(inst, GSM411_CPS_MM_CONN_PENDING);
	/* clear stored release flag */
	inst->cp_rel = 0;
	/* send MMSMS_EST_REQ */
	nmsg = gsm411_msgb_alloc();
	return inst->mm_send(inst, GSM411_MMSMS_EST_REQ, nmsg, 0);
}

static int gsm411_mmsms_send_msg(struct gsm411_smc_inst *inst)
{
	struct msgb *nmsg;

	LOGP(DLSMS, LOGL_INFO, "Send CP data\n");
	/* reset retry counter */
	if (inst->cp_state != GSM411_CPS_WAIT_CP_ACK)
		inst->cp_retx = 0;
	/* 5.2.3.1.2: enter MO-wait for CP-ACK */
	/* 5.2.3.2.3: enter MT-wait for CP-ACK */
	new_cp_state(inst, GSM411_CPS_WAIT_CP_ACK);
	inst->cp_timer.data = inst;
	inst->cp_timer.cb = cp_timer_expired;
	/* 5.3.2.1: Set Timer TC1A */
	osmo_timer_schedule(&inst->cp_timer, inst->cp_tc1, 0);
	/* clone cp_msg */
	nmsg = gsm411_msgb_alloc();
	memcpy(msgb_put(nmsg, inst->cp_msg->len), inst->cp_msg->data,
		inst->cp_msg->len);
	/* send MMSMS_DATA_REQ with CP-DATA */
	return inst->mm_send(inst, GSM411_MMSMS_DATA_REQ, nmsg,
				GSM411_MT_CP_DATA);
}

static int gsm411_mmsms_est_cnf(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	if (!inst->cp_msg) {
		LOGP(DLSMS, LOGL_FATAL, "EST CNF, but we have no cp_msg. This "
			"should never happen, please fix!\n");
		return -EINVAL;
	}

	return gsm411_mmsms_send_msg(inst);
}

/* SMC TC1* is expired */
static void cp_timer_expired(void *data)
{
	struct gsm411_smc_inst *inst = data;
	struct msgb *nmsg;

	if (inst->cp_retx == inst->cp_max_retr) {

		LOGP(DLSMS, LOGL_INFO, "TC1* timeout, no more retries.\n");
		/* 5.3.2.1: enter idle state */
		new_cp_state(inst, GSM411_CPS_IDLE);
		/* indicate error */
		nmsg = gsm411_msgb_alloc();
		inst->mn_recv(inst, GSM411_MNSMS_ERROR_IND, nmsg);
		msgb_free(nmsg);
		/* free pending stored msg */
		if (inst->cp_msg) {
			msgb_free(inst->cp_msg);
			inst->cp_msg = NULL;
		}
		/* release MM connection */
		nmsg = gsm411_msgb_alloc();
		inst->mm_send(inst, GSM411_MMSMS_REL_REQ, nmsg, 0);
		return;
	}

	LOGP(DLSMS, LOGL_INFO, "TC1* timeout, retrying...\n");
	inst->cp_retx++;
	gsm411_mmsms_est_cnf(inst, NULL);
}

static int gsm411_mmsms_cp_ack(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	/* free stored msg */
	if (inst->cp_msg) {
		msgb_free(inst->cp_msg);
		inst->cp_msg = NULL;
	}

	LOGP(DLSMS, LOGL_INFO, "Received CP-ACK\n");
	/* 5.3.2.1 enter MM Connection established */
	new_cp_state(inst, GSM411_CPS_MM_ESTABLISHED);
	/* 5.3.2.1: Reset Timer TC1* */
	osmo_timer_del(&inst->cp_timer);

	/* pending release? */
	if (inst->cp_rel) {
		struct msgb *nmsg;

		LOGP(DLSMS, LOGL_INFO, "We have pending release.\n");
		new_cp_state(inst, GSM411_CPS_IDLE);
		/* release MM connection */
		nmsg = gsm411_msgb_alloc();
		return inst->mm_send(inst, GSM411_MMSMS_REL_REQ, nmsg, 0);
	}

	return 0;
}

static int gsm411_mmsms_cp_data(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	struct msgb *nmsg;
	int mt = GSM411_MNSMS_DATA_IND;

	LOGP(DLSMS, LOGL_INFO, "Received CP-DATA\n");
	/* 5.3.1 enter MM Connection established (if idle) */
	if (inst->cp_state == GSM411_CPS_IDLE) {
		new_cp_state(inst, GSM411_CPS_MM_ESTABLISHED);
		mt = GSM411_MNSMS_EST_IND;
		/* clear stored release flag */
		inst->cp_rel = 0;
	}
	/* send MMSMS_DATA_REQ (CP ACK) */
	nmsg = gsm411_msgb_alloc();
	inst->mm_send(inst, GSM411_MMSMS_DATA_REQ, nmsg, GSM411_MT_CP_ACK);
	/* indicate data */
	inst->mn_recv(inst, mt, msg);

	return 0;
}

/* send CP DATA */
static int gsm411_mnsms_data_req(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	if (inst->cp_msg) {
		LOGP(DLSMS, LOGL_FATAL, "DATA REQ, but we already have an "
			"cp_msg. This should never happen, please fix!\n");
		msgb_free(inst->cp_msg);
	}

	/* store and send */
	inst->cp_msg = msg;
	return gsm411_mmsms_send_msg(inst);
}

/* release SMC connection */
static int gsm411_mnsms_rel_req(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	struct msgb *nmsg;

	msgb_free(msg);

	/* discard silently */
	if (inst->cp_state == GSM411_CPS_IDLE)
		return 0;

	/* store release, until established or released */
	if (inst->cp_state != GSM411_CPS_MM_ESTABLISHED) {
		LOGP(DLSMS, LOGL_NOTICE,
			"Cannot release yet current state: %s\n",
			smc_state_names[inst->cp_state]);
		inst->cp_rel = 1;
		return 0;
	}

	/* free stored msg */
	if (inst->cp_msg) {
		msgb_free(inst->cp_msg);
		inst->cp_msg = NULL;
	}

	new_cp_state(inst, GSM411_CPS_IDLE);
	/* release MM connection */
	nmsg = gsm411_msgb_alloc();
	return inst->mm_send(inst, GSM411_MMSMS_REL_REQ, nmsg, 0);
}

static int gsm411_mmsms_cp_error(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	struct msgb *nmsg;

	/* free stored msg */
	if (inst->cp_msg) {
		msgb_free(inst->cp_msg);
		inst->cp_msg = NULL;
	}

	LOGP(DLSMS, LOGL_INFO, "Received CP-ERROR\n");
	/* 5.3.4 enter idle */
	new_cp_state(inst, GSM411_CPS_IDLE);
	/* indicate error */
	inst->mn_recv(inst, GSM411_MNSMS_ERROR_IND, msg);
	/* release MM connection */
	nmsg = gsm411_msgb_alloc();
	return inst->mm_send(inst, GSM411_MMSMS_REL_REQ, nmsg, 0);
}

static int gsm411_mmsms_rel_ind(struct gsm411_smc_inst *inst, struct msgb *msg)
{
	struct msgb *nmsg;

	/* free stored msg */
	if (inst->cp_msg) {
		msgb_free(inst->cp_msg);
		inst->cp_msg = NULL;
	}

	LOGP(DLSMS, LOGL_INFO, "MM layer is released\n");
	/* 5.3.4 enter idle */
	new_cp_state(inst, GSM411_CPS_IDLE);
	/* indicate error */
	nmsg = gsm411_msgb_alloc();
	inst->mn_recv(inst, GSM411_MNSMS_ERROR_IND, nmsg);
	msgb_free(nmsg);

	return 0;
}

/* abort SMC connection */
static int gsm411_mnsms_abort_req(struct gsm411_smc_inst *inst,
	struct msgb *msg)
{
	struct msgb *nmsg;

	/* free stored msg */
	if (inst->cp_msg) {
		msgb_free(inst->cp_msg);
		inst->cp_msg = NULL;
	}

	/* 5.3.4 go idle */
	new_cp_state(inst, GSM411_CPS_IDLE);
	/* send MMSMS_DATA_REQ with CP-ERROR */
	inst->mm_send(inst, GSM411_MMSMS_DATA_REQ, msg, GSM411_MT_CP_ERROR);
	/* release MM connection */
	nmsg = gsm411_msgb_alloc();
	return inst->mm_send(inst, GSM411_MMSMS_REL_REQ, nmsg, 0);
}

/* statefull handling for MNSMS SAP messages */
static struct smcdownstate {
	uint32_t	states;
	int		type;
	const char 	*name;
	int		(*rout) (struct gsm411_smc_inst *inst,
					struct msgb *msg);
} smcdownstatelist[] = {
	/* establish request */
	{SBIT(GSM411_CPS_IDLE),
	GSM411_MNSMS_EST_REQ,
	 "MNSMS-EST-REQ", gsm411_mnsms_est_req},

	/* release request */
	{ALL_STATES,
	GSM411_MNSMS_REL_REQ,
	 "MNSMS-REL-REQ", gsm411_mnsms_rel_req},

	/* data request */
	{SBIT(GSM411_CPS_MM_ESTABLISHED),
	GSM411_MNSMS_DATA_REQ,
	 "MNSMS-DATA-REQ", gsm411_mnsms_data_req},

	/* abort request */
	{ALL_STATES - SBIT(GSM411_CPS_IDLE),
	GSM411_MNSMS_ABORT_REQ,
	 "MNSMS-ABORT-REQ", gsm411_mnsms_abort_req},
};

#define SMCDOWNSLLEN \
	(sizeof(smcdownstatelist) / sizeof(struct smcdownstate))

/* message from upper layer */
int gsm411_smc_send(struct gsm411_smc_inst *inst, int msg_type,
	struct msgb *msg)
{
	int i, rc;

	/* find function for current state and message */
	for (i = 0; i < SMCDOWNSLLEN; i++) {
		if ((msg_type == smcdownstatelist[i].type)
		  && (SBIT(inst->cp_state) & smcdownstatelist[i].states))
				break;
	}
	if (i == SMCDOWNSLLEN) {
		LOGP(DLSMS, LOGL_NOTICE, "Message %u unhandled at this state "
			"%s.\n", msg_type, smc_state_names[inst->cp_state]);
		msgb_free(msg);
		return 0;
	}

	LOGP(DLSMS, LOGL_INFO, "Message %s received in state %s\n",
		smcdownstatelist[i].name, smc_state_names[inst->cp_state]);

	rc = smcdownstatelist[i].rout(inst, msg);

	return rc;
}

/* statefull handling for MMSMS SAP messages */
static struct smcdatastate {
	uint32_t	states;
	int		type, cp_type;
	const char 	*name;
	int		(*rout) (struct gsm411_smc_inst *inst,
					struct msgb *msg);
} smcdatastatelist[] = {
	/* establish confirm */
	{SBIT(GSM411_CPS_MM_CONN_PENDING),
	 GSM411_MMSMS_EST_CNF, 0,
	 "MMSMS-EST-CNF", gsm411_mmsms_est_cnf},

	/* establish indication (CP DATA) */
	{SBIT(GSM411_CPS_IDLE),
	 GSM411_MMSMS_EST_IND, GSM411_MT_CP_DATA,
	 "MMSMS-EST-IND (CP DATA)", gsm411_mmsms_cp_data},

	/* data indication (CP DATA) */
	{SBIT(GSM411_CPS_MM_ESTABLISHED),
	 GSM411_MMSMS_DATA_IND, GSM411_MT_CP_DATA,
	 "MMSMS-DATA-IND (CP DATA)", gsm411_mmsms_cp_data},

	/* data indication (CP ACK) */
	{SBIT(GSM411_CPS_WAIT_CP_ACK),
	 GSM411_MMSMS_DATA_IND, GSM411_MT_CP_ACK,
	 "MMSMS-DATA-IND (CP ACK)", gsm411_mmsms_cp_ack},

	/* data indication (CP ERROR) */
	{ALL_STATES,
	 GSM411_MMSMS_DATA_IND, GSM411_MT_CP_ERROR,
	 "MMSMS-DATA-IND (CP_ERROR)", gsm411_mmsms_cp_error},

	/* release indication */
	{ALL_STATES - SBIT(GSM411_CPS_IDLE),
	 GSM411_MMSMS_REL_IND, 0,
	 "MMSMS-REL-IND", gsm411_mmsms_rel_ind},

};

#define SMCDATASLLEN \
	(sizeof(smcdatastatelist) / sizeof(struct smcdatastate))

/* message from lower layer
 * WARNING: We must not free msg, since it will be performed by the
 * lower layer. */
int gsm411_smc_recv(struct gsm411_smc_inst *inst, int msg_type,
	struct msgb *msg, int cp_msg_type)
{
	int i, rc;

	/* find function for current state and message */
	for (i = 0; i < SMCDATASLLEN; i++) {
		/* state must machtch, MM message must match
		 * CP msg must match only in case of MMSMS_DATA_IND
		 */
		if ((msg_type == smcdatastatelist[i].type)
		  && (SBIT(inst->cp_state) & smcdatastatelist[i].states)
		  && (msg_type != GSM411_MMSMS_DATA_IND
		   || cp_msg_type == smcdatastatelist[i].cp_type))
				break;
	}
	if (i == SMCDATASLLEN) {
		LOGP(DLSMS, LOGL_NOTICE, "Message 0x%x/%u unhandled at this "
			"state %s.\n", msg_type, cp_msg_type,
			smc_state_names[inst->cp_state]);
		if (msg_type == GSM411_MMSMS_EST_IND
		 || msg_type == GSM411_MMSMS_DATA_IND) {
			struct msgb *nmsg;

			LOGP(DLSMS, LOGL_NOTICE, "RX Unimplemented CP "
				"msg_type: 0x%02x\n", msg_type);
			/* 5.3.4 enter idle */
			new_cp_state(inst, GSM411_CPS_IDLE);
			/* indicate error */
			gsm411_tx_cp_error(inst,
				GSM411_CP_CAUSE_MSGTYPE_NOTEXIST);
			/* send error indication to upper layer */
			nmsg = gsm411_msgb_alloc();
			inst->mn_recv(inst, GSM411_MNSMS_ERROR_IND, nmsg);
			msgb_free(nmsg);
			/* release MM connection */
			nmsg = gsm411_msgb_alloc();
			return inst->mm_send(inst, GSM411_MMSMS_REL_REQ, nmsg,
						0);
		}
		return 0;
	}

	LOGP(DLSMS, LOGL_INFO, "Message %s received in state %s\n",
		smcdatastatelist[i].name, smc_state_names[inst->cp_state]);

	rc = smcdatastatelist[i].rout(inst, msg);

	return rc;
}
