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
 * state transition
 */

char *rr_state_names[] = {
	"IDLE",
	"CONN PEND",
	"DEDICATED",
};

static void new_rr_state(struct gsm_rrlayer *rr, int state)
{
	if (state < 0 || state >= (sizeof(rr_state_names) / sizeof(char *)))
		return;

	if (state == GSM_RRSTATE_IDLE) {
		rr->rr_est_req = 0;
		if (rr->rr_est_msg) {
			msgb_free(rr->rr_est_msg);
			rr->rr_est_msg = NULL;
		}
	}

	DEBUGP(DRR, "new state %s -> %s\n",
		rr_state_names[rr->state], rr_state_names[state]);

	rr->state = state;
}

/*
 * timers handling
 */

static void start_rr_t3122(struct gsm_rrlayer *rr, int sec, int micro)
{
	DEBUGP(DRR, "starting T3122 with %d seconds\n", current, sec);
	rr->t3122.cb = timeout_rr_t3122;
	rr->t3122.data = rr;
	bsc_schedule_timer(&rr->t3122, sec, micro);
}

static void start_rr_t3126(struct gsm_rrlayer *rr, int sec, int micro)
{
	DEBUGP(DRR, "starting T3126 with %d seconds\n", current, sec);
	rr->t3126.cb = timeout_rr_t3126;
	rr->t3126.data = rr;
	bsc_schedule_timer(&rr->t3126, sec, micro);
}

static void stop_rr_t3122(struct gsm_rrlayer *rr)
{
	if (timer_pending(rr->t3122)) {
		DEBUGP(DRR, "stopping pending timer T3122\n");
		bsc_del_timer(&rr->t3122);
	}
	rr->t3122_running = 0;
}

static void stop_rr_t3126(struct gsm_rrlayer *rr)
{
	if (bsc_timer_pending(rr->t3126)) {
		DEBUGP(DRR, "stopping pending timer T3126\n");
		bsc_del_timer(&rr->t3126);
	}
}

static void timeout_rr_t3122(void *arg)
{
}

static void timeout_rr_t3126(void *arg)
{
	struct gsm_rrlayer *rr = arg;

	if (rr->rr_est_req) {
		struct msgb *msg = gsm48_mm_msgb_alloc();
		struct gsm_mm_hdr *mmh;

		if (!msg)
			return -ENOMEM;
		mmh = (struct gsm_mm_hdr *)msg->data;
		mmh->msg_type RR_REL_IND;
		mmh->cause = GSM_MM_CAUSE_RA_FAILURE;
		rr_rcvmsg(ms, msg);
	}

	new_rr_state(rr, GSM_RRSTATE_IDLE);
}

/*
 * status
 */

/* send rr status request */
static int gsm_rr_tx_rr_status(struct osmocom_ms *ms, uint8_t cause)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *msg = gsm48_rr_msgb_alloc();
	struct gsm48_hdr *gh;
	struct gsm48_rr_status *st;

	if (!msg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	st = (struct gsm48_rr_status *) msgb_put(msg, sizeof(*st));

	gh->proto = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_CIPH_M_COMPL;

	/* rr cause */
	st->rr_cause = cause;

	return rslms_data_req(ms, msg, 0);
}

/*
 * ciphering
 */

/* send chiperhing mode complete */
static int gsm_rr_tx_cip_mode_cpl(struct osmocom_ms *ms, uint8_t cr)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm_subscriber *subcr = ms->subscr;
	struct msgb *msg = gsm48_rr_msgb_alloc();
	struct gsm48_hdr *gh;
	u_int8_t buf[11], *ie;

	if (!msg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));

	gh->proto = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_CIPH_M_COMPL;

	/* MI */
	if (cr) {
		gsm48_generate_mid_from_imsi(ie, subscr->imei);
		ie = msgb_put(msg, 1 + buf[1]);
		memcpy(ie, buf + 1, 1 + buf[1]);
	}

	return rslms_data_req(ms, msg, 0);
}

/* receive ciphering mode command */
static int gsm_rr_rx_cip_mode_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_cip_mode_cmd *cm = (struct gsm48_cip_mode_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*cm);
	uint8_t sc, alg_id, cr;

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of CIPHERING MODE COMMAND message.\n");
		return -EINVAL;
	}

	/* cipher mode setting */
	sc = cm->sc;
	alg_id = cm->alg_id;
	/* cipher mode response */
	cr = cm->cr;

	/* 3.4.7.2 */
	if (rr->sc && sc)
		return gsm_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);

	/* change to ciphering */
	tx_ph_cipher_req(ms, sc, alg_id);
	rr->sc = sc, rr->alg_id = alg_id;

	/* response */
	return gsm_rr_tx_cip_mode_cpl(ms, cr);
}

/*
 * classmark
 */

