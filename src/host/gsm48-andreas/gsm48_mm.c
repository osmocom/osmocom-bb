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

/* state transition */
static void new_mm_state(struct gsm_mmlayer *mm, int state, int substate)
{
	DEBUGP(DMM, "(ms %s) new state %s", mm->ms, mm_state_names[mm->state]);
	if (mm->state == GSM_MMSTATE_MM_ILDE)
		DEBUGP(DMM, " substate %s", mm_substate_names[mm->substate]);
	DEBUGP(DMM, "-> %s", mm_state_names[state]);
	if (state == GSM_MMSTATE_MM_ILDE)
		DEBUGP(DMM, " substate %s", mm_substate_names[substate]);
	DEBUGP(DMM, "\n");

	mm->state = state;
	mm->substate = substate;
}

static void new_mmu_state(struct gsm_mmlayer *mm, int state)
{
	DEBUGP(DMM, "(ms %s) new state %s -> %s\n", mm->ms,
		mm_ustate_names[mm->ustate], mm_ustate_names[state]);

	mm->ustate = state;
}

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

/* 4.2.3 when returning to MM IDLE state, this function is called */
static int gsm48_mm_return_idle(struct osmocom_ms *ms)
{
	/* no sim present */
	if (no sim) {
		/* imsi detach due to power off */
		if (ms->mm->power_off) {
			struct gsm_mm_msg *off;
	
			memset(&off, 0, sizeof(off));
			return mm_recvmsg(ms, NULL, MMEVENT_POWER_OFF, off);
		}
		new_mm_state(mm, MM_IDLE, GSM_MMIDLESS_NO_IMSI);
		return 0;
	}

	/* no cell found */
	if (no cell found) {
		new_mm_state(mm, MM_IDLE, GSM_MMIDLESS_PLMN_SEARCH);
		return 0;
	}

	/* return from location update with roaming not allowed */
	if (mm->state == GSM_MMSTATE_LOC_UPD_REJ) {
		todo 4.4.4.9 (read 4.4.4.7)
		if (mm->lupd_rej_cause == 13) { /* roaming not allowed */
			new_mm_state(mm, MM_IDLE, GSM_MMIDLESS_PLMN_SEARCH);
			return 0;
		}
	}

	/* selected cell equals the registered LAI */
	if (selected cell->lai == ms->registered_lai) {
		todo 4.4.4.9
		new_mm_state(mm, MM_IDLE, GSM_MMIDLESS_NORMAL_SERVICE);
		return 0;
	} else {
		/* location update not allowed */
		if (lupd not allowed) {
			new_mm_state(mm, MM_IDLE, GSM_MMIDLESS_LOC_UPD_NEEDED);
			return 0;
		} else {
			new_mm_state(mm, MM_IDLE, GSM_MMIDLESS_LIMITED_SERVICE);
			return 0;
		}
	}
}

/* TMSI reallocation complete message from upper layer */
static int gsm48_mm_tx_tmsi_reall_cpl(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_TMSI_REALL_COMPL;

	return gsm48_sendmsg(msg, NULL);
}

/* mm status message from upper layer */
static int gsm48_mm_tx_mm_status(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	u_int8_t *reject_cause = msgb_put(msg, 1);

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_STATUS;

	*reject_cause = *((int *)arg);

	return gsm48_sendmsg(msg, NULL);
}

