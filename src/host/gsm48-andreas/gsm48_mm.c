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
 * init and exit
 */

/* initialize Mobility Management process */
int gsm48_mm_init(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;
	struct msgb *msg;

	tall_conn_ctx = talloc_named_const(tall_osmocom_bb_ctx, 0, "MM connection");

	memset(mm, 0, sizeof(*mm));
	mm->ms = ms;

	/* 4.2.1.1 */
	mm->state = GSM48_MM_ST_MM_ILDE;
	mm->sstate = GSM48_MM_SST_PLMN_SEARCH;

	/* init lists */
	INIT_LLIST_HEAD(&mm->mm_conn);
	INIT_LLIST_HEAD(&mm->mm_upqueue);
	INIT_LLIST_HEAD(&mm->mmevent_queue);

	/* schedule IMSI attach */
	nmsg = gsm322_msgb_alloc(GSM48_MM_EVENT_IMSI_ATTACH);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	return 0;
}

/* exit MM process */
int gsm48_mm_exit(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;
	struct gsm48_mm_conn *conn;
	struct msgb *msg;

	/* flush lists */
	while (!llist_empty(&mm->mm_conn)) {
		conn = llist_entry(mm->mm_conn.next, struct gsm48_mm_conn, list);
		llist_del(&conn->mm_conn);
		mm_conn_free(conn);
	}
	while ((msg = msgb_dequeue(&mm->mm_upqueue)))
		msgb_free(msgb);
	while ((msg = msgb_dequeue(&mm->mmevent_queue)))
		msgb_free(msgb);
		
	/* stop timers */
	stop_mm_t3210(mm);
	stop_mm_t3211(mm);
	stop_mm_t3212(mm);
	stop_mm_t3213(mm);
	stop_mm_t3240(mm);

	return 0;
}

/*
 * support functions
 */

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

/*
 * MM connection management
 */

void *tall_conn_ctx;

uint32_t mm_conn_new_ref = 1;

/* find MM connection by protocol+ID */
struct gsm48_mm_conn *mm_conn_by_id(struct gsm48_mmlayer *mm,
				   uint8_t proto, uint8_t trans_id)
{
	struct gsm48_mm_conn *conn;

	llist_for_each_entry(conn, &mm->mm_conn, list) {
		if (conn->protocol == proto &&
		    conn->transaction_id == trans_id)
			return conn;
	}
	return NULL;
}

/* find MM connection by reference */
struct gsm48_mm_conn *mm_conn_by_ref(struct gsm48_mmlayer *mm,
					uint32_t ref)
{
	struct gsm48_mm_conn *conn;

	llist_for_each_entry(conn, &mm->mm_conn, list) {
		if (conn->ref == ref)
			return conn;
	}
	return NULL;
}

/* create MM connection instance */
static struct gsm48_mm_conn* mm_conn_new(struct gsm48_mmlayer *mm,
	int proto, uint8_t trans_id, uint32_t ref)
{
	struct gsm48_mm_conn *conn = talloc_zero(tall_conn_ctx, sizeof(*conn));

	if (!conn)
		return NULL;

	conn->type = type;
	conn->state = GSM48_MMXX_ST_IDLE;
	conn->transaction_id = trans_id;
	conn->protocol = proto;
	conn->ref = ref;

	llist_add(conn, &mm->mm_conn);

	return conn;
}

/* destroy MM connection instance */
void mm_conn_free(struct gsm48_mm_conn *conn)
{
	new_conn_state(conn, GSM48_MMXX_ST_CONN_PEND);

	llist_del(&conn->list);

	talloc_free(conn);
}

static const char *gsm48_mmxx_state_names[] = {
	"IDLE",
	"CONN_PEND",
	"DEDICATED",
	"CONN_SUSP",
	"REESTPEND"
};

/* new MM connection state */
static void new_conn_state(struct gsm48_mm_conn *conn, int state)
{
	DEBUGP(DMM, "(ref %d) new state %s -> %s", conn->ref,
		gsm48_mmxx_state_names[mm->state], gsm48_mmxx_state_names[state]);
	conn->state = state;
}

/*
 * timers
 */

static void start_mm_t3210(struct gsm_rrlayer *mm)
{
	DEBUGP(DRR, "starting T3210 (loc. upd. timeout) with %d seconds\n", GSM48_T3210_MS);
	mm->t3210.cb = timeout_mm_t3210;
	mm->t3210.data = mm;
	bsc_schedule_timer(&mm->t3210, GSM48_T3210_MS);
}

static void start_mm_t3211(struct gsm_rrlayer *mm)
{
	DEBUGP(DRR, "starting T3211 (loc. upd. retry delay) with %d seconds\n", GSM48_T3211_MS);
	mm->t3211.cb = timeout_mm_t3211;
	mm->t3211.data = mm;
	bsc_schedule_timer(&mm->t3211, GSM48_T3211_MS);
}

static void start_mm_t3212(struct gsm_rrlayer *mm, int sec)
{
	/* don't start, if is not available */
	if (!sec)
		return;

	DEBUGP(DRR, "starting T3212 (periodic loc. upd. delay) with %d seconds\n", sec);
	mm->t3212.cb = timeout_mm_t3212;
	mm->t3212.data = mm;
	bsc_schedule_timer(&mm->t3212, sec, 0);
}

static void start_mm_t3213(struct gsm_rrlayer *mm)
{
	DEBUGP(DRR, "starting T3213 (delay after RA failure) with %d seconds\n", GSM48_T3213_MS);
	mm->t3213.cb = timeout_mm_t3213;
	mm->t3213.data = mm;
	bsc_schedule_timer(&mm->t3213, GSM48_T3213_MS);
}

static void start_mm_t3230(struct gsm_rrlayer *mm)
{
	DEBUGP(DRR, "starting T3230 (MM connection timeout) with %d seconds\n", GSM48_T3230_MS);
	mm->t3230.cb = timeout_mm_t3230;
	mm->t3230.data = mm;
	bsc_schedule_timer(&mm->t3230, GSM48_T3230_MS);
}

static void start_mm_t3240(struct gsm_rrlayer *mm)
{
	DEBUGP(DRR, "starting T3240 (RR release timeout) with %d seconds\n", GSM48_T3240_MS);
	mm->t3240.cb = timeout_mm_t3240;
	mm->t3240.data = mm;
	bsc_schedule_timer(&mm->t3240, GSM48_T3240_MS);
}

static void stop_mm_t3210(struct gsm_rrlayer *mm)
{
	if (bsc_timer_pending(mm->t3210)) {
		DEBUGP(DRR, "stopping pending (loc. upd. timeout) timer T3210\n");
		bsc_del_timer(&mm->t3210);
	}
}

static void stop_mm_t3211(struct gsm_rrlayer *mm)
{
	if (bsc_timer_pending(mm->t3211)) {
		DEBUGP(DRR, "stopping pending (loc. upd. retry delay) timer T3211\n");
		bsc_del_timer(&mm->t3211);
	}
}

static void stop_mm_t3212(struct gsm_rrlayer *mm)
{
	if (bsc_timer_pending(mm->t3212)) {
		DEBUGP(DRR, "stopping pending (periodic loc. upd. delay) timer T3212\n");
		bsc_del_timer(&mm->t3212);
	}
}

static void stop_mm_t3213(struct gsm_rrlayer *mm)
{
	if (bsc_timer_pending(mm->t3213)) {
		DEBUGP(DRR, "stopping pending (delay after RA failure) timer T3213\n");
		bsc_del_timer(&mm->t3213);
	}
}

static void stop_mm_t3230(struct gsm_rrlayer *mm)
{
	if (bsc_timer_pending(mm->t3230)) {
		DEBUGP(DRR, "stopping pending (MM connection timeout) timer T3230\n");
		bsc_del_timer(&mm->t3230);
	}
}

static void stop_mm_t3240(struct gsm_rrlayer *mm)
{
	if (bsc_timer_pending(mm->t3240)) {
		DEBUGP(DRR, "stopping pending (RR release timeout) timer T3240\n");
		bsc_del_timer(&mm->t3240);
	}
}

static void stop_mm_t3241(struct gsm_rrlayer *mm)
{
	/* not implemented, not required */
}

static void timeout_mm_t3210(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	DEBUGP(DRR, "timer T3210 (loc. upd. timeout) has fired\n");
	gsm48_mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3210, NULL);
}

static void timeout_mm_t3211(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	DEBUGP(DRR, "timer T3211 (loc. upd. retry delay) has fired\n");
	gsm48_mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3211, NULL);
}

static void timeout_mm_t3212(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	DEBUGP(DRR, "timer T3212 (periodic loc. upd. delay) has fired\n");

	/* reset attempt counter when attempting to update (4.4.4.5) */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && mm->sstate == mm->sstate == GSM48_MM_SST_ATTEMPT_UPDATE)
		ms->lupd_attempt = 0;

	gsm48_mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3212, NULL);
}

static void timeout_mm_t3213(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	DEBUGP(DRR, "timer T3213 (delay after RA failure) has fired\n");
	gsm48_mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3213, NULL);
}

static void timeout_mm_t3230(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	DEBUGP(DRR, "timer T3230 (MM connection timeout) has fired\n");
	gsm48_mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3230, NULL);
}

static void timeout_mm_t3240(void *arg)
{
	struct gsm48_mmlayer *mm = arg;

	DEBUGP(DRR, "timer T3240 (RR release timeout) has fired\n");
	gsm48_mm_event(mm->ms, GSM48_MM_EVENT_TIMEOUT_T3240, NULL);
}

/*
 * messages
 */

