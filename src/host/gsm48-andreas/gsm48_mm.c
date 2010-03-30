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

/*
 * notes
 */

/*
 * Notes on IMSI detach procedure:
 *
 * At the end of the procedure, the state of MM, RR, cell selection: No SIM.
 *
 * In MM IDLE state, cell available: RR is establised, IMSI detach specific
 * procedure is performed.
 *
 * In MM IDLE state, no cell: State is silently changed to No SIM.
 *
 * During any MM connection state, or Wait for network command: All MM
 * connections (if any) are released locally, and IMSI detach specific
 * procedure is performed.
 *
 * During IMSI detach processing: Request of IMSI detach is ignored.
 *
 * Any other state: The special 'delay_detach' flag is set only. If set, at any
 * state transition we will clear the flag and restart the procedure again.
 *
 * The procedure is not spec conform, but always succeeds.
 *
 */

/*
 * messages
 */

/* allocate GSM 04.08 mobility management message (betreen MM and RR) */
static struct msgb *gsm48_mm_msgb_alloc(void)
{
	struct msgb *msg;

	msg = msgb_alloc_headroom(GSM48_MM_ALLOC_SIZE, GSM48_MM_ALLOC_HEADROOM,
		"GSM 04.08 MM");
	if (!msg)
		return NULL;

	return msg;
}

/*
 * state transition
 */

/* Set new MM state, also new substate in case of MM IDLE state. */
static void new_mm_state(struct gsm48_mmlayer *mm, int state, int substate)
{
	DEBUGP(DMM, "(ms %s) new state %s", mm->ms, mm_state_names[mm->state]);
	if (mm->state == GSM48_MM_ST_MM_ILDE)
		DEBUGP(DMM, " substate %s", mm_substate_names[mm->substate]);
	DEBUGP(DMM, "-> %s", mm_state_names[state]);
	if (state == GSM48_MM_ST_MM_ILDE)
		DEBUGP(DMM, " substate %s", mm_substate_names[substate]);
	DEBUGP(DMM, "\n");

	mm->state = state;
	mm->substate = substate;

	/* resend detach event, if flag is set */
	if (mm->delay_detach) {
		struct nmsg *msg;

		mm->delay_detach = 0;

		nmsg = gsm48_mm_msgb_alloc(GSM48_MM_EVENT_IMSI_DETACH);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mm_sendevent(ms, nmsg);
	}
}

/* 4.2.3 when returning to MM IDLE state, this function is called */
static int gsm48_mm_return_idle(struct osmocom_ms *ms)
{
	struct gsm_subscriber *subsr = &ms->subscr;
	struct gsm48_mmlayer *mm == &ms->mmlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;

	/* no sim present */
	if (!subscr->sim_valid) {
		DEBUGP(DMM, "(ms %s) SIM invalid as returning to IDLE",
			ms->name);
		/* imsi detach due to power off */
		if (mm->power_off) {
			struct msgb *nmsg;
	
			nmsg = gsm48_mm_msgb_alloc(GSM48_MM_EVENT_POWER_OFF);
			if (!nmsg)
				return -ENOMEM;
			gsm48_mm_sendevent(ms, nmsg);
** do we need this
		}
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_NO_IMSI);

		return 0;
	}

	/* no cell found */
	if (cs->state != GSM_C3_CAMPED_NORMALLY
	 && cs->state != GSM_C7_CAMPED_ANY_CELL) {
		DEBUGP(DMM, "(ms %s) No cell found as returning to IDLE",
			ms->name);
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_PLMN_SEARCH);

		return 0;
	}

	/* return from location update with "Roaming not allowed" */
	if (mm->state == GSM48_MM_ST_LOC_UPD_REJ && mm->lupd_rej_cause == 13) {
		DEBUGP(DMM, "(ms %s) Roaming not allowed as returning to IDLE",
			ms->name);
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_PLMN_SEARCH);

		return 0;
	}

	/* selected cell equals the registered LAI */
	if (subscr->plmn_valid && cs->mcc == subscr->plmn_mcc
	 && cs->mnc == subscr->plmn_mnc && cs->lac == subscr->plmn_lac) {
		DEBUGP(DMM, "(ms %s) We are in registered LAI as returning to IDLE",
			ms->name);
** todo 4.4.4.9
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_NORMAL_SERVICE);

		return 0;
	}

	/* location update allowed */
** is it enough to use the CAMPED_NORMALLY state to check if location update is allowed
	if (cs->state == GSM_C3_CAMPED_NORMALLY)
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_LOC_UPD_NEEDED);
	else
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_LIMITED_SERVICE);

	return 0;
}

/*
 * process handlers
 */

/* sending MM STATUS message */
static int gsm48_mm_tx_mm_status(struct osmocom_ms *ms, u_int8_t reject)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	u_int8_t *reject_cause;

	nmsg = gsm48_mm_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));
	reject_cause = msgb_put(nmsg, 1);

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_STATUS;
	*reject_cause = reject;

	return gsm48_mm_sendmsg(ms, nmsg, RR_DATA_REQ);
}

/* 4.3.1.2 sending TMSI REALLOCATION COMPLETE message */
static int gsm48_mm_tx_tmsi_reall_cpl(struct osmocom_ms *ms)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	nmsg = gsm48_mm_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_TMSI_REALL_COMPL;

	return gsm48_mm_sendmsg(ms, nmsg, RR_DATA_REQ);
}