/* IMSI detach indication message from upper layer */
static int gsm48_mm_tx_imsi_detach_ind(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm48_classmark1 *classmark1 = msgb_put(msg, sizeof(struct gsm48_classmark1));
	u_int8_t buf[11]; /* 1+9 should be enough, but it is not really clear */
	u_int8_t *ie;

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_IMSI_DETACH_IND;

	while(translist not empty)
		release trans
		todo: release trans must send a release to it's application entitity

	if (att flag set) {
		gsm48_start_mm_timer(mm, 0x3220, GSM48_T3220_MS);

		new_mm_state(mm, GSM_MMSTATE_IMSI_DETACH_INIT, 0);

		/* classmark 1 */
		memcpy(classmark1, , sizeof(struct gsm48_classmark1));
		/* MI */
		switch(mi_type) {
		case GSM_MI_TYPE_TMSI:
			gsm48_generate_mid_from_tmsi(buf, tmsi);
			break;
		case GSM_MI_TYPE_IMSI:
			gsm48_generate_mid_from_imsi(buf, imsi);
			break;
		case GSM_MI_TYPE_IMEI:
		case GSM_MI_TYPE_IMEISV:
			gsm48_generate_mid_from_imsi(buf, imei);
			break;
		case GSM_MI_TYPE_NONE:
		default:
			buf[0] = GSM48_IE_MOBILE_ID;
			buf[1] = 1;
			buf[2] = 0xf0 | GSM_MI_TYPE_NONE;
			break;
		}
		/* MI as LV */
		ie = msgb_put(msg, 1 + buf[1]);
		memcpy(ie, buf + 1, 1 + buf[1]);

		return gsm48_sendmsg(msg, NULL);
	} else {
		return gsm48_mm_return_idle(ms);
	}
		
}

todo: timeout t3220

static int gsm48_mm_imsi_detach_no_rr(struct osmocom_ms *ms, void *arg)
{
	request rr

	new_mm_state(mm, GSM_MMSTATE_WAIT_RR_CONN_IMSI_D, 0);

}

/* identity response message from upper layer */
static int gsm48_mm_tx_id_rsp(struct osmocom_ms *ms, u_int8_t mi_type)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	u_int8_t buf[11]; /* 1+9 should be enough, but it is not really clear */
	u_int8_t *ie;

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_ID_RSP;

	/* MI */
	switch(mi_type) {
	case GSM_MI_TYPE_TMSI:
		gsm48_generate_mid_from_tmsi(buf, tmsi);
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_generate_mid_from_imsi(buf, imsi);
		break;
	case GSM_MI_TYPE_IMEI:
	case GSM_MI_TYPE_IMEISV:
		gsm48_generate_mid_from_imsi(buf, imei);
		break;
	case GSM_MI_TYPE_NONE:
	default:
	        buf[0] = GSM48_IE_MOBILE_ID;
	        buf[1] = 1;
	        buf[2] = 0xf0 | GSM_MI_TYPE_NONE;
		break;
	}
	/* MI as LV */
	ie = msgb_put(msg, 1 + buf[1]);
	memcpy(ie, buf + 1, 1 + buf[1]);

	return gsm48_sendmsg(msg, NULL);
}

/* cm reestablish request message from upper layer */
static int gsm48_mm_tx_cm_serv_req(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm48_service_request *serv_req = msgb_put(msg, 1 + 1 + sizeof(struct gsm48_classmark2));
	u_int8_t *classmark2 = ((u_int8_t *)serv_req) + 1;
	u_int8_t buf[11];
	u_int8_t *ie;

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_CM_SERV_REQ;

	/* type and key */
	serv_req->cm_service_type =
	serv_req->cypher_key_seq =
	/* classmark 2 */
	classmark2[0] = sizeof(struct gsm48_classmark2);
	memcpy(classmark2+1, , sizeof(struct gsm48_classmark2));
	/* MI */
	switch(mi_type) {
	case GSM_MI_TYPE_TMSI:
		gsm48_generate_mid_from_tmsi(buf, tmsi);
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_generate_mid_from_imsi(buf, imsi);
		break;
	case GSM_MI_TYPE_IMEI:
	case GSM_MI_TYPE_IMEISV:
		gsm48_generate_mid_from_imsi(buf, imei);
		break;
	case GSM_MI_TYPE_NONE:
	default:
	        buf[0] = GSM48_IE_MOBILE_ID;
	        buf[1] = 1;
	        buf[2] = 0xf0 | GSM_MI_TYPE_NONE;
		break;
	}
	/* MI as LV */
	ie = msgb_put(msg, 1 + buf[1]);
	memcpy(ie, buf + 1, 1 + buf[1]);
	/* prio is optional for eMLPP */

	return gsm48_sendmsg(msg, NULL);
}