/* encode classmark 3 */
#define ADD_BIT(a) \
{ \
	if (bit-- == 0) { \
		bit = 7; \
		buf[byte] = 0; \
	} \
	buf[byte] |= ((a) << bit); \
	if (bit == 0) \
		byte++; \
}
static int gsm_rr_enc_cm3(struct osmocom_sm *ms, uint8_t *buf, uint8_t *len)
{
	struct gsm_support *sup = ms->support;
	uint8_t bit = 0, byte = 0;

	/* spare bit */
	ADD_BIT(0)
	/* band 3 supported */
	if (sup->dcs_1800)
		ADD_BIT(1)
	else
		ADD_BIT(0)
	/* band 2 supported */
	if (sup->e_gsm || sup->r_gsm)
		ADD_BIT(1)
	else
		ADD_BIT(0)
	/* band 1 supported */
	if (sup->p_gsm && !(sup->e_gsm || sup->r_gsm))
		ADD_BIT(1)
	else
		ADD_BIT(0)
	/* a5 bits */
	if (sup->a5_7)
		ADD_BIT(1)
	else
		ADD_BIT(0)
	if (sup->a5_6)
		ADD_BIT(1)
	else
		ADD_BIT(0)
	if (sup->a5_5)
		ADD_BIT(1)
	else
		ADD_BIT(0)
	if (sup->a5_4)
		ADD_BIT(1)
	else
		ADD_BIT(0)
	/* radio capability */
	if (sup->dcs_1800 && !sup->p_gsm && !(sup->e_gsm || sup->r_gsm)) {
		/* dcs only */
		ADD_BIT(0)
		ADD_BIT(0)
		ADD_BIT(0)
		ADD_BIT(0)
		ADD_BIT((sup->dcs_capa >> 3) & 1)
		ADD_BIT((sup->dcs_capa >> 2) & 1)
		ADD_BIT((sup->dcs_capa >> 1) & 1)
		ADD_BIT(sup->dcs_capa & 1)
	} else
	if (sup->dcs_1800 && (sup->p_gsm || (sup->e_gsm || sup->r_gsm))) {
		/* dcs */
		ADD_BIT((sup->dcs_capa >> 3) & 1)
		ADD_BIT((sup->dcs_capa >> 2) & 1)
		ADD_BIT((sup->dcs_capa >> 1) & 1)
		ADD_BIT(sup->dcs_capa & 1)
		/* low band */
		ADD_BIT((sup->low_capa >> 3) & 1)
		ADD_BIT((sup->low_capa >> 2) & 1)
		ADD_BIT((sup->low_capa >> 1) & 1)
		ADD_BIT(sup->low_capa & 1)
	} else {
		/* low band only */
		ADD_BIT(0)
		ADD_BIT(0)
		ADD_BIT(0)
		ADD_BIT(0)
		ADD_BIT((sup->low_capa >> 3) & 1)
		ADD_BIT((sup->low_capa >> 2) & 1)
		ADD_BIT((sup->low_capa >> 1) & 1)
		ADD_BIT(sup->low_capa & 1)
	}
	/* r support */
	if (sup->r_gsm) {
		ADD_BIT(1)
		ADD_BIT((sup->r_capa >> 2) & 1)
		ADD_BIT((sup->r_capa >> 1) & 1)
		ADD_BIT(sup->r_capa & 1)
	} else {
		ADD_BIT(0)
	}
	/* multi slot support */
	if (sup->ms_sup) {
		ADD_BIT(1)
		ADD_BIT((sup->ms_capa >> 4) & 1)
		ADD_BIT((sup->ms_capa >> 3) & 1)
		ADD_BIT((sup->ms_capa >> 2) & 1)
		ADD_BIT((sup->ms_capa >> 1) & 1)
		ADD_BIT(sup->ms_capa & 1)
	} else {
		ADD_BIT(0)
	}
	/* ucs2 treatment */
	if (sup->ucs2_treat) {
		ADD_BIT(1)
	} else {
		ADD_BIT(0)
	}
	/* support extended measurements */
	if (sup->ext_meas) {
		ADD_BIT(1)
	} else {
		ADD_BIT(0)
	}
	/* support measurement capability */
	if (sup->meas_cap) {
		ADD_BIT(1)
		ADD_BIT((sup->sms_val >> 3) & 1)
		ADD_BIT((sup->sms_val >> 2) & 1)
		ADD_BIT((sup->sms_val >> 1) & 1)
		ADD_BIT(sup->sms_val & 1)
		ADD_BIT((sup->sm_val >> 3) & 1)
		ADD_BIT((sup->sm_val >> 2) & 1)
		ADD_BIT((sup->sm_val >> 1) & 1)
		ADD_BIT(sup->sm_val & 1)
	} else {
		ADD_BIT(0)
	}
	/* positioning method capability */
	if (sup->loc_serv) {
		ADD_BIT(1)
		ADD_BIT(sup->e_otd_ass == 1)
		ADD_BIT(sup->e_otd_based == 1)
		ADD_BIT(sup->gps_ass == 1)
		ADD_BIT(sup->gps_based == 1)
		ADD_BIT(sup->gps_conv == 1)
	} else {
		ADD_BIT(0)
	}

	/* partitial bytes will be completed */
	if (bit)
		byte++;
	*len = byte;

	return 0;
}

/* encode classmark 2 */
static int gsm_rr_enc_cm2(struct osmocom_sm *ms, struct gsm48_classmark2 *cm)
{
	struct gsm_support *sup = ms->support;

	cm->pwr_lev = sup->pwr_lev;
	cm->a5_1 = sup->a5_1;
	cm->es_ind = sup->es_ind;
	cm->rev_lev = sup->rev_lev;
	cm->fc = (sup->r_gsm || sup->e_gsm);
	cm->vgcs = sup->vgcs;
	cm->vbs = sup->vbs;
	cm->sm = sup->sms_ptp;
	cm->ss_ind = sup->ss_ind;
	cm->ps_cap = sup->ps_cap;
	cm->a5_2 = sup->a5_2;
	cm->a5_3 = sup->a5_3;
	cm->cmsp = sup->cmsp;
	cm->solsa = sup->solsa;
	cm->lcsva = sup->lcsva;
}

/* send classmark change */
static int gsm_rr_tx_cm_change(struct osmocom_ms *ms)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm_support *sup = ms->support;
	struct msgb *msg = gsm48_rr_msgb_alloc();
	struct gsm48_hdr *gh;
	struct gsm48_cm_change *cc;
	int len;
	uint8_t buf[14];

	if (!msg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	cc = (struct gsm48_cm_change *) msgb_put(msg, sizeof(*cc));

	gh->proto = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_CLSM_CHG;

	/* classmark 2 */
	cc->cm_len = sizeof(cm->cm2);
	gsm_rr_enc_cm2(ms, &cc->cm2)

	/* classmark 3 */
	if (sup->dcs_1800 || sup->e_gsm || sup->r_gsm
	 || sup->a5_7 || sup->a5_6 || sup->a5_5 || sup->a5_4
	 || sup->ms_sup
	 || sup->ucs2_treat
	 || sup->ext_meas || sup->meas_cap
	 || sup->loc_serv) {
		cm->cm2.cm3 = 1;
		buf[0] = GSM48_IE_CLASSMARK2;
		gsm_rr_enc_cm3(ms, buf + 1, &buf[0]);
	}

	return rslms_data_req(ms, msg, 0);
}

/* receiving classmark enquiry */
static int gsm_rr_rx_cm_enq(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*gh);

	/* send classmark */
	return gsm_rr_tx_cm_change(ms);
}

/*
 * random access
 */