/* names of MMxx-SAP */
static const struct value_string gsm48_mmxx_msg_names[] = {
	{ GSM48_MMCC_EST_REQ,		"MMCC_EST_REQ" },
	{ GSM48_MMCC_EST_IND,		"MMCC_EST_IND" },
	{ GSM48_MMCC_EST_CNF,		"MMCC_EST_CNF" },
	{ GSM48_MMCC_REL_REQ,		"MMCC_REL_REQ" },
	{ GSM48_MMCC_REL_IND,		"MMCC_REL_IND" },
	{ GSM48_MMCC_DATA_REQ,		"MMCC_DATA_REQ" },
	{ GSM48_MMCC_DATA_IND,		"MMCC_DATA_IND" },
	{ GSM48_MMCC_UNIT_DATA_REQ,	"MMCC_UNIT_DATA_REQ" },
	{ GSM48_MMCC_UNIT_DATA_IND,	"MMCC_UNIT_DATA_IND" },
	{ GSM48_MMCC_SYNC_IND,		"MMCC_SYNC_IND" },
	{ GSM48_MMCC_REEST_REQ,		"MMCC_REEST_REQ" },
	{ GSM48_MMCC_REEST_CNF,		"MMCC_REEST_CNF" },
	{ GSM48_MMCC_ERR_IND,		"MMCC_ERR_IND" },
	{ GSM48_MMCC_PROMPT_IND,	"MMCC_PROMPT_IND" },
	{ GSM48_MMCC_PROMPT_REJ,	"MMCC_PROMPT_REJ" },
	{ GSM48_MMSS_EST_REQ,		"MMSS_EST_REQ" },
	{ GSM48_MMSS_EST_IND,		"MMSS_EST_IND" },
	{ GSM48_MMSS_EST_CNF,		"MMSS_EST_CNF" },
	{ GSM48_MMSS_REL_REQ,		"MMSS_REL_REQ" },
	{ GSM48_MMSS_REL_IND,		"MMSS_REL_IND" },
	{ GSM48_MMSS_DATA_REQ,		"MMSS_DATA_REQ" },
	{ GSM48_MMSS_DATA_IND,		"MMSS_DATA_IND" },
	{ GSM48_MMSS_UNIT_DATA_REQ,	"MMSS_UNIT_DATA_REQ" },
	{ GSM48_MMSS_UNIT_DATA_IND,	"MMSS_UNIT_DATA_IND" },
	{ GSM48_MMSS_REEST_REQ,		"MMSS_REEST_REQ" },
	{ GSM48_MMSS_REEST_CNF,		"MMSS_REEST_CNF" },
	{ GSM48_MMSS_ERR_IND,		"MMSS_ERR_IND" },
	{ GSM48_MMSS_PROMPT_IND,	"MMSS_PROMPT_IND" },
	{ GSM48_MMSS_PROMPT_REJ,	"MMSS_PROMPT_REJ" },
	{ GSM48_MMSMS_EST_REQ,		"MMSMS_EST_REQ" },
	{ GSM48_MMSMS_EST_IND,		"MMSMS_EST_IND" },
	{ GSM48_MMSMS_EST_CNF,		"MMSMS_EST_CNF" },
	{ GSM48_MMSMS_REL_REQ,		"MMSMS_REL_REQ" },
	{ GSM48_MMSMS_REL_IND,		"MMSMS_REL_IND" },
	{ GSM48_MMSMS_DATA_REQ,		"MMSMS_DATA_REQ" },
	{ GSM48_MMSMS_DATA_IND,		"MMSMS_DATA_IND" },
	{ GSM48_MMSMS_UNIT_DATA_REQ,	"MMSMS_UNIT_DATA_REQ" },
	{ GSM48_MMSMS_UNIT_DATA_IND,	"MMSMS_UNIT_DATA_IND" },
	{ GSM48_MMSMS_REEST_REQ,	"MMSMS_REEST_REQ" },
	{ GSM48_MMSMS_REEST_CNF,	"MMSMS_REEST_CNF" },
	{ GSM48_MMSMS_ERR_IND,		"MMSMS_ERR_IND" },
	{ GSM48_MMSMS_PROMPT_IND,	"MMSMS_PROMPT_IND" },
	{ GSM48_MMSMS_PROMPT_REJ,	"MMSMS_PROMPT_REJ" },
	{ 0,				NULL }
};

const char *get_mmxx_name(int value)
{
	return get_value_string(gsm48_mmxx_msg_names, value);
}

/* allocate GSM 04.08 message (MMxx-SAP) */
static struct msgb *gsm48_mmxx_msgb_alloc(int msg_type, uint32_t ref, uint8_t trans_id)
{
	struct msgb *msg;
	struct gsm48_mmxx_hdr *mmh;

	msg = msgb_alloc_headroom(MMXX_ALLOC_SIZE+MMXX_ALLOC_HEADROOM,
		MMXX_ALLOC_HEADROOM, "GSM 04.08 MMxx");
	if (!msg)
		return NULL;

	mmh = (struct gsm48_mmxx_hdr *)msgb_put(msg, sizeof(*mmh));
	mmh->msg_type = msg_type;
	mmh->ref = ref;
	mmh->trans_id = trans_id;

	return msg;
}

/* allocate MM event message */
static struct msgb *gsm48_mmevent_msgb_alloc(int msg_type)
{
	struct msgb *msg;
	struct gsm48_mmevent *mme;

	msg = msgb_alloc_headroom(MMEVENT_ALLOC_SIZE+MMEVENT_ALLOC_HEADROOM,
		MMEVENT_ALLOC_HEADROOM, "GSM 04.08 MM event");
	if (!msg)
		return NULL;

	mme = (struct gsm48_mmevent *)msgb_put(msg, sizeof(*mme));
	mme->msg_type = msg_type;

	return msg;
}

/* queue message (MMxx-SAP) */
int gsm48_mmxx_upmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_mmlayer *mm = &ms->mmlayer;

	msgb_enqueue(&mm->mm_upqueue, msg);
}

/* queue MM event message */
int gsm48_mmevent_msg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_mmlayer *mm = &ms->mmlayer;

	msgb_enqueue(&mm->mmevent_queue, msg);
}

/* dequeue messages (MMxx-SAP) */
int gsm48_mm_dequeue(struct osmocom_ms *ms)
{
	struct gsm_mmlayer *rr = &ms->rrlayer;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&rr->rr_upqueue))) {
		switch (msg->msg_type & GSM48_MMXX_MASK) {
		case GSM48_MMCC_CLASS:
			gsm48_rcv_cc(ms, msg);
			break;
#if 0
		case GSM48_MMSS_CLASS:
			gsm48_rcv_ss(ms, msg);
			break;
		case GSM48_MMSMS_CLASS:
			gsm48_rcv_sms(ms, msg);
			break;
#endif
		}
		free_msgb(msg);
		work = 1; /* work done */
	}
	
	return work;
}

/* dequeue messages (RR-SAP) */
int gsm48_rr_dequeue(struct osmocom_ms *ms)
{
	struct gsm_mmlayer *rr = &ms->rrlayer;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&rr->rr_upqueue))) {
		/* msg is freed there */
		gsm48_rcv_rr(ms, msg);
		work = 1; /* work done */
	}
	
	return work;
}

/* dequeue MM event messages */
int gsm48_mmevent_dequeue(struct osmocom_ms *ms)
{
	struct gsm_mmlayer *mm = &ms->mmlayer;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&mm->mmevent_queue))) {
		gsm48_mm_event(ms, msg);
		free_msgb(msg);
		work = 1; /* work done */
	}
	
	return work;
}

/* push RR header and send to RR */
static int gsm48_mm_to_rr(ms, msg, int msg_type, uint8_t cause)
{
	/* push RR header */
	msgb_push(msg, sizeof(struct gsm48_rr_hdr));
	rrh = (struct gsm48_rr_hdr *)msg->data;
	rrh->msg_type = msg_type;
	rrh->cause = cause;

	/* send message to RR */
	return gsm48_rr_downmsg(ms, msg);
}

/*
 * state transition
 */

static const char *gsm48_mm_state_names[] = {
	"NULL",
	"undefined 1",
	"undefined 2",
	"LOC_UPD_INIT",
	"undefined 4",
	"WAIT_OUT_MM_CONN",
	"MM_CONN_ACTIVE",
	"IMSI_DETACH_INIT",
	"PROCESS_CM_SERV_P",
	"WAIT_NETWORK_CMD",
	"LOC_UPD_REJ",
	"undefined 11",
	"undefined 12",
	"WAIT_RR_CONN_LUPD",
	"WAIT_RR_CONN_MM_CON",
	"WAIT_RR_CONN_IMSI_D",
	"undefined 16",
	"WAIT_REEST",
	"WAIT_RR_ACTIVE",
	"MM_IDLE",
	"WAIT_ADD_OUT_MM_CON",
	"MM_CONN_ACTIVE_VGCS",
	"WAIT_RR_CONN_VGCS",
	"LOC_UPD_PEND",
	"IMSI_DETACH_PEND",
	"RR_CONN_RELEASE_NA"
};

static const char *gsm48_mm_substate_names[] = {
	"NORMAL_SERVICE",
	"ATTEMPT_UPDATE",
	"LIMITED_SERVICE",
	"NO_IMSI",
	"NO_CELL_AVAIL",
	"LOC_UPD_NEEDED",
	"PLMN_SEARCH",
	"PLMN_SEARCH_NORMAL",
	"RX_VGCS_NORMAL",
	"RX_VGCS_LIMITED"
};