/* cm service abort message from upper layer */
static int gsm48_mm_tx_cm_service_abort(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_CM_SERV_ABORT;

	return gsm48_sendmsg(msg, NULL);
}

/* cm reestablish request message from upper layer */
static int gsm48_mm_tx_cm_reest_req(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	u_int8_t *key_seq = msgb_put(msg, 1);
	u_int8_t *classmark2 = msgb_put(msg, 1 + sizeof(struct gsm48_classmark2));
	u_int8_t buf[11];
	u_int8_t *ie;

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_CM_REEST_REQ;

	/* key */
	*key_seq =
	/* classmark2 */
	classmark2[0] = sizeof(struct gsm48_classmark2);
	memcpy(classmark2+1, , sizeof(struct gsm48_classmark2));
	/* MI */
	switch(mi_type) {
	case GSM_MI_TYPE_TMSI:
		gsm48_generate_mid_from_tmsi(buf, tmsi);
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_generate_mid_from_imsi(buf, imsi);
		break;
	case GSM_MI_TYPE_IMEI:
	case GSM_MI_TYPE_IMEISV:
		gsm48_generate_mid_from_imsi(buf, imei);
		break;
	case GSM_MI_TYPE_NONE:
	default:
	        buf[0] = GSM48_IE_MOBILE_ID;
	        buf[1] = 1;
	        buf[2] = 0xf0 | GSM_MI_TYPE_NONE;
		break;
	}
	/* MI as LV */
	ie = msgb_put(msg, 1 + buf[1]);
	memcpy(ie, buf + 1, 1 + buf[1]);
	/* LAI */
	if (mi_type == GSM_MI_TYPE_TMSI) {
		buf[0] = GSM48_IE_LOC_AREA;
		gsm0408_generate_lai((struct gsm48_loc_area_id *)(buf + 1),
			country_code, network_code, location_area_code);
		/* LAI as TV */
		ie = msgb_put(msg, 1 + sizeof(struct gsm48_loc_area_id));
		memcpy(ie, buf, 1 + sizeof(struct gsm48_loc_area_id));
	}

	return gsm48_sendmsg(msg, NULL);
}

/* authentication response message from upper layer */
static int gsm48_mm_tx_auth_rsp(struct osmocom_ms *ms, void *arg)
{
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm_mmevent *mmevent = arg;
	u_int8_t *sres = msgb_put(msg, 4);

	msg->lchan = lchan;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_AUTH_RSP;

	/* SRES */
	memcpy(sres, mmevent->sres, 4);

	return gsm48_sendmsg(msg, NULL);
}

/* initiate a location update */
static int gsm48_mm_loc_update_no_rr(struct osmocom_ms *ms, void *arg)
{
	struct gsm_rr est;

	/* stop all timers 4.4.4.1 */
	gsm48_stop_cc_timer(mm, 0x3210);
	gsm48_stop_cc_timer(mm, 0x3211);
	gsm48_stop_cc_timer(mm, 0x3212);
	gsm48_stop_cc_timer(mm, 0x3213);

	memset(est, 0, sizeof(struct gsm_rr));
todo: set cause
	gsm48_sendrr(sm, est, RR_EST_REQ);

	new_mm_state(ms, GSM_MMSTATE_WAIT_RR_CONN_LUPD, 0);
}

/* initiate an IMSI detach */
static int gsm48_mm_imsi_detach(struct osmocom_ms *ms, void *arg)
{
	todo
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
	case GSM_MMIDLESS_NORMAL_SERVICE:
	case GSM_MMIDLESS_PLMN_SEARCH_NORMAL:
		break; /* allow when normal */
	case GSM_MMIDLESS_ATTEMPT_UPDATE:
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
	new_mm_state(ms, GSM_MMSTATE_WAIT_RR_CONN_MM_CON, 0);
	return 0;
}