/* send channel request burst message */
static int gsm_rr_tx_chan_req(struct osmocom_ms *ms, int cause)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *msg;
	struct gsm_mm_hdr *mmh;
	uint8_t chan_req;

	/* 3.3.1.1.2 */
	new_rr_state(rr, GSM_RRSTATE_CONN_PEND);

	/* number of retransmissions (without first transmission) */
	rr->n_chan_req = ms->si.max_retrans;

	/* generate CHAN REQ (9.1.8) */
	chan_req = random();
	switch (cause) {
	case RR_EST_CAUSE_EMERGENCY:
		/* 101xxxxx */
		chan_req &= 0x1f;
		chan_req |= 0xa0;
		DEBUGP(DRR, "CHANNEL REQUEST: %02x (Emergency call)\n", chan_req);
		break;
	case RR_EST_CAUSE_REESTAB_TCH_F:
		chan_req &= 0x1f;
		chan_req |= 0xc0;
		DEBUGP(DRR, "CHANNEL REQUEST: %02x (re-establish TCH/F)\n", chan_req);
		break;
	case RR_EST_CAUSE_REESTAB_TCH_H:
		if (ms->si.neci) {
			chan_req &= 0x03;
			chan_req |= 0x68;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (re-establish TCH/H with NECI)\n", chan_req);
		} else {
			chan_req &= 0x1f;
			chan_req |= 0xc0;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (re-establish TCH/H no NECI)\n", chan_req);
		}
		break;
	case RR_EST_CAUSE_REESTAB_2_TCH_H:
		if (ms->si.neci) {
			chan_req &= 0x03;
			chan_req |= 0x6c;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (re-establish TCH/H+TCH/H with NECI)\n", chan_req);
		} else {
			chan_req &= 0x1f;
			chan_req |= 0xc0;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (re-establish TCH/H+TCH/H no NECI)\n", chan_req);
		}
		break;
	case RR_EST_CAUSE_ANS_PAG_ANY:
		chan_req &= 0x1f;
		chan_req |= 0x80;
		DEBUGP(DRR, "CHANNEL REQUEST: %02x (PAGING Any channel)\n", chan_req);
		break;
	case RR_EST_CAUSE_ANS_PAG_SDCCH:
		chan_req &= 0x0f;
		chan_req |= 0x10;
		DEBUGP(DRR, "CHANNEL REQUEST: %02x (PAGING SDCCH)\n", chan_req);
		break;
	case RR_EST_CAUSE_ANS_PAG_TCH_F:
		/* ms supports no dual rate */
		chan_req &= 0x1f;
		chan_req |= 0x80;
		DEBUGP(DRR, "CHANNEL REQUEST: %02x (PAGING TCH/F)\n", chan_req);
		break;
	case RR_EST_CAUSE_ANS_PAG_TCH_ANY:
		/* ms supports no dual rate */
		chan_req &= 0x1f;
		chan_req |= 0x80;
		DEBUGP(DRR, "CHANNEL REQUEST: %02x (PAGING TCH/H or TCH/F)\n", chan_req);
		break;
	case RR_EST_CAUSE_ORIG_TCHF:
		/* ms supports no dual rate */
		chan_req &= 0x1f;
		chan_req |= 0xe0;
		DEBUGP(DRR, "CHANNEL REQUEST: %02x (Orig TCH/F)\n", chan_req);
		break;
	case RR_EST_CAUSE_LOC_UPD:
		if (ms->si.neci) {
			chan_req &= 0x0f;
			chan_req |= 0x00;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (Location Update with NECI)\n", chan_req);
		} else {
			chan_req &= 0x1f;
			chan_req |= 0x00;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (Location Update no NECI)\n", chan_req);
		}
		break;
	case RR_EST_CAUSE_OTHER_SDCCH:
		if (ms->si.neci) {
			chan_req &= 0x0f;
			chan_req |= 0x01;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (OHTER with NECI)\n", chan_req);
		} else {
			chan_req &= 0x1f;
			chan_req |= 0xe0;
			DEBUGP(DRR, "CHANNEL REQUEST: %02x (OTHER no NECI)\n", chan_req);
		}
		break;
	default:
		if (!rr->rr_est_req) /* no request from MM */
			return -EINVAL;

		DEBUGP(DRR, "CHANNEL REQUEST: with unknown establishment cause: %d\n", rrmsg->cause);
		msg = gsm48_mm_msgb_alloc();
		if (!msg)
			return -ENOMEM;
		mmh = (struct gsm_mm_hdr *)msg->data;
		mmh->msg_type RR_REL_IND;
		mmh->cause = GSM_MM_CAUSE_UNDEFINED;
		rr_rcvmsg(ms, msg);
		new_rr_state(rr, GSM_RRSTATE_IDLE);
		return -EINVAL;
	}

	rr->wait_assign = 1;

	/* create and send RACH msg */
	msg = msgb_alloc_headroom(20, 16, "CHAN_REQ");
	if (!msg)
		return -ENOMEM;
	*msgb_put(msg, 1) = chan_req;
	rr->chan_req = chan_req;
	t = ms->si.tx_integer;
	if (t < 8)
		t = 8;
	*msgb_put(msg, 1) = random() % t; /* delay */
	rr->cr_hist[3] = -1;
	rr->cr_hist[2] = -1;
	rr->cr_hist[1] = chan_req;

	return rslms_tx_rll_req_l3(ms, RSL_MT_RAND_ACC_REQ, chan_nr, 0, msg);
}

/* send next channel request in conn pend state */
static int gsm_rr_rand_acc_cnf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *newmsg;
	int s;

	if (!rr->n_chan_req) {
		if (!timer_pending(rr->t3126))
			start_rr_t3126(rr, GSM_T3126_MS);
		return 0;
	}
	rr->n_chan_req--;

	/* table 3.1 */
	switch(ms->si.tx_integer) {
	case 3: case 8: case 14: case 50:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 55;
		else
			s = 41;
	case 4: case 9: case 16:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 76;
		else
			s = 52;
	case 5: case 10: case 20:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 109;
		else
			s = 58;
	case 6: case 11: case 25:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 163;
		else
			s = 86;
	default:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 217;
		else
			s = 115;

	/* resend chan_req */
	newmsg = msgb_alloc_headroom(20, 16, "CHAN_REQ");
	if (!newmsg)
		return -ENOMEM;
	*msgb_put(newmsg, 1) = rr->chan_req;
	*msgb_put(newmsg, 1) = (random() % ms->si.tx_integer) + s; /* delay */
	rr->cr_hist[3] = rr->cr_hist[2];
	rr->cr_hist[2] = rr->cr_hist[1];
	rr->cr_hist[1] = chan_req;
	return rslms_tx_rll_req_l3(ms, RSL_MT_RAND_ACC_REQ, chan_nr, 0, newmsg);
}

/*
 * paging
 */

/* paging channel request */
static int gsm_rr_chan2cause[4] = {
	RR_EST_CAUSE_ANS_PAG_ANY,
	RR_EST_CAUSE_ANS_PAG_SDCCH,
	RR_EST_CAUSE_ANS_PAG_TCH_F,
	RR_EST_CAUSE_ANS_PAG_TCH_ANY
};

/* given LV of mobile identity is checked agains ms */
static int gsm_match_mi(struct osmocom_ms *ms, u_int8_t mi)
{
	char imsi[16];
	u_int32_t tmsi;

	if (mi[0] < 1)
		return 0;
	mi_type = mi[1] & GSM_MI_TYPE_MASK;
	switch (mi_type) {
	case GSM_MI_TYPE_TMSI:
		if (mi[0] < 5)
			return;
		memcpy(&tmsi, mi+2, 4);
		if (ms->subscr.tmsi == ntohl(tmsi)
		 && ms->subscr.tmsi_valid)
			return 1;
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_mi_to_string(imsi, sizeof(imsi), mi + 1, mi[0]);
		if (!strcmp(imsi, ms->subscr.imsi))
			return 1;
		break;
	default:
		DEBUGP(DRR, "paging with unsupported MI type %d.\n", mi_type);
	}

	return 0;
}

/* paging request 1 message */
static int gsm_rr_rx_pag_req_1(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_rr_paging1 *pa = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*pa);
	int chan_first, chan_second;
	uint8_t mi;

	/* 3.3.1.1.2: ignore paging while establishing */
	if (rr->state != GSM_RRSTATE_IDLE)
		return 0;

	if (payload_len < 2) {
		short:
		DEBUGP(DRR, "Short read of paging request 1 message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_first = pa->cneed1;
	chan_second = pa->cneed2;
	/* first MI */
	mi = pa->data + 1;
	if (payload_len < mi[0] + 1)
		goto short;
	if (gsm_match_mi(ms, mi) > 0)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_first]);
	/* second MI */
	payload_len -= mi[0] + 1;
	mi = pa->data + mi[0] + 1;
	if (payload_len < 2)
		return 0;
	if (mi[0] != GSM48_IE_MOBILE_ID)
		return 0;
	if (payload_len < mi[1] + 2)
		goto short;
	if (gsm_match_mi(ms, mi + 1) > 0)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_second]);

	return 0;
}