/* Set new MM state, also new substate in case of MM IDLE state. */
static void new_mm_state(struct gsm48_mmlayer *mm, int state, int substate)
{
	DEBUGP(DMM, "(ms %s) new state %s", mm->ms, gsm48_mm_state_names[mm->state]);
	if (mm->state == GSM48_MM_ST_MM_ILDE)
		DEBUGP(DMM, " substate %s", gsm48_mm_substate_names[mm->substate]);
	DEBUGP(DMM, "-> %s", mm_state_names[state]);
	if (state == GSM48_MM_ST_MM_ILDE)
		DEBUGP(DMM, " substate %s", gsm48_mm_substate_names[substate]);
	DEBUGP(DMM, "\n");

	/* remember most recent substate */
	if (mm->state == GSM48_MM_ST_MM_IDLE)
		mm->mr_substate = mm->substate;

	mm->state = state;
	mm->substate = substate;

	/* resend detach event, if flag is set */
	if (mm->delay_detach) {
		struct nmsg *msg;

		mm->delay_detach = 0;

		nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_IMSI_DETACH);
		if (!nmsg)
			return -ENOMEM;
		gsm48_mmevent_msg(ms, nmsg);
	}

	/* 4.4.2 start T3212 in MM IDLE mode if not started or has expired */
	if (mm->state == GSM48_MM_ST_MM_IDLE
	 && (mm->sstate == GSM48_MM_SST_NORMAL_SERVICE
	  || mm->sstate == GSM48_MM_SST_ATTEMPT_UPDATE)) {
		if (!timer_pending(&mm->t3212))
			start_mm_t3212(mm, mm->t3212_value);
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

		/* stop periodic location updating */
		mm->lupd_pending = 0;
		stop_mm_t3212(mm); /* 4.4.2 */

#if 0
		/* imsi detach due to power off */
		if (mm->power_off) {
			struct msgb *nmsg;
	
			nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_POWER_OFF);
			if (!nmsg)
				return -ENOMEM;
			gsm48_mmevent_msg(ms, nmsg);
		}
#endif
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
		/* if SIM is not updated (abnormal case as described in 4.4.4.9 */
		if (subscr->ustate != GSM_SIM_U1_UPDATED)
			new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_ATTEMPT_UPDATE);
		else
			new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_NORMAL_SERVICE);

		return 0;
	}

	/* location update allowed */
	if (cs->state == GSM_C3_CAMPED_NORMALLY)
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_LOC_UPD_NEEDED);
	else
		new_mm_state(mm, GSM48_MM_ST_IDLE, GSM48_MM_SST_LIMITED_SERVICE);

	return 0;
}

/*
 * process handlers (Common procedures)
 */

/* sending MM STATUS message */
static int gsm48_mm_tx_mm_status(struct osmocom_ms *ms, u_int8_t reject)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	uint8_t *reject_cause;

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));
	reject_cause = msgb_put(nmsg, 1);

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_STATUS;
	*reject_cause = reject;

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, RR_DATA_REQ, 0);
}

/* 4.3.1.2 sending TMSI REALLOCATION COMPLETE message */
static int gsm48_mm_tx_tmsi_reall_cpl(struct osmocom_ms *ms)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_TMSI_REALL_COMPL;

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, RR_DATA_REQ, 0);
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
		DEBUGP(DMM, "Short read of TMSI REALLOCATION COMMAND message error.\n");
		return -EINVAL;
	}
	/* LAI */
	decode_lai(lai, &subscr->lai_mcc, &subscr->lai_mnc, &subscr->lai_lac);
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
		DEBUGP(DMM, "Short read of AUTHENTICATION REQUEST message error.\n");
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
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	struct gsm_mmevent *mmevent = arg;
	u_int8_t *sres = msgb_put(msg, 4);

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_AUTH_RSP;

	/* SRES */
	memcpy(sres, ev->sres, 4);

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, RR_DATA_REQ, 0);
}

/* 4.3.2.5 AUTHENTICATION REJECT is received */
static int gsm48_mm_rx_auth_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;

	gsm48_stop_mm_timer(mm, 0x3212); /* 4.4.2 */

	/* SIM invalid */
	subscr->sim_valid = 0;

	/* TMSI and LAI invalid */
	subscr->lai_valid = 0;
	subscr->tmsi_valid = 0;

	/* key is invalid */
	subscr->key_seq = 7;

	/* update status */
	new_sim_ustate(ms, GSM_SIM_U3_ROAMING_NA);

#ifdef TODO
	sim: delete tmsi, lai
	sim: delete key seq number
	sim: set update status
#endif

	/* abort IMSI detach procedure */
	if (mm->state == GSM48_MM_ST_IMSI_DETACH_INIT) {
		/* abort RR connection */
		nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_REQ);
		if (!nmsg)
			return -ENOMEM;
		rrh = (struct gsm48_rr_hdr *) msgb_put(nmsg, sizeof(*rrh));
		rrh->cause = GSM48_RR_CAUSE_NORMAL;
		gsm48_rr_downmsg(ms, msg);

		/* return to MM IDLE / No SIM */
		gsm48_mm_return_idle(ms);

	}

	return 0;
}

/* 4.3.3.1 IDENTITY REQUEST is received */
static int gsm48_mm_rx_id_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	u_int8_t mi_type;

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of IDENTITY REQUEST message error.\n");
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

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_ID_RSP;

	/* MI */
	gsm48_encode_mi(nmsg, subscr, mi_type);

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, RR_DATA_REQ, 0);
}

/* 4.3.4.1 sending IMSI DETACH INDICATION message */
static int gsm48_mm_tx_imsi_detach(struct osmocom_ms *ms, int rr_prim)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_sysinfo *s = &ms->sysinfo;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	struct gsm48_classmark1 *classmark1;

	nmsg = gsm48_l3_msgb_alloc();
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

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, rr_prim, RR_EST_CAUSE_OTHER_SDCCH);
}

/* detach has ended */
static int gsm48_mm_imsi_detach_end(struct osmocom_ms *ms, void *arg)
{
	struct gsm_subscriber *subscr = &ms->subscr;
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
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_sysinfo *s = &ms->sysinfo;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	/* we may silently finish IMSI detach */
	if (!s->att_allowed)
		return gsm48_mm_imsi_detach_silent(ms, arg);

	new_mm_state(mm, GSM48_MM_ST_WAIT_RR_CONN_IMSI_D, 0);

	/* establish RR and send IMSI detach */
	return gsm48_mm_tx_imsi_detach(ms, RR_EST_REQ);
}

/* IMSI detach has been sent, wait for RR release */
static int gsm48_mm_imsi_detach_sent(struct osmocom_ms *ms, void *arg)
{
	/* start T3220 (4.3.4.1) */
	gsm48_start_mm_timer(mm, 0x3220, GSM48_T3220_MS);

	new_mm_state(mm, GSM48_MM_ST_IMSI_DETACH_INIT, 0);

	return 0;
}
	
/* release MM connection and proceed with IMSI detach */
static int gsm48_mm_imsi_detach_release(struct osmocom_ms *ms, void *arg)
{
	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* release all connections */
	gsm48_mm_release_mm_conn(ms, 1, 16, 0);

	/* wait for release of RR */
	if (!s->att_allowed) {
		return new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

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

/* support function to release pending/all ongoing MM connections */
static int gsm48_mm_release_mm_conn(struct osmocom_ms *ms, int abort_any,
				    uint8_t cause, int error)
{
	struct gsm48_mmlayer *mm = &ms->mm;
	struct gsm48_mm_conn *conn, *conn2;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;
	int msg_type;

	/* release MM connection(s) */
	llist_for_each_entry_safe(conn, conn2, &mm->mm_conn, list) {
		/* abort any OR the pending connection */
		if (abort_any || conn->state == GSM48_MMXX_ST_CONN_PEND) {
			/* send MMxx-REL-IND */
			nmsg = NULL;
			switch(conn->protocol) {
			case GSM48_PDISC_CC:
				nmsg = gsm48_mmxx_msgb_alloc(
					error ? GSM48_MMCC_ERR_IND
					: GSM48_MMCC_REL_IND, conn->ref, conn->trans_id);
				break;
			case GSM48_PDISC_SS:
				nmsg = gsm48_mmxx_msgb_alloc(
					error ? GSM48_MMSS_ERR_IND
					: GSM48_MMSS_REL_IND, conn->ref, conn->trans_id);
				break;
			case GSM48_PDISC_SMS:
				nmsg = gsm48_mmxx_msgb_alloc(
					error ? GSM48_MMSMS_ERR_IND
					: GSM48_MMSMS_REL_IND, conn->ref, conn->trans_id);
				break;
			}
			if (!nmsg) {
				/* this should not happen */
				mm_conn_free(conn);
				continue; /* skip if not of CC type */
			}
			nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
			nmmh->cause = cause;
			/* copy L3 message */
			nmsg->l3h = msgb_put(msg, msgb_l3len(msg));
			memcpy(nmsg->l3h, msg->l3h, msgb_l3len(msg));
			gsm48_mmxx_upmsg(ms, nmsg);

			mm_conn_free(conn);
		}
	}
	return 0;
}

/* 4.3.5.2 ABORT is received */
static int gsm48_mm_rx_abort(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mm;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of ABORT message error.\n");
		return -EINVAL;
	}

	reject_cause = *gh->data;

	if (llist_empty(&mm->mm_conn)) {
		DEBUGP(DMM, "Abort (cause #%d) while no MM connection is established.\n", reject_cause);
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_NOT_COMPATIBLE);
	} else {
		/* stop MM connection timer */
		stop_mm_t3230(mm);

		DEBUGP(DMM, "Abort (cause #%d) while MM connection is established.\n", reject_cause);
		gsm48_mm_release_mm_conn(ms, 1, 16, 0);
	}

	if (reject_cause == GSM48_REJECT_ILLEGAL_ME) { 
		/* SIM invalid */
		subscr->sim_valid = 0;

		/* TMSI and LAI invalid */
		subscr->lai_valid = 0;
		subscr->tmsi_valid = 0;

		/* key is invalid */
		subscr->key_seq = 7;

		/* update status */
		new_sim_ustate(ms, GSM_SIM_U3_ROAMING_NA);

#ifdef TODO
		sim: delete tmsi, lai
		sim: delete key seq number
		sim: apply update state
#endif

		/* return to MM IDLE / No SIM */
		gsm48_mm_return_idle(ms);
	}

	return 0;
}