static int gsm48_mm_init_mm_first(struct osmocom_ms *ms, void *arg)
{
	int emergency = 0, cause;
	struct gsm_mm *mmmsg;
	struct gsm_trans *trans = mmmsg->trans;
	struct gsm_rr est;

	send cm service request 

	gsm48_stop_cc_timer(mm, 0x3241);
	gsm48_start_mm_timer(mm, 0x3230, GSM48_T3220_MS);
	new_mm_state(ms, GSM_MMSTATE_WAIT_OUT_MM_CONN, 0);
}

static int gsm48_mm_init_mm_more(struct osmocom_ms *ms, void *arg)
{
	int emergency = 0, cause;
	struct gsm_mm *mmmsg;
	struct gsm_trans *trans = mmmsg->trans;
	struct gsm_rr est;

	send cm service request 

	gsm48_stop_cc_timer(mm, 0x3241);
	gsm48_start_mm_timer(mm, 0x3230, GSM48_T3220_MS);
	new_mm_state(ms, GSM_MMSTATE_WAIT_ADD_OUT_MM_CONN, 0);
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
	/* 4.2.2.1 */
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMEVENT_NEW_LAI, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMEVENT_TIMEOUT_T3211, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMEVENT_TIMEOUT_T3213, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMEVENT_TIMEOUT_T3212, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMEVENT_IMSI_DETACH, gsm48_mm_imsi_detach},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NORMAL_SERVICE),
	 MMEVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.2 */
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_ATTEMPT_UPDATE),
	 MMEVENT_TIMEOUT_T3211, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_ATTEMPT_UPDATE),
	 MMEVENT_TIMEOUT_T3213, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_ATTEMPT_UPDATE),
	 MMEVENT_NEW_LAI, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_ATTEMPT_UPDATE),
	 MMEVENT_TIMEOUT_T3212, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_ATTEMPT_UPDATE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_ATTEMPT_UPDATE),
	 MMEVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.3 */
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_LIMITED_SERVICE),
	 MMEVENT_NEW_LAI, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_LIMITED_SERVICE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_LIMITED_SERVICE),
	 MMEVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.4 */
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_NO_IMSI),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.2.2.5 */
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMEVENT_TIMEOUT_T3211, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMEVENT_TIMEOUT_T3213, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMEVENT_TIMEOUT_T3212, gsm48_mm_loc_update_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMEVENT_IMSI_DETACH, gsm48_mm_imsi_detach},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH_NORMAL),
	 MMEVENT_PAGING, gsm48_mm_paging},
	/* 4.2.2.4 */
	{SBIT(GSM_MMSTATE_MM_IDLE), SBIT(GSM_MMILDESS_PLMN_SEARCH),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.5.1.1 */
	{SBIT(GSM_MMSTATE_RR_CONN_RELEASE_NA), 0,
	 MMCC_EST_REQ, gsm48_mm_init_mm_first},
	{SBIT(GSM_MMSTATE_RR_CONN_RELEASE_NA), 0,
	 MMSS_EST_REQ, gsm48_mm_init_mm_first},
	{SBIT(GSM_MMSTATE_RR_CONN_RELEASE_NA), 0,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_first},
	{SBIT(GSM_MMSTATE_MM_CONN_ACTIVE), 0,
	 MMCC_EST_REQ, gsm48_mm_init_mm_more},
	{SBIT(GSM_MMSTATE_MM_CONN_ACTIVE), 0,
	 MMSS_EST_REQ, gsm48_mm_init_mm_more},
	{SBIT(GSM_MMSTATE_MM_CONN_ACTIVE), 0,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_more},
	{ALL_STATES, 0,
	 MMCC_EST_REQ, gsm48_mm_init_mm_other},
	{ALL_STATES, 0,
	 MMSS_EST_REQ, gsm48_mm_init_mm_other},
	{ALL_STATES, 0,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_other},
	/* MS events */
	{ALL_STATES, ALL_STATES, /* 4.3.2.2 */
	 MMEVENT_AUTH_RESPONSE, gsm48_mm_tx_auth_rsp},
	{SBIT(GSM_MMSTATE_LOC_UPD_INIT) | /* all states where RR active */
	 SBIT(GSM_MMSTATE_WAIT_OUT_MM_CONN) |
	 SBIT(GSM_MMSTATE_MM_CONN_ACTIVE) |
	 SBIT(GSM_MMSTATE_IMSI_DETACH_INIT) |
	 SBIT(GSM_MMSTATE_PROCESS_CM_SERV_P) |
	 SBIT(GSM_MMSTATE_WAIT_NETWORK_CMD) |
	 SBIT(GSM_MMSTATE_LOC_UPD_REJ) |
	 SBIT(GSM_MMSTATE_WAIT_REEST) |
	 SBIT(GSM_MMSTATE_WAIT_ADD_OUT_MM_CONN) |
	 SBIT(GSM_MMSTATE_MM_CONN_ACTIVE_VGCS) |
	 SBIT(GSM_MMSTATE_RR_CONN_RELEASE_NA), ALL_STATES, /* 4.3.4.1 */
	 MMEVENT_IMSI_DETACH, gsm48_mm_tx_imsi_detach_ind},
	{ALL_STATES, ALL_STATES,
	 MMEVENT_IMSI_DETACH, gsm48_mm_imsi_detach_no_rr},
	{ALL_STATES, ALL_STATES,
	 MMEVENT_CLASSMARK_CHG, gsm48_mm_classm_chg},
todo all other states (sim removed)

	{SBIT(GSM_MMSTATE_MM_IDLE), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},

	/* 4.4.4.8 */
	{SBIT(GSM_MMSTATE_WAIT_NETWORK_CMD) | SBIT(GSM_MMSTATE_LOC_UPD_REJ), ALL_STATES,
	 MMEVENT_TIMEOUT_T3240, gsm48_mm_abort_rr},

	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
	{SBIT(GSM_MMSTATE_), ALL_STATES,
	 MMEVENT_, gsm48_mm_tx},
};