/* 4.3.1 TMSI REALLOCATION COMMAND is received */
static int gsm48_mm_rx_tmsi_realloc_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm48_loc_area_id *lai = gh->data;
	u_int8_t mi_type;
	char *str_cur = string;
	u_int32_t tmsi;

	if (payload_len < sizeof(struct gsm48_loc_area_id) + 2) {
		short:
		DEBUGP(DMM, "Short read of TMSI reallocation command accept message error.\n");
		return -EINVAL;
	}
	/* LAI */
	decode_lai(lai, &subscr->tmsi_mcc, &subscr->tmsi_mnc, &subscr->tmsi_lac);
	/* MI */
	mi = gh->data + sizeof(struct gsm48_loc_area_id);
	mi_type = mi[1] & GSM_MI_TYPE_MASK;
	switch (mi_type) {
	case GSM_MI_TYPE_TMSI:
		if (gh->data + sizeof(struct gsm48_loc_area_id) < 6
		 || mi[0] < 5)
			goto short;
		memcpy(&tmsi, mi+2, 4);
		subscr->tmsi = ntohl(tmsi);
		subscr->tmsi_valid = 1;
		DEBUGP(DMM, "TMSI 0x%08x assigned.\n", subscr->tmsi);
		gsm48_mm_tx_tmsi_reall_cpl(ms);
		break;
	case GSM_MI_TYPE_IMSI:
		subscr->tmsi_valid = 0;
		DEBUGP(DMM, "TMSI removed.\n");
		gsm48_mm_tx_tmsi_reall_cpl(ms);
		break;
	default:
		DEBUGP(DMM, "TMSI reallocation with unknown MI type %d.\n", mi_type);
		gsm48_mm_tx_mm_status(ms, GSM48_REJECT_INCORRECT_MESSAGE);

		return 0; /* don't store in SIM */
	}

#ifdef TODO
	store / remove from sim
#endif

	return 0;
}

/* 4.3.2.2 AUTHENTICATION REQUEST is received */
static int gsm48_mm_rx_auth_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm48_auth_req *ar = gh->data;

	if (payload_len < sizeof(struct gsm48_auth_req)) {
		DEBUGP(DMM, "Short read of authentication request message error.\n");
		return -EINVAL;
	}

	/* SIM is not available */
	if (!subscr->sim_valid)
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);

	/* key_seq and random */
#ifdef TODO
	new key to sim:
	(..., ar->key_seq, ar->rand);
#endif

	/* wait for auth response event from SIM */
	return 0;
}

/* 4.3.2.2 sending AUTHENTICATION RESPONSE */
static int gsm48_mm_tx_auth_rsp(struct osmocom_ms *ms, struct gsm48_mmevent *ev)
{
	struct msgb *msg = gsm48_mm_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm_mmevent *mmevent = arg;
	u_int8_t *sres = msgb_put(msg, 4);

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_AUTH_RSP;

	/* SRES */
	memcpy(sres, ev->sres, 4);

	return gsm48_mm_sendmsg(ms, nmsg, RR_DATA_REQ);
}

/* 4.3.2.5 AUTHENTICATION REJECT is received */
static int gsm48_mm_rx_auth_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	/* SIM invalid */
	subscr->sim_valid = 0;

	/* TMSI and LAI invalid */
	subscr->tmsi_valid = 0;

	/* key is invalid */
	subscr->key_seq = 7;

	/* update status */
	new_sim_ustate(ms, GSM_MMUSTATE_U3_ROAMING_NA);

#ifdef TODO
	sim: delete tmsi
	sim: delete key seq number
	sim: set update status
#endif

	/* abort IMSI detach procedure */
	if (mm->state == GSM48_MM_ST_IMSI_DETACH_INIT) {
		/* send abort to RR */
todo		gsm48_sendrr(sm, abort, RR_ABORT_REQ);

		/* return to MM IDLE / No SIM */
		gsm48_mm_return_idle(ms);

	}

	return 0;
}

/* 4.3.3.1 IDENTITY REQUEST is received */
static int gsm48_mm_rx_id_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	u_int8_t mi_type;

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of identity request message error.\n");
		return -EINVAL;
	}
	/* id type */
	mi_type = *gh->data;

	/* check if request can be fulfilled */
	if (!subscr->sim_valid)
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);
	if (mi_type == GSM_MI_TYPE_TMSI && !subscr->tmsi_valid)
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);

	gsm48_mm_tx_id_rsp(ms, mi_type);
}

/* send IDENTITY RESPONSE message */
static int gsm48_mm_tx_id_rsp(struct osmocom_ms *ms, u_int8_t mi_type)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	nmsg = gsm48_mm_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_ID_RSP;

	/* MI */
	gsm48_encode_mi(nmsg, subscr, mi_type);

	return gsm48_mm_sendmsg(ms, nmsg, RR_DATA_REQ);
}

/* 4.3.4.1 sending IMSI DETACH INDICATION message */
static int gsm48_mm_tx_imsi_detach(struct osmocom_ms *ms, int rr_prim)
{
	struct gsm48_sysinfo *s = &ms->sysinfo;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	struct gsm48_classmark1 *classmark1;

	nmsg = gsm48_mm_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_IMSI_DETACH_IND;

	/* classmark 1 */
	gsm48_encode_classmark1(nmsg, s->rev_lev, s->es_ind, s->a5_1,
		s->pwr_lev);
	/* MI */
	gsm48_encode_mi(nmsg, subscr, mi_type);

	return gsm48_mm_sendmsg(ms, nmsg, rr_prim);
}