/* 4.3.6.2 MM INFORMATION is received */
static int gsm48_mm_rx_info(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;

	if (payload_len < 0)
		short:
		DEBUGP(DMM, "Short read of MM INFORMATION message error.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &rsl_att_tlvdef, gh->data, payload_len, 0, 0);

	/* long name */
	if (TLVP_PRESENT(&tp, GSM48_IE_NAME_LONG)) {
		decode_network_name(mm->name_long, sizeof(mm->name_long),
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}
	/* short name */
	if (TLVP_PRESENT(&tp, GSM48_IE_NAME_SHORT)) {
		decode_network_name(mm->name_short, sizeof(mm->name_short),
				TLVP_VAL(&tp, GSM48_IE_FACILITY)-1);
	}

	return 0;
}

/*
 * process handlers for Location Update + IMSI attach (MM specific procedures)
 */

/* received sysinfo change event */
static int gsm48_mm_sysinfo(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mm;

	/* new periodic location update timer timeout */
	if (s->t3212 && s->t3212 != mm->t3212_value && timer_pend) {
		if (bsc_timer_pending(mm->t3212)) {
			int t;
			struct timeval current_time;

			/* get rest time */
			gettimeofday(&current_time, NULL);
			t = mm->t3212.timeout.tv_sec - curren_time.tv_sec;
			if (t < 0)
				t = 0;

			/* rest time modulo given value */
			mm->t3212.timeout.tv_sec = current_time.tv_sec
				+ (t % s->t3212);
		} else {
			uint32_t rand = random();

			/* value between 0 and given value */
			start_mm_t3212(mm, rand % (s->t3212 + 1));
		}
		mm->t3212_value = s->t3212;
	}
	
	/* stop timer if not required anymore */
	if (s->si3 && !s->t3212 && bsc_timer_pending(mm->t3212))
		stop_mm_t3212(mm);

	return 0;
}

/* 4.4.4.1 (re)start location update
 *
 * this function is called by
 * - normal location update
 * - periodic location update
 * - imsi attach
 * - retry timers (T3211 and T3213)
 */
static int gsm48_mm_loc_upd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;
	struct gsm48_sysinfo *s = &ms->sysinfo;
	
	/* (re)start only if we still require location update */
	if (!mm->lupd_pending) {
		return 0;
	}

	/* if LAI is forbidden, don't start */
	if (gsm322_is_forbidden_plmn(ms, s->mcc, s->mnc)) {
		return 0;
	}
	if (gsm322_is_forbidden_la(ms, s->mcc, s->mnc, s->lac)) {
		return 0;
	}

	/* 4.4.4.9 if cell is barred, don't start */
	if (gsm58_is_barred(ms, 0)) {
		return 0;
	}

	return gsm48_mm_tx_loc_upd_req(ms, msg);
}

/* initiate a normal location update */
static int gsm48_mm_loc_upd_normal(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;

	/* in case we already have a location update going on */
	if (lupd_pending) {
		return -EBUSY;
	}

	/* start normal location update */
	mm->lupd_type = 0;
	mm->lupd_pending = 1;
	mm->lupd_ra_failure = 0;

	return gsm48_mm_loc_upd(ms, msg);
}

/* initiate a periodic location update */
static int gsm48_mm_loc_upd_periodic(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;

	/* in case we already have a location update going on */
	if (lupd_pending) {
		return -EBUSY;
	}

	/* start normal location update */
	mm->lupd_type = 1;
	mm->lupd_pending = 1;
	mm->lupd_ra_failure = 0;

	return gsm48_mm_loc_upd(ms, msg);
}

/* initiate a location update due to IMSI attach */
static int gsm48_mm_loc_upd_imsi(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;

	/* in case we already have a location update going on */
	if (lupd_pending) {
		return -EBUSY;
	}

	/* is attachment required? */
	if (subscr->ustate == MMSTATE_U1_UPDATED && s->att_allowed) {
		/* start location update for IMSI attach */
		mm->lupd_type = 2;
	} else {
		/* start normal periodic update */
		mm->lupd_type = 1;
	}

	/* reset attempt counter */
	ms->lupd_attempt = 0;
	mm->lupd_pending = 1;
	mm->lupd_ra_failure = 0;

	return gsm48_mm_loc_upd(ms, msg);
}

/* 9.2.15 send LOCATION UPDATING REQUEST message */
static int gsm48_mm_tx_loc_upd_req(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;
	struct gsm48_sysinfo *s = &ms->sysinfo;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	struct gsm48_loc_upd_req *lu;

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));
	lu = (struct gsm48_loc_upd_req *)msgb_put(nmsg, sizeof(*lu) - 1);

	ngh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_LOC_UPD_REQ;

	/* location updating type */
	lu->type = mm->lupd_type;
	/* cipering key */
	lu->key_seq = subscr->key_seq;
	/* LAI */
*** unsolved issue, what shall be used if lai is not valid
	gsm0408_generate_lai(&lu->lai,
		subscr->lai_mcc, subscr->lai_mnc, subscr->lai_lac);
	/* classmark 1 */
	gsm48_encode_classmark1(nmsg, s->rev_lev, s->es_ind, s->a5_1,
		s->pwr_lev);
	/* MI */
	if (subsr->tmsi_valid) /* have TSMI ? */
		gsm48_encode_mi(nmsg, subscr, GSM_MI_TYPE_TSMI);
	else
		gsm48_encode_mi(nmsg, subscr, GSM_MI_TYPE_IMSI);

	new_mm_state(mm, GSM48_MM_ST_WAIT_RR_CONN_LUPD, 0);

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, RR_EST_REQ, RR_EST_CAUSE_LOC_UPD);
}

/* 4.4.4.1 RR is esablised during location update */
static int gsm48_mm_est_loc_upd(struct osmocom_ms *ms, struct msgb *msg)
{
	/* start location update timer */
	start_mm_t3210(mm);

	new_mm_state(mm, GSM48_MM_ST_LOC_UPD_INIT, 0);
}

/* 4.4.4.6 LOCATION UPDATING ACCEPT is received */
static int gsm48_mm_rx_loc_upd_acc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_loc_area_id *lai = gh->data;
	u_int8_t *tlv_start = gh->data;
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct tlv_parsed tp;

	if (payload_len < sizeof(struct gsm48_loc_area_id)) {
		short:
		DEBUGP(DMM, "Short read of LOCATION UPDATING ACCEPT message error.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &rsl_att_tlvdef, gh->data + sizeof(struct gsm48_loc_area_id),
		payload_len - sizeof(struct gsm48_loc_area_id), 0, 0);

	/* update has finished */
	mm->lupd_pending = 0;

	/* RA was successfull */
	mm->lupd_ra_failure = 0;

	/* stop periodic location updating timer */
	stop_mm_t3212(mm); /* 4.4.2 */

	/* LAI */
	subscr->lai_valid = 1;
	decode_lai(lai, &subscr->lai_mcc, &subscr->lai->mnc, &subscr->lai_lac);

	/* stop location update timer */
	stop_mm_t3210(mm);

	/* reset attempt counter */
	ms->lupd_attempt = 0;

	/* set the status in the sim to updated */
	new_sim_ustate(ms, GSM_SIM_U1_UPDATED);
#ifdef TODO
	sim: apply update state
#endif

	/* remove LA from forbidden list */
	gsm322_del_forbidden_la(ms, subscr->lai_mcc, subscr->lai_mnc,
		subscr->lai_lac);

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
			subscr->tmsi = ntohl(tmsi);
			subscr->tmsi_valid = 1;
#ifdef TODO
	sim: store tmsi
#endif
			break;
		case GSM_MI_TYPE_IMSI:
			subscr->tmsi_valid = 0;
#ifdef TODO
	sim: delete tmsi
#endif
			/* send TMSI REALLOCATION COMPLETE */
			gsm48_mm_tx_tmsi_reall_cpl(ms);
			break;
		default:
			DEBUGP(DMM, "TMSI reallocation with unknown MI type %d.\n", mi_type);
		}
	}

	/* send message to PLMN search process */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_REG_SUCCESS);
	if (!nmsg)
		return -ENOMEM;
	ngm = (struct gsm322_msg *)nmsg->data;
	gsm322_sendmsg(ms, nmsg);

	/* follow on proceed */
	if (TLVP_PRESENT(&tp, GSM48_IE_MOBILE_ID))
		DEBUGP(DMM, "follow-on proceed not supported.\n");

	/* start RR release timer */
	start_mm_t3240(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

	return 0;
}

/* 4.4.4.7 LOCATION UPDATING REJECT is received */
static int gsm48_mm_rx_loc_upd_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of LOCATION UPDATING REJECT message error.\n");
		return -EINVAL;
	}

	/* RA was successfull */
	mm->lupd_ra_failure = 0;

	/* stop periodic location updating timer */
	stop_mm_t3212(mm); /* 4.4.2 */

	/* stop location update timer */
	stop_mm_t3210(mm);

	/* store until RR is released */
	mm->lupd_rej_cause = *gh_data;

	/* start RR release timer */
	start_mm_t3240(mm);

	new_mm_state(mm, GSM48_MM_ST_LOC_UPD_REJ, 0);
}