#define EVENTSLLEN \
	(sizeof(eventstatelist) / sizeof(struct eventstate))

/* MM event handler */
int mm_event(struct osmocom_ms *ms, int msg_type, void *arg)
{
	struct gsm_mmlayer *mm = &ms->mmlayer;

	DEBUGP(DMM, "(ms %s) Received '%s' event in state %s", ms->name,
		get_mmevent_name(msg_type), mm_state_names[mm->state]);
	if (mm->state == GSM_MMSTATE_MM_ILDE)
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

/* TMSI reallocation command accept is received from lower layer */
static int gsm48_mm_rx_tmsi_realloc_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
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
	decode_lai(lai, &ms->current_mcc, &ms->current->mnc, &ms->current_lac);
	todo: store in sim
	remove from forbidden list
	/* MI */
	mi = gh->data + sizeof(struct gsm48_loc_area_id);
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

	return 0;
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
		sim: delete tmsi
		sim: delete lai
		sim: delete key seq number
		nnnew_mmu_state(ms, GSM_MMUSTATE_U3_ROAMING_NA);
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

	gsm48_stop_cc_timer(mm, 0x3210);

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
	new_mm_state(ms, GSM_MMSTATE_WAIT_NETWORK_CMD, 0);

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

	gsm48_stop_cc_timer(mm, 0x3210);

	/* store until RR is released */
	mm->lupd_rej_cause = reject_cause;

	gsm48_start_mm_timer(mm, 0x3240, GSM48_T3240_MS);
	new_mm_state(ms, GSM_MMSTATE_LOC_UPD_REJ, 0);
}

/* identitiy request is received from lower layer */
static int gsm48_mm_rx_id_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	u_int8_t id_type;

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of identity request message error.\n");
		return -EINVAL;
	}
	/* id type */
	id_type = *gh->data;

	gsm48_mm_tx_id_rsp(ms, id_type);
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
	new_mm_state(ms, GSM_MMSTATE_MM_CONN_ACTIVE, 0);
}

/* authentication reject is received from lower layer */
static int gsm48_mm_rx_auth_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	ms->tmsi_valid = 0;
	ms->sim_invalid = 1;
	sim: delete tmsi
	sim: delete lai
	sim: delete key seq number
	new_mmu_state(ms, GSM_MMUSTATE_U3_ROAMING_NA);

	if (mm->state == GSM_MMSTATE_IMSI_DETACH_INIT) {
		todo 4.3.4.3
	}

	return 0;
}