/* detach has ended */
static int gsm48_mm_imsi_detach_end(struct osmocom_ms *ms, void *arg)
{
	struct msgb *nmsg;

	/* stop IMSI detach timer (if running) */
	gsm48_stop_mm_timer(mm, 0x3210);

	/* update SIM */
#ifdef TODO
	sim: store BA list
	sim: what else?:
#endif

	/* SIM invalid */
	subscr->sim_valid = 0;

	/* send SIM remove event to gsm322 */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_SIM_REMOVE);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	/* return to MM IDLE / No SIM */
	return gsm48_mm_return_idle(ms);
}

/* start an IMSI detach in MM IDLE */
static int gsm48_mm_imsi_detach_start(struct osmocom_ms *ms, void *arg)
{
	struct gsm48_sysinfo *s = &ms->sysinfo;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	/* we may silently finish IMSI detach */
	if (!s->att_allowed)
		return gsm48_mm_imsi_detach_silent(ms, arg);

	new_mm_state(ms, GSM48_MM_ST_WAIT_RR_CONN_IMSI_D, 0);

	/* establish RR and send IMSI detach */
	return gsm48_mm_tx_imsi_detach(ms, RR_EST_REQ);
}

/* IMSI detach has been sent, wait for RR release */
static int gsm48_mm_imsi_detach_sent(struct osmocom_ms *ms, void *arg)
{
	/* start T3220 (4.3.4.1) */
	gsm48_start_mm_timer(mm, 0x3220, GSM48_T3220_MS);

	new_mm_state(ms, GSM48_MM_ST_IMSI_DETACH_INIT, 0);

	return 0;
}
	