/* 4.4.4.7 RR is released after location update reject */
static int gsm48_mm_rel_loc_upd_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm322_msg *ngm;
	
	/* new status */
	switch (mm->lupd_rej_cause) {
	case GSM48_REJECT_PLMN_NOT_ALLOWED:
	case GSM48_REJECT_LOC_NOT_ALLOWED:
	case GSM48_REJECT_ROAMING_NOT_ALLOWED:
		/* reset attempt counter */
		ms->lupd_attempt = 0;
		// fall through
	case GSM48_REJECT_IMSI_UNKNOWN_IN_HLR:
	case GSM48_REJECT_IMSI_UNKNOWN_IN_HLR:
	case GSM48_REJECT_ILLEGAL_ME:
		/* SIM invalid */
		subscr->sim_valid = 0;

		/* TMSI and LAI invalid */
		subscr->lai_valid = 0;
		subscr->tmsi_valid = 0;

		/* key is invalid */
		subscr->key_seq = 7;

		/* update status */
		new_sim_ustate(ms, GSM_SIM_U3_ROAMING_NA);
#ifdef TODO
		sim: delete tmsi, lai
		sim: delete key seq number
		sim: apply update state
#endif
	}

	/* send event to PLMN search process */
FAILURE if it was an attachment
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_LU_REJECT);
	if (!nmsg)
		return -ENOMEM;
	ngm = (struct gsm322_msg *)nmsg->data;
	ngm->reject = mm->lupd_rej_cause;
	gsm322_sendmsg(ms, nmsg);

	/* forbidden list */
	switch (mm->lupd_rej_cause) {
	case GSM48_REJECT_IMSI_UNKNOWN_IN_HLR:
	case GSM48_REJECT_IMSI_UNKNOWN_IN_HLR:
	case GSM48_REJECT_ILLEGAL_ME:
		break;
	case GSM48_REJECT_PLMN_NOT_ALLOWED:
		gsm322_add_forbidden_plmn(ms, subscr->lai_mcc,
			subscr->lai_mnc);
		break;
	case GSM48_REJECT_LOC_NOT_ALLOWED:
	case GSM48_REJECT_ROAMING_NOT_ALLOWED:
		gsm322_add_forbidden_la(ms, subscr->lai_mcc, subscr->lai_mnc,
			subscr->lai_lac, mm->lupd_rej_cause);
		break;
	default:
		/* 4.4.4.9 continue with failure handling */
		return gsm48_mm_loc_upd_failed(ms);
	}

	/* return to IDLE, case 13 is also handled there */
	return gsm48_mm_return_idle(ms);
}

/* delay a location update */
static int gsm48_mm_loc_upd_delay(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;

	/* 4.2.2 in case we are not idle, periodic update is started when
	 * becomming idle. (Because the timer expired.)
	 */
	return 0;
}

/* process failues as described in the lower part of 4.4.4.9 */
static int gsm48_mm_loc_upd_failed(struct osmocom_ms *ms)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;

	/* stop location update timer, if running */
	stop_mm_t3210(mm);

	if (subscr->ustate == GSM_SIM_U1_UPDATED
	 && subscr->sim_mcc = s->mcc
	 && subscr->sim_mnc = s->mnc
	 && subscr->sim_lac = s->lac
	 && mm->lupd_attempt < 4) {
		/* start update retry timer */
		start_mm_t3211(ms);

		/* return to MM IDLE */
		return gsm48_mm_return_idle(ms);
	}

	/* TMSI and LAI invalid */
	subscr->lai_valid = 0;
	subscr->tmsi_valid = 0;

	/* key is invalid */
	subscr->key_seq = 7;

	/* update status */
	new_sim_ustate(ms, GSM_SIM_U2_NOT_UPDATED);

#ifdef TODO
	sim: delete tmsi, lai
	sim: delete key seq number
	sim: set update status
#endif

	/* start update retry timer */
	if (mm->lupd_attempt < 4)
		start_mm_t3211(ms);

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms);
}

/* abort a location update due to radio failure or release */
static int gsm48_mm_rel_loc_upd_abort(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *)msg->data;

	/* random access failure, but not two successive failures */
	if (rrh->cause == GSM_MM_CAUSE_RA_FAILURE && !mm->lupd_ra_failure) {
		mm->lupd_ra_failure = 1;

		/* start RA failure timer */
		start_mm_t3213(ms);

		return 0;
	}

	/* RA was successfull */
	mm->lupd_ra_failure = 0;

	/* continue with failure handling */
	return gsm48_mm_loc_upd_failed(ms);
}

/* location update has timed out */
static int gsm48_mm_loc_upd_timeout(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm == &ms->mmlayer;

	/* abort RR connection */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_REQ);
	if (!nmsg)
		return -ENOMEM;
	rrh = (struct gsm48_rr_hdr *) msgb_put(nmsg, sizeof(*rrh));
	rrh->cause = GSM48_RR_CAUSE_ABNORMAL_TIMER;
	gsm48_rr_downmsg(ms, msg);

	/* continue with failure handling */
	return gsm48_mm_loc_upd_failed(ms);
}

/*
 * process handlers for MM connections
 */

/* cm reestablish request message from upper layer */
static int gsm48_mm_tx_cm_serv_req(struct osmocom_ms *ms, int rr_prim, uint8_t cause, uint8_t cm_serv)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;
	struct gsm48_service_request *sr;
	struct uint8_t *cm2lv;

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));
	sr = (struct gsm48_service_request *)msgb_put(msg, sizeof(*sr) - 1);
	*cm2lv = (uint8_t *)&sr->classmark;

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_CM_SERV_REQ;

	/* type and key */
	serv_req->cm_service_type = cm_serv;
	serv_req->cypher_key_seq = subscr->key_seq;
	/* classmark 2 */
	cm2lv[0] = sizeof(struct gsm48_classmark2);
	gsm_rr_enc_cm2(ms, (struct gsm48_classmark2 *)(cm2lv + 1));
	/* MI */
	if (!subsr->sim_valid) /* have no SIM ? */
		gsm48_encode_mi(nmsg, subscr, GSM_MI_TYPE_IMEI);
	else if (subsr->tmsi_valid) /* have TSMI ? */
		gsm48_encode_mi(nmsg, subscr, GSM_MI_TYPE_TSMI);
	else
		gsm48_encode_mi(nmsg, subscr, GSM_MI_TYPE_IMSI);
	/* prio is optional for eMLPP */

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, rr_prim, cause);
}

/* cm service abort message from upper layer */
static int gsm48_mm_tx_cm_service_abort(struct osmocom_ms *ms)
{
	struct msgb *nmsg;
	struct gsm48_hdr *ngh;

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg)
		return -ENOMEM;
	ngh = (struct gsm48_hdr *)msgb_put(nmsg, sizeof(*ngh));

	ngh->proto_discr = GSM48_PDISC_MM;
	ngh->msg_type = GSM48_MT_MM_CM_SERV_ABORT;

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, RR_DATA_REQ, 0);
}

/* cm service acknowledge is received from lower layer */
static int gsm48_mm_rx_cm_service_ack(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return gsm48_mm_conn_go_dedic(ms);
}

/* 9.2.6 CM SERVICE REJECT message received */
static int gsm48_mm_rx_cm_service_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm48_mm_conn *conn, *conn2;
	uint8_t *abort_any;

	if (payload_len < 1) {
		DEBUGP(DMM, "Short read of cm service reject message error.\n");
		return -EINVAL;
	}

	/* reject cause */
	reject_cause = *gh->data;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* selection action on cause value */
	switch (reject_cause) {
	case GSM48_REJECT_IMSI_UNKNOWN_IN_VLR:
	case GSM48_REJECT_ILLEGAL_ME:
		abort_any = 1;

		/* TMSI and LAI invalid */
		subscr->lai_valid = 0;
		subscr->tmsi_valid = 0;

		/* key is invalid */
		subscr->key_seq = 7;

		/* update status */
		new_sim_ustate(ms, GSM_SIM_U2_NOT_UPDATED);

#ifdef TODO
		sim: delete tmsi, lai
		sim: delete key seq number
		sim: set update status
#endif

		/* change to WAIT_NETWORK_CMD state impied by abort_any == 1 */

		if (reject_cause == GSM48_REJECT_ILLEGAL_ME)
			subscr->sim_valid = 0;

		break;
	default:
		/* state implied by the number of remaining connections */
	}

	/* release MM connection(s) */
	gsm48_mm_release_mm_conn(ms, abort_any, 16, 0);

	/* state depends on the existance of remaining MM connections */
	if (llist_empty(&mm->mm_conn))
		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);
	else
		new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);
}

/* initiate an MM connection 4.5.1.1
 *
 * this function is called when:
 * - no RR connection exists
 * - an RR connection exists, but this is the first MM connection
 * - an RR connection exists, and there are already MM connection(s)
 */