/* authentication reject is received from lower layer */
static int gsm48_mm_rx_auth_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm48_auth_req *ar = gh->data;

	if (payload_len < sizeof(struct gsm48_auth_req)) {
		DEBUGP(DMM, "Short read of authentication request message error.\n");
		return -EINVAL;
	}

	/* key_seq and random */
	new key to sim:
	ar->key_seq;
	ar->rand;

	/* wait for auth response event from sim */
	return 0;
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
	{GSM_MMSTATE_LOC_UPD_INIT, /* 4.4.4.5 */
	 GSM48_MT_MM_LOC_UPD_ACCEPT, gsm48_mm_rx_loc_upd_acc},
	{GSM_MMSTATE_LOC_UPD_INIT, /* 4.4.4.7 */
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

static int gsm48_mm_sendmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	int msg_type = gh->msg_type & 0xbf;

	DEBUGP(DMM, "(ms %s) Received '%s' in MM state %s\n", ms->name,
		gsm48_mm_msg_name(msg_type), mm_state_names[mm->state]);

	/* find function for current state and message */
	for (i = 0; i < MMDATASLLEN; i++)
		if ((msg_type == mmdatastatelist[i].type)
		 && ((1 << mm->state) & mmdatastatelist[i].states))
			break;
	if (i == MMDATASLLEN) {
		DEBUGP(DMM, "Message unhandled at this state.\n");
		return 0;
	}

	rc = mmdatastatelist[i].rout(ms, msg);

	return rc;
}

/* timeout events of all timers */
static void gsm48_mm_t3210(void *arg)
{
	struct gsm_mmlayer *mm = arg;
	mm_event(mm->ms, GSM_MMEVENT_TIMEOUT_T3210);
}
static void gsm48_mm_t3211(void *arg)
{
	struct gsm_mmlayer *mm = arg;
	mm_event(mm->ms, GSM_MMEVENT_TIMEOUT_T3211);
}
static void gsm48_mm_t3212(void *arg)
{
	struct gsm_mmlayer *mm = arg;
	mm_event(mm->ms, GSM_MMEVENT_TIMEOUT_T3212);
}
static void gsm48_mm_t3213(void *arg)
{
	struct gsm_mmlayer *mm = arg;
	mm_event(mm->ms, GSM_MMEVENT_TIMEOUT_T3213);
}

/* RR is esablised during location update */
static int gsm48_mm_est_loc_upd(struct osmocom_ms *ms, struct gsm_rr *est)
{
	/* 4.4.4.1 */
	struct msgb *msg = gsm48_msgb_alloc();
	struct gsm48_hdr *gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	struct gsm48_loc_upd *loc_upd = msgb_put(msg, 7);
	u_int8_t *classmark1 = ((u_int8_t *)loc_upd) + 6;
	u_int8_t buf[11];
	u_int8_t *ie;

	gsm48_start_mm_timer(mm, 0x3210, GSM48_T3210_MS);
	new_mm_state(ms, GSM_MMSTATE_LOC_UPD_INIT, 0);

	msg->lchan = lchan;
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
	switch(mi_type) {
	case GSM_MI_TYPE_TMSI:
		gsm48_generate_mid_from_tmsi(buf, tmsi);
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_generate_mid_from_imsi(buf, imsi);
		break;
	case GSM_MI_TYPE_IMEI:
	case GSM_MI_TYPE_IMEISV:
		gsm48_generate_mid_from_imsi(buf, imei);
		break;
	case GSM_MI_TYPE_NONE:
	default:
	        buf[0] = GSM48_IE_MOBILE_ID;
	        buf[1] = 1;
	        buf[2] = 0xf0 | GSM_MI_TYPE_NONE;
		break;
	}
	/* MI as LV */
	ie = msgb_put(msg, 1 + buf[1]);
	memcpy(ie, buf + 1, 1 + buf[1]);

	return gsm48_sendmsg(msg, NULL);
}