/* release MM connection and proceed with IMSI detach */
static int gsm48_mm_imsi_detach_release(struct osmocom_ms *ms, void *arg)
{
	release all connections

	/* wait for release of RR */
	if (!s->att_allowed) {
		return new_mm_state(ms, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

	/* send IMSI detach */
	gsm48_mm_tx_imsi_detach(ms, RR_DATA_REQ);

	/* go to sent state */
	return gsm48_mm_imsi_detach_sent(ms, arg);
}

/* ignore ongoing IMSI detach */
static int gsm48_mm_imsi_detach_ignore(struct osmocom_ms *ms, void *arg)
{
}

/* delay until state change (and then retry) */
static int gsm48_mm_imsi_detach_delay(struct osmocom_ms *ms, void *arg)
{

	/* remember to detach later */
	mm->delay_detach = 1;

	return 0;
}




the process above is complete
------------------------------------------------------------------------------
incomplete


/* initialize Mobility Management process */
int gsm48_init(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;

	memset(mm, 0, sizeof(*mm));

	/* 4.2.1.1 */
	mm->state = GSM48_MM_ST_MM_ILDE;
	mm->sstate = GSM48_MM_SST_PLMN_SEARCH;

	/* init lists */
	INIT_LLIST_HEAD(&mm->up_queue);

	return 0;
}

to subscriber.c
static void new_sim_ustate(struct gsm_subscriber *subscr, int state)
{
	DEBUGP(DMM, "(ms %s) new state %s -> %s\n", subscr->ms,
		subscr_ustate_names[subscr->ustate], subscr_ustate_names[state]);

	subscr->ustate = state;
}


/* cm reestablish request message from upper layer */
static int gsm48_mm_tx_cm_serv_req(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_mm_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm48_service_request *serv_req = msgb_put(msg, 1 + 1 + sizeof(struct gsm48_classmark2));
	u_int8_t *classmark2 = ((u_int8_t *)serv_req) + 1;

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_CM_SERV_REQ;

	/* type and key */
	serv_req->cm_service_type =
	serv_req->cypher_key_seq =
	/* classmark 2 */
	classmark2[0] = sizeof(struct gsm48_classmark2);
	memcpy(classmark2+1, , sizeof(struct gsm48_classmark2));
	/* MI */
	gsm48_encode_mi(nmsg, subscr, mi_type);
	/* prio is optional for eMLPP */

	return gsm48_mm_sendmsg(ms, nmsg, RR_EST_REQ);
}

/* cm service abort message from upper layer */
static int gsm48_mm_tx_cm_service_abort(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_mm_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_CM_SERV_ABORT;

	return gsm48_mm_sendmsg(ms, nmsg, RR_DATA_REQ);
}

/* cm reestablish request message from upper layer */
static int gsm48_mm_tx_cm_reest_req(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_mm_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	u_int8_t *key_seq = msgb_put(msg, 1);
	u_int8_t *classmark2 = msgb_put(msg, 1 + sizeof(struct gsm48_classmark2));

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_CM_REEST_REQ;

	/* key */
	*key_seq =
	/* classmark2 */
	classmark2[0] = sizeof(struct gsm48_classmark2);
	memcpy(classmark2+1, , sizeof(struct gsm48_classmark2));
	/* MI */
	gsm48_encode_mi(nmsg, subscr, mi_type);
	/* LAI */
	if (mi_type == GSM_MI_TYPE_TMSI) {
		buf[0] = GSM48_IE_LOC_AREA;
		gsm0408_generate_lai((struct gsm48_loc_area_id *)(buf + 1),
			country_code, network_code, location_area_code);
		/* LAI as TV */
		ie = msgb_put(msg, 1 + sizeof(struct gsm48_loc_area_id));
		memcpy(ie, buf, 1 + sizeof(struct gsm48_loc_area_id));
	}

	return gsm48_mm_sendmsg(ms, nmsg, RR__REQ);
}

/* initiate a location update */
static int gsm48_mm_loc_update_no_rr(struct osmocom_ms *ms, void *arg)
{
	struct gsm_rr est;

	/* stop all timers 4.4.4.1 */
	gsm48_stop_mm_timer(mm, 0x3210);
	gsm48_stop_mm_timer(mm, 0x3211);
	gsm48_stop_mm_timer(mm, 0x3212);
	gsm48_stop_mm_timer(mm, 0x3213);

	memset(est, 0, sizeof(struct gsm_rr));
todo: set cause
	gsm48_sendrr(sm, est, RR_EST_REQ);

	new_mm_state(ms, GSM48_MM_ST_WAIT_RR_CONN_LUPD, 0);
}

/* initiate an mm connection 4.5.1.1 */
static int gsm48_mm_init_mm_no_rr(struct osmocom_ms *ms, void *arg)
{
	int emergency = 0, cause;
	struct gsm_mm *mmmsg;
	struct gsm_trans *trans = mmmsg->trans;
	struct gsm_rr est;

todo

	if (msg_type == MMCC_EST_REQ && mmmsg->emergency)
		emergency = 1;

	if (!emergency && mm->mmustate != MMUSTATE_U1_UPDATED) {
		todo set cause
		reject:
		switch(msg_type) {
		case MMCC_EST_REQ:
			mmmsg->cause = cause;
			return mm_recvmsg(ms, trans, MMCC_REL_IND, mmmsg);
		case MMSS_EST_REQ:
			mmmsg->cause = cause;
			return mm_recvmsg(ms, trans, MMSS_REL_IND, mmmsg);
		case MMSMS_EST_REQ:
			mmmsg->cause = cause;
			return mm_recvmsg(ms, trans, MMSMS_REL_IND, mmmsg);
		default:
			return 0;
		}
	}

		
	switch (mm->substate) {
	case GSM48_MM_SST_NORMAL_SERVICE:
	case GSM48_MM_SST_PLMN_SEARCH_NORMAL:
		break; /* allow when normal */
	case GSM48_MM_SST_ATTEMPT_UPDATE:
		/* store mm request if attempting to update */
		if (!emergency) {
			store mm connection request (status waiting)
			return trigger location update
		}
		break;
	}
	default:
		/* reject if not emergency */
		if (!emergency) {
			todo set cause
			goto reject;
		}
		break;
	}

	memset(est, 0, sizeof(struct gsm_rr));
	est->cause = todo establishment cause;
	todo: add msgb with cm_service request. todo this, change the cm_serv_req function into a general msgb-generation message for this and other functions (like service request during mm connection)
todo: set cause
	gsm48_sendrr(sm, est, RR_EST_REQ);
	new_mm_state(ms, GSM48_MM_ST_WAIT_RR_CONN_MM_CON, 0);
	return 0;
}


static int gsm48_mm_init_mm_first(struct osmocom_ms *ms, void *arg)
{
	int emergency = 0, cause;
	struct gsm_mm *mmmsg;
	struct gsm_trans *trans = mmmsg->trans;
	struct gsm_rr est;

	send cm service request 

	gsm48_stop_mm_timer(mm, 0x3241);
	gsm48_start_mm_timer(mm, 0x3230, GSM48_T3230_MS);
	new_mm_state(ms, GSM48_MM_ST_WAIT_OUT_MM_CONN, 0);
}

static int gsm48_mm_init_mm_more(struct osmocom_ms *ms, void *arg)
{
	int emergency = 0, cause;
	struct gsm_mm *mmmsg;
	struct gsm_trans *trans = mmmsg->trans;
	struct gsm_rr est;

	send cm service request 

	gsm48_stop_mm_timer(mm, 0x3241);
	gsm48_start_mm_timer(mm, 0x3230, GSM48_T3230_MS);
	new_mm_state(ms, GSM48_MM_ST_WAIT_ADD_OUT_MM_CONN, 0);
}

/* initiate an mm connection other cases */
static int gsm48_mm_init_mm_other(struct osmocom_ms *ms, void *arg)
{
	int emergency = 0;
	struct gsm_mm *mmmsg;
	struct gsm_trans *trans = mmmsg->trans;

	switch(msg_type) {
	case MMCC_EST_REQ:
		mmmsg->cause = cause;
		return mm_recvmsg(ms, trans, MMCC_REL_IND, mmmsg);
	case MMSS_EST_REQ:
		mmmsg->cause = cause;
		return mm_recvmsg(ms, trans, MMSS_REL_IND, mmmsg);
	case MMSMS_EST_REQ:
		mmmsg->cause = cause;
		return mm_recvmsg(ms, trans, MMSMS_REL_IND, mmmsg);
	default:
		return 0;
	}
}

/* respond to paging */
static int gsm48_mm_paging(struct osmocom_ms *ms, void *arg)
{
	todo
}

/* abort RR connection */
static int gsm48_mm_paging(struct osmocom_ms *ms, void *arg)
{
	struct gsm_rr abort;

	memset(abort, 0, sizeof(struct gsm_rr));
	gsm48_sendrr(sm, abort, RR_ABORT_REQ);
}

/* abort RR connection */
static int gsm48_mm_classm_chg(struct osmocom_ms *ms, void *arg)
{
	if (rr->state == in the dedicated without transitions)
	gsm_rr_tx_cm_change(sm, abort, RR_ABORT_REQ);
}

/* state trasitions for mobile managemnt messages (upper layer / events) */
static struct eventstate {
	u_int32_t	states;
	u_int32_t	substates;
	int		type;
	int		(*rout) (struct gsm_trans *trans, void *arg);
} eventstatelist[] = {
	/* 4.2.2.1 Normal service */
** todo: check if there is a senders of every event
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_NEW_LAI, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_start},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.2 Attempt to update */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_NEW_LAI, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.3 Limited service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MM_EVENT_NEW_LAI, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MM_EVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.4 No IMSI */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NO_IMSI),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.2.2.5 PLMN search, normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_start},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.4 PLMN search */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.5.1.1 MM Connection requests */
	{SBIT(GSM48_MM_ST_RR_CONN_RELEASE_NA), ALL_STATES,
	 MMCC_EST_REQ, gsm48_mm_init_mm_first},
	{SBIT(GSM48_MM_ST_RR_CONN_RELEASE_NA), ALL_STATES,
	 MMSS_EST_REQ, gsm48_mm_init_mm_first},
	{SBIT(GSM48_MM_ST_RR_CONN_RELEASE_NA), ALL_STATES,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_first},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 MMCC_EST_REQ, gsm48_mm_init_mm_more},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 MMSS_EST_REQ, gsm48_mm_init_mm_more},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_more},
	{ALL_STATES, ALL_STATES,
	 MMCC_EST_REQ, gsm48_mm_init_mm_other},
	{ALL_STATES, ALL_STATES,
	 MMSS_EST_REQ, gsm48_mm_init_mm_other},
	{ALL_STATES, ALL_STATES,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_other},
	/* IMSI detach in other cases */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NO_IMSI), /* no SIM */
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_end},
	{SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES, /* silently detach */
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_end},
	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_PROCESS_CM_SERV_P) |
	 SBIT(GSM48_MM_ST_WAIT_REEST) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_MM_CONN_ACTIVE_VGCS) |
	 SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES, /* we can release */
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_release},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D) |
	 SBIT(GSM48_MM_ST_IMSI_DETACH_INIT) |
	 SBIT(GSM48_MM_ST_IMSI_DETACH_PEND), ALL_STATES, /* ignore */
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_ignore},
	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_delay},
	{GSM48_MM_ST_IMSI_DETACH_INIT, ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3220, gsm48_mm_imsi_detach_end},




	{ALL_STATES, ALL_STATES, /* 4.3.2.2 */
	 GSM48_MM_EVENT_AUTH_RESPONSE, gsm48_mm_tx_auth_rsp},
	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_CLASSMARK_CHG, gsm48_mm_classm_chg},