static int gsm48_mm_init_mm(struct osmocom_ms *ms, struct mgsb *msg, int rr_prim)
{
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	int emergency = 0;
	uint8_t cause, cm_serv, proto;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;
	struct gsm48_mm_conn *conn, conn_found = NULL;

	/* reset loc. upd. counter on CM service request */
	ms->lupd_attempt = 0;

	/* find if there is already a pending connection */
	llist_for_each(conn, &mm->mm_conn, list) {
		if (conn->state == GSM48_MMXX_ST_CONN_PEND) {
			conn_found = conn;
			break;
		}
	}

	/* if pending connection */
	if (conn_found) {
		cause = 17;
		reject:
		nmsg = NULL;
		switch(msg_type) {
		case GSM48_MMCC_EST_REQ:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_REL_IND, mmh->ref, mmh->trans_id);
			break;
		case GSM48_MMSS_EST_REQ:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSS_REL_IND, mmh->ref, mmh->trans_id);
			break;
		case GSM48_MMSMS_EST_REQ:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSMS_REL_IND, mmh->ref, mmh->trans_id);
			break;
		}
		if (!nmsg)
			return -ENOMEM;
		nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
		nmmh->cause = cause;
		gsm48_mmxx_upmsg(ms, nmsg);

		return -EBUSY;
	}
	/* in case of an emergency setup */
	if (msg_type == MMCC_EST_REQ && mmh->emergency)
		emergency = 1;

	/* if sim is not updated */
	if (!emergency && subscr->ustate != GSM_SIM_U1_UPDATED) {
		cause = 21;
		goto reject;
	}

	/* current MM idle state
	 * (implicitly IDLE, otherwise this function is not called)
	 */
	switch (mm->substate) {
	case GSM48_MM_SST_NORMAL_SERVICE:
	case GSM48_MM_SST_PLMN_SEARCH_NORMAL:
		break; /* allow when normal */
	case GSM48_MM_SST_ATTEMPT_UPDATE:
		/* store mm request if attempting to update */
		if (!emergency) {
			cause = 21;
			goto reject;
			/* Some day implement delay and start loc upd. */
		}
		break;
	}
	default:
		/* reject if not emergency */
		if (!emergency) {
			cause = 21;
			goto reject;
		}
		break;
	}

	/* set cause, service, proto */
	switch(msg_type) {
	case GSM48_MMCC_EST_REQ:
		if (emergency) {
			cause = RR_EST_CAUSE_EMERGENCY;
			cm_serv = GSM48_CMSERV_EMERGENCY;
		} else {
			cause = RR_EST_CAUSE_ORIG_TCHF;
			cm_serv = GSM48_CMSERV_MO_CALL_PACKET;
		}
		proto = GSM48_PDISC_CC;
		break;
	case GSM48_MMSS_EST_REQ:
		cause = RR_EST_CAUSE_OTHER_SDCCH;
		cm_serv = GSM48_CMSERV_SUP_SERV;
		proto = GSM48_PDISC_SS;
		break;
	case GSM48_MMSMS_EST_REQ:
		cause = RR_EST_CAUSE_OTHER_SDCCH;
		cm_serv = GSM48_CMSERV_SMS;
		proto = GSM48_PDISC_SMS;
		break;
	}

	/* create MM connection instance */
	conn = mm_conn_new(mm, proto, mmh->trans_id, mmh->ref);
	if (!conn)
		return -ENOMEM;

	new_conn_state(conn, GSM48_MMXX_ST_CONN_PEND);

	/* send CM SERVICE REQUEST */
	if (rr_prim)
		return gsm48_mm_tx_cm_serv_req(ms, rr_prim, cause, cm_serv);
	else
		return 0;
}

/* 4.5.1.1 a) MM connection request triggers RR connection */
static int gsm48_mm_init_mm_no_rr(struct osmocom_ms *ms, struct mgsb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mm;
	int rc;

	/* start MM connection by requesting RR connection */
	rc = gsm48_mm_init_mm(ms, msg, GSM48_RR_EST_REQ);
	if (rc)
		return rc;

	new_mm_state(mm, GSM48_MM_ST_WAIT_RR_CONN_MM_CON, 0);

	return 0;
}

/* 4.5.1.1 a) RR is esablised during mm connection, wait for CM accepted */
static int gsm48_mm_est_mm_con(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* 4.5.1.7 if there is no more MM connection */
	if (llist_empty(&mm->mm_conn)) {
		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

		/* send abort */
		return gsm48_mm_tx_cm_service_abort(ms);
	}

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_OUT_MM_CONN, 0);
}

/* 4.5.1.1 b) MM connection request on existing RR connection */
static int gsm48_mm_init_mm_first(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int rc;

	/* start MM connection by sending data */
	rc = gsm48_mm_init_mm(ms, msg, GSM48_RR_DATA_REQ);
	if (rc)
		return rc;

	/* stop "RR connection release not allowed" timer */
	stop_mm_t3241(mm);

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_OUT_MM_CONN, 0);
}

/* 4.5.1.1 b) another MM connection request on existing RR connection */
static int gsm48_mm_init_mm_more(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int rc;

	/* start MM connection by sending data */
	rc = gsm48_mm_init_mm(ms, msg, GSM48_RR_DATA_REQ);
	if (rc)
		return rc;

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_ADD_OUT_MM_CONN, 0);
}

/* 4.5.1.1 b) delay on WAIT FOR NETWORK COMMAND state */
static int gsm48_mm_init_mm_wait(struct osmocom_ms *ms, struct msgb *msg)
{
	/* reject */
	gsm48_mm_init_mm_reject(ms, msg);
#if 0
	this requires handling when leaving this state...

	struct gsm48_mmlayer *mm = &ms->mmlayer;
	int rc;

	/* just create the MM connection in pending state */
	rc = gsm48_mm_init_mm(ms, msg, 0);
	if (rc)
		return rc;

	/* start MM connection timer */
	start_mm_t3230(mm);

	new_mm_state(mm, GSM48_MM_ST_WAIT_ADD_OUT_MM_CONN, 0);
#endif
}

/* initiate an mm connection other cases */
static int gsm48_mm_init_mm_reject(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;

	/* reject */
	nmsg = NULL;
	switch(msg_type) {
	case GSM48_MMCC_EST_REQ:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_REL_REQ, mmh->ref, mmh->trans_id);
		break;
	case GSM48_MMSS_EST_REQ:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSS_REL_REQ, mmh->ref, mmh->trans_id);
		break;
	case GSM48_MMSMS_EST_REQ:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSMS_REL_REQ, mmh->ref, mmh->trans_id);
		break;
	}
	if (!nmsg)
		return -ENOMEM;
	nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
	nmmh->cause = 17;
	gsm48_mmxx_upmsg(ms, nmsg);

	return 0;
}

/* accepting pending connection, got dedicated mode
 *
 * this function is called:
 * - when ciphering command is received
 * - when cm service is accepted 
 */
static int gsm48_mm_conn_go_dedic(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_conn *conn, *conn_found = NULL;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;

	/* the first and only pending connection is the recent requested */
	llist_for_each(conn, &mm->mm_conn, list) {
		if (conn->state == GSM48_MMXX_ST_CONN_PEND) {
			conn_found = conn;
			break;
		}
	}

	/* if no pending connection (anymore) */
	if (!conn_found) {
		return 0;
	}

	new_conn_state(conn, GSM48_MMXX_ST_DEDICATED);

	/* send establishment confirm */
	nmsg = NULL;
	switch(conn_found->protocol) {
	case GSM48_PDISC_CC:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_EST_CNF, conn->ref, conn->trans_id);
		break;
	case GSM48_PDISC_SS:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSS_EST_CNF, conn->ref, conn->trans_id);
		break;
	case GSM48_PDISC_SMS:
		nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMSMS_EST_CNF, conn->ref, conn->trans_id);
		break;
	}
	if (!nmsg)
		return -ENOMEM;
	nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
	nmmh->cause = 17;
	gsm48_mmxx_upmsg(ms, nmsg);
}

/* a RR-SYNC-IND is received during MM connection establishment */
static int gsm48_mm_sync_ind_wait(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	return gsm48_mm_conn_go_dedic(ms);
}

/* a RR-SYNC-IND is received during MM connection active */
static int gsm48_mm_sync_ind_active(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_conn *conn;
	struct msgb *nmsg;
	struct gsm48_mmxx_hdr *nmmh;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* broadcast all MMCC connection(s) */
	llist_for_each(conn, &mm->mm_conn, list) {
		/* send MMCC-SYNC-IND */
		nmsg = NULL;
		switch(conn_found->protocol) {
		case GSM48_PDISC_CC:
			nmsg = gsm48_mmxx_msgb_alloc(GSM48_MMCC_SYNC_IND, conn->ref, conn->trans_id);
			break;
		}
		if (!nmsg)
			continue; /* skip if not of CC type */
		nmmh = (struct gsm48_mmxx_hdr *)nmsg->data;
		nmmh->cause = 17;
		/* copy L3 message */
		nmsg->l3h = msgb_put(msg, msgb_l3len(msg));
		memcpy(nmsg->l3h, msg->l3h, msgb_l3len(msg));
		gsm48_mmxx_upmsg(ms, nmsg);
	}

	return 0;
}

/* 4.5.1.2 RR abort is received during MM connection establishment */
static int gsm48_mm_abort_mm_con(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* stop MM connection timer */
	stop_mm_t3230(mm);

	/* release all connections */
	gsm48_mm_release_mm_conn(ms, 1, 16, 1);

	/* return to MM IDLE */
	return gsm48_mm_return_idle(ms);
}

/* 4.5.1.2 timeout is received during MM connection establishment */
static int gsm48_mm_timeout_mm_con(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	/* release pending connection */
	gsm48_mm_release_mm_conn(ms, 0, 102, 0);

	/* state depends on the existance of remaining MM connections */
	if (llist_empty(&mm->mm_conn)) {
		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);
	} else
		new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return 0;
}

/* respond to paging */
static int gsm48_mm_est(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);
}

/* send CM data */
static int gsm48_mm_data(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (!conn) {
		switch(msg_type & GSM48_MMXX_MASK) {
		case GSM48_MMCC_CLASS:
			mmh->msg_type = GSM48_MMCC_REL_IND;
			break;
		case GSM48_MMSS_CLASS:
			mmh->msg_type = GSM48_MMSS_REL_IND;
			break;
		case GSM48_MMSMS_CLASS:
			mmh->msg_type = GSM48_MMSMS_REL_IND;
			break;
		}
		mmh->cause = 31;

		return gsm48_mmxx_upmsg(ms, nmsg);
	}
	
	/* pull MM header */
	msgb_pull(msg, sizeof(struct gsm48_mmxx_hdr));

	/* push RR header and send down */
	return gsm48_mm_to_rr(ms, nmsg, RR_DATA_REQ, 0);
}