/* paging request 2 message */
static int gsm_rr_rx_pag_req_2(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_rr_paging2 *pa = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*pa);
	u_int32_t tmsi;
	int chan_first, chan_second, chan_third;

	/* 3.3.1.1.2: ignore paging while establishing */
	if (rr->state != GSM_RRSTATE_IDLE)
		return 0;

	if (payload_len < 0) {
		short:
		DEBUGP(DRR, "Short read of paging request 2 message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_first = pa->cneed1;
	chan_second = pa->cneed2;
	/* first MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi1)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_first]);
	/* second MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi2)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_second]);
	/* third MI */
	mi = pa->data;
	if (payload_len < 2)
		return 0;
	if (mi[0] != GSM48_IE_MOBILE_ID)
		return 0;
	if (payload_len < mi[1] + 2 + 1) /* must include "channel needed" */
		goto short;
	chan_third = mi[mi[1] + 2] & 0x03; /* channel needed */
	if (gsm_match_mi(ms, mi + 1) > 0)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_third]);

	return 0;
}

/* paging request 3 message */
static int gsm_rr_rx_pag_req_3(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_rr_paging3 *pa = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*pa);
	u_int32_t tmsi;
	int chan_first, chan_second, chan_third, chan_fourth;

	/* 3.3.1.1.2: ignore paging while establishing */
	if (rr->state != GSM_RRSTATE_IDLE)
		return 0;

	if (payload_len < 0) { /* must include "channel needed", part of *pa */
		short:
		DEBUGP(DRR, "Short read of paging request 3 message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_first = pa->cneed1;
	chan_second = pa->cneed2;
	chan_third = pa->cneed3;
	chan_fourth = pa->cneed4;
	/* first MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi1)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_first]);
	/* second MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi2)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_second]);
	/* thrid MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi3)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_third]);
	/* fourth MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi4)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_fourth]);

	return 0;
}

/*
 * (immediate) assignment
 */

/* match request reference agains request history */
static int gsm_match_ra(struct osmocom_ms *ms, struct gsm48_req_ref *req)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	int i;

	for (i = 0; i < 3; i++) {
		if (rr->cr_hist[i] >= 0
		 && ref->ra == rr->cr_hist[i]) {
		 	// todo: match timeslot
			return 1;
		}
	}

	return 0;
}

/* transmit assignment complete after establishing link */
static int gsm_rr_tx_ass_cpl(struct osmocom_ms *ms, uint8_t cause)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *msg = gsm48_rr_msgb_alloc();
	struct gsm48_hdr *gh;
	struct gsm48_ass_cpl *ac;

	if (!msg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	ac = (struct gsm48_ass_cpl *) msgb_put(msg, sizeof(*ac));

	gh->proto = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_ASS_COMPL;

	/* RR_CAUSE */
	ac->rr_cause = cause;

	return rslms_data_req(ms, msg, 0);
}

/* transmit failure to old link */
static int gsm_rr_tx_ass_fail(struct osmocom_ms *ms, uint8_t cause)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *msg = gsm48_rr_msgb_alloc();
	struct gsm48_hdr *gh;
	struct gsm48_ass_fail *ac;

	if (!msg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	af = (struct gsm48_ass_fail *) msgb_put(msg, sizeof(*af));

	gh->proto = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_ASS_COMPL;

	/* RR_CAUSE */
	af->rr_cause = cause;

	return rslms_data_req(ms, msg, 0);
}

/* receive immediate assignment */
static int gsm_rr_rx_imm_ass(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_imm_ass *ia = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*ia);

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM_RRSTATE_CONN_PEND || !rr->wait_assign)
		return 0;

	if (payload_len < 1 /* mobile allocation IE must be included */
	 || *gh->data + 1 > payload_len) { /* short read of IE */
		DEBUGP(DRR, "Short read of immediate assignment message.\n");
		return -EINVAL;
	}
	if (*gh->data > 8) {
		DEBUGP(DRR, "moble allocation in immediate assignment too large.\n");
		return -EINVAL;
	}

	/* request ref */
	if (gsm_match_ra(ms, ia->req_ref)) {
		/* channel description */
		memcpy(rr->chan_desc, ia->chan_desc, 3);
		/* timing advance */
		rr->timing_advance = ia->timing_advance;
		/* mobile allocation */
		memcpy(rr->mobile_alloc_lv, gh->data, *gh->data + 1);
		rr->wait_assing = 0;
		return gsm_rr_dl_est(ms);
	}

	return 0;
}

/* receive immediate assignment extended */
static int gsm_rr_rx_imm_ass_ext(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_imm_ass_ext *ia = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*ia);

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM_RRSTATE_CONN_PEND || !rr->wait_assign)
		return 0;

	if (payload_len < 1 /* mobile allocation IE must be included */
	 || *gh->data + 1 > payload_len) { /* short read of IE */
		DEBUGP(DRR, "Short read of immediate assignment extended message.\n");
		return -EINVAL;
	}
	if (*gh->data > 4) {
		DEBUGP(DRR, "moble allocation in immediate assignment extended too large.\n");
		return -EINVAL;
	}

	/* request ref 1 */
	if (gsm_match_ra(ms, ia->req_ref1)) {
		/* channel description */
		memcpy(rr->chan_desc, ia->chan_desc1, 3);
		/* timing advance */
		rr->timing_advance = ia->timing_advance1;
		/* mobile allocation */
		memcpy(rr->mobile_alloc_lv, gh->data, *gh->data + 1);
		rr->wait_assing = 0;
		return gsm_rr_dl_est(ms);
	}
	/* request ref 1 */
	if (gsm_match_ra(ms, ia->req_ref2)) {
		/* channel description */
		memcpy(rr->chan_desc, ia->chan_desc2, 3);
		/* timing advance */
		rr->timing_advance = ia->timing_advance2;
		/* mobile allocation */
		memcpy(rr->mobile_alloc_lv, gh->data, *gh->data + 1);
		rr->wait_assing = 0;
		return gsm_rr_dl_est(ms);
	}

	return 0;
}

/* receive immediate assignment reject */
static int gsm_rr_rx_imm_ass_rej(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_imm_ass_rej *ia = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*ia);
	int i;
	struct gsm48_req_ref *req_ref;
	uint8_t t3122_value;

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM_RRSTATE_CONN_PEND || !rr->wait_assign)
		return 0;

	if (payload_len < 0) {
		short:
		DEBUGP(DRR, "Short read of immediate assignment reject message.\n");
		return -EINVAL;
	}

	for (i = 0; i < 4; i++) {
		/* request reference */
		req_ref = (struct gsm48_req_ref *)(((uint8_t *)&ia->req_ref1) + i * 4);
		if (gsm_match_ra(ms, req_ref)) {
			/* wait indication */
			t3122_value = ((uint8_t *)&ia->wait_ind1) + i * 4;
			if (t3122_value)
				start_rr_t3122(rr, t3122_value, 0);
			/* start timer 3126 if not already */
			if (!timer_pending(rr->t3126))
				start_rr_t3126(rr, GSM_T3126_MS);
			/* stop assignmnet requests */
			rr->n_chan_req = 0;

			/* wait until timer 3126 expires, then release
			 * or wait for channel assignment */
			return 0;
		}
	}

	return 0;
}

/* receive additional assignment */
static int gsm_rr_rx_add_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_add_ass *aa = (struct gsm48_add_ass *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*aa);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of ADDITIONAL ASSIGNMENT message.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &rsl_att_tlvdef, aa->data, payload_len, 0, 0);

	return gsm_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
}