todo all other states (sim removed)

	{SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},

	/* 4.4.4.8 */
	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD) | SBIT(GSM48_MM_ST_LOC_UPD_REJ), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3240, gsm48_mm_abort_rr},

	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
	{SBIT(GSM48_MM_ST_), ALL_STATES,
	 GSM48_MM_EVENT_, gsm48_mm_tx},
};

#define EVENTSLLEN \
	(sizeof(eventstatelist) / sizeof(struct eventstate))

/* MM event handler */
int mm_event(struct osmocom_ms *ms, int msg_type, struct gsm48_mmevent *mmevent)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int msg_type = mmevent->msg_type;

	DEBUGP(DMM, "(ms %s) Received '%s' event in state %s", ms->name,
		get_mmevent_name(msg_type), mm_state_names[mm->state]);
	if (mm->state == GSM48_MM_ST_MM_ILDE)
		DEBUGP(DMM, " substate %s", mm_substate_names[mm->substate]);
	DEBUGP(DMM, "\n");

	/* Find function for current state and message */
	for (i = 0; i < EVENTSLLEN; i++)
		if ((msg_type == eventstatelist[i].type)
		 && ((1 << mm->state) & eventstatelist[i].states)
		 && ((1 << mm->substate) & eventstatelist[i].substates))
			break;
	if (i == EVENTSLLEN) {
		DEBUGP(DMM, "Message unhandled at this state.\n");
		return 0;
	}

	rc = eventstatelist[i].rout(ms, msg_type, arg);

	return rc;
}

/* decode network name */
static int decode_network_name(char *name, int name_len,
	const u_int8_t *lv)
{
	u_int8_t in_len = lv[0];
	int length, padding;

	name[0] = '\0';
	if (in_len < 1)
		return -EINVAL;

	/* must be CB encoded */
	if ((lv[1] & 0x70) != 0x00)
		return -ENOTSUP;

	padding = lv[1] & 0x03;
	length = ((in_len - 1) * 8 - padding) / 7;
	if (length <= 0)
		return 0;
	if (length >= name_len)
		length = name_len - 1;
	gsm_7bit_decode(name, lv + 2, length);
	name[length] = '\0';

	return length;
}

static char bcd2char(u_int8_t bcd)
{
	if (bcd < 0xa)
		return '0' + bcd;
	else
		return 'A' + (bcd - 0xa);
}

static int decode_lai(struct gsm48_loc_area_id *lai, u_int16_t *mcc, u_int16_t *mnc, u_int16_t *lac)
{
	*mcc = bcd2char(lai->digits[0] & 0x0f) * 100
		+ bcd2char(lai->digits[0] >> 4) * 10
		+ bcd2char(lai->digits[1] & 0x0f);
	*mnc = bcd2char(lai->digits[1] >> 4) * 100
		+ bcd2char(lai->digits[2] & 0x0f) * 10;
		+ bcd2char(lai->digits[2] >> 4);
	*lac = ntohs(lai->lac);
}