/* release of MM connection (active state) */
static int gsm48_mm_release_active(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	/* state depends on the existance of remaining MM connections */
	if (llist_empty(&mm->mm_conn)) {
		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);
	} else
		new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);

	return 0;
}

/* release of MM connection (wait for additional state) */
static int gsm48_mm_release_wait_add(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	return 0;
}

/* release of MM connection (wait for active state) */
static int gsm48_mm_release_wait_active(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	/* 4.5.1.7 if there is no MM connection during wait for active state */
	if (llist_empty(&mm->mm_conn)) {
		/* start RR release timer */
		start_mm_t3240(mm);

		new_mm_state(mm, GSM48_MM_ST_WAIT_NETWORK_CMD, 0);

		/* send abort */
		return gsm48_mm_tx_cm_service_abort(ms);
	}

	return 0;
}

/* release of MM connection (wait for RR state) */
static int gsm48_mm_release_wait_active(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;

	/* get connection, if not exist (anymore), release */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		mm_conn_free(conn);

	/* later, if RR connection is established, the CM SERIVE ABORT
	 * message will be sent
	 */
	return 0;
}

/* abort RR connection (due to T3240) */
static int gsm48_mm_abort_rr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rr_hdr *rrh;

	/* send abort to RR */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_REQ);
	if (!nmsg)
		return -ENOMEM;
	rrh = (struct gsm48_rr_hdr *) msgb_put(nmsg, sizeof(*rrh));
	rrh->cause = GSM48_RR_CAUSE_ABNORMAL_TIMER;
	gsm48_rr_downmsg(ms, msg);

	/* return to MM IDLE / No SIM */
	gsm48_mm_return_idle(ms);

	return 0;
}

/*
 * other processes
 */

/* RR is released in other states */
static int gsm48_mm_rel_other(struct osmocom_ms *ms, struct msgb *msg)
{
	return gsm48_mm_return_idle(ms);
}

/*
 * state machines
 */

/* state trasitions for MMxx-SAP messages from upper layers */
static struct downstate {
	u_int32_t	states;
	u_int32_t	substates;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} downstatelist[] = {
	/* 4.2.2.1 Normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.2.2.2 Attempt to update */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.2.2.3 Limited service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.2.2.4 No IMSI */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NO_IMSI),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.2.2.5 PLMN search, normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 MMSS_EST_REQ, gsm48_mm_init_mm_no_rr},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 MMSMS_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.2.2.4 PLMN search */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH),
	 MMCC_EST_REQ, gsm48_mm_init_mm_no_rr},
	/* 4.5.1.1 MM Connection (EST) */
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
	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES,
	 MMCC_EST_REQ, gsm48_mm_init_mm_wait},
	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES,
	 MMSS_EST_REQ, gsm48_mm_init_mm_wait},
	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD), ALL_STATES,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_wait},
	{ALL_STATES, ALL_STATES,
	 MMCC_EST_REQ, gsm48_mm_init_mm_reject},
	{ALL_STATES, ALL_STATES,
	 MMSS_EST_REQ, gsm48_mm_init_mm_reject},
	{ALL_STATES, ALL_STATES,
	 MMSMS_EST_REQ, gsm48_mm_init_mm_reject},
	/* 4.5.2.1 MM Connection (DATA) */
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 MMCC_DATA_REQ, gsm48_mm_data},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 MMSS_DATA_REQ, gsm48_mm_data},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 MMSMS_DATA_REQ, gsm48_mm_data},
	/* 4.5.2.1 MM Connection (REL) */
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 MMCC_REL_REQ, gsm48_mm_release_active},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 MMSS_REL_REQ, gsm48_mm_release_active},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), ALL_STATES,
	 MMSMS_REL_REQ, gsm48_mm_release_active},
	{SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 MMCC_REL_REQ, gsm48_mm_release_wait_add},
	{SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 MMSS_REL_REQ, gsm48_mm_release_wait_add},
	{SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), ALL_STATES,
	 MMSMS_REL_REQ, gsm48_mm_release_wait_add},
	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN), ALL_STATES,
	 MMCC_REL_REQ, gsm48_mm_release_wait_active},
	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN), ALL_STATES,
	 MMSS_REL_REQ, gsm48_mm_release_wait_active},
	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN), ALL_STATES,
	 MMSMS_REL_REQ, gsm48_mm_release_wait_active},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), ALL_STATES,
	 MMCC_REL_REQ, gsm48_mm_release_wait_rr},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), ALL_STATES,
	 MMSS_REL_REQ, gsm48_mm_release_wait_rr},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), ALL_STATES,
	 MMSMS_REL_REQ, gsm48_mm_release_wait_rr},
};

#define DOWNSLLEN \
	(sizeof(downstatelist) / sizeof(struct downstate))

int gsm48_mmxx_downmsg(struct osmocom_ms *ms, struct msgb msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mmxx_hdr *mmh = (struct gsm48_mmxx_hdr *)msg->data;
	int msg_type = mmh->msg_type;

	/* keep up to date with the transaction ID */
	conn = mm_conn_by_ref(mm, mmh->ref);
	if (conn)
		conn->trans_id = mmh->trans_id;

	DEBUGP(DMM, "(ms %s) Received '%s' event in state %s", ms->name,
		get_mmevent_name(msg_type), gsm48_mm_state_names[mm->state]);
	if (mm->state == GSM48_MM_ST_MM_ILDE)
		DEBUGP(DMM, " substate %s", gsm48_mm_substate_names[mm->substate]);
	DEBUGP(DMM, "\n");

	/* Find function for current state and message */
	for (i = 0; i < DOWNSLLEN; i++)
		if ((msg_type == downstatelist[i].type)
		 && ((1 << mm->state) & downstatelist[i].states)
		 && ((1 << mm->substate) & downstatelist[i].substates))
			break;
	if (i == DOWNSLLEN) {
		DEBUGP(DMM, "Message unhandled at this state.\n");
		free_msgb(msg);
		return 0;
	}

	rc = downstatelist[i].rout(ms, msg);

	if (downstatelist[i].rout != gsm48_mm_data)
		free_msgb(msg);

	return rc;
}

/* state trasitions for radio ressource messages (lower layer) */
static struct rrdatastate {
	u_int32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} rrdatastatelist[] = {
	/* paging */
	{SBIT(GSM48_MM_IDLE),
	 GSM48_RR_EST_IND, gsm48_mm_est},
	/* imsi detach */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 */
	 GSM48_RR_EST_CNF, gsm48_mm_imsi_detach_sent},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 (unsuc.) */
	 GSM48_RR_REL_IND, gsm48_mm_imsi_detach_end},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_IMSI_D), /* 4.3.4.4 (lost) */
	 GSM48_RR_ABORT_IND, gsm48_mm_imsi_detach_end},
	/* location update */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_LUPD), /* 4.4.4.1 */
	 GSM48_RR_EST_CNF, gsm48_mm_est_loc_upd},
	{SBIT(GSM48_MM_ST_LOC_UPD_INIT) |
	 SBIT(GSM48_MM_ST_WAIT_RR_CONN_LUPD), /* 4.4.4.9 */
	 GSM48_RR_REL_IND, gsm48_mm_rel_loc_upd_abort},
	{SBIT(GSM48_MM_ST_LOC_UPD_INIT) |
	 SBIT(GSM48_MM_ST_WAIT_RR_CONN_LUPD), /* 4.4.4.9 */
	 GSM48_RR_ABORT_IND, gsm48_mm_rel_loc_upd_abort},
	{SBIT(GSM48_MM_ST_LOC_UPD_REJ), /* 4.4.4.7 */
	 GSM48_RR_REL_IND, gsm48_mm_rel_loc_upd_rej},
	{SBIT(GSM48_MM_ST_LOC_UPD_REJ), /* 4.4.4.7 */
	 GSM48_RR_ABORT_IND, gsm48_mm_rel_loc_upd_rej},
	/* MM connection (EST) */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON), /* 4.5.1.1 */
	 GSM48_RR_EST_CNF, gsm48_mm_est_mm_con},
	/* MM connection (DATA) */
	{ALL_STATES,
	 GSM48_RR_DATA_IND, gsm48_mm_data_ind},
	/* MM connection (SYNC) */
	{SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* 4.5.1.1 */
	 GSM48_RR_SYNC_IND, gsm48_mm_sync_ind_wait},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE),
	 GSM48_RR_SYNC_IND, gsm48_mm_sync_ind_active},
	/* MM connection (REL/ABORT) */
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON) |
	 SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* 4.5.1.2 */
	 GSM48_RR_REL_IND, gsm48_mm_abort_mm_con},
	{SBIT(GSM48_MM_ST_WAIT_RR_CONN_MM_CON) |
	 SBIT(GSM48_MM_ST_WAIT_OUT_MM_CONN) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* 4.5.1.2 */
	 GSM48_RR_ABORT_IND, gsm48_mm_abort_mm_con},
	/* MM connection (REL/ABORT with re-establishment possibility) */
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE), /* not supported */
	 GSM48_RR_REL_IND, gsm48_mm_abort_mm_con},
	{SBIT(GSM48_MM_ST_MM_CONN_ACTIVE) |
	 SBIT(GSM48_MM_ST_WAIT_ADD_OUT_MM_CON), /* not supported */
	 GSM48_RR_ABORT_IND, gsm48_mm_abort_mm_con},
	/* other */
	{ALL_STATES,
	 GSM48_RR_REL_IND, gsm48_mm_rel_other},
	{ALL_STATES,
	 GSM48_RR_ABORT_IND, gsm48_mm_rel_other},
};