/*
 * measturement reports
 */

static int gsm_rr_tx_meas_rep(struct osmocom_ms *ms)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm_rr_meas *meas = &rr->meas;
	struct msgb *msg = gsm48_rr_msgb_alloc();
	struct gsm48_hdr *gh;
	struct gsm48_meas_res *mr;

	if (!msg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
	mr = (struct gsm48_meas_res *) msgb_put(msg, sizeof(*mr));

	gh->proto = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_MEAS_RES;

	/* measurement results */
	mr->rxlev_full = meas->rxlev_full;
	mr->rxlev_sub = meas->rxlev_sub;
	mr->rxqual_full = meas->rxqual_full;
	mr->rxqual_sub = meas->rxqual_sub;
	mr->dtx = meas->dtx;
	mr->ba = meas->ba;
	mr->meas_valid = meas->meas_valid;
	if (meas->ncell_na) {
		/* no results for serving cells */
		mr->no_n_hi = 1;
		mr->no_n_lo = 3;
	} else {
		mr->no_n_hi = meas->count >> 2;
		mr->no_n_lo = meas->count & 3;
	}
	rxlev_nc1 = meas->rxlev_nc[0];
	rxlev_nc2_hi = meas->rxlev_nc[1] >> 1;
	rxlev_nc2_lo = meas->rxlev_nc[1] & 1;
	rxlev_nc3_hi = meas->rxlev_nc[2] >> 2;
	rxlev_nc3_lo = meas->rxlev_nc[2] & 3;
	rxlev_nc4_hi = meas->rxlev_nc[3] >> 3;
	rxlev_nc4_lo = meas->rxlev_nc[3] & 7;
	rxlev_nc5_hi = meas->rxlev_nc[4] >> 4;
	rxlev_nc5_lo = meas->rxlev_nc[4] & 15;
	rxlev_nc6_hi = meas->rxlev_nc[5] >> 5;
	rxlev_nc6_lo = meas->rxlev_nc[5] & 31;
	bsic_nc1_hi = meas->bsic_nc[0] >> 3;
	bsic_nc1_lo = meas->bsic_nc[0] & 7;
	bsic_nc2_hi = meas->bsic_nc[1] >> 4;
	bsic_nc2_lo = meas->bsic_nc[1] & 15;
	bsic_nc3_hi = meas->bsic_nc[2] >> 5;
	bsic_nc3_lo = meas->bsic_nc[2] & 31;
	bsic_nc4 = meas->bsic_nc[3];
	bsic_nc5 = meas->bsic_nc[4];
	bsic_nc6 = meas->bsic_nc[5];
	bcch_f_nc1 = meas->bcch_f_nc[0];
	bcch_f_nc2 = meas->bcch_f_nc[1];
	bcch_f_nc3 = meas->bcch_f_nc[2];
	bcch_f_nc4 = meas->bcch_f_nc[3];
	bcch_f_nc5_hi = meas->bcch_f_nc[4] >> 1;
	bcch_f_nc5_lo = meas->bcch_f_nc[4] & 1;
	bcch_f_nc6_hi = meas->bcch_f_nc[5] >> 2;
	bcch_f_nc6_lo = meas->bcch_f_nc[5] & 3;

	//todo return rslms_data_req(ms, msg, 0);
}

/*
 * link establishment and release
 */

/* activate link and send establish request */
static int gsm_rr_dl_est(struct osmocom_ms *ms)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm_subscriber *subcr = ms->subscr;
	struct msgb *msg;
	struct gsm48_hdr *gh;
	struct gsm48_pag_rsp *pa;

	/* 3.3.1.1.3.1 */
	stop_rr_t3126(rr);

	/* flush pending RACH requests */
	rr->n_chan_req = 0; // just to be safe
	msg = msgb_alloc_headroom(20, 16, "RAND_FLUSH");
	if (!msg)
		return -ENOMEM;
	rslms_tx_rll_req_l3(ms, RSL_MT_RAND_ACC_FLSH, chan_nr, 0, msg);

	/* send DL_EST_REQ */
	if (rr->rr_est_msg) {
		/* use queued message */
		msg = rr->rr_est_msg;
		rr->rr_est_msg = 0;
	} else {
		uint8_t mi[11];

		/* create paging response */
		msg = gsm48_rr_msgb_alloc();
		if (!msg)
			return -ENOMEM;
		gh = (struct gsm48_hdr *) msgb_put(msg, sizeof(*gh));
		pr = (struct gsm48_pag_rsp *) msgb_put(msg, sizeof(*pr));
		/* key sequence */
		if (subscr->key_valid)
			pr->key_seq = subscr->key_seq;
		else
			pr->key_seq = 7;
		/* classmark 2 */
		cc->cm_len = sizeof(cm->cm2);
		gsm_rr_enc_cm2(ms, &cc->cm2)
		/* mobile identity */
		if (ms->subscr.tmsi_valid) {
			gsm48_generate_mid_from_tmsi(mi, subscr->tmsi);
		} else if (subscr->imsi[0])
			gsm48_generate_mid_from_imsi(mi, subscr->imsi);
		else {
			mi[1] = 1;
			mi[2] = 0xf0 | GSM_MI_TYPE_NONE;
		}
		msgb_put(msg, 1 + mi[1]);
		memcpy(cm->data, mi + 1, 1 + mi[1]);
	}

	/* activate channel */
	tx_ph_dm_est_req(ms, arfcn, rr->chan_desc.chan_nr);

	/* start establishmnet */
	return rslms_tx_rll_req_l3(ms, RSL_MT_EST_REQ, rr->chan_desc.chan_nr, 0, msg);
}

/* the link is established */
static int gsm_rr_estab_cnf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct msgb *newmsg;
	struct gsm_mm_hdr *newmmh;

	/* if MM has releases before confirm, we start release */
	if (rr->state == GSM_RRSTATE_IDLE) {
		/* release message */
		newmsg = gsm48_rr_msgb_alloc();
		if (!newmsg)
			return -ENOMEM;
		/* start release */
		return rslms_tx_rll_req_l3(ms, RSL_MT_REL_REQ, 0, 0, newmsg);
	}

	/* 3.3.1.1.4 */
	new_rr_state(rr, GSM_RRSTATE_DEDICATED);

	/* send confirm to upper layer */
	newmsg = gsm48_mm_msgb_alloc();
	if (!newmsg)
		return -ENOMEM;
	newmmh = (struct gsm_mm_hdr *)newmsg->data;
	newmmh->msg_type = (rr->rr_est_req) ? RR_EST_CNF : RR_EST_IND;
	return rr_rcvmsg(ms, newmsg);
}

/* the link is released */
static int gsm_rr_rel_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	/* deactivate channel */
	tx_ph_dm_rel_req(ms, arfcn, rr->chan_desc.chan_nr);

	/* do nothing, because we aleady IDLE
	 * or we received the rel cnf of the last connection
	 * while already requesting a new one (CONN PEND)
	 */

	return 0;
}

/*
 * radio ressource requests 
 */