/* mm info is received from lower layer */
static int gsm48_mm_rx_info(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;

	tlv_parse(&tp, &rsl_att_tlvdef, gh->data, payload_len, 0, 0);
	/* long name */
	if (TLVP_PRESENT(&tp, GSM48_IE_NAME_LONG)) {
		decode_network_name(name_long, sizeof(name_long),
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* short name */
	if (TLVP_PRESENT(&tp, GSM48_IE_NAME_SHORT)) {
		decode_network_name(name_short, sizeof(name_short),
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
}

/* location updating reject is received from lower layer */
static int gsm48_mm_rx_abort(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of location updating reject message error.\n");
		return -EINVAL;
	}

	reject_cause = *gh->data;

	if (llist_empty(ms->trans)) {
		DEBUGP(DMM, "Abort (cause #%d) while no MM connection is established.\n", reject_cause);
		return 0;
	} else {
		DEBUGP(DMM, "Abort (cause #%d) while MM connection is established.\n", reject_cause);
		while(translist not empty)
			release trans
			todo: release trans must send a release to it's application entitity
			todo: release must cause release of state 6 and transfer to state 9
	}

	if (reject_cause == 6) { 
		new_sim_ustate(ms, GSM_MMUSTATE_U3_ROAMING_NA);
#ifdef TODO
		sim: delete tmsi
		sim: delete key seq number
		sim: apply update state
#endif
	}
}

/* location updating accept is received from lower layer */
static int gsm48_mm_rx_loc_upd_acc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_loc_area_id *lai = gh->data;
	u_int8_t *tlv_start = gh->data;
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;

	if (payload_len < sizeof(struct gsm48_loc_area_id)) {a
		short:
		DEBUGP(DMM, "Short read of location updating accept message error.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &rsl_att_tlvdef, gh->data, payload_len, GSM48_IE_LOC_AREA, 0);
	/* LAI */
	decode_lai(lai, &ms->current_mcc, &ms->current->mnc, &ms->current_lac);
	todo: store in sim
	remove from forbidden list

	nnnew_mmu_state(ms, GSM_MMUSTATE_U1_UPDATED);

	gsm48_stop_mm_timer(mm, 0x3210);

	tlv_parse(&tp, &rsl_att_tlvdef, gh->data + sizeof(struct gsm48_loc_area_id),
		payload_len - sizeof(struct gsm48_loc_area_id), 0, 0);
	/* MI */
	if (TLVP_PRESENT(&tp, GSM48_IE_MOBILE_ID)) {
		mi = TLVP_VAL(&tp, GSM48_IE_FACILITY)-1;
		if (mi[0] < 1)
			goto short;
		mi_type = mi[1] & GSM_MI_TYPE_MASK;
		switch (mi_type) {
		case GSM_MI_TYPE_TMSI:
			if (gh->data + sizeof(struct gsm48_loc_area_id) < 6
			 || mi[0] < 5)
				goto short;
			memcpy(&tmsi, mi+2, 4);
			ms->tmsi = ntohl(tmsi);
			ms->tmsi_valid = 1;
			todo: store in sim
			gsm48_mm_tx_tmsi_reall_cpl(ms);
			break;
		case GSM_MI_TYPE_IMSI:
			ms->tmsi_valid = 0;
			todo: remove tmsi from sim
			gsm48_mm_tx_tmsi_reall_cpl(ms);
			break;
		default:
			DEBUGP(DMM, "TMSI reallocation with unknown MI type %d.\n", mi_type);
		}
	}
	/* follow on proceed */
	if (TLVP_PRESENT(&tp, GSM48_IE_MOBILE_ID)) {
		DEBUGP(DMM, "follow-on proceed not supported.\n");
	}

	gsm48_start_mm_timer(mm, 0x3240, GSM48_T3240_MS);
	new_mm_state(ms, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

	return 0;
}

/* location update reject is received from lower layer */
static int gsm48_mm_rx_loc_upd_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of location update reject message error.\n");
		return -EINVAL;
	}
	/* reject cause */
	reject_cause = *gh->data;

	gsm48_stop_mm_timer(mm, 0x3210);

	/* store until RR is released */
	mm->lupd_rej_cause = reject_cause;

	gsm48_start_mm_timer(mm, 0x3240, GSM48_T3240_MS);
	new_mm_state(ms, GSM48_MM_ST_LOC_UPD_REJ, 0);
}

/* abort is received from lower layer */
static int gsm48_mm_rx_abort(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of abort message error.\n");
		return -EINVAL;
	}
	/* reject cause */
	reject_cause = *gh->data;
}

/* cm service reject is received from lower layer */
static int gsm48_mm_rx_cm_service_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of cm service reject message error.\n");
		return -EINVAL;
	}
	/* reject cause */
	reject_cause = *gh->data;
}

/* cm service acknowledge is received from lower layer */
static int gsm48_mm_rx_cm_service_ack(struct osmocom_ms *ms, struct msgb *msg)
{
	loop through all transactions. there must only be one in the "MMCONSTATE_CM_REQ", others are "MMCONSTATE_WAITING" or "MMCONSTATE_ACTIVE".
	then send mm est confirm
	change state to active

	todo: an indication by the RR that the cyphering mode setting procedure is complete, shall be treated as cm_serv_ack!!

	gsm48_stop_mm_timer(mm, 0x3230);
	new_mm_state(ms, GSM48_MM_ST_MM_CONN_ACTIVE, 0);
}