#define RRDATASLLEN \
	(sizeof(rrdatastatelist) / sizeof(struct rrdatastate))

static int gsm48_rcv_rr(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *)msg->data;
	int msg_type = rrh->msg_type;

	DEBUGP(DMM, "(ms %s) Received '%s' from RR in state %s\n", ms->name,
		gsm48_rr_msg_name(msg_type), gsm48_mm_state_names[mm->state]);

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
	
	if (rrdatastatelist[i].rout != gsm48_mm_data_ind)
		free_msgb(msg);

	return rc;
}

/* state trasitions for mobile managemnt messages (lower layer) */
static struct mmdatastate {
	u_int32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
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
	{GSM48_MM_ST_LOC_UPD_INIT, /* 4.4.4.6 */
	 GSM48_MT_MM_LOC_UPD_ACCEPT, gsm48_mm_rx_loc_upd_acc},
	{GSM48_MM_ST_LOC_UPD_INIT, /* 4.4.4.7 */
	 GSM48_MT_MM_LOC_UPD_REJECT, gsm48_mm_rx_loc_upd_rej},
	{ALL_STATES, /* 4.5.1.1 */
	 GSM48_MT_MM_CM_SERV_ACK, gsm48_mm_rx_cm_service_ack},
	{ALL_STATES, /* 4.5.1.1 */
	 GSM48_MT_MM_CM_SERV_REJ, gsm48_mm_rx_cm_service_rej},
};

#define DMMATASLLEN \
	(sizeof(mmdatastatelist) / sizeof(struct mmdatastate))

static int gsm48_mm_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	uint8_t pdisc = gh->proto_discr & 0x0f;
	uint8_t msg_type = gh->msg_type & 0xbf;
	struct gsm48_cc_hdr *mmh;
	int msg_supported = 0; /* determine, if message is supported at all */
	int rr_data = -1, rr_est = -1; /* no prim set */

	/* pull the RR header */
	msgb_pull(msg, sizeof(struct gsm48_rr_hdr));

	/* create transaction (if not exists) and push message */
	switch (pdisc) {
	case GSM48_PDISC_CC:
		rr_prim = GSM48_MMCC_DATA_IND;
		rr_est = GSM48_MMCC_EST_IND;
		break;
	case GSM48_PDISC_SS:
		rr_prim = GSM48_MMSS_DATA_IND;
		rr_est = GSM48_MMSS_EST_IND;
		break;
	case GSM48_PDISC_SMS:
		rr_prim = GSM48_MMSMS_DATA_IND;
		rr_est = GSM48_MMSMS_EST_IND;
		break;
	}
	if (rr_prim != -1) {
		uint8_t trans_id = ((gh->proto_discr & 0xf0) ^ 0x80) >> 4; /* flip */

		/* find transaction, if any */
		conn = mm_conn_by_id(mm, pdisc, trans_id);

		/* create MM connection instance */
		if (!conn) {
			conn = mm_conn_new(mm, pdisc, trans_id,
				mm_conn_new_ref++);
			rr_prim = rr_est;
		}
		if (!conn)
			return -ENOMEM;

		/* push new header */
		msgb_push(msg, sizeof(struct gsm48_mmxx_hdr));
		mmh = (struct gsm48_mmxx_hdr *)msg->data;
		mmh->msg_type = rr_prim;
		mmh->ref = conn->ref;

		/* go MM CONN ACTIVE state */
		if (mm->state == GSM48_MM_ST_WAIT_NETWORK_CMD
		 || mm->state == GSM48_MM_ST_RR_CONN_RELEASE_NA) {
			/* stop RR release timer */
			stop_mm_t3240(mm);

			/* stop "RR connection release not allowed" timer */
			stop_mm_t3241(mm);

			new_mm_state(mm, GSM48_MM_ST_MM_CONN_ACTIVE, 0);
		}
	}

	/* forward message */
	switch (pdisc) {
	case GSM48_PDISC_MM:
		uint8_t skip_ind = (gh->proto_discr & 0xf0) >> 4;

		/* ignore if skip indicator is not B'0000' */
		if (skip_ind)
			return 0;
		break; /* follow the selection proceedure below */

	case GSM48_PDISC_CC:
		return gsm48_cc_upmsg(ms, msg);

#if 0
	case GSM48_PDISC_SS:
		return gsm48_sms_upmsg(ms, msg);

	case GSM48_PDISC_SMS:
		return gsm48_sms_upmsg(ms, msg);
#endif

	default:
		DEBUGP(DRR, "Protocol type 0x%02x unsupported.\n", pdisc);
		free_msgb(msg);
		return gsm48_mm_tx_mm_status(ms,
			GSM48_REJECT_MSG_TYPE_NOT_IMPLEMENTED);
	}

	DEBUGP(DMM, "(ms %s) Received '%s' in MM state %s\n", ms->name,
		gsm48_mm_msg_name(msg_type), gsm48_mm_state_names[mm->state]);

	gsm48_stop_mm_timer(mm, 0x3212); /* 4.4.2 */

	/* 11.2 re-start pending RR release timer */
	if (bsc_timer_pending(mm->t3240)) {
		stop_mm_t3240(mm);
		start_mm_t3240(mm);
	}

	/* find function for current state and message */
	for (i = 0; i < MMDATASLLEN; i++)
		if (msg_type == mmdatastatelist[i].type)
			msg_supported = 1;
		if ((msg_type == mmdatastatelist[i].type)
		 && ((1 << mm->state) & mmdatastatelist[i].states))
			break;
	if (i == MMDATASLLEN) {
		free_msgb(msg);
		if (msg_supported) {
			DEBUGP(DMM, "Message unhandled at this state.\n");
			return gsm48_mm_tx_mm_status(ms,
				GSM48_REJECT_MSG_TYPE_NOT_COMPATIBLE);
		} else {
			DEBUGP(DMM, "Message not supported.\n");
			return gsm48_mm_tx_mm_status(ms,
				GSM48_REJECT_MSG_TYPE_NOT_IMPLEMENTED);
		}
	}

	rc = mmdatastatelist[i].rout(ms, msg);

	free_msgb(msg);

	return rc;
}

/* state trasitions for mobile management events */
static struct eventstate {
	u_int32_t	states;
	u_int32_t	substates;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} eventstatelist[] = {
	/* 4.2.2.1 Normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_NEW_LAI, gsm48_mm_loc_upd_normal},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_upd},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_upd},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_periodic},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_start},
	/* 4.2.2.2 Attempt to update */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_upd},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_upd},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_NEW_LAI, gsm48_mm_loc_upd_normal},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_ATTEMPT_UPDATE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_periodic},
	/* 4.2.2.3 Limited service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_LIMITED_SERVICE),
	 GSM48_MM_EVENT_NEW_LAI, gsm48_mm_loc_upd_normal},
	/* 4.2.2.4 No IMSI */
	/* 4.2.2.5 PLMN search, normal service */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_TIMEOUT_T3211, gsm48_mm_loc_upd},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_TIMEOUT_T3213, gsm48_mm_loc_upd},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay},
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH_NORMAL),
	 GSM48_MM_EVENT_IMSI_DETACH, gsm48_mm_imsi_detach_start},
	/* 4.2.2.4 PLMN search */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_PLMN_SEARCH),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay},
	/* No cell available */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NO_CELL_AVAILABLE),
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay},
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
	/* location update in other cases */
	{ALL_STATES - SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3212, gsm48_mm_loc_upd_delay},
	{ALL_STATES - SBIT(GSM48_MM_ST_MM_IDLE), ALL_STATES,
	 GSM48_MM_EVENT_TIMEOUT_T3210, gsm48_mm_loc_upd_timeout},
	/* SYSINFO event */
	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_SYSINFO, gsm48_mm_sysinfo},
	/* IMSI attach */
	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_IMSI_ATTACH, gsm48_mm_loc_upd_imsi},
	/* T3240 timed out */
	{SBIT(GSM48_MM_ST_WAIT_NETWORK_CMD) |
	 SBIT(GSM48_MM_ST_LOC_UPD_REJ), ALL_STATES, /* 4.4.4.8 */
	 GSM48_MM_EVENT_TIMEOUT_T3240, gsm48_mm_abort_rr},
	/* T3230 timed out */
	{SBIT(GSM48_MM_ST_MM_IDLE), SBIT(GSM48_MM_SST_NORMAL_SERVICE),
	 GSM48_MM_EVENT_TIMEOUT_T3230, gsm48_mm_timeout_mm_con},
	/* SIM reports SRES */
	{ALL_STATES, ALL_STATES, /* 4.3.2.2 */
	 GSM48_MM_EVENT_AUTH_RESPONSE, gsm48_mm_tx_auth_rsp},
#if 0
	/* change in classmark is reported */
	{ALL_STATES, ALL_STATES,
	 GSM48_MM_EVENT_CLASSMARK_CHG, gsm48_mm_classm_chg},
#endif
};

#define EVENTSLLEN \
	(sizeof(eventstatelist) / sizeof(struct eventstate))

int gsm48_mm_event(struct osmocom_ms *ms, int msg_type, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;
	struct gsm48_mm_event *mmevent = (struct gsm48_mm_event *)msg->data;
	int msg_type = mmevent->msg_type;

	DEBUGP(DMM, "(ms %s) Received '%s' event in state %s", ms->name,
		get_mmevent_name(msg_type), gsm48_mm_state_names[mm->state]);
	if (mm->state == GSM48_MM_ST_MM_ILDE)
		DEBUGP(DMM, " substate %s", gsm48_mm_substate_names[mm->substate]);
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




the process above is complete
------------------------------------------------------------------------------
incomplete



to subscriber.c