/* establish request for dedicated mode */
static int gsm_rr_est_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm_mm_hdr *mmh = msgb->data;
	struct gsm48_hdr *gh = msgb_l3(msg);

	/* 3.3.1.1.3.2 */
	if (timer_pending(rr->t3122)) {
		if (rrmsg->cause != RR_EST_CAUSE_EMERGENCY) {
			struct msgb *newmsg;
			struct gsm_mm_hdr *newmmh;

			newmsg = gsm48_mm_msgb_alloc();
			if (!newmsg)
				return -ENOMEM;
			newmmh = (struct gsm_mm_hdr *)newmsg->data;
			newmmh->msg_type RR_REL_IND;
			newmmh->cause = GSM_MM_CAUSE_T3122_PEND;
			return rr_rcvmsg(ms, newmsg);
		} else
			stop_rr_t3122(rr);
	}

	/* 3.3.1.1.1 */
	if (rrmsg->cause != RR_EST_CAUSE_EMERGENCY) {
		if (!(ms->access_class & ms->si.access_class)) {
			reject:
			if (!ms->opt.access_class_override) {
				struct msgb *newmsg;
				struct gsm_mm_hdr *newmmh;

				newmsg = gsm48_mm_msgb_alloc();
				if (!newmsg)
					return -ENOMEM;
				newmmh = (struct gsm_mm_hdr *)newmsg->data;
				newmmh->msg_type RR_REL_IND;
				newmmh->cause = GSM_MM_CAUSE_NOT_AUTHORIZED;
				return rr_rcvmsg(ms, newmsg);
			}
		}
	} else {
		if (!(ms->access_class & ms->si.access_class)
		 && !ms->si.emergency)
		 	goto reject;
	}

	/* requested by RR */
	rr->rr_est_req = 1;

	/* clone and store REQUEST message */
	if (!gh) {
		printf("Error, missing l3 message\n");
		return -EINVAL;
	}
	rr->rr_est_msg = msgb_alloc_headroom(256, 16, "EST_REQ");
	if (!rr->rr_est_msg)
		return -ENOMEM;
	memcpy(msgb_put(rr_est_msg, msgb_l3len(msg)),
		msgb_l3(msg), msgb_l3len(msg));

	/* request channel */
	return gsm_rr_tx_chan_req(ms, mmh->cause);
}

/* 3.4.2 transfer data in dedicated mode */
static int gsm_rr_data_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_dl dlmsg;

	if (rr->state != GSM_RRSTATE_DEDICATED) {
		msgb_free(msg)
		return -EINVAL;
	}
	
	/* pull header */
	msgb_pull(msg, sizeof(struct gsm_mm_hdr));
	/* forward message */
	return rslms_tx_rll_req_l3(ms, RSL_MT_DATA_REQ, chan_nr, 0, msg);
}

/*
 * data indications from data link
 */

/* 3.4.2 data from layer 2 to RR and upper layer*/
static int gsm_rr_data_ind(struct osmocom_ms *ms, struct msbg *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	u_int8_t pdisc = gh->proto_discr & 0x0f;

	if (pdisc == GSM48_PDISC_RR) {
		int rc = -EINVAL;

		switch(gh->msg_type) {
		case GSM48_MT_RR_ADD_ASS:
			rc = gsm_rr_rx_add_ass(ms, msg);
			break;
		case GSM48_MT_RR_ASS_CMD:
			rc = gsm_rr_rx_ass_cmd(ms, msg);
			break;
		case GSM48_MT_RR_CIP_MODE_CMD:
			rc = gsm_rr_rx_cip_mode_cmd(ms, msg);
			break;
		case GSM48_MT_RR_CLSM_ENQ:
			rc = gsm_rr_rx_cm_enq(ms, msg);
			break;
		case GSM48_MT_RR_HANDO_CMD:
			rc = gsm_rr_rx_hando_cmd(ms, msg);
			break;
		case GSM48_MT_RR_FREQ_REDEF:
			rc = gsm_rr_rx_freq_redef(ms, msg);
			break;
		default:
			DEBUGP(DRR, "Message type 0x%02x unknown.\n", gh->msg_type);
		}

		free_msgb(msg);
		return rc;
	}

	/* push header */
	msgb_push(msg, sizeof(struct gsm_mm_hdr));
	mmh = (struct gsm_mm_hdr *)msg->data;
	mmh->msg_type = RR_DATA_IND;
	/* forward message */
	return rr_rcvmsg(ms, msg);
}

/* unit data from layer 2 to RR layer */
static int gsm_rr_unit_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);

	switch (gh->msg_type) {
	case GSM48_MT_RR_PAG_REQ_1:
		return gsm_rr_rx_pag_req_1(ms, dlmsg->msg);
	case GSM48_MT_RR_PAG_REQ_2:
		return gsm_rr_rx_pag_req_2(ms, dlmsg->msg);
	case GSM48_MT_RR_PAG_REQ_3:
		return gsm_rr_rx_pag_req_3(ms, dlmsg->msg);
	case GSM48_MT_RR_IMM_ASS:
		return gsm_rr_rx_imm_ass(ms, dlmsg->msg);
	case GSM48_MT_RR_IMM_ASS_EXT:
		return gsm_rr_rx_imm_ass_ext(ms, dlmsg->msg);
	case GSM48_MT_RR_IMM_ASS_REJ:
		return gsm_rr_rx_imm_ass_rej(ms, dlmsg->msg);
	default:
		DEBUGP(DRR, "Message type 0x%02x unknown.\n", gh->msg_type);
		return -EINVAL;
	}
}



complete
-------------------------------------------------------------------------------
uncomplete



todo:

add support structure
initialize support structure

queue messages (rslms_data_req) if channel changes

flush rach msg in all cases: during sending, after its done, and when aborted
stop timers on abort
debugging. (wenn dies todo erledigt ist, bitte in den anderen code moven)
wird beim abbruch immer der gepufferte cm-service-request entfernt, auch beim verschicken?:
measurement reports
todo rr_sync_ind when receiving ciph, re ass, channel mode modify

todo change procedures, release procedure

during procedures, like "channel assignment" or "handover", rr requests must be queued
they must be dequeued when complete
they queue must be flushed when rr fails

#include <osmocore/protocol/gsm_04_08.h>
#include <osmocore/msgb.h>
#include <osmocore/gsm48.h>

static struct rr_names {
	char *name;
	int value;
} rr_names[] = {
	{ "RR_EST_REQ",		RR_EST_REQ },
	{ "RR_EST_IND",		RR_EST_IND },
	{ "RR_EST_CNF",		RR_EST_CNF },
	{ "RR_REL_IND",		RR_REL_IND },
	{ "RR_SYNC_IND",	RR_SYNC_IND },
	{ "RR_DATA_REQ",	RR_DATA_REQ },
	{ "RR_DATA_IND",	RR_DATA_IND },
	{ "RR_UNIT_DATA_IND",	RR_UNIT_DATA_IND },
	{ "RR_ABORT_REQ",	RR_ABORT_REQ },
	{ "RR_ABORT_IND",	RR_ABORT_IND },
	{ "RR_ACT_REQ",		RR_ACT_REQ },

	{NULL, 0}
};

char *get_rr_name(int value)
{
	int i;

	for (i = 0; rr_names[i].name; i++) {
		if (rr_names[i].value == value)
			return rr_names[i].name;
	}

	return "RR_Unknown";
}