/* state trasitions for mobile managemnt messages (lower layer) */
static struct mmdatastate {
	u_int32_t	states;
	int		type;
	int		(*rout) (struct gsm_trans *trans, struct msgb *msg);
} mmdatastatelist[] = {
	{ALL_STATES, /* 4.3.1.2 */
	 GSM48_MT_MM_TMSI_REALL_CMD, gsm48_mm_rx_tmsi_realloc_cmd},
	{ALL_STATES, /* 4.3.2.2 */
	 GSM48_MT_MM_AUTH_REQ, gsm48_mm_rx_auth_req},
	{ALL_STATES, /* 4.3.2.5 */
	 GSM48_MT_MM_AUTH_REJ, gsm48_mm_rx_auth_rej},
	{ALL_STATES, /* 4.3.3.2 */
	 GSM48_MT_MM_ID_REQ, gsm48_mm_rx_id_req},
	{ALL_STATES, /* 4.3.5.2 */
	 GSM48_MT_MM_ABORT, gsm48_mm_rx_abort},
	{ALL_STATES, /* 4.3.6.2 */
	 GSM48_MT_MM_INFO, gsm48_mm_rx_info},
	{GSM48_MM_ST_LOC_UPD_INIT, /* 4.4.4.5 */
	 GSM48_MT_MM_LOC_UPD_ACCEPT, gsm48_mm_rx_loc_upd_acc},
	{GSM48_MM_ST_LOC_UPD_INIT, /* 4.4.4.7 */
	 GSM48_MT_MM_LOC_UPD_REJECT, gsm48_mm_rx_loc_upd_rej},
	{ALL_STATES, /* 4.5.1.1 */
	 GSM48_MT_MM_, gsm48_mm_rx_cm_service_ack},
	{ALL_STATES,
	 GSM48_MT_MM_, gsm48_mm_rx_},
	{ALL_STATES,
	 GSM48_MT_MM_, gsm48_mm_rx_},
	{ALL_STATES,
	 GSM48_MT_MM_, gsm48_mm_rx_},

	{SBIT(GSM_MSTATE_),
	 GSM48_MT_MM_, gsm48_mm_rx_},
};

#define DMMATASLLEN \
	(sizeof(mmdatastatelist) / sizeof(struct mmdatastate))

static int gsm48_mm_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	int msg_type = gh->msg_type & 0xbf;

	/* pull the MM header */
	msgb_pull(msg, sizeof(struct gsm48_mm_hdr));

	/* forward message */
	switch (pdisc) {
	case GSM48_PDISC_MM:
		break; /* follow the selection proceedure below */

	case GSM48_PDISC_CC:
		/* push new header */
		msgb_push(msg, sizeof(struct gsm48_cc_hdr));
		cch = (struct gsm48_cc_hdr *)msg->data;
		cch->msg_type = MM_DATA_IND;

		return gsm48_cc_upmsg(ms, msg);

#if 0
	case GSM48_PDISC_SMS:
		/* push new header */
		msgb_push(msg, sizeof(struct gsm48_sms_hdr));
		smsh = (struct gsm48_smscc_hdr *)msg->data;
		smsh->msg_type = MM_DATA_IND;

		return gsm48_sms_upmsg(ms, msg);
#endif

	default:
		DEBUGP(DRR, "Protocol type 0x%02x unsupported.\n", pdisc);
		free_msgb(msg);
		return;
	}

	DEBUGP(DMM, "(ms %s) Received '%s' in MM state %s\n", ms->name,
		gsm48_mm_msg_name(msg_type), mm_state_names[mm->state]);

	/* find function for current state and message */
	for (i = 0; i < MMDATASLLEN; i++)
		if ((msg_type == mmdatastatelist[i].type)
		 && ((1 << mm->state) & mmdatastatelist[i].states))
			break;
	if (i == MMDATASLLEN) {
		DEBUGP(DMM, "Message unhandled at this state.\n");
		free_msgb(msg);
		return 0;
	}

	rc = mmdatastatelist[i].rout(ms, msg);

	free_msgb(msg);

	return rc;
}

/* timeout events of all timers */
static void gsm48_mm_t3210(void *arg)
{
	struct gsm48_mmlayer *mm = arg;
	mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3210);
}
static void gsm48_mm_t3211(void *arg)
{
	struct gsm48_mmlayer *mm = arg;
	mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3211);
}
static void gsm48_mm_t3212(void *arg)
{
	struct gsm48_mmlayer *mm = arg;
	mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3212);
}
static void gsm48_mm_t3213(void *arg)
{
	struct gsm48_mmlayer *mm = arg;
	mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3213);
}

/* RR is esablised during location update */
static int gsm48_mm_est_loc_upd(struct osmocom_ms *ms, struct gsm_rr *est)
{
	/* 4.4.4.1 */
	struct msgb *msg = gsm48_mm_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm48_loc_upd *loc_upd = msgb_put(msg, 7);
	u_int8_t *classmark1 = ((u_int8_t *)loc_upd) + 6;

	gsm48_start_mm_timer(mm, 0x3210, GSM48_T3210_MS);
	new_mm_state(ms, GSM48_MM_ST_LOC_UPD_INIT, 0);

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_LOC_UPD_REQ;

	/* type and key */
	loc_upd->type =
	loc_upd->key_seq =
	/* classmark 1 */
	memcpy(classmark1, , sizeof(struct gsm48_classmark1));
	/* LAI */
	gsm0408_generate_lai(loc_upd->lai,
		country_code, network_code, location_area_code);
	/* MI */
	gsm48_encode_mi(nmsg, subscr, mi_type);

	return gsm48_mm_sendmsg(ms, nmsg, RR_DATA_REQ);
}

