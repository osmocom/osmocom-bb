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
		struct msgb *msg;

		/* free establish message, if any */
		rr->rr_est_req = 0;
		if (rr->rr_est_msg) {
			msgb_free(rr->rr_est_msg);
			rr->rr_est_msg = NULL;
		}
		/* free all pending messages */
		while((msg = msgb_dequeue(&rr->downqueue)))
			free_msgb(msg);
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
		return gsm_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
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

/* Encode  "Classmark 3" (10.5.2.20) */
static int gsm_rr_enc_cm3(struct osmocom_sm *ms, uint8_t *buf, uint8_t *len)
{
	struct gsm_support *sup = ms->support;
	struct bitvec bv;

	memset(&bv, 0, sizeof(bv));
	bv.data = data;
	bv.data_len = 12;

	/* spare bit */
	bitvec_set_bit(&bv, 0);
	/* band 3 supported */
	if (sup->dcs_1800)
		bitvec_set_bit(&bv, ONE);
	else
		bitvec_set_bit(&bv, ZERO);
	/* band 2 supported */
	if (sup->e_gsm || sup->r_gsm)
		bitvec_set_bit(&bv, ONE);
	else
		bitvec_set_bit(&bv, ZERO);
	/* band 1 supported */
	if (sup->p_gsm && !(sup->e_gsm || sup->r_gsm))
		bitvec_set_bit(&bv, ONE);
	else
		bitvec_set_bit(&bv, ZERO);
	/* a5 bits */
	if (sup->a5_7)
		bitvec_set_bit(&bv, ONE);
	else
		bitvec_set_bit(&bv, ZERO);
	if (sup->a5_6)
		bitvec_set_bit(&bv, ONE);
	else
		bitvec_set_bit(&bv, ZERO);
	if (sup->a5_5)
		bitvec_set_bit(&bv, ONE);
	else
		bitvec_set_bit(&bv, ZERO);
	if (sup->a5_4)
		bitvec_set_bit(&bv, ONE);
	else
		bitvec_set_bit(&bv, ZERO);
	/* radio capability */
	if (sup->dcs_1800 && !sup->p_gsm && !(sup->e_gsm || sup->r_gsm)) {
		/* dcs only */
		bitvec_set_uint(&bv, 0, 4);
		bitvec_set_uint(&bv, sup->dcs_capa, 4);
	} else
	if (sup->dcs_1800 && (sup->p_gsm || (sup->e_gsm || sup->r_gsm))) {
		/* dcs */
		bitvec_set_uint(&bv, sup->dcs_capa, 4);
		/* low band */
		bitvec_set_uint(&bv, sup->low_capa, 4);
	} else {
		/* low band only */
		bitvec_set_uint(&bv, 0, 4);
		bitvec_set_uint(&bv, sup->low_capa, 4);
	}
	/* r support */
	if (sup->r_gsm) {
		bitvec_set_bit(&bv, ONE);
		bitvec_set_uint(&bv, sup->r_capa, 3);
	} else {
		bitvec_set_bit(&bv, ZERO);
	}
	/* multi slot support */
	if (sup->ms_sup) {
		bitvec_set_bit(&bv, ONE);
		bitvec_set_uint(&bv, sup->ms_capa, 5);
	} else {
		bitvec_set_bit(&bv, ZERO);
	}
	/* ucs2 treatment */
	if (sup->ucs2_treat) {
		bitvec_set_bit(&bv, ONE);
	} else {
		bitvec_set_bit(&bv, ZERO);
	}
	/* support extended measurements */
	if (sup->ext_meas) {
		bitvec_set_bit(&bv, ONE);
	} else {
		bitvec_set_bit(&bv, ZERO);
	}
	/* support measurement capability */
	if (sup->meas_cap) {
		bitvec_set_bit(&bv, ONE);
		bitvec_set_uint(&bv, sup->sms_val, 4);
		bitvec_set_uint(&bv, sup->sm_val, 4);
	} else {
		bitvec_set_bit(&bv, ZERO);
	}
	/* positioning method capability */
	if (sup->loc_serv) {
		bitvec_set_bit(&bv, ONE);
		bitvec_set_bit(&bv, sup->e_otd_ass == 1);
		bitvec_set_bit(&bv, sup->e_otd_based == 1);
		bitvec_set_bit(&bv, sup->gps_ass == 1);
		bitvec_set_bit(&bv, sup->gps_based == 1);
		bitvec_set_bit(&bv, sup->gps_conv == 1);
	} else {
		bitvec_set_bit(&bv, ZERO);
	}

	/* partitial bytes will be completed */
	*len = (bv.cur_bit + 7) >> 3;
	bitvec_spare_padding(&bv, (*len * 8) - 1);

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
		gsm_rr_enc_cm3(ms, buf + 2, &buf[1]);
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
 * system information
 */

/* decode "Cell Channel Description" (10.5.2.1b) and other frequency lists */
static int gsm48_decode_freq_list(struct gsm_sysinfo_freq *f, uint8_t *cd, uint8_t len, uint8_t mask, uint8_t frqt)
{
	int i;

	/* NOTES:
	 *
	 * The Range format uses "SMOD" computation.
	 * e.g. "n SMOD m" equals "((n - 1) % m) + 1"
	 * A cascade of multiple SMOD computations is simpified:
	 * "(n SMOD m) SMOD o" equals "(((n - 1) % m) % o) + 1"
	 *
	 * The Range format uses 16 octets of data in SYSTEM INFORMATION.
	 * When used in dedicated messages, the length can be less.
	 * In this case the ranges are decoded for all frequencies that
	 * fit in the block of given length.
	 */

	/* tabula rasa */
	for (i = 0; i < 1024; i++)
		f[i].mask &= ~frqt;

	/* 00..XXX. */
	if ((cd[0] & 0xc0 & mask) == 0x00) {
		/* Bit map 0 format */
		if (len < 16)
			return -EINVAL;
		for (i = 1; i <= 124; i++)
			if ((cd[15 - ((i-1) >> 3)] & (1 << ((i-1) & 7))))
				f[i].mask |= frqt;

		return 0;
	}

	/* only Bit map 0 format for P-GSM */
	if (ms->support.p_gsm && !ms->support.e_gsm
	 && !ms->support.r_gsm && !ms->support.dcs_1800)
	 	return 0;

	/* 10..0XX. */
	if ((cd[0] & 0xc8 & mask) == 0x80) {
		/* Range 1024 format */
		uint16_t w[17]; /* 1..16 */
		struct gsm_range_1024 *r = (struct gsm_range_1024 *)cd;

		if (len < 2)
			return -EINVAL;
		memset(w, 0, sizeof(w));
		if (r->f0)
			f[0].mask |= frqt;
		w[1] = (r->w1_hi << 8) | r->w1_lo;
		if (len >= 4)
			w[2] = (r->w2_hi << 1) | r->w2_lo;
		if (len >= 5)
			w[3] = (r->w3_hi << 2) | r->w3_lo;
		if (len >= 6)
			w[4] = (r->w4_hi << 2) | r->w4_lo;
		if (len >= 7)
			w[5] = (r->w5_hi << 2) | r->w5_lo;
		if (len >= 8)
			w[6] = (r->w6_hi << 2) | r->w6_lo;
		if (len >= 9)
			w[7] = (r->w7_hi << 2) | r->w7_lo;
		if (len >= 10)
			w[8] = (r->w8_hi << 1) | r->w8_lo;
		if (len >= 10)
			w[9] = r->w9;
		if (len >= 11)
			w[10] = r->w10;
		if (len >= 12)
			w[11] = (r->w11_hi << 6) | r->w11_lo;
		if (len >= 13)
			w[12] = (r->w12_hi << 5) | r->w12_lo;
		if (len >= 14)
			w[13] = (r->w13_hi << 4) | r->w13_lo;
		if (len >= 15)
			w[14] = (r->w14_hi << 3) | r->w14_lo;
		if (len >= 16)
			w[15] = (r->w15_hi << 2) | r->w15_lo;
		if (len >= 16)
			w[16] = r->w16;
		if (w[1])
			f[w[1]].mask |= frqt;
		if (w[2])
			f[((w[1] - 512 + w[2] - 1) % 1023) + 1].mask |= frqt;
		if (w[3])
			f[((w[1]       + w[3] - 1) % 1023) + 1].mask |= frqt;
		if (w[4])
			f[((w[1] - 512 + ((w[2] - 256 + w[4] - 1) % 511)) % 1023) + 1].mask |= frqt;
		if (w[5])
			f[((w[1]       + ((w[3] - 256 - w[5] - 1) % 511)) % 1023) + 1].mask |= frqt;
		if (w[6])
			f[((w[1] - 512 + ((w[2]       + w[6] - 1) % 511)) % 1023) + 1].mask |= frqt;
		if (w[7])
			f[((w[1]       + ((w[3]       + w[7] - 1) % 511)) % 1023) + 1].mask |= frqt;
		if (w[8])
			f[((w[1] - 512 + ((w[2] - 256 + ((w[4] - 128 + w[8] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[9])
			f[((w[1]       + ((w[3] - 256 + ((w[5] - 128 + w[9] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[10])
			f[((w[1] - 512 + ((w[2]       + ((w[6] - 128 + w[10] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[11])
			f[((w[1]       + ((w[3]       + ((w[7] - 128 + w[11] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[12])
			f[((w[1] - 512 + ((w[2] - 256 + ((w[4]       + w[12] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[13])
			f[((w[1]       + ((w[3] - 256 + ((w[5]       + w[13] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[14])
			f[((w[1] - 512 + ((w[2]       + ((w[6]       + w[14] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[15])
			f[((w[1]       + ((w[3]       + ((w[7]       + w[15] - 1) % 255)) % 511)) % 1023) + 1].mask |= frqt;
		if (w[16])
			f[((w[1] - 512 + ((w[2] - 256 + ((w[4] - 128 + ((w[8] - 64 + w[16] - 1) % 127)) % 255)) % 511)) % 1023) + 1].mask |= frqt;

		return 0;
	}
	/* 10..100. */
	if ((cd[0] & 0xce & mask) == 0x88) {
		/* Range 512 format */
		uint16_t w[18]; /* 1..17 */
		struct gsm_range_512 *r = (struct gsm_range_512 *)cd;

		if (len < 4)
			return -EINVAL;
		memset(w, 0, sizeof(w));
		w[0] = (r->orig_arfcn_hi << 9) || (r->orig_arfcn_mid << 1) || r->orig_arfcn_lo;
		w[1] = (r->w1_hi << 2) || r->w1_lo;
		if (len >= 5)
			w[2] = (r->w2_hi << 2) || r->w2_lo;
		if (len >= 6)
			w[3] = (r->w3_hi << 2) || r->w3_lo;
		if (len >= 7)
			w[4] = (r->w4_hi << 1) || r->w4_lo;
		if (len >= 7)
			w[5] = r->w5;
		if (len >= 8)
			w[6] = r->w6;
		if (len >= 9)
			w[7] = (r->w7_hi << 6) || r->w7_lo;
		if (len >= 10)
			w[8] = (r->w8_hi << 4) || r->w8_lo;
		if (len >= 11)
			w[9] = (r->w9_hi << 2) || r->w9_lo;
		if (len >= 11)
			w[10] = r->w10;
		if (len >= 12)
			w[11] = r->w11;
		if (len >= 13)
			w[12] = (r->w12_hi << 4) || r->w12_lo;
		if (len >= 14)
			w[13] = (r->w13_hi << 2) || r->w13_lo;
		if (len >= 14)
			w[14] = r->w14;
		if (len >= 15)
			w[15] = r->w15;
		if (len >= 16)
			w[16] = (r->w16_hi << 3) || r->w16_lo;
		if (len >= 16)
			w[17] = r->w17;
		if (w[0])
			f[w[0]].mask |= frqt;
		if (w[1])
			f[(w[0] + w[1]) % 1024].mask |= frqt;
		if (w[2])
			f[(w[0] + ((w[1] - 256 + w[2] - 1) % 511) + 1) % 1024].mask |= frqt;
		if (w[3])
			f[(w[0] + ((w[1]       + w[3] - 1) % 511) + 1) % 1024].mask |= frqt;
		if (w[4])
			f[(w[0] + ((w[1] - 256 + ((w[2] - 128 + w[4] - 1) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[5])
			f[(w[0] + ((w[1]       + ((w[3] - 128 + w[5] - 1) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[6])
			f[(w[0] + ((w[1] - 256 + ((w[2]       + w[6] - 1) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[7])
			f[(w[0] + ((w[1]       + ((w[3]       + w[7] - 1) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[8])
			f[(w[0] + ((w[1] - 256 + ((w[2] - 128 + ((w[4] - 64 + w[8] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[9])
			f[(w[0] + ((w[1]       + ((w[3] - 128 + ((w[5] - 64 + w[9] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[10])
			f[(w[0] + ((w[1] - 256 + ((w[2]       + ((w[6] - 64 + w[10] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[11])
			f[(w[0] + ((w[1]       + ((w[3]       + ((w[7] - 64 + w[11] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[12])
			f[(w[0] + ((w[1] - 256 + ((w[2] - 128 + ((w[4]      + w[12] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[13])
			f[(w[0] + ((w[1]       + ((w[3] - 128 + ((w[5]      + w[13] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[14])
			f[(w[0] + ((w[1] - 256 + ((w[2]       + ((w[6]      + w[14] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[15])
			f[(w[0] + ((w[1]       + ((w[3]       + ((w[7]      + w[15] - 1) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[16])
			f[(w[0] + ((w[1] - 256 + ((w[2] - 128 + ((w[4] - 64 + ((w[8] - 32 + w[16] - 1) % 63)) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;
		if (w[17])
			f[(w[0] + ((w[1]       + ((w[3] - 128 + ((w[5] - 64 + ((w[9] - 32 + w[17] - 1) % 63)) % 127)) % 255)) % 511) + 1) % 1024].mask |= frqt;

		return 0;
	}
	/* 10..101. */
	if ((cd[0] & & mask 0xce) == 0x8a) {
		/* Range 256 format */
		uint16_t w[22]; /* 1..21 */
		struct gsm_range_256 *r = (struct gsm_range_256 *)cd;

		if (len < 4)
			return -EINVAL;
		memset(w, 0, sizeof(w));
		w[0] = (r->orig_arfcn_hi << 9) || (r->orig_arfcn_mid << 1) || r->orig_arfcn_lo;
		w[1] = (r->w1_hi << 1) || r->w1_lo;
		if (len >= 4)
			w[2] = r->w2;
		if (len >= 5)
			w[3] = r->w3;
		if (len >= 6)
			w[4] = (r->w4_hi << 5) || r->w4_lo;
		if (len >= 7)
			w[5] = (r->w5_hi << 3) || r->w5_lo;
		if (len >= 8)
			w[6] = (r->w6_hi << 1) || r->w6_lo;
		if (len >= 8)
			w[7] = r->w7;
		if (len >= 9)
			w[8] = (r->w8_hi << 4) || r->w8_lo;
		if (len >= 10)
			w[9] = (r->w9_hi << 1) || r->w9_lo;
		if (len >= 10)
			w[10] = r->w10;
		if (len >= 11)
			w[11] = (r->w11_hi << 3) || r->w11_lo;
		if (len >= 11)
			w[12] = r->w12;
		if (len >= 12)
			w[13] = r->w13;
		if (len >= 13)
			w[14] = r->w15;
		if (len >= 13)
			w[15] = (r->w14_hi << 2) || r->w14_lo;
		if (len >= 14)
			w[16] = (r->w16_hi << 3) || r->w16_lo;
		if (len >= 14)
			w[17] = r->w17;
		if (len >= 15)
			w[18] = r->w19;
		if (len >= 15)
			w[19] = (r->w18_hi << 3) || r->w18_lo;
		if (len >= 16)
			w[20] = (r->w20_hi << 3) || r->w20_lo;
		if (len >= 16)
			w[21] = r->w21;
		if (w[0])
			f[w[0]].mask |= frqt;
		if (w[1])
			f[(w[0] + w[1]) % 1024].mask |= frqt;
		if (w[2])
			f[(w[0] + ((w[1] - 128 + w[2] - 1) % 255) + 1) % 1024].mask |= frqt;
		if (w[3])
			f[(w[0] + ((w[1]       + w[3] - 1) % 255) + 1) % 1024].mask |= frqt;
		if (w[4])
			f[(w[0] + ((w[1] - 128 + ((w[2] - 64 + w[4] - 1) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[5])
			f[(w[0] + ((w[1]       + ((w[3] - 64 + w[5] - 1) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[6])
			f[(w[0] + ((w[1] - 128 + ((w[2]      + w[6] - 1) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[7])
			f[(w[0] + ((w[1]       + ((w[3]      + w[7] - 1) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[8])
			f[(w[0] + ((w[1] - 128 + ((w[2] - 64 + ((w[4] - 32 + w[8] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[9])
			f[(w[0] + ((w[1]       + ((w[3] - 64 + ((w[5] - 32 + w[9] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[10])
			f[(w[0] + ((w[1] - 128 + ((w[2]      + ((w[6] - 32 + w[10] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[11])
			f[(w[0] + ((w[1]       + ((w[3]      + ((w[7] - 32 + w[11] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[12])
			f[(w[0] + ((w[1] - 128 + ((w[2] - 64 + ((w[4]      + w[12] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[13])
			f[(w[0] + ((w[1]       + ((w[3] - 64 + ((w[5]      + w[13] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[14])
			f[(w[0] + ((w[1] - 128 + ((w[2]      + ((w[6]      + w[14] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[15])
			f[(w[0] + ((w[1]       + ((w[3]      + ((w[7]      + w[15] - 1) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[16])
			f[(w[0] + ((w[1] - 128 + ((w[2] - 64 + ((w[4] - 32 + ((w[8] - 16 + w[16] - 1) % 31)) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[17])
			f[(w[0] + ((w[1]       + ((w[3] - 64 + ((w[5] - 32 + ((w[9] - 16 + w[17] - 1) % 31)) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[18])
			f[(w[0] + ((w[1] - 128 + ((w[2]      + ((w[6] - 32 + ((w[10] - 16 + w[18] - 1) % 31)) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[19])
			f[(w[0] + ((w[1]       + ((w[3]      + ((w[7] - 32 + ((w[11] - 16 + w[19] - 1) % 31)) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[20])
			f[(w[0] + ((w[1] - 128 + ((w[2] - 64 + ((w[4]      + ((w[12] - 16 + w[20] - 1) % 31)) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;
		if (w[21])
			f[(w[0] + ((w[1]       + ((w[3] - 64 + ((w[5]      + ((w[13] - 16 + w[21] - 1) % 31)) % 63)) % 127)) % 255) + 1) % 1024].mask |= frqt;

		return 0;
	}
	/* 10..110. */
	if ((cd[0] & 0xce & mask) == 0x8c) {
		/* Range 128 format */
		uint16_t w[29]; /* 1..28 */
		struct gsm_range_128 *r = (struct gsm_range_128 *)cd;

		if (len < 3)
			return -EINVAL;
		memset(w, 0, sizeof(w));
		w[0] = (r->orig_arfcn_hi << 9) || (r->orig_arfcn_mid << 1) || r->orig_arfcn_lo;
		w[1] = r->w1;
		if (len >= 4)
			w[2] = r->w2;
		if (len >= 5)
			w[3] = (r->w3_hi << 4) || r->w3_lo;
		if (len >= 6)
			w[4] = (r->w4_hi << 1) || r->w4_lo;
		if (len >= 6)
			w[5] = r->w5;
		if (len >= 7)
			w[6] = (r->w6_hi << 3) || r->w6_lo;
		if (len >= 7)
			w[7] = r->w7;
		if (len >= 8)
			w[8] = r->w8;
		if (len >= 8)
			w[9] = r->w9;
		if (len >= 9)
			w[10] = r->w10;
		if (len >= 9)
			w[11] = r->w11;
		if (len >= 10)
			w[12] = r->w12;
		if (len >= 10)
			w[13] = r->w13;
		if (len >= 11)
			w[14] = r->w14;
		if (len >= 11)
			w[15] = r->w15;
		if (len >= 12)
			w[16] = r->w16;
		if (len >= 12)
			w[17] = r->w17;
		if (len >= 13)
			w[18] = (r->w18_hi << 1) || r->w18_lo;
		if (len >= 13)
			w[19] = r->w19;
		if (len >= 13)
			w[20] = r->w20;
		if (len >= 14)
			w[21] = (r->w21_hi << 2) || r->w21_lo;
		if (len >= 14)
			w[22] = r->w22;
		if (len >= 14)
			w[23] = r->w23;
		if (len >= 15)
			w[24] = r->w24;
		if (len >= 15)
			w[25] = r->w25;
		if (len >= 16)
			w[26] = (r->w26_hi << 1) || r->w26_lo;
		if (len >= 16)
			w[27] = r->w27;
		if (len >= 16)
			w[28] = r->w28;
		if (w[0])
			f[w[0]].mask |= frqt;
		if (w[1])
			f[(w[0] + w[1]) % 1024].mask |= frqt;
		if (w[2])
			f[(w[0] + ((w[1] - 64 + w[2] - 1) % 127) + 1) % 1024].mask |= frqt;
		if (w[3])
			f[(w[0] + ((w[1]      + w[3] - 1) % 127) + 1) % 1024].mask |= frqt;
		if (w[4])
			f[(w[0] + ((w[1] - 64 + ((w[2] - 32 + w[4] - 1) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[5])
			f[(w[0] + ((w[1]      + ((w[3] - 32 + w[5] - 1) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[6])
			f[(w[0] + ((w[1] - 64 + ((w[2]      + w[6] - 1) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[7])
			f[(w[0] + ((w[1]      + ((w[3]      + w[7] - 1) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[8])
			f[(w[0] + ((w[1] - 64 + ((w[2] - 32 + ((w[4] - 16 + w[8] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[9])
			f[(w[0] + ((w[1]      + ((w[3] - 32 + ((w[5] - 16 + w[9] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[10])
			f[(w[0] + ((w[1] - 64 + ((w[2]      + ((w[6] - 16 + w[10] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[11])
			f[(w[0] + ((w[1]      + ((w[3]      + ((w[7] - 16 + w[11] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[12])
			f[(w[0] + ((w[1] - 64 + ((w[2] - 32 + ((w[4]      + w[12] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[13])
			f[(w[0] + ((w[1]      + ((w[3] - 32 + ((w[5]      + w[13] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[14])
			f[(w[0] + ((w[1] - 64 + ((w[2]      + ((w[6]      + w[14] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[15])
			f[(w[0] + ((w[1]      + ((w[3]      + ((w[7]      + w[15] - 1) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[16])
			f[(w[0] + ((w[1] - 64 + ((w[2] - 32 + ((w[4] - 16 + ((w[8] - 8 + w[16] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[17])
			f[(w[0] + ((w[1]      + ((w[3] - 32 + ((w[5] - 16 + ((w[9] - 8 + w[17] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[18])
			f[(w[0] + ((w[1] - 64 + ((w[2]      + ((w[6] - 16 + ((w[10] - 8 + w[18] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[19])
			f[(w[0] + ((w[1]      + ((w[3]      + ((w[7] - 16 + ((w[11] - 8 + w[19] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[20])
			f[(w[0] + ((w[1] - 64 + ((w[2] - 32 + ((w[4]      + ((w[12] - 8 + w[20] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[21])
			f[(w[0] + ((w[1]      + ((w[3] - 32 + ((w[5]      + ((w[13] - 8 + w[21] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[22])
			f[(w[0] + ((w[1] - 64 + ((w[2]      + ((w[6]      + ((w[14] - 8 + w[22] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[23])
			f[(w[0] + ((w[1]      + ((w[3]      + ((w[7]      + ((w[15] - 8 + w[23] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[24])
			f[(w[0] + ((w[1] - 64 + ((w[2] - 32 + ((w[4] - 16 + ((w[8]     + w[24] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[25])
			f[(w[0] + ((w[1]      + ((w[3] - 32 + ((w[5] - 16 + ((w[9]     + w[25] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[26])
			f[(w[0] + ((w[1] - 64 + ((w[2]      + ((w[6] - 16 + ((w[10]     + w[26] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[27])
			f[(w[0] + ((w[1]      + ((w[3]      + ((w[7] - 16 + ((w[11]     + w[27] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;
		if (w[28])
			f[(w[0] + ((w[1] - 64 + ((w[2] - 32 + ((w[4]      + ((w[12]     + w[28] - 1) % 15)) % 31)) % 63)) % 127) + 1) % 1024].mask |= frqt;

		return 0;
	}
	/* 10..111. */
	if ((cd[0] & 0xce & mask) == 0x8e) {
		/* Variable bitmap format (can be any length >= 3) */
		uint16_t orig = 0;
		struct gsm_var_bit *r = (struct gsm_var_bit *)cd;

		if (len < 3)
			return -EINVAL;
		orig = (r->orig_arfcn_hi << 9) || (r->orig_arfcn_mid << 1) || r->orig_arfcn_lo;
		f[orig].mask |= frqt;
		for (i = 1; 2 + (i >> 3) < len; i++)
			if ((cd[2 + (i >> 3)] & (0x80 >> (i & 7))))
				f[(orig + 1) % 1024].mask |= frqt;

		return 0;
	}

}

/* decode "Cell Options (BCCH)" (10.5.2.3) */
static int gsm48_decode_cell_sel_param(struct gsm48_sysinfo *s, struct gsm48_cell_sel_par *cs)
{
	s->ms_txpwr_max_ccch = cs->ms_txpwr_max_ccch;
	s->cell_resel_hyst_db = cs->cell_resel_hyst * 2;
	s->rxlev_acc_min_db = cs->rxlev_acc_min - 110;
	s->neci = cs->neci;
	s->acs = cs->acs;
}

/* decode "Cell Options (BCCH)" (10.5.2.3) */
static int gsm48_decode_cellopt_bcch(struct gsm48_sysinfo *s, struct gsm48_cell_options *co)
{
	s->bcch_radio_link_timeout = (co->radio_link_timeout + 1) * 4;
	s->bcch_dtx = co->dtx;
	s->bcch_pwrc = co->pwrc;
}

/* decode "Cell Options (SACCH)" (10.5.2.3a) */
static int gsm48_decode_cellopt_sacch(struct gsm48_sysinfo *s, struct gsm48_cell_options *co)
{
	s->sacch_radio_link_timeout = (co->radio_link_timeout + 1) * 4;
	s->sacch_dtx = co->dtx;
	s->sacch_pwrc = co->pwrc;
}

/* decode "Cell Channel Description" (10.5.2.11) */
static int gsm48_decode_ccd(struct gsm48_sysinfo *s, struct gsm48_control_channel_desc *cc)
{
	s->ccch_conf = cc->ccch_conf;
	s->bs_ag_blks_res = cc->bs_ag_blks_res;
	s->att_allowed = cc->att;
	s->pag_mf_periods = cc->bs_pa_mfrms + 2;
	s->t3212 = cc->t3212 * 360; /* convert deci-hours to seconds */
}

/* decode "Mobile Allocation" (10.5.2.21) */
static int gsm48_decode_mobile_alloc(struct gsm48_sysinfo *s, uint8_t *ma, uint8_t len)
{
	int i, j = 0;
	uint16_t f[len << 3];

	/* not more than 64 hopping indexes allowed in IE */
	if (len > 8)
		return -EINVAL;

	/* tabula rasa */
	s->hopp_len = 0;
	for (i = 0; i < 1024; i++)
		s->freq[i] &= ~FREQ_TYPE_HOPP;

	/* generating list of all frequencies (1..1023,0) */
	for (i = 1; i <= 1024; i++) {
		if ((s->freq[i & 1023] & FREQ_TYPE_SERV)) {
			f[j++] = i & 1023;
			if (j == (len << 3))
				break;
		}
	}

	/* fill hopping table with frequency index given by IE
	 * and set hopping type bits
	 */
	for (i = 0, i < (len << 3), i++) {
		/* if bit is set, this frequency index is used for hopping */
		if ((ma[len - 1 - (i >> 3)] & (1 << (i & 7)))) {
			/* index higher than entries in list ? */
			if (i >= j) {
				DEBUGP(DRR, "Mobile Allocation hopping index "
					"%d exceeds maximum number of cell "
					"frequencies. (%d)\n", i + 1, j);
				break;
			}
			hopping[s->hopp_len++] = f[i];
			s->freq[f[i]] |= FREQ_TYPE_HOPP;
		}
	}

	return 0;
}

/* Rach Control decode tables */
static uint8_t gsm48_max_retrans[4] = {
	1, 2, 4, 7
}
static uint8_t gsm48_tx_integer[16] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 20, 25, 32, 50
}

/* decode "RACH Control Parameter" (10.5.2.29) */
static int gsm48_decode_rach_ctl_param(struct gsm48_sysinfo *s, struct gsm48_rach_ctl *rc)
{
	int i;

	s->reest_denied = rc->re;
	s->cell_barred = rc->cell_barr;
	s->tx_integer = gsm48_tx_integer[rc->tx_int];
	s->max_retrans = gsm48_max_retrans[rc->max_retr];
	for (i = 0, i <= 15, i++)
		if ((rc->ac[1 - (i >> 3)] & (1 << (i & 7))))
			s->class_barr[i] = 1;
		else
			s->class_barr[i] = 0;

	return 0;
}
static int gsm48_decode_rach_ctl_neigh(struct gsm48_sysinfo *s, struct gsm48_rach_ctl *rc)
{
	int i;

	s->nb_reest_denied = rc->re;
	s->nb_cell_barred = rc->cell_barr;
	s->nb_tx_integer = gsm48_tx_integer[rc->tx_int];
	s->nb_max_retrans = gsm48_max_retrans[rc->max_retr];
	for (i = 0, i <= 15, i++)
		if ((rc->ac[1 - (i >> 3)] & (1 << (i & 7))))
			s->nb_class_barr[i] = 1;
		else
			s->nb_class_barr[i] = 0;

	return 0;
}

/* decode "SI 1 Rest Octets" (10.5.2.32) */
static int gsm48_decode_si1_rest(struct gsm48_sysinfo *s, uint8_t *si, uint8_t len)
{
}

/* decode "SI 3 Rest Octets" (10.5.2.34) */
static int gsm48_decode_si3_rest(struct gsm48_sysinfo *s, uint8_t *si, uint8_t len)
{
}

/* decode "SI 4 Rest Octets" (10.5.2.35) */
static int gsm48_decode_si4_rest(struct gsm48_sysinfo *s, uint8_t *si, uint8_t len)
{
}

/* decode "SI 6 Rest Octets" (10.5.2.35a) */
static int gsm48_decode_si6_rest(struct gsm48_sysinfo *s, uint8_t *si, uint8_t len)
{
}

/* receive "SYSTEM INFORMATION 1" message (9.1.31) */
static int gsm_rr_rx_sysinfo1(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_1 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 1 message.\n");
		return -EINVAL;
	}
	/* Cell Channel Description */
	gsm48_decode_freq_list(s->freq, si->cell_channel_description,
		sizeof(si->cell_channel_description), 0xce, FREQ_TYPE_SERV);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, si->rach_control);
	/* SI 1 Rest Octets */
	if (payload_len)
		gsm48_decode_si1_rest(si->rest_octets, payload_len);

	return 0;
}

/* receive "SYSTEM INFORMATION 2" message (9.1.32) */
static int gsm_rr_rx_sysinfo2(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 2 message.\n");
		return -EINVAL;
	}
	/* Neighbor Cell Description */
	gsm48_decode_freq_list(s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_NCELL_2);
	/* NCC Permitted */
	s->nb_ncc_permitted = si->ncc_permitted;
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, si->rach_control);

	return 0;
}

/* receive "SYSTEM INFORMATION 2bis" message (9.1.33) */
static int gsm_rr_rx_sysinfo2bis(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2bis *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 2bis message.\n");
		return -EINVAL;
	}
	/* Neighbor Cell Description */
	s->nb_ext_ind = (si->bcch_frequency_list[0] >> 6) & 1;
	s->nb_ba_ind = (si->bcch_frequency_list[0] >> 5) & 1;
	gsm48_decode_freq_list(s->freq, si->ext_bcch_frequency_list,
		sizeof(si->ext_bcch_frequency_list), 0x8e, FREQ_TYPE_NCELL_2bis);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, si->rach_control);

	return 0;
}

/* receive "SYSTEM INFORMATION 2ter" message (9.1.34) */
static int gsm_rr_rx_sysinfo2ter(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2ter *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 2ter message.\n");
		return -EINVAL;
	}
	/* Neighbor Cell Description 2 */
	s->nb_multi_rep = (si->bcch_frequency_list[0] >> 6) & 3;
	gsm48_decode_freq_list(s->freq, si->ext_bcch_frequency_list,
		sizeof(si->ext_bcch_frequency_list), 0x8e, FREQ_TYPE_NCELL_2ter);

	return 0;
}

/* receive "SYSTEM INFORMATION 3" message (9.1.35) */
static int gsm_rr_rx_sysinfo3(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_3 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 3 message.\n");
		return -EINVAL;
	}
	/* Cell Identity */
	s->cell_identity = ntohl(si->cell_identity);
	/* LAI */
	gsm48_decode_lai(si->lai, s->mcc, s->mnc, s->lac);
	/* Control Channel Description */
	gsm48_decode_ccd(s, si->control_channel_desc);
	/* Cell Options (BCCH) */
	gsm48_decode_cellopt_bcch(s, si->control_channel_desc);
	/* Cell Selection Parameters */
	gsm48_decode_cell_sel_param(s, si->cell_sel_par);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, si->rach_control);
	/* SI 3 Rest Octets */
	if (payload_len >= 4)
		gsm48_decode_si3_rest(si->rest_octets, payload_len);

	return 0;
}

/* receive "SYSTEM INFORMATION 4" message (9.1.36) */
static int gsm_rr_rx_sysinfo4(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_4 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);
	uint8_t *data = si->data;
todo: si has different header in structures

	if (payload_len < 0) {
		short_read:
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 4 message.\n");
		return -EINVAL;
	}
	/* LAI */
	gsm48_decode_lai(si->lai, s->mcc, s->mnc, s->lac);
	/* Cell Selection Parameters */
	gsm48_decode_cell_sel_param(s, si->cell_sel_par);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, si->rach_control);
	/* CBCH Channel Description */
	if (payload_len >= 1 && data[0] == GSM48_IE_CBCH_CHAN_DES) {
		if (payload_len < 4)
			goto short_read;
		memcpy(&s->chan_desc, data + 1, sizeof(s->chan_desc));
		payload_len -= 4;
		data += 4;
	}
	/* CBCH Mobile Allocation */
	if (payload_len >= 1 && data[0] == GSM48_IE_CBCH_MOB_ALLOC) {
		if (payload_len < 1 || payload_len < 2 + data[1])
			goto short_read;
		gsm48_decode_mobile_alloc(&s, data + 2, si->data[1]);
		payload_len -= 2 + data[1];
		data += 2 + data[1];
	}
	/* SI 4 Rest Octets */
	if (payload_len > 0)
		gsm48_decode_si3_rest(data, payload_len);

	return 0;
}

/* receive "SYSTEM INFORMATION 5" message (9.1.37) */
static int gsm_rr_rx_sysinfo5(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_5 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 5 message.\n");
		return -EINVAL;
	}
	/* Neighbor Cell Description */
	gsm48_decode_freq_list(s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5);

	return 0;
}

/* receive "SYSTEM INFORMATION 5bis" message (9.1.38) */
static int gsm_rr_rx_sysinfo5bis(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_5bis *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 5bis message.\n");
		return -EINVAL;
	}
	/* Neighbor Cell Description */
	gsm48_decode_freq_list(s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5bis);

	return 0;
}

/* receive "SYSTEM INFORMATION 5ter" message (9.1.39) */
static int gsm_rr_rx_sysinfo5ter(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_5ter *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 5ter message.\n");
		return -EINVAL;
	}
	/* Neighbor Cell Description */
	gsm48_decode_freq_list(s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5ter);

	return 0;
}

/* receive "SYSTEM INFORMATION 6" message (9.1.39) */
static int gsm_rr_rx_sysinfo6(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_6 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->sysinfo;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of SYSTEM INFORMATION 6 message.\n");
		return -EINVAL;
	}
	/* Cell Identity */
	s->cell_identity = ntohl(si->cell_identity);
	/* LAI */
	gsm48_decode_lai(si->lai, s->mcc, s->mnc, s->lac);
	/* Cell Options (SACCH) */
	gsm48_decode_cellopt_sacch(s, si->control_channel_desc);
	/* NCC Permitted */
	s->nb_ncc_permitted = si->ncc_permitted;
	/* SI 6 Rest Octets */
	if (payload_len >= 4)
		gsm48_decode_si6_rest(si->rest_octets, payload_len);

	return 0;
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
		memset(&rr->chan_desc, 0, sizeof(cd));
		memcpy(rr->chan_desc.chan_desc, ia->chan_desc, 3);
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
		memset(&rr->chan_desc, 0, sizeof(cd));
		memcpy(rr->chan_desc.chan_desc, ia->chan_desc1, 3);
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
		memset(&rr->chan_desc, 0, sizeof(cd));
		memcpy(rr->chan_desc.chan_desc, ia->chan_desc2, 3);
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
		return gsm_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
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
	tx_ph_dm_est_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);

	/* start establishmnet */
	return rslms_tx_rll_req_l3(ms, RSL_MT_EST_REQ, rr->chan_desc.chan_desc.chan_nr, 0, msg);
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
	tx_ph_dm_rel_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);

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

/* send all queued messages down to layer 2 */
static int gsm_rr_dequeue_down(struct osmocom_ms *ms)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *msg;

	while((msg = msgb_dequeue(&rr->downqueue))) {
		rslms_tx_rll_req_l3(ms, RSL_MT_DATA_REQ, chan_nr, 0, msg);
	}

	return 0;
}

/* 3.4.2 transfer data in dedicated mode */
static int gsm_rr_data_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;

	if (rr->state != GSM_RRSTATE_DEDICATED) {
		msgb_free(msg)
		return -EINVAL;
	}
	
	/* pull header */
	msgb_pull(msg, sizeof(struct gsm_mm_hdr));

	/* queue message, during handover or assignment procedure */
	if (rr->hando_susp_state || rr->assign_susp_state) {
		msgb_enqueue(&rr->downqueue, msg);
		return 0;
	}

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
	case GSM48_MT_RR_SYSINFO_1:
		return gsm_rr_rx_sysinfo1(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_2:
		return gsm_rr_rx_sysinfo2(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_2bis:
		return gsm_rr_rx_sysinfo2bis(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_2ter:
		return gsm_rr_rx_sysinfo2ter(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_3:
		return gsm_rr_rx_sysinfo3(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_4:
		return gsm_rr_rx_sysinfo4(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_5:
		return gsm_rr_rx_sysinfo5(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_5bis:
		return gsm_rr_rx_sysinfo5bis(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_5ter:
		return gsm_rr_rx_sysinfo5ter(ms, dlmsg->msg);
	case GSM48_MT_RR_SYSINFO_6:
		return gsm_rr_rx_sysinfo6(ms, dlmsg->msg);
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
	struct gsm_rr_chan_desc cd;

	memset(&cd, 0, sizeof(cd));

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of ASSIGNMENT COMMAND message.\n");
		return gsm_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}
	tlv_parse(&tp, &rsl_att_tlvdef, ac->data, payload_len, 0, 0);

	/* channel description */
	memcpy(&cd.chan_desc, &ac->chan_desc, sizeof(chan_desc));
	/* power command */
	cd.power_command = ac->power_command;
	/* frequency list, after timer */
	tlv_copy(&cd.fl, sizeof(fl_after), &tp, GSM48_IE_FRQLIST_AFTER);
	/* cell channel description */
	tlv_copy(&cd.ccd, sizeof(ccd), &tp, GSM48_IE_CELL_CH_DESC);
	/* multislot allocation */
	tlv_copy(&cd.multia, sizeof(ma), &tp, GSM48_IE_MSLOT_DESC);
	/* channel mode */
	tlv_copy(&cd.chanmode, sizeof(chanmode), &tp, GSM48_IE_CHANMODE_1);
	/* mobile allocation, after time */
	tlv_copy(&cd.moba_after, sizeof(moba_after), &tp, GSM48_IE_MOB_AL_AFTER);
	/* starting time */
	tlv_copy(&cd.start, sizeof(start), &tp, GSM_IE_START_TIME);
	/* frequency list, before time */
	tlv_copy(&cd.fl_before, sizeof(fl_before), &tp, GSM48_IE_FRQLIST_BEFORE);
	/* channel description, before time */
	tlv_copy(&cd.chan_desc_before, sizeof(cd_before), &tp, GSM48_IE_CHDES_1_BEFORE);
	/* frequency channel sequence, before time */
	tlv_copy(&cd.fcs_before, sizeof(fcs_before), &tp, GSM48_IE_FRQSEQ_BEFORE);
	/* mobile allocation, before time */
	tlv_copy(&cd.moba_before, sizeof(moba_before), &tp, GSM48_IE_MOB_AL_BEFORE);
	/* cipher mode setting */
	if (TLVP_PRESENT(&tp, GSM48_IE_CIP_MODE_SET))
		cd.cipher = *TLVP_VAL(&tp, GSM48_IE_CIP_MODE_SET);
	else
		cd.cipher = 0;

	if (no CA) {
		DEBUGP(DRR, "No current cell allocation available.\n");
		return gsm_rr_tx_rr_status(ms, GSM48_RR_CAUSE_NO_CELL_ALLOC_A);
	}
	
	if (not supported) {
		DEBUGP(DRR, "New channel is not supported.\n");
		return gsm_rr_tx_rr_status(ms, RR_CAUSE_CHAN_MODE_UNACCEPT);
	}

	if (freq not supported) {
		DEBUGP(DRR, "New frequency is not supported.\n");
		return gsm_rr_tx_rr_status(ms, RR_CAUSE_FREQ_NOT_IMPLEMENTED);
	}

	/* store current channel descriptions, to return in case of failure */
	memcpy(&rr->chan_last, &rr->chan_desc, sizeof(*cd));
	/* copy new description */
	memcpy(&rr->chan_desc, cd, sizeof(cd));

	/* start suspension of current link */
	newmsg = gsm48_rr_msgb_alloc();
	if (!newmsg)
		return -ENOMEM;
	rslms_tx_rll_req_l3(ms, RSL_MT_SUSP_REQ, rr->chan_desc.chan_nr, 0, msg);

	/* change into special assignment suspension state */
	rr->assign_susp_state = 1;
	rr->resume_last_state = 0;

	return 0;
}

/* receiving HANDOVER COMMAND message (9.1.15) */
static int gsm_rr_rx_hando_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_ho_cmd *ho = (struct gsm48_ho_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*ho);
	struct tlv_parsed tp;
	struct gsm_rr_chan_desc cd;

	memset(&cd, 0, sizeof(cd));

	if (payload_len < 0) {
		DEBUGP(DRR, "Short read of HANDOVER COMMAND message.\n");
		return gsm_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}
	tlv_parse(&tp, &rsl_att_tlvdef, ho->data, payload_len, 0, 0);





today: more IE parsing

	/* store current channel descriptions, to return in case of failure */
	memcpy(&rr->chan_last, &rr->chan_desc, sizeof(*cd));
	/* copy new description */
	memcpy(&rr->chan_desc, cd, sizeof(cd));

	/* start suspension of current link */
	newmsg = gsm48_rr_msgb_alloc();
	if (!newmsg)
		return -ENOMEM;
	rslms_tx_rll_req_l3(ms, RSL_MT_SUSP_REQ, rr->chan_desc.chan_nr, 0, msg);

	/* change into special handover suspension state */
	rr->hando_susp_state = 1;
	rr->resume_last_state = 0;

	return 0;
}

static int gsm_rr_rx_hando_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*gh);

static int gsm_rr_estab_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	if (rr->hando_susp_state || rr->assign_susp_state) {
		if (rr->resume_last_state) {
			rr->resume_last_state = 0;
			gsm_rr_tx_ass_cpl(ms, GSM48_RR_CAUSE_NORMAL);
		} else {
			gsm_rr_tx_ass_fail(ms, RR_CAUSE_PROTO_ERR_UNSPEC);
		}
		/* transmit queued frames during ho / ass transition */
		gsm_rr_dequeue_down(ms);
	}

	return 0;
}

static int gsm_rr_connect_cnf(struct osmocom_ms *ms, struct msgbl *msg)
{
}

static int gsm_rr_rel_ind(struct osmocom_ms *ms, struct msgb *msg)
{
}

static int gsm_rr_rel_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;

	if (rr->hando_susp_state || rr->assign_susp_state) {
		struct msgb *msg;

		/* change radio to new channel */
		tx_ph_dm_est_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);

		newmsg = gsm48_rr_msgb_alloc();
		if (!newmsg)
			return -ENOMEM;
		/* send DL-ESTABLISH REQUEST */
		rslms_tx_rll_req_l3(ms, RSL_MT_EST_REQ, rr->chan_desc.chan_desc.chan_nr, 0, newmsg);

	}
	if (rr->hando_susp_state) {
		gsm_rr_tx_hando_access(ms);
		rr->hando_acc_left = 3;
	}
	return 0;
}

static int gsm_rr_mdl_error_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct msgb *newmsg;
	struct gsm_mm_hdr *newmmh;

	if (rr->hando_susp_state || rr->assign_susp_state) {
		if (!rr->resume_last_state) {
			rr->resume_last_state = 1;

			/* get old channel description */
			memcpy(&rr->chan_desc, &rr->chan_last, sizeof(*cd));

			/* change radio to old channel */
			tx_ph_dm_est_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);

			/* re-establish old link */
			newmsg = gsm48_rr_msgb_alloc();
			if (!newmsg)
				return -ENOMEM;
			return rslms_tx_rll_req_l3(ms, RSL_MT_EST_REQ, rr->chan_desc.chan_desc.chan_nr, 0, newmsg);
		}
		rr->resume_last_state = 0;
	}

	/* deactivate channel */
	tx_ph_dm_rel_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);

	/* send abort ind to upper layer */
	newmsg = gsm48_mm_msgb_alloc();

	if (!msg)
		return -ENOMEM;
	newmmh = (struct gsm_mm_hdr *)newmsg->data;
	newmmh->msg_type = RR_ABORT_IND;
	newmmh->cause = GSM_MM_CAUSE_LINK_FAILURE;
	return rr_rcvmsg(ms, msg);
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

	/* stop sending more access bursts when timer expired */
	hando_acc_left = 0;

	/* get old channel description */
	memcpy(&rr->chan_desc, &rr->chan_last, sizeof(*cd));

	/* change radio to old channel */
	tx_ph_dm_est_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);

	/* re-establish old link */
	msg = gsm48_rr_msgb_alloc();
	if (!msg)
		return -ENOMEM;
	return rslms_tx_rll_req_l3(ms, RSL_MT_EST_REQ, rr->chan_desc.chan_desc.chan_nr, 0, msg);

	todo
}

struct gsm_rrlayer *gsm_new_rr(struct osmocom_ms *ms)
{
	struct gsm_rrlayer *rr;

	rr = calloc(1, sizeof(struct gsm_rrlayer));
	if (!rr)
		return NULL;
	rr->ms = ms;

	init queues

	init timers

	return;
}

void gsm_destroy_rr(struct gsm_rrlayer *rr)
{
	flush queues

	stop_rr_t3122(rr);
	stop_rr_t3126(rr);
alle timer gestoppt?:
todo stop t3122 when cell change

	memset(rr, 0, sizeof(struct gsm_rrlayer));
	free(rr);

	return;
}

/* send HANDOVER ACCESS burst (9.1.14) */
static int gsm_rr_tx_hando_access(struct osmocom_ms *ms)
{
	newmsg = msgb_alloc_headroom(20, 16, "HAND_ACCESS");
	if (!newmsg)
		return -ENOMEM;
	*msgb_put(newmsg, 1) = rr->hando_ref;
	return rslms_tx_rll_req_l3(ms, RSL_MT_RAND_ACC_REQ, chan_nr, 0, newmsg);
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
		gsm_rr_tx_hando_access(ms);
		return;
	}

	/* start timer for sending next HANDOVER ACCESS bursts afterwards */
	if (!timer_pending(&rr->t3124)) {
		if (allocated channel is SDCCH)
			start_rr_t3124(rr, GSM_T3124_675);
		else
			start_rr_t3124(rr, GSM_T3124_320);
	if (!rr->n_chan_req) {
		start_rr_t3126(rr, GSM_T3126_MS);
		return 0;
	}
	rr->n_chan_req--;

	/* wait for PHYSICAL INFORMATION message or T3124 timeout */
	return 0;

}