/* RR is released after location update reject */
static int gsm48_mm_rel_loc_upd_rej(struct osmocom_ms *ms, struct gsm_rr *rel)
{
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
		sim: delete tmsi
		sim: delete lai
		sim: delete key seq number
		new_mmu_state(ms, GSM_MMUSTATE_U3_ROAMING_NA);
	}

	/* forbidden list */
	switch (mm->lupd_rej_cause) {
	case 2:
	case 3:
	case 6:
		set sim invalid	
		break;
	case 11:
		add_forbidden_list(ms, FORBIDDEN_PLMN, lai);
		break;
	case 12:
		add_forbidden_list(ms, FORBIDDEN_LOC_AREA_RPOS, lai);
		break;
	case 13:
		add_forbidden_list(ms, FORBIDDEN_LOC_AREA_ROAM, lai);
		break
	default:
		todo 4.4.4.9
	}

	return gsm48_mm_return_idle(ms);
}

/* RR is released in other states */
static int gsm48_mm_rel_other(struct osmocom_ms *ms, struct gsm_rr *rel)
{
	return gsm48_mm_return_idle(ms);
}

/* RR is esablised during mm connection */
static int gsm48_mm_est_mm_con(struct osmocom_ms *ms, struct gsm_rr *est)
{
	loop through all transactions. there must only be one in the "MMCONSTATE_CM_REQ", others are "MMCONSTATE_WAITING" or "MMCONSTATE_ACTIVE".

	gsm48_start_mm_timer(mm, 0x3230, GSM48_T3220_MS);
	new_mm_state(ms, GSM_MMSTATE_WAIT_OUT_MM_CONN, 0);
}

/* state trasitions for radio ressource messages (lower layer) */
static struct rrdatastate {
	u_int32_t	states;
	int		type;
	int		(*rout) (struct gsm_trans *trans, struct gsm_rr *msg);
} rrdatastatelist[] = {
	{SBIT(GSM_MMSTATE_WAIT_RR_CONN_LUPD), /* 4.4.4.1 */
	 RR_ESTAB_CNF, gsm48_mm_est_loc_upd},
	{SBIT(GSM_MMSTATE_LOC_UPD_REJ), /* 4.4.4.7 */
	 RR_REL_IND, gsm48_mm_rel_loc_upd_rej},
	{ALL_STATES,
	 RR_REL_IND, gsm48_mm_rel_other},
	{SBIT(GSM_MMSTATE_WAIT_RR_CONN_MM_CON), /* 4.5.1.1 */
	 RR_ESTAB_CNF, gsm48_mm_est_mm_con},
};

#define RRDATASLLEN \
	(sizeof(rrdatastatelist) / sizeof(struct rrdatastate))

static int gsm48_rcv_rr(struct osmocom_ms *ms, struct gsm_rr *rrmsg)
{
	struct gsm48_mmlayer *mm = ms->mmlayer;
	int msg_type = rrmsg->msg_type;

	DEBUGP(DMM, "(ms %s) Received '%s' from RR in state %s\n", ms->name,
		gsm48_rr_msg_name(msg_type), mm_state_names[mm->state]);

	/* find function for current state and message */
	for (i = 0; i < RRDATASLLEN; i++)
		if ((msg_type == rrdatastatelist[i].type)
		 && ((1 << mm->state) & rrdatastatelist[i].states))
			break;
	if (i == RRDATASLLEN) {
		DEBUGP(DMM, "Message unhandled at this state.\n");
		return 0;
	}

	rc = rrdatastatelist[i].rout(ms, rrmsg);

	return rc;
}

/* dequeue messages from RR */
int gsm48_mm_queue(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm = ms->mmlayer;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&mm->up_queue))) {
		/* msg is freed there */
		gsm48_rcv_rr(ms, msg);
		work = 1; /* work done */
	}
	
	return work;
}


wichtig: nur eine MM connection zur zeit, da ja auch nur ein cm-service-request laufen kann. die anderen werden dann als "waiting" deklariert.