/* RR is released after location update reject */
static int gsm48_mm_rel_loc_upd_rej(struct osmocom_ms *ms, struct gsm_rr *rel)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;
	
	/* new status */
	switch (mm->lupd_rej_cause) {
	case 11:
	case 12:
	case 13:
		attempt_counter = 0;
		// fall through
	case 2:
	case 3:
	case 6:
		new_sim_ustate(ms, GSM_MMUSTATE_U3_ROAMING_NA);
#ifdef TODO
		sim: delete tmsi
		sim: delete key seq number
		sim: apply update state
#endif
	}

	/* forbidden list */
	switch (mm->lupd_rej_cause) {
	case 2:
	case 3:
	case 6:
		/* sim becomes invalid */
		subscr->sim_valid = 0;
		break;
	case 11:
		add_forbidden_list(ms, FORBIDDEN_PLMN, lai);
		break;
	case 12:
		add_forbidden_list(ms, FORBIDDEN_LOC_AREA_RPOS, lai);
		break;
	case 13:
		add_forbidden_list(ms, FORBIDDEN_LOC_AREA_ROAM, lai);
		break;
	default:
		todo 4.4.4.9
	}

	/* send event */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_LU_REJECT);
	if (!nmsg)
		return -ENOMEM;
	ngm = (struct gsm322_msg *)nmsg->data;
	ngm->reject = mm->lupd_rej_cause;
	gsm322_sendmsg(ms, nmsg);

	/* return to IDLE, case 13 is also handled there */
	return gsm48_mm_return_idle(ms);
}

/* RR is released in other states */
static int gsm48_mm_rel_other(struct osmocom_ms *ms, struct gsm_rr *rel)
{
	/* send event */
** tothink: shall we do this here or at radio ressource
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_RET_IDLE);
	if (!nmsg)
		return -ENOMEM;
	gsm322_sendmsg(ms, nmsg);

	return gsm48_mm_return_idle(ms);
}

/* RR is esablised during mm connection */
static int gsm48_mm_est_mm_con(struct osmocom_ms *ms, struct gsm_rr *est)
{
	loop through all transactions. there must only be one in the "MMCONSTATE_CM_REQ", others are "MMCONSTATE_WAITING" or "MMCONSTATE_ACTIVE".

	gsm48_start_mm_timer(mm, 0x3230, GSM48_T3230_MS);
	new_mm_state(ms, GSM48_MM_ST_WAIT_OUT_MM_CONN, 0);
}

/* state trasitions for radio ressource messages (lower layer) */
static struct rrdatastate {
	u_int32_t	states;
	int		type;
	int		(*rout) (struct gsm_trans *trans, struct gsm_rr *msg);
} rrdatastatelist[] = {
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_LUPD), /* 4.4.4.1 */
	 RR_ESTAB_CNF, gsm48_mm_est_loc_upd},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 */
	 RR_ESTAB_CNF, gsm48_mm_imsi_detach_sent},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 (unsuc.) */
	 RR_REL_IND, gsm48_mm_imsi_detach_end},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 (lost) */
	 RR_ABORT_IND, gsm48_mm_imsi_detach_end},
	{SBIT(GSM48_MM_ST_LOC_UPD_REJ), /* 4.4.4.7 */
	 RR_REL_IND, gsm48_mm_rel_loc_upd_rej},
	{ALL_STATES,
	 RR_REL_IND, gsm48_mm_rel_other},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), /* 4.5.1.1 */
	 RR_ESTAB_CNF, gsm48_mm_est_mm_con},
	{ALL_STATES,
	 RR_DATA_IND, gsm48_mm_data_ind},
};

#define RRDATASLLEN \
	(sizeof(rrdatastatelist) / sizeof(struct rrdatastate))

static int gsm48_rcv_rr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int msg_type = msg->msg_type;

	DEBUGP(DMM, "(ms %s) Received '%s' from RR in state %s\n", ms->name,
		gsm48_rr_msg_name(msg_type), mm_state_names[mm->state]);

	/* find function for current state and message */
	for (i = 0; i < RRDATASLLEN; i++)
		if ((msg_type == rrdatastatelist[i].type)
		 && ((1 << mm->state) & rrdatastatelist[i].states))
			break;
	if (i == RRDATASLLEN) {
		DEBUGP(DMM, "Message unhandled at this state.\n");
		free_msgb(msg);
		return 0;
	}

	rc = rrdatastatelist[i].rout(ms, rrmsg);
	
	if (msg_type != RR_DATA_IND)
		free_msgb(msg);

	return rc;
}

/* dequeue messages from RR */
int gsm48_mm_queue(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&mm->up_queue))) {
		/* message is also freed here */
		gsm48_rcv_rr(ms, msg);
		work = 1; /* work done */
	}
	
	return work;
}


wichtig: nur eine MM connection zur zeit, da ja auch nur ein cm-service-request laufen kann. die anderen werden dann als "waiting" deklariert.