static int rr_rcvmsg(struct osmocom_ms *ms,
			int msg_type, struct gsm_mncc *rrmsg)
{
	struct msgb *msg;

	DEBUGP(DRR, "(MS %s) Sending '%s' to MM.\n", ms->name,
		get_rr_name(msg_type));

	rrmsg->msg_type = msg_type;
	
	msg = msgb_alloc(sizeof(struct gsm_rr), "RR");
	if (!msg)
		return -ENOMEM;
	memcpy(msg->data, rrmsg, sizeof(struct gsm_rr));
	msgb_enqueue(&ms->rr.upqueue, msg);

	return 0;
}

static int gsm_rr_abort_req(struct osmocom_ms *ms, struct gsm_rr *rrmsg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	stop_rr_t3126(rr);
	if (rr->state == GSM_RRSTATE_DEDICATED) {
		struct gsm_dl dlmsg;

		memset(&dlmsg, 0, sizeof(dlmsg));
		return gsm_send_dl(ms, DL_RELEASE_REQ, dlmsg);
	}
	new_rr_state(rr, GSM_RRSTATE_IDLE);
}

static int gsm_rr_act_req(struct osmocom_ms *ms, struct gsm_rr *rrmsg)
{
}

/* state trasitions for radio ressource messages (upper layer) */
static struct rrdownstate {
	u_int32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct gsm_dl *rrmsg);
} rrdownstatelist[] = {
	{SBIT(GSM_RRSTATE_IDLE), /* 3.3.1.1 */
	 RR_EST_REQ, gsm_rr_est_req},
	{SBIT(GSM_RRSTATE_DEDICATED), /* 3.4.2 */
	 RR_DATA_REQ, gsm_rr_data_req},
	{SBIT(GSM_RRSTATE_CONN_PEND) | SBIT(GSM_RRSTATE_DEDICATED),
	 RR_ABORT_REQ, gsm_rr_abort_req},
	{SBIT(GSM_RRSTATE_DEDICATED),
	 RR_ACT_REQ, gsm_rr_act_req},
};

#define RRDOWNSLLEN \
	(sizeof(rrdownstatelist) / sizeof(struct rrdownstate))

static int gsm_send_rr(struct osmocom_ms *ms, struct gsm_rr *msg)
{
	struct gsm_mm_hdr *mmh = msgb->data;
	int msg_type = mmh->msg_type;

	DEBUGP(DRR, "(ms %s) Sending '%s' to DL in state %s\n", ms->name,
		gsm0408_rr_msg_names[msg_type], mm_state_names[mm->state]);

	/* find function for current state and message */
	for (i = 0; i < RRDOWNSLLEN; i++)
		if ((msg_type == rrdownstatelist[i].type)
		 && ((1 << mm->state) & rrdownstatelist[i].states))
			break;
	if (i == RRDOWNSLLEN) {
		DEBUGP(DRR, "Message unhandled at this state.\n");
		return 0;
	}

	rc = rrdownstatelist[i].rout(ms, dlmsg);

	/* free msgb uless it is forwarded */
	if (rrdownstatelist[i].rout != gsm_rr_data_req)
		free_msgb(msg);

	return rc;
}

remove the rest
	/* channel description */
	rsl_dec_chan_nr(aa->chan_desc.chan_nr, &ch_type, &ch_subch, &ch_ts);
	h = aa->chan_desc.h0.h;
	if (h)
		rsl_dec_chan_h1(&aa->chan_desc, &tsc, &maio, &hsn);
	else
		rsl_dec_chan_h0(&aa->chan_desc, &tsc, &arfcn);
	/* mobile allocation */
	if (h) {
		if (!TLVP_PRESENT(&tp, GSM48_IE_MOBILE_ALLOC))
			return gsm_rr_tx_rr_status(ms, ...);
		gsm48_decode_mobile_alloc(&ma,
			TLVP_VAL(&tp, GSM48_IE_MOBILE_ALLOC)-1);
	}
	/* starting time */
	if (TLVP_PRESENT(&tp, GSM48_IE_START_TIME)) {
		gsm48_decode_start_time(&frame,
			TLVP_VAL(&tp, GSM48_IE_START_TIME)-1);
	}

}

/* memcopy of LV of given IE from tlv_parsed structure */
static int tlv_copy(void *dest, int dest_len, struct tlv_parsed *tp, uint8_t ie)
{
	uint8_t *lv = dest;
	uint8_t len;

	if (dest_len < 1)
		return -EINVAL;
	lv[0] = 0;

	if (!TLVP_PRESENT(tp, ie))
		return 0;

	len = TLVP_LEN(tp, ie);
	if (len < 1)
		return 0;
	if (len + 1 > dest_len)
		return -ENOMEM;

	memcpy(dest, TLVP_VAL(tp, ie) - 1, len + 1);
	return 0;
}

static int gsm_rr_rx_ass_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_ass_cmd *ac = (struct gsm48_ass_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*ac);
	struct tlv_parsed tp;

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of ASSIGNMENT COMMAND message.\n");
		return -EINVAL;
	}
	tlv_parse(&tp, &rsl_att_tlvdef, ac->data, payload_len, 0, 0);

	/* channel description */
	memcpy(&cd_ater, &ac->chan_desc, sizeof(chan_desc));
	/* power command */
	power_command = ac->power_command;
	/* frequency list, after timer */
	tlv_copy(&fl_after, sizeof(fl_after), &tp, GSM48_IE_FRQLIST_AFTER);
	/* cell channel description */
	ccd becomes lv
	tlv_copy(&ccd, sizeof(ccd), &tp, GSM48_IE_CELL_CH_DESC);
	/* multislot allocation */
	tlv_copy(&multia, sizeof(ma), &tp, GSM48_IE_MSLOT_DESC);
	/* channel mode */
	chanmode bevomes lv
	tlv_copy(&chanmode, sizeof(chanmode), &tp, GSM48_IE_CHANMODE_1);
	/* mobile allocation, after time */
	tlv_copy(&moba_after, sizeof(moba_after), &tp, GSM48_IE_MOB_AL_AFTER);
	/* starting time */
	start bevomes lv
	tlv_copy(&start, sizeof(start), &tp, GSM_IE_START_TIME);
	/* frequency list, before time */
	tlv_copy(&fl_before, sizeof(fl_before), &tp, GSM48_IE_FRQLIST_BEFORE);
	/* channel description, before time */
	tlv_copy(&cd_before, sizeof(cd_before), &tp, GSM48_IE_CHDES_1_BEFORE);
	/* frequency channel sequence, before time */
	tlv_copy(&fcs_before, sizeof(fcs_before), &tp, GSM48_IE_FRQSEQ_BEFORE);
	/* mobile allocation, before time */
	tlv_copy(&moba_before, sizeof(moba_before), &tp, GSM48_IE_MOB_AL_BEFORE);
	/* cipher mode setting */
	if (TLVP_PRESENT(&tp, GSM48_IE_CIP_MODE_SET))
		cipher = *TLVP_VAL(&tp, GSM48_IE_CIP_MODE_SET);
	else
		cipher = 0;

	if (no CA)
		send assignment failure (ms, RR_CAUSE_NO_CELL_ALL_AVAIL);
	
	if (not supported)
		send assignment failure (ms, RR_CAUSE_CHAN_MODE_UNACCEPT);

	if (freq not supported)
		send assignment failure (ms, RR_CAUSE_FREQ_NOT_IMPLEMENTED);

	send dl suspend req

	change into special assignment suspension state
	 - queue messages during this state
	 - flush/send when leaving this state
}

static int gsm_rr_rx_hando_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*gh);

	parsing

	send dl suspend req

	change into special handover suspension state
	 - queue messages during this state
	 - flush/send when leaving this state
}

static int gsm_rr_rx_hando_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*gh);

static int gsm_rr_estab_cnf_dedicated(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	if (special "channel assignment" state) {
		if (resume to last channel flag is NOT set) {
			gsm_rr_tx_ass_cpl(ms, cause);
			flush queued radio ressource messages

			return 0;
		} else {
			gsm_rr_tx_ass_fail(ms, RR_CAUSE_PROTO_ERR_UNSPEC);
			return 0;
		}
	}
}

static int gsm_rr_connect_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
}

static int gsm_rr_rel_ind(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
}

static int gsm_rr_rel_cnf_dedicated(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	if (special assignment suspension state
	 || special handover suspension state) {
		send DL EST REQ

	}
	if (special handover suspension state) {
		send HANDOVER ACCESS via DL_RANDOM_ACCESS_REQ
		rr->hando_acc_left = 3;
	}
	return 0;
}

static int gsm_rr_mdl_error_ind(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	if (special assignment suspension state
	 || special handover suspension state) {
		if (resume to last channel flag is NOT set) {
			set resume to the last channel flag;
			send reconnect req with last channel;

			return 0;
		}
	}

	send abort ind to upper layer
}

/* state trasitions for link layer messages (lower layer) */
static struct dldatastate {
	u_int32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct gsm_dl *dlmsg);
} dldatastatelist[] = {
	{SBIT(GSM_RRSTATE_IDLE) | SBIT(GSM_RRSTATE_CONN_PEND),
	 DL_UNIT_DATA_IND, gsm_rr_unit_data_ind},
	{SBIT(GSM_RRSTATE_DEDICATED), /* 3.4.2 */
	 DL_DATA_IND, gsm_rr_data_ind},
	{SBIT(GSM_RRSTATE_IDLE) | SBIT(GSM_RRSTATE_CONN_PEND),
	 DL_ESTABLISH_CNF, gsm_rr_estab_cnf},
	{SBIT(GSM_RRSTATE_DEDICATED),
	 DL_ESTABLISH_CNF, gsm_rr_estab_cnf_dedicated},
	{SBIT(GSM_RRSTATE),
	 DL_CONNECT_CNF, gsm_rr_connect_cnf},
	{SBIT(GSM_RRSTATE),
	 DL_RELEASE_IND, gsm_rr_rel_ind},
	{SBIT(GSM_RRSTATE_IDLE) | SBIT(GSM_RRSTATE_CONN_PENDING),
	 DL_RELEASE_CNF, gsm_rr_rel_cnf},
	{SBIT(GSM_RRSTATE_DEDICATED),
	 DL_RELEASE_CNF, gsm_rr_rel_cnf_dedicated},
	{SBIT(GSM_RRSTATE_CONN_PEND), /* 3.3.1.1.2 */
	 DL_RANDOM_ACCESS_CNF, gsm_rr_rand_acc_cnf},
	{SBIT(GSM_RRSTATE_DEDICATED),
	 DL_RANDOM_ACCESS_CNF, gsm_rr_rand_acc_cnf_dedicated},
	{SBIT(GSM_RRSTATE),
	 MDL_ERROR_IND, gsm_rr_mdl_error_ind},
};

#define DLDATASLLEN \
	(sizeof(dldatastatelist) / sizeof(struct dldatastate))

static int gsm_rcv_dl(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	int msg_type = dlmsg->msg_type;

	DEBUGP(DRR, "(ms %s) Received '%s' from DL in state %s\n", ms->name,
		gsm0408_dl_msg_names[msg_type], mm_state_names[mm->state]);

	/* find function for current state and message */
	for (i = 0; i < DLDATASLLEN; i++)
		if ((msg_type == dldatastatelist[i].type)
		 && ((1 << mm->state) & dldatastatelist[i].states))
			break;
	if (i == DLDATASLLEN) {
		DEBUGP(DRR, "Message unhandled at this state.\n");
		return 0;
	}

	rc = dldatastatelist[i].rout(ms, dlmsg);

	/* free msgb uless it is forwarded */
	if (dldatastatelist[i].rout != gsm_rr_data_ind)
		free_msgb(msg);

	return rc;
}

static void timeout_rr_t3124(void *arg)
{
	struct gsm_rrlayer *rr = arg;

	todo
}

struct gsm_rrlayer *gsm_new_rr(struct osmocom_ms *ms)
{
	struct gsm_rrlayer *rr;

	rr = calloc(1, sizeof(struct gsm_rrlayer));
	if (!rr)
		return NULL;
	rr->ms = ms;

	return;
}

void gsm_destroy_rr(struct gsm_rrlayer *rr)
{
	stop_rr_t3122(rr);
	stop_rr_t3126(rr);
alle timer gestoppt?:
todo stop t3122 when cell change

	memset(rr, 0, sizeof(struct gsm_rrlayer));
	free(rr);

	return;
}

/* send next channel request in dedicated state */
static int gsm_rr_rand_acc_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *newmsg;
	int s;

	if (!rr->hando_susp_state) {
		DEBUGP(DRR, "Random acces confirm, but not in handover state.\n");
		return 0;
	}

	/* send up to four handover access bursts */
	if (rr->hando_acc_left) {
		rr->hando_acc_left--;
		send HANDOVER ACCESS via DL_RANDOM_ACCESS_REQ;
		return;
	}

	if (!timer 3124 running) {
		if (allocated channel is SDCCH)
			start_rr_t3124(rr, GSM_T3124_675);
		else
			start_rr_t3124(rr, GSM_T3124_320);
	if (!rr->n_chan_req) {
		start_rr_t3126(rr, GSM_T3126_MS);
		return 0;
	}
	rr->n_chan_req--;

	/* table 3.1 */
	switch(ms->si.tx_integer) {
	case 3: case 8: case 14: case 50:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 55;
		else
			s = 41;
	case 4: case 9: case 16:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 76;
		else
			s = 52;
	case 5: case 10: case 20:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 109;
		else
			s = 58;
	case 6: case 11: case 25:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 163;
		else
			s = 86;
	default:
		if (ms->si.bcch_type == GSM_NON_COMBINED_CCCH)
			s = 217;
		else
			s = 115;

	/* resend chan_req */
	newmsg = msgb_alloc_headroom(20, 16, "CHAN_REQ");
	if (!newmsg)
		return -ENOMEM;
	*msgb_put(newmsg, 1) = rr->chan_req;
	*msgb_put(newmsg, 1) = (random() % ms->si.tx_integer) + s; /* delay */
	rr->cr_hist[3] = rr->cr_hist[2];
	rr->cr_hist[2] = rr->cr_hist[1];
	rr->cr_hist[1] = chan_req;
	return rslms_tx_rll_req_l3(ms, RSL_MT_RAND_ACC_REQ, chan_nr, 0, newmsg);
}

