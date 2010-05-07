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

/* Very short description of some of the procedures:
 *
 * A radio ressource request causes sendig a channel request on RACH.
 * After receiving of an immediate assignment the link will be establised.
 * After the link is established, the dedicated mode is entered and confirmed.
 *
 * A Paging request also triggers the channel request as above...
 * After the link is established, the dedicated mode is entered and indicated.
 *
 * During dedicated mode, messages are transferred.
 *
 * When an assignment command or a handover command is received, the current
 * link is released. After release, the new channel is activated and the
 * link is established again. After link is establised, pending messages from
 * radio ressource are sent.
 *
 * When the assignment or handover fails, the old channel is activate and the
 * link is established again. Also pending messages are sent.
 *
 */

#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <osmocore/msgb.h>
#include <osmocore/utils.h>
#include <osmocore/rsl.h>
#include <osmocore/gsm48.h>
#include <osmocore/bitvec.h>

#include <osmocom/logging.h>
#include <osmocom/osmocom_data.h>

static int gsm48_rcv_rsl(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_rr_dl_est(struct osmocom_ms *ms);

/*
 * support
 */

#define MIN(a, b) ((a < b) ? a : b)

int gsm48_decode_lai(struct gsm48_loc_area_id *lai, uint16_t *mcc,
	uint16_t *mnc, uint16_t *lac)
{
	*mcc = (lai->digits[0] & 0x0f) * 100
		+ (lai->digits[0] >> 4) * 10
		+ (lai->digits[1] & 0x0f);
	*mnc = (lai->digits[2] & 0x0f) * 10
		+ (lai->digits[2] >> 4);
	if ((lai->digits[1] >> 4) != 0xf) /* 3 digits MNC */
		*mnc += (lai->digits[1] >> 4) * 100;
	*lac = ntohs(lai->lac);

	return 0;
}

static int gsm48_encode_chan_h0(struct gsm48_chan_desc *cd, uint8_t tsc,
	uint16_t arfcn)
{
	cd->h0.tsc = tsc;
	cd->h0.h = 0;
	cd->h0.arfcn_low = arfcn & 0xff;
	cd->h0.arfcn_high = arfcn >> 8;

	return 0;
}

static int gsm48_encode_chan_h1(struct gsm48_chan_desc *cd, uint8_t tsc,
	uint8_t maio, uint8_t hsn)
{
	cd->h1.tsc = tsc;
	cd->h1.h = 1;
	cd->h1.maio_low = maio & 0x03;
	cd->h1.maio_high = maio >> 2;
	cd->h1.hsn = hsn;

	return 0;
}


static int gsm48_decode_chan_h0(struct gsm48_chan_desc *cd, uint8_t *tsc, 
	uint16_t *arfcn)
{
	*tsc = cd->h0.tsc;
	*arfcn = cd->h0.arfcn_low | (cd->h0.arfcn_high << 8);

	return 0;
}

static int gsm48_decode_chan_h1(struct gsm48_chan_desc *cd, uint8_t *tsc,
	uint8_t *maio, uint8_t *hsn)
{
	*tsc = cd->h1.tsc;
	*maio = cd->h1.maio_low | (cd->h1.maio_high << 2);
	*hsn = cd->h1.hsn;

	return 0;
}

/* 10.5.2.38 decode Starting time IE */
static int gsm48_decode_start_time(struct gsm48_rr_cd *cd,
	struct gsm48_start_time *st)
{
	cd->start_t1 = st->t1;
	cd->start_t2 = st->t2;
	cd->start_t3 = (st->t3_high << 3) | st->t3_low;

	return 0;
}

/*
 * state transition
 */

static const char *gsm48_rr_state_names[] = {
	"IDLE",
	"CONN PEND",
	"DEDICATED",
};

static void new_rr_state(struct gsm48_rrlayer *rr, int state)
{
	if (state < 0 || state >= 
		(sizeof(gsm48_rr_state_names) / sizeof(char *)))
		return;

	LOGP(DRR, LOGL_INFO, "new state %s -> %s\n",
		gsm48_rr_state_names[rr->state], gsm48_rr_state_names[state]);

	rr->state = state;

	if (state == GSM48_RR_ST_IDLE) {
		struct msgb *msg, *nmsg;

		/* free establish message, if any */
		rr->rr_est_req = 0;
		if (rr->rr_est_msg) {
			msgb_free(rr->rr_est_msg);
			rr->rr_est_msg = NULL;
		}
		/* free all pending messages */
		while((msg = msgb_dequeue(&rr->downqueue)))
			msgb_free(msg);
		/* clear all descriptions of last channel */
		memset(&rr->cd_now, 0, sizeof(rr->cd_now));
		/* reset cipering */
		rr->cipher_on = 0;
		/* tell cell selection process to return to idle mode
		 * NOTE: this must be sent unbuffered, because it will
		 * leave camping state, so it locks against subsequent
		 * establishment of dedicated channel, before the
		 * cell selection process returned to camping state
		 * again. (after cell reselection)
		 */
		nmsg = gsm322_msgb_alloc(GSM322_EVENT_RET_IDLE);
		if (!nmsg)
			return;
		gsm322_c_event(rr->ms, nmsg);
		msgb_free(nmsg);
	}
}

/*
 * messages
 */

/* names of RR-SAP */
static const struct value_string gsm48_rr_msg_names[] = {
	{ GSM48_RR_EST_REQ,		"RR_EST_REQ" },
	{ GSM48_RR_EST_IND,		"RR_EST_IND" },
	{ GSM48_RR_EST_CNF,		"RR_EST_CNF" },
	{ GSM48_RR_REL_IND,		"RR_REL_IND" },
	{ GSM48_RR_SYNC_IND,		"RR_SYNC_IND" },
	{ GSM48_RR_DATA_REQ,		"RR_DATA_REQ" },
	{ GSM48_RR_DATA_IND,		"RR_DATA_IND" },
	{ GSM48_RR_UNIT_DATA_IND,	"RR_UNIT_DATA_IND" },
	{ GSM48_RR_ABORT_REQ,		"RR_ABORT_REQ" },
	{ GSM48_RR_ABORT_IND,		"RR_ABORT_IND" },
	{ GSM48_RR_ACT_REQ,		"RR_ACT_REQ" },
	{ 0,				NULL }
};

const char *get_rr_name(int value)
{
	return get_value_string(gsm48_rr_msg_names, value);
}

/* allocate GSM 04.08 layer 3 message */
struct msgb *gsm48_l3_msgb_alloc(void)
{
	struct msgb *msg;

	msg = msgb_alloc_headroom(L3_ALLOC_SIZE+L3_ALLOC_HEADROOM,
		L3_ALLOC_HEADROOM, "GSM 04.08 L3");
	if (!msg)
		return NULL;
	msg->l3h = msg->data;

	return msg;
}

/* allocate GSM 04.08 message (RR-SAP) */
struct msgb *gsm48_rr_msgb_alloc(int msg_type)
{
	struct msgb *msg;
	struct gsm48_rr_hdr *rrh;

	msg = msgb_alloc_headroom(RR_ALLOC_SIZE+RR_ALLOC_HEADROOM,
		RR_ALLOC_HEADROOM, "GSM 04.08 RR");
	if (!msg)
		return NULL;

	rrh = (struct gsm48_rr_hdr *) msgb_put(msg, sizeof(*rrh));
	rrh->msg_type = msg_type;

	return msg;
}

/* queue message (RR-SAP) */
int gsm48_rr_upmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_mmlayer *mm = &ms->mmlayer;

	msgb_enqueue(&mm->rr_upqueue, msg);

	return 0;
}

/* push rsl header and send (RSL-SAP) */
static int gsm48_send_rsl(struct osmocom_ms *ms, uint8_t msg_type,
				struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	if (!msg->l3h) {
		printf("FIX l3h\n");
		exit (0);
	}
	rsl_rll_push_l3(msg, msg_type, rr->cd_now.chan_nr,
		rr->cd_now.link_id, 1);

	return rslms_recvmsg(msg, ms);
}

/* enqueue messages (RSL-SAP) */
static int gsm48_rx_rll(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

#warning HACK!!!!!!
return gsm48_rcv_rsl(ms, msg);
	msgb_enqueue(&rr->rsl_upqueue, msg);

	return 0;
}

/* input function that L2 calls when sending messages up to L3 */
static int gsm48_rx_rsl(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = gsm48_rx_rll(msg, ms);
		break;
	default:
		/* FIXME: implement this */
		LOGP(DRSL, LOGL_NOTICE, "unknown RSLms msg_discr 0x%02x\n",
			rslh->msg_discr);
		rc = -EINVAL;
		break;
	}

	return rc;
}

/* dequeue messages (RSL-SAP) */
int gsm48_rsl_dequeue(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *msg;
	int work = 0;
	
	while ((msg = msgb_dequeue(&rr->rsl_upqueue))) {
		/* msg is freed there */
		gsm48_rcv_rsl(ms, msg);
		work = 1; /* work done */
	}
	
	return work;
}

/*
 * timers handling
 */

static void timeout_rr_t3122(void *arg)
{
	LOGP(DRR, LOGL_INFO, "timer T3122 has fired\n");
}

static void timeout_rr_t3126(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct osmocom_ms *ms = rr->ms;

	LOGP(DRR, LOGL_INFO, "timer T3126 has fired\n");
	if (rr->rr_est_req) {
		struct msgb *msg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
		struct gsm48_rr_hdr *rrh;

		if (!msg)
			return;
		rrh = (struct gsm48_rr_hdr *)msg->data;
		rrh->cause = RR_REL_CAUSE_RA_FAILURE;
		gsm48_rr_upmsg(ms, msg);
	}

	new_rr_state(rr, GSM48_RR_ST_IDLE);
}

static void start_rr_t3122(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T3122 with %d seconds\n", sec);
	rr->t3122.cb = timeout_rr_t3122;
	rr->t3122.data = rr;
	bsc_schedule_timer(&rr->t3122, sec, micro);
}

static void start_rr_t3126(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T3126 with %d seconds\n", sec);
	rr->t3126.cb = timeout_rr_t3126;
	rr->t3126.data = rr;
	bsc_schedule_timer(&rr->t3126, sec, micro);
}

static void stop_rr_t3122(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t3122)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T3122\n");
		bsc_del_timer(&rr->t3122);
	}
}

static void stop_rr_t3126(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t3126)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T3126\n");
		bsc_del_timer(&rr->t3126);
	}
}

/*
 * status
 */

/* send rr status request */
static int gsm48_rr_tx_rr_status(struct osmocom_ms *ms, uint8_t cause)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_rr_status *st;

	LOGP(DRR, LOGL_INFO, "RR STATUS (cause #%d)\n", cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	st = (struct gsm48_rr_status *) msgb_put(nmsg, sizeof(*st));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_CIPH_M_COMPL;

	/* rr cause */
	st->rr_cause = cause;

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/*
 * ciphering
 */

/* send chiperhing mode complete */
static int gsm48_rr_tx_cip_mode_cpl(struct osmocom_ms *ms, uint8_t cr)
{
	struct gsm_support *sup = &ms->support;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	uint8_t buf[11], *tlv;

	LOGP(DRR, LOGL_INFO, "CIPHERING MODE COMPLETE (cr %d)\n", cr);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_CIPH_M_COMPL;

	/* MI */
	if (cr) {
		gsm48_generate_mid_from_imsi(buf, sup->imeisv);
		tlv = msgb_put(nmsg, 2 + buf[1]);
		memcpy(tlv, buf, 2 + buf[1]);
	}

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/* receive ciphering mode command */
static int gsm48_rr_rx_cip_mode_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_support *sup = &ms->support;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_cip_mode_cmd *cm = (struct gsm48_cip_mode_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*cm);
	uint8_t sc, alg_id, cr;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of CIPHERING MODE COMMAND "
			"message.\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}

	/* cipher mode setting */
	sc = cm->sc;
	alg_id = cm->alg_id;
	/* cipher mode response */
	cr = cm->cr;

	if (sc)
		LOGP(DRR, LOGL_INFO, "CIPHERING MODE COMMAND (sc=%u, cr=%u)",
			sc, cr);
	else
		LOGP(DRR, LOGL_INFO, "CIPHERING MODE COMMAND (sc=%u, "
			"algo=A5/%d cr=%u)", sc, alg_id + 1, cr);

	/* 3.4.7.2 */
	if (rr->cipher_on && sc) {
		LOGP(DRR, LOGL_INFO, "cipering already applied.\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}

	/* check if we actually support this cipher */
	if ((alg_id == GSM_CIPHER_A5_1 && !sup->a5_1)
	 || (alg_id == GSM_CIPHER_A5_2 && !sup->a5_2)
	 || (alg_id == GSM_CIPHER_A5_3 && !sup->a5_3)
	 || (alg_id == GSM_CIPHER_A5_4 && !sup->a5_4)
	 || (alg_id == GSM_CIPHER_A5_5 && !sup->a5_5)
	 || (alg_id == GSM_CIPHER_A5_6 && !sup->a5_6)
	 || (alg_id == GSM_CIPHER_A5_7 && !sup->a5_7))
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_CHAN_MODE_UNACCT);

	/* change to ciphering */
#ifdef TODO
	rsl command to activate ciperhing
#endif
	rr->cipher_on = sc, rr->cipher_type = alg_id;

	/* response */
	return gsm48_rr_tx_cip_mode_cpl(ms, cr);
}

/*
 * classmark
 */

/* Encode  "Classmark 3" (10.5.1.7) */
static int gsm48_rr_enc_cm3(struct osmocom_ms *ms, uint8_t *buf, uint8_t *len)
{
	struct gsm_support *sup = &ms->support;
	struct bitvec bv;

	memset(&bv, 0, sizeof(bv));
	bv.data = buf;
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
		bitvec_set_uint(&bv, sup->ms_sup, 5);
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
int gsm48_rr_enc_cm2(struct osmocom_ms *ms, struct gsm48_classmark2 *cm)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_support *sup = &ms->support;

	if (rr->cd_now.arfcn >= 512 && rr->cd_now.arfcn <= 885)
		cm->pwr_lev = sup->pwr_lev_1800;
	else
		cm->pwr_lev = sup->pwr_lev_900;
	cm->a5_1 = sup->a5_1;
	cm->es_ind = sup->es_ind;
	cm->rev_lev = sup->rev_lev;
	cm->fc = (sup->r_gsm || sup->e_gsm);
	cm->vgcs = sup->vgcs;
	cm->vbs = sup->vbs;
	cm->sm_cap = sup->sms_ptp;
	cm->ss_scr = sup->ss_ind;
	cm->ps_cap = sup->ps_cap;
	cm->a5_2 = sup->a5_2;
	cm->a5_3 = sup->a5_3;
	cm->cmsp = sup->cmsp;
	cm->solsa = sup->solsa;
	cm->lcsva_cap = sup->lcsva;

	return 0;
}

/* send classmark change */
static int gsm48_rr_tx_cm_change(struct osmocom_ms *ms)
{
	struct gsm_support *sup = &ms->support;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_cm_change *cc;
	uint8_t cm3[14], *tlv;

	LOGP(DRR, LOGL_INFO, "CLASSMARK CHANGE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	cc = (struct gsm48_cm_change *) msgb_put(nmsg, sizeof(*cc));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_CLSM_CHG;

	/* classmark 2 */
	cc->cm2_len = sizeof(cc->cm2);
	gsm48_rr_enc_cm2(ms, &cc->cm2);

	/* classmark 3 */
	if (sup->dcs_1800 || sup->e_gsm || sup->r_gsm
	 || sup->a5_7 || sup->a5_6 || sup->a5_5 || sup->a5_4
	 || sup->ms_sup
	 || sup->ucs2_treat
	 || sup->ext_meas || sup->meas_cap
	 || sup->loc_serv) {
		cc->cm2.cm3 = 1;
		cm3[0] = GSM48_IE_CLASSMARK3;
		gsm48_rr_enc_cm3(ms, cm3 + 2, &cm3[1]);
		tlv = msgb_put(nmsg, 2 + cm3[1]);
		memcpy(tlv, cm3, 2 + cm3[1]);
	}

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/* receiving classmark enquiry */
static int gsm48_rr_rx_cm_enq(struct osmocom_ms *ms, struct msgb *msg)
{
	/* send classmark */
	return gsm48_rr_tx_cm_change(ms);
}

/*
 * random access
 */

/* temporary timer until we have time control over channnel request */
/* TODO: turn this into a channel activation timeout, later */
#define RSL_MT_CHAN_CNF 0x19
#include <osmocom/l1ctl.h>
static void temp_rach_to(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct osmocom_ms *ms = rr->ms;
	struct msgb *msg = msgb_alloc_headroom(23+10, 10, "LAPDm RR");
	struct abis_rsl_rll_hdr *rllh = (struct abis_rsl_rll_hdr *) msgb_put(msg, sizeof(*rllh));

	rllh->c.msg_type = RSL_MT_CHAN_CNF;
	msg->l2h = (unsigned char *)rllh;
	gsm48_rcv_rsl(ms, msg);

	return;
}

/* start random access */
static int gsm48_rr_chan_req(struct osmocom_ms *ms, int cause, int paging)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;
	uint8_t chan_req_val, chan_req_mask;
	int rc;

	/* ignore paging, if not camping */
	if (paging
	 && (!cs->selected || (cs->state != GSM322_C3_CAMPED_NORMALLY
			    && cs->state != GSM322_C7_CAMPED_ANY_CELL))) {
		LOGP(DRR, LOGL_INFO, "Paging, but not camping, ignore.\n");
	 	return -EINVAL;
	}

	/* tell cell selection process to leave idle mode
	 * NOTE: this must be sent unbuffered, because the state may not
	 * change until idle mode is left
	 */
	nmsg = gsm322_msgb_alloc(GSM322_EVENT_LEAVE_IDLE);
	if (!nmsg)
		return -ENOMEM;
	rc = gsm322_c_event(ms, nmsg);
	msgb_free(nmsg);
	if (rc) {
		if (paging)
			return rc;
		LOGP(DRR, LOGL_INFO, "Failed to leave IDLE mode.\n");
		goto undefined;
	}

	/* 3.3.1.1.2 */
	new_rr_state(rr, GSM48_RR_ST_CONN_PEND);

	/* number of retransmissions (with first transmission) */
	rr->n_chan_req = s->max_retrans + 1;

	/* generate CHAN REQ (9.1.8) */
	switch (cause) {
	case RR_EST_CAUSE_EMERGENCY:
		/* 101xxxxx */
		chan_req_mask = 0x1f;
		chan_req_val = 0xa0;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (Emergency call)\n",
			chan_req_val);
		break;
	case RR_EST_CAUSE_REESTAB_TCH_F:
		chan_req_mask = 0x1f;
		chan_req_val = 0xc0;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (re-establish "
			"TCH/F)\n", chan_req_val);
		break;
	case RR_EST_CAUSE_REESTAB_TCH_H:
		if (s->neci) {
			chan_req_mask = 0x03;
			chan_req_val = 0x68;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x "
				"(re-establish TCH/H with NECI)\n",
				chan_req_val);
		} else {
			chan_req_mask = 0x1f;
			chan_req_val = 0xc0;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x "
				"(re-establish TCH/H no NECI)\n", chan_req_val);
		}
		break;
	case RR_EST_CAUSE_REESTAB_2_TCH_H:
		if (s->neci) {
			chan_req_mask = 0x03;
			chan_req_val = 0x6c;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x "
				"(re-establish TCH/H+TCH/H with NECI)\n",
				chan_req_val);
		} else {
			chan_req_mask = 0x1f;
			chan_req_val = 0xc0;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x "
				"(re-establish TCH/H+TCH/H no NECI)\n",
				chan_req_val);
		}
		break;
	case RR_EST_CAUSE_ANS_PAG_ANY:
		chan_req_mask = 0x1f;
		chan_req_val = 0x80;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (PAGING "
			"Any channel)\n", chan_req_val);
		break;
	case RR_EST_CAUSE_ANS_PAG_SDCCH:
		chan_req_mask = 0x0f;
		chan_req_val = 0x10;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (PAGING SDCCH)\n",
			chan_req_val);
		break;
	case RR_EST_CAUSE_ANS_PAG_TCH_F:
		/* ms supports no dual rate */
		chan_req_mask = 0x1f;
		chan_req_val = 0x80;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (PAGING TCH/F)\n",
			chan_req_val);
		break;
	case RR_EST_CAUSE_ANS_PAG_TCH_ANY:
		/* ms supports no dual rate */
		chan_req_mask = 0x1f;
		chan_req_val = 0x80;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (PAGING TCH/H or "
				"TCH/F)\n", chan_req_val);
		break;
	case RR_EST_CAUSE_ORIG_TCHF:
		/* ms supports no dual rate */
		chan_req_mask = 0x1f;
		chan_req_val = 0xe0;
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (Orig TCH/F)\n",
			chan_req_val);
		break;
	case RR_EST_CAUSE_LOC_UPD:
		if (s->neci) {
			chan_req_mask = 0x0f;
			chan_req_val = 0x00;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (Location "
				"Update with NECI)\n", chan_req_val);
		} else {
			chan_req_mask = 0x1f;
			chan_req_val = 0x00;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (Location "
				"Update no NECI)\n", chan_req_val);
		}
		break;
	case RR_EST_CAUSE_OTHER_SDCCH:
		if (s->neci) {
			chan_req_mask = 0x0f;
			chan_req_val = 0x01;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (OHTER "
				"with NECI)\n", chan_req_val);
		} else {
			chan_req_mask = 0x1f;
			chan_req_val = 0xe0;
			LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (OTHER "
				"no NECI)\n", chan_req_val);
		}
		break;
	default:
		if (!rr->rr_est_req) /* no request from MM */
			return -EINVAL;

		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: with unknown "
			"establishment cause: %d\n", cause);
		undefined:
		nmsg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
		if (!nmsg)
			return -ENOMEM;
		nrrh = (struct gsm48_rr_hdr *)nmsg->data;
		nrrh->cause = RR_REL_CAUSE_UNDEFINED;
		gsm48_rr_upmsg(ms, nmsg);
		new_rr_state(rr, GSM48_RR_ST_IDLE);
		return -EINVAL;
	}

// TODO: turn this into the channel activation timer
	rr->temp_rach_ti.cb = temp_rach_to;
	rr->temp_rach_ti.data = rr;
	bsc_schedule_timer(&rr->temp_rach_ti, ms->support.sync_to, 0);

	/* store value, mask and history */
	rr->chan_req_val = chan_req_val;
	rr->chan_req_mask = chan_req_mask;
	rr->cr_hist[2] = -1;
	rr->cr_hist[1] = -1;
	rr->cr_hist[0] = -1;

	/* if channel is already active somehow */
	if (cs->ccch_state == GSM322_CCCH_ST_DATA)
		return gsm48_rr_tx_rand_acc(ms, NULL);

	return 0;
}

/* send first/next channel request in conn pend state */
int gsm48_rr_tx_rand_acc(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	struct msgb *nmsg;
	struct l1ctl_info_ul *nul;
	struct l1ctl_rach_req *nra;
	int slots;
	uint8_t chan_req;

	if (cs->ccch_state != GSM322_CCCH_ST_DATA) {
		LOGP(DRR, LOGL_INFO, "CCCH channel activation failed.\n");

		if (rr->rr_est_req) {
			struct msgb *msg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
			struct gsm48_rr_hdr *rrh;

			if (!msg)
				return -ENOMEM;
			rrh = (struct gsm48_rr_hdr *)msg->data;
			rrh->cause = RR_REL_CAUSE_RA_FAILURE;
			gsm48_rr_upmsg(ms, msg);
		}

		new_rr_state(rr, GSM48_RR_ST_IDLE);

		return 0;
	}

	if (rr->state == GSM48_RR_ST_IDLE) {
		LOGP(DRR, LOGL_INFO, "MM already released RR.\n");

		return 0;
	}

	LOGP(DRR, LOGL_INFO, "RANDOM ACCESS confirm (requests left %d)\n",
		rr->n_chan_req);

	if (!rr->n_chan_req) {
		LOGP(DRR, LOGL_INFO, "Done with sending RANDOM ACCESS "
			"bursts\n");
		if (!bsc_timer_pending(&rr->t3126))
			start_rr_t3126(rr, 5, 0); /* TODO improve! */
		return 0;
	}
	rr->n_chan_req--;

	if (!rr->wait_assign) {
		/* first random acces, without delay of slots */
		slots = 0;
		rr->wait_assign = 1;
	} else {
		/* subsequent random acces, with slots from table 3.1 */
		switch(s->tx_integer) {
		case 3: case 8: case 14: case 50:
			if (s->ccch_conf != 1) /* not combined CCCH */
				slots = 55;
			else
				slots = 41;
		case 4: case 9: case 16:
			if (s->ccch_conf != 1)
				slots = 76;
			else
				slots = 52;
		case 5: case 10: case 20:
			if (s->ccch_conf != 1)
				slots = 109;
			else
				slots = 58;
		case 6: case 11: case 25:
			if (s->ccch_conf != 1)
				slots = 163;
			else
				slots = 86;
		default:
			if (s->ccch_conf != 1)
				slots = 217;
			else
				slots = 115;
		}
	}

	/* resend chan_req with new randiom */
#ifdef TODO
	nmsg = gsm48_rsl_msgb_alloc();
#else
	nmsg = msgb_alloc_headroom(64, 48, "RAND_ACC");
	struct l1ctl_hdr *l1h;
	nmsg->l1h = msgb_put(nmsg, sizeof(*l1h));
	l1h = (struct l1ctl_hdr *) nmsg->l1h;
	l1h->msg_type = L1CTL_RACH_REQ;
	if (!nmsg)
		return -ENOMEM;
	nul = (struct l1ctl_info_ul *) msgb_put(nmsg, sizeof(*nul));
#endif
	nra = (struct l1ctl_rach_req *) msgb_put(nmsg, sizeof(*nra));
	chan_req = random();
	chan_req &= rr->chan_req_mask;
	chan_req |= rr->chan_req_val;
	nra->ra = chan_req;
#ifdef TODO
	at this point we require chan req to be sent at a given delay
	also we require a confirm from radio part
	nra->delay = (random() % s->tx_integer) + slots;

	LOGP(DRR, LOGL_INFO, "RANDOM ACCESS (ra 0x%02x delay %d)\n", nra->ra,
		nra->delay);
#else
	rr->temp_rach_ti.cb = temp_rach_to;
	rr->temp_rach_ti.data = rr;
	bsc_schedule_timer(&rr->temp_rach_ti, 0, 900000);

	LOGP(DRR, LOGL_INFO, "RANDOM ACCESS (ra 0x%02x)\n", nra->ra);
#endif

	/* shift history and store */
	rr->cr_hist[2] = rr->cr_hist[1];
	rr->cr_hist[1] = rr->cr_hist[0];
	rr->cr_hist[0] = chan_req;

#ifdef TODO
	add layer 1 conrols to RSL...
	return gsm48_send_rsl(ms, RSL_MT_CHAN_REQ, nmsg);
#else
//#warning disabled!
	return osmo_send_l1(ms, nmsg);
#endif
}

/*
 * system information
 */

/* decode "Cell Channel Description" (10.5.2.1b) and other frequency lists */
static int gsm48_decode_freq_list(struct gsm_support *sup,
	struct gsm_sysinfo_freq *f, uint8_t *cd, uint8_t len, uint8_t mask,
	uint8_t frqt)
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
	if (sup->p_gsm && !sup->e_gsm && !sup->r_gsm && !sup->dcs_1800)
	 	return 0;

	/* 10..0XX. */
	if ((cd[0] & 0xc8 & mask) == 0x80) {
		/* Range 1024 format */
		uint16_t w[17]; /* 1..16 */
		struct gsm48_range_1024 *r = (struct gsm48_range_1024 *)cd;

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
		struct gsm48_range_512 *r = (struct gsm48_range_512 *)cd;

		if (len < 4)
			return -EINVAL;
		memset(w, 0, sizeof(w));
		w[0] = (r->orig_arfcn_hi << 9) | (r->orig_arfcn_mid << 1) | r->orig_arfcn_lo;
		w[1] = (r->w1_hi << 2) | r->w1_lo;
		if (len >= 5)
			w[2] = (r->w2_hi << 2) | r->w2_lo;
		if (len >= 6)
			w[3] = (r->w3_hi << 2) | r->w3_lo;
		if (len >= 7)
			w[4] = (r->w4_hi << 1) | r->w4_lo;
		if (len >= 7)
			w[5] = r->w5;
		if (len >= 8)
			w[6] = r->w6;
		if (len >= 9)
			w[7] = (r->w7_hi << 6) | r->w7_lo;
		if (len >= 10)
			w[8] = (r->w8_hi << 4) | r->w8_lo;
		if (len >= 11)
			w[9] = (r->w9_hi << 2) | r->w9_lo;
		if (len >= 11)
			w[10] = r->w10;
		if (len >= 12)
			w[11] = r->w11;
		if (len >= 13)
			w[12] = (r->w12_hi << 4) | r->w12_lo;
		if (len >= 14)
			w[13] = (r->w13_hi << 2) | r->w13_lo;
		if (len >= 14)
			w[14] = r->w14;
		if (len >= 15)
			w[15] = r->w15;
		if (len >= 16)
			w[16] = (r->w16_hi << 3) | r->w16_lo;
		if (len >= 16)
			w[17] = r->w17;
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
	if ((cd[0] & 0xce & mask) == 0x8a) {
		/* Range 256 format */
		uint16_t w[22]; /* 1..21 */
		struct gsm48_range_256 *r = (struct gsm48_range_256 *)cd;

		if (len < 4)
			return -EINVAL;
		memset(w, 0, sizeof(w));
		w[0] = (r->orig_arfcn_hi << 9) | (r->orig_arfcn_mid << 1) | r->orig_arfcn_lo;
		w[1] = (r->w1_hi << 1) | r->w1_lo;
		if (len >= 4)
			w[2] = r->w2;
		if (len >= 5)
			w[3] = r->w3;
		if (len >= 6)
			w[4] = (r->w4_hi << 5) | r->w4_lo;
		if (len >= 7)
			w[5] = (r->w5_hi << 3) | r->w5_lo;
		if (len >= 8)
			w[6] = (r->w6_hi << 1) | r->w6_lo;
		if (len >= 8)
			w[7] = r->w7;
		if (len >= 9)
			w[8] = (r->w8_hi << 4) | r->w8_lo;
		if (len >= 10)
			w[9] = (r->w9_hi << 1) | r->w9_lo;
		if (len >= 10)
			w[10] = r->w10;
		if (len >= 11)
			w[11] = (r->w11_hi << 3) | r->w11_lo;
		if (len >= 11)
			w[12] = r->w12;
		if (len >= 12)
			w[13] = r->w13;
		if (len >= 13)
			w[14] = r->w15;
		if (len >= 13)
			w[15] = (r->w14_hi << 2) | r->w14_lo;
		if (len >= 14)
			w[16] = (r->w16_hi << 3) | r->w16_lo;
		if (len >= 14)
			w[17] = r->w17;
		if (len >= 15)
			w[18] = r->w19;
		if (len >= 15)
			w[19] = (r->w18_hi << 3) | r->w18_lo;
		if (len >= 16)
			w[20] = (r->w20_hi << 3) | r->w20_lo;
		if (len >= 16)
			w[21] = r->w21;
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
		struct gsm48_range_128 *r = (struct gsm48_range_128 *)cd;

		if (len < 3)
			return -EINVAL;
		memset(w, 0, sizeof(w));
		w[0] = (r->orig_arfcn_hi << 9) | (r->orig_arfcn_mid << 1) | r->orig_arfcn_lo;
		w[1] = r->w1;
		if (len >= 4)
			w[2] = r->w2;
		if (len >= 5)
			w[3] = (r->w3_hi << 4) | r->w3_lo;
		if (len >= 6)
			w[4] = (r->w4_hi << 1) | r->w4_lo;
		if (len >= 6)
			w[5] = r->w5;
		if (len >= 7)
			w[6] = (r->w6_hi << 3) | r->w6_lo;
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
			w[18] = (r->w18_hi << 1) | r->w18_lo;
		if (len >= 13)
			w[19] = r->w19;
		if (len >= 13)
			w[20] = r->w20;
		if (len >= 14)
			w[21] = (r->w21_hi << 2) | r->w21_lo;
		if (len >= 14)
			w[22] = r->w22;
		if (len >= 14)
			w[23] = r->w23;
		if (len >= 15)
			w[24] = r->w24;
		if (len >= 15)
			w[25] = r->w25;
		if (len >= 16)
			w[26] = (r->w26_hi << 1) | r->w26_lo;
		if (len >= 16)
			w[27] = r->w27;
		if (len >= 16)
			w[28] = r->w28;
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
		struct gsm48_var_bit *r = (struct gsm48_var_bit *)cd;

		if (len < 3)
			return -EINVAL;
		orig = (r->orig_arfcn_hi << 9) | (r->orig_arfcn_mid << 1) | r->orig_arfcn_lo;
		f[orig].mask |= frqt;
		for (i = 1; 2 + (i >> 3) < len; i++)
			if ((cd[2 + (i >> 3)] & (0x80 >> (i & 7))))
				f[(orig + i) % 1024].mask |= frqt;

		return 0;
	}

	return 0;
}

/* decode "Cell Selection Parameters" (10.5.2.4) */
static int gsm48_decode_cell_sel_param(struct gsm48_sysinfo *s,
	struct gsm48_cell_sel_par *cs)
{
#ifdef TODO
	convert ms_txpwr_max_ccch dependant on the current frequenc and support
	to the right powe level
#endif
	s->ms_txpwr_max_ccch = cs->ms_txpwr_max_ccch;
	s->cell_resel_hyst_db = cs->cell_resel_hyst * 2;
	s->rxlev_acc_min_db = cs->rxlev_acc_min - 110;
	s->neci = cs->neci;
	s->acs = cs->acs;

	return 0;
}

/* decode "Cell Options (BCCH)" (10.5.2.3) */
static int gsm48_decode_cellopt_bcch(struct gsm48_sysinfo *s,
	struct gsm48_cell_options *co)
{
	s->bcch_radio_link_timeout = (co->radio_link_timeout + 1) * 4;
	s->bcch_dtx = co->dtx;
	s->bcch_pwrc = co->pwrc;

	return 0;
}

/* decode "Cell Options (SACCH)" (10.5.2.3a) */
static int gsm48_decode_cellopt_sacch(struct gsm48_sysinfo *s,
	struct gsm48_cell_options *co)
{
	s->sacch_radio_link_timeout = (co->radio_link_timeout + 1) * 4;
	s->sacch_dtx = co->dtx;
	s->sacch_pwrc = co->pwrc;

	return 0;
}

/* decode "Control Channel Description" (10.5.2.11) */
static int gsm48_decode_ccd(struct gsm48_sysinfo *s,
	struct gsm48_control_channel_descr *cc)
{
	s->ccch_conf = cc->ccch_conf;
	s->bs_ag_blks_res = cc->bs_ag_blks_res;
	s->att_allowed = cc->att;
	s->pag_mf_periods = cc->bs_pa_mfrms + 2;
	s->t3212 = cc->t3212 * 360; /* convert deci-hours to seconds */

	return 0;
}

/* decode "Mobile Allocation" (10.5.2.21) */
static int gsm48_decode_mobile_alloc(struct gsm48_sysinfo *s,
	uint8_t *ma, uint8_t len)
{
	int i, j = 0;
	uint16_t f[len << 3];

	/* not more than 64 hopping indexes allowed in IE */
	if (len > 8)
		return -EINVAL;

	/* tabula rasa */
	s->hopp_len = 0;
	for (i = 0; i < 1024; i++)
		s->freq[i].mask &= ~FREQ_TYPE_HOPP;

	/* generating list of all frequencies (1..1023,0) */
	for (i = 1; i <= 1024; i++) {
		if ((s->freq[i & 1023].mask & FREQ_TYPE_SERV)) {
			f[j++] = i & 1023;
			if (j == (len << 3))
				break;
		}
	}

	/* fill hopping table with frequency index given by IE
	 * and set hopping type bits
	 */
	for (i = 0; i < (len << 3); i++) {
		/* if bit is set, this frequency index is used for hopping */
		if ((ma[len - 1 - (i >> 3)] & (1 << (i & 7)))) {
			/* index higher than entries in list ? */
			if (i >= j) {
				LOGP(DRR, LOGL_NOTICE, "Mobile Allocation "
					"hopping index %d exceeds maximum "
					"number of cell frequencies. (%d)\n",
					i + 1, j);
				break;
			}
			s->hopping[s->hopp_len++] = f[i];
			s->freq[f[i]].mask |= FREQ_TYPE_HOPP;
		}
	}

	return 0;
}

/* Rach Control decode tables */
static uint8_t gsm48_max_retrans[4] = {
	1, 2, 4, 7
};
static uint8_t gsm48_tx_integer[16] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 20, 25, 32, 50
};

/* decode "RACH Control Parameter" (10.5.2.29) */
static int gsm48_decode_rach_ctl_param(struct gsm48_sysinfo *s,
	struct gsm48_rach_control *rc)
{
	s->reest_denied = rc->re;
	s->cell_barr = rc->cell_bar;
	s->tx_integer = gsm48_tx_integer[rc->tx_integer];
	s->max_retrans = gsm48_max_retrans[rc->max_trans];
	s->class_barr = (rc->t2 << 8) | rc->t3;

	return 0;
}
static int gsm48_decode_rach_ctl_neigh(struct gsm48_sysinfo *s,
	struct gsm48_rach_control *rc)
{
	s->nb_reest_denied = rc->re;
	s->nb_cell_barr = rc->cell_bar;
	s->nb_tx_integer = gsm48_tx_integer[rc->tx_integer];
	s->nb_max_retrans = gsm48_max_retrans[rc->max_trans];
	s->nb_class_barr = (rc->t2 << 8) | rc->t3;

	return 0;
}

/* decode "SI 1 Rest Octets" (10.5.2.32) */
static int gsm48_decode_si1_rest(struct gsm48_sysinfo *s, uint8_t *si,
	uint8_t len)
{
	return 0;
}

/* decode "SI 3 Rest Octets" (10.5.2.34) */
static int gsm48_decode_si3_rest(struct gsm48_sysinfo *s, uint8_t *si,
	uint8_t len)
{
	return 0;
}

/* decode "SI 4 Rest Octets" (10.5.2.35) */
static int gsm48_decode_si4_rest(struct gsm48_sysinfo *s, uint8_t *si,
	uint8_t len)
{
	return 0;
}

/* decode "SI 6 Rest Octets" (10.5.2.35a) */
static int gsm48_decode_si6_rest(struct gsm48_sysinfo *s, uint8_t *si,
	uint8_t len)
{
	return 0;
}

/* send sysinfo event to other layers */
static int gsm48_send_sysinfo(struct osmocom_ms *ms, uint8_t type)
{
	struct msgb *nmsg;
	struct gsm322_msg *em;

	nmsg = gsm322_msgb_alloc(GSM322_EVENT_SYSINFO);
	if (!nmsg)
		return -ENOMEM;
	em = (struct gsm322_msg *) nmsg->data;
	em->sysinfo = type;
	gsm322_cs_sendmsg(ms, nmsg);

	/* send timer info to location update process */
	nmsg = gsm48_mmevent_msgb_alloc(GSM48_MM_EVENT_SYSINFO);
	if (!nmsg)
		return -ENOMEM;
	gsm48_mmevent_msg(ms, nmsg);

	return 0;
}

/* receive "SYSTEM INFORMATION 1" message (9.1.31) */
static int gsm48_rr_rx_sysinfo1(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_1 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 1 "
			"message.\n");
		return -EINVAL;
	}

	if (!memcmp(si, s->si1_msg, MIN(msgb_l3len(msg), sizeof(s->si1_msg))))
		return 0;
	memcpy(s->si1_msg, si, MIN(msgb_l3len(msg), sizeof(s->si1_msg)));

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 1\n");

	/* Cell Channel Description */
	gsm48_decode_freq_list(&ms->support, s->freq,
		si->cell_channel_description,
		sizeof(si->cell_channel_description), 0xce, FREQ_TYPE_SERV);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, &si->rach_control);
	/* SI 1 Rest Octets */
	if (payload_len)
		gsm48_decode_si1_rest(s, si->rest_octets, payload_len);

	s->si1 = 1;

	return gsm48_send_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 2" message (9.1.32) */
static int gsm48_rr_rx_sysinfo2(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 2 "
			"message.\n");
		return -EINVAL;
	}
//printf("len = %d\n", MIN(msgb_l3len(msg), sizeof(s->si2_msg)));

	if (!memcmp(si, s->si2_msg, MIN(msgb_l3len(msg), sizeof(s->si2_msg))))
		return 0;
	memcpy(s->si2_msg, si, MIN(msgb_l3len(msg), sizeof(s->si2_msg)));

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 2\n");

	/* Neighbor Cell Description */
	s->nb_ext_ind_si2 = (si->bcch_frequency_list[0] >> 6) & 1;
	s->nb_ba_ind_si2 = (si->bcch_frequency_list[0] >> 5) & 1;
	gsm48_decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_NCELL_2);
	/* NCC Permitted */
	s->nb_ncc_permitted = si->ncc_permitted;
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, &si->rach_control);

	s->si2 = 1;

	return gsm48_send_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 2bis" message (9.1.33) */
static int gsm48_rr_rx_sysinfo2bis(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2bis *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 2bis "
			"message.\n");
		return -EINVAL;
	}

	if (!memcmp(si, s->si2b_msg, MIN(msgb_l3len(msg),
			sizeof(s->si2b_msg))))
		return 0;
	memcpy(s->si2b_msg, si, MIN(msgb_l3len(msg), sizeof(s->si2b_msg)));

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 2bis\n");

	/* Neighbor Cell Description */
	s->nb_ext_ind_si2bis = (si->bcch_frequency_list[0] >> 6) & 1;
	s->nb_ba_ind_si2bis = (si->bcch_frequency_list[0] >> 5) & 1;
	gsm48_decode_freq_list(&ms->support, s->freq,
		si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0x8e,
		FREQ_TYPE_NCELL_2bis);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, &si->rach_control);

	s->si2bis = 1;

	return gsm48_send_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 2ter" message (9.1.34) */
static int gsm48_rr_rx_sysinfo2ter(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2ter *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 2ter "
			"message.\n");
		return -EINVAL;
	}

	if (!memcmp(si, s->si2t_msg, MIN(msgb_l3len(msg),
			sizeof(s->si2t_msg))))
		return 0;
	memcpy(s->si2t_msg, si, MIN(msgb_l3len(msg), sizeof(s->si2t_msg)));

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 2ter\n");

	/* Neighbor Cell Description 2 */
	s->nb_multi_rep_si2ter = (si->ext_bcch_frequency_list[0] >> 6) & 3;
	gsm48_decode_freq_list(&ms->support, s->freq,
		si->ext_bcch_frequency_list,
		sizeof(si->ext_bcch_frequency_list), 0x8e,
		FREQ_TYPE_NCELL_2ter);

	s->si2ter = 1;

	return gsm48_send_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 3" message (9.1.35) */
static int gsm48_rr_rx_sysinfo3(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_3 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 3 "
			"message.\n");
		return -EINVAL;
	}

	if (!memcmp(si, s->si3_msg, MIN(msgb_l3len(msg), sizeof(s->si3_msg))))
		return 0;
	memcpy(s->si3_msg, si, MIN(msgb_l3len(msg), sizeof(s->si3_msg)));

	/* Cell Identity */
	s->cell_id = ntohs(si->cell_identity);
	/* LAI */
	gsm48_decode_lai(&si->lai, &s->mcc, &s->mnc, &s->lac);
	/* Control Channel Description */
	gsm48_decode_ccd(s, &si->control_channel_desc);
	/* Cell Options (BCCH) */
	gsm48_decode_cellopt_bcch(s, &si->cell_options);
	/* Cell Selection Parameters */
	gsm48_decode_cell_sel_param(s, &si->cell_sel_par);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, &si->rach_control);
	/* SI 3 Rest Octets */
	if (payload_len >= 4)
		gsm48_decode_si3_rest(s, si->rest_octets, payload_len);

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 3 (mcc %03d mnc %02d "
		"lac 0x%04x)\n", s->mcc, s->mnc, s->lac);

	s->si3 = 1;

	return gsm48_send_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 4" message (9.1.36) */
static int gsm48_rr_rx_sysinfo4(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_4 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);
	uint8_t *data = si->data;
	struct gsm48_chan_desc *cd;

	if (payload_len < 0) {
		short_read:
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 4 "
			"message.\n");
		return -EINVAL;
	}

	if (!memcmp(si, s->si4_msg, MIN(msgb_l3len(msg), sizeof(s->si4_msg))))
		return 0;
	memcpy(s->si4_msg, si, MIN(msgb_l3len(msg), sizeof(s->si4_msg)));

	/* LAI */
	gsm48_decode_lai(&si->lai, &s->mcc, &s->mnc, &s->lac);
	/* Cell Selection Parameters */
	gsm48_decode_cell_sel_param(s, &si->cell_sel_par);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, &si->rach_control);
	/* CBCH Channel Description */
	if (payload_len >= 1 && data[0] == GSM48_IE_CBCH_CHAN_DESC) {
		if (payload_len < 4)
			goto short_read;
		cd = (struct gsm48_chan_desc *) (data + 1);
		if (cd->h0.h) {
			s->h = 1;
			gsm48_decode_chan_h1(cd, &s->tsc, &s->maio, &s->hsn);
		} else {
			s->h = 0;
			gsm48_decode_chan_h0(cd, &s->tsc, &s->arfcn);
		}
		payload_len -= 4;
		data += 4;
	}
	/* CBCH Mobile Allocation */
	if (payload_len >= 1 && data[0] == GSM48_IE_CBCH_MOB_AL) {
		if (payload_len < 1 || payload_len < 2 + data[1])
			goto short_read;
		gsm48_decode_mobile_alloc(s, data + 2, si->data[1]);
		payload_len -= 2 + data[1];
		data += 2 + data[1];
	}
	/* SI 4 Rest Octets */
	if (payload_len > 0)
		gsm48_decode_si4_rest(s, data, payload_len);

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 4 (mcc %03d mnc %02d "
		"lac 0x%04x)\n", s->mcc, s->mnc, s->lac);

	s->si4 = 1;

	return gsm48_send_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 5" message (9.1.37) */
static int gsm48_rr_rx_sysinfo5(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_5 *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 5 "
			"message.\n");
		return -EINVAL;
	}

	if (!memcmp(si, s->si5_msg, MIN(msgb_l3len(msg), sizeof(s->si5_msg))))
		return 0;
	memcpy(s->si5_msg, si, MIN(msgb_l3len(msg), sizeof(s->si5_msg)));

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 5\n");

	/* Neighbor Cell Description */
	s->nb_ext_ind_si5 = (si->bcch_frequency_list[0] >> 6) & 1;
	s->nb_ba_ind_si5 = (si->bcch_frequency_list[0] >> 5) & 1;
	gsm48_decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5);

	s->si5 = 1;

	return gsm48_send_sysinfo(ms, si->system_information);
}

/* receive "SYSTEM INFORMATION 5bis" message (9.1.38) */
static int gsm48_rr_rx_sysinfo5bis(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_5bis *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 5bis "
			"message.\n");
		return -EINVAL;
	}

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 5bis\n");

	if (!memcmp(si, s->si5b_msg, MIN(msgb_l3len(msg),
			sizeof(s->si5b_msg))))
		return 0;
	memcpy(s->si5b_msg, si, MIN(msgb_l3len(msg), sizeof(s->si5b_msg)));

	/* Neighbor Cell Description */
	s->nb_ext_ind_si5bis = (si->bcch_frequency_list[0] >> 6) & 1;
	s->nb_ba_ind_si5bis = (si->bcch_frequency_list[0] >> 5) & 1;
	gsm48_decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5bis);

	s->si5bis = 1;

	return gsm48_send_sysinfo(ms, si->system_information);
}

/* receive "SYSTEM INFORMATION 5ter" message (9.1.39) */
static int gsm48_rr_rx_sysinfo5ter(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_5ter *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 5ter "
			"message.\n");
		return -EINVAL;
	}

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 5ter\n");

	if (!memcmp(si, s->si5t_msg, MIN(msgb_l3len(msg),
			sizeof(s->si5t_msg))))
		return 0;
	memcpy(s->si5t_msg, si, MIN(msgb_l3len(msg), sizeof(s->si5t_msg)));

	/* Neighbor Cell Description */
	gsm48_decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5ter);

	s->si5ter = 1;

	return gsm48_send_sysinfo(ms, si->system_information);
}

/* receive "SYSTEM INFORMATION 6" message (9.1.39) */
static int gsm48_rr_rx_sysinfo6(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_6 *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 6 "
			"message.\n");
		return -EINVAL;
	}

	if (!memcmp(si, s->si6_msg, MIN(msgb_l3len(msg), sizeof(s->si6_msg))))
		return 0;
	memcpy(s->si6_msg, si, MIN(msgb_l3len(msg), sizeof(s->si6_msg)));

	/* Cell Identity */
	if (s->si6 && s->cell_id != ntohs(si->cell_identity))
		LOGP(DRR, LOGL_INFO, "Cell ID on SI 6 differs from previous "
			"read.\n");
	s->cell_id = ntohs(si->cell_identity);
	/* LAI */
	gsm48_decode_lai(&si->lai, &s->mcc, &s->mnc, &s->lac);
	/* Cell Options (SACCH) */
	gsm48_decode_cellopt_sacch(s, &si->cell_options);
	/* NCC Permitted */
	s->nb_ncc_permitted = si->ncc_permitted;
	/* SI 6 Rest Octets */
	if (payload_len >= 4)
		gsm48_decode_si6_rest(s, si->rest_octets, payload_len);

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 6 (mcc %03d mnc %02d "
		"lac 0x%04x)\n", s->mcc, s->mnc, s->lac);

	s->si6 = 1;

	return gsm48_send_sysinfo(ms, si->system_information);
}

/*
 * paging
 */

/* paging channel request */
static int gsm48_rr_chan2cause[4] = {
	RR_EST_CAUSE_ANS_PAG_ANY,
	RR_EST_CAUSE_ANS_PAG_SDCCH,
	RR_EST_CAUSE_ANS_PAG_TCH_F,
	RR_EST_CAUSE_ANS_PAG_TCH_ANY
};

/* given LV of mobile identity is checked agains ms */
static int gsm_match_mi(struct osmocom_ms *ms, uint8_t *mi)
{
	char imsi[16];
	uint32_t tmsi;
	uint8_t mi_type;

	if (mi[0] < 1)
		return 0;
	mi_type = mi[1] & GSM_MI_TYPE_MASK;
	switch (mi_type) {
	case GSM_MI_TYPE_TMSI:
		if (mi[0] < 5)
			return 0;
		memcpy(&tmsi, mi+2, 4);
		if (ms->subscr.tmsi == ntohl(tmsi)
		 && ms->subscr.tmsi_valid) {
			LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n",
				ntohl(tmsi));

			return 1;
		} else
			LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
				ntohl(tmsi));
		break;
	case GSM_MI_TYPE_IMSI:
		gsm48_mi_to_string(imsi, sizeof(imsi), mi + 1, mi[0]);
		if (!strcmp(imsi, ms->subscr.imsi)) {
			LOGP(DPAG, LOGL_INFO, "IMSI %s matches\n", imsi);

			return 1;
		} else
			LOGP(DPAG, LOGL_INFO, "IMSI %s (not for us)\n", imsi);
		break;
	default:
		LOGP(DPAG, LOGL_NOTICE, "Paging with unsupported MI type %d.\n",
			mi_type);
	}

	return 0;
}

/* 9.1.22 PAGING REQUEST 1 message received */
static int gsm48_rr_rx_pag_req_1(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_paging1 *pa = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*pa);
	int chan_1, chan_2;
	uint8_t *mi;

	/* empty paging request */
	if (payload_len >= 2 && (pa->data[1] & GSM_MI_TYPE_MASK) == 0)
		return 0;

	/* 3.3.1.1.2: ignore paging while not camping on a cell */
	if (rr->state != GSM48_RR_ST_IDLE || !cs->selected
	 || (cs->state != GSM322_C3_CAMPED_NORMALLY
	  && cs->state != GSM322_C7_CAMPED_ANY_CELL)) {
		LOGP(DRR, LOGL_INFO, "PAGING ignored, we are not camping "
			"normally.\n");
		return 0;
	}
	LOGP(DPAG, LOGL_INFO, "PAGING REQUEST 1\n");

	if (payload_len < 2) {
		short_read:
		LOGP(DRR, LOGL_NOTICE, "Short read of PAGING REQUEST 1 "
			"message.\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_1 = pa->cneed1;
	chan_2 = pa->cneed2;
	/* first MI */
	mi = pa->data;
	if (payload_len < mi[0] + 1)
		goto short_read;
	if (gsm_match_mi(ms, mi) > 0)
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_1], 1);
	/* second MI */
	payload_len -= mi[0] + 1;
	mi = pa->data + mi[0] + 1;
	if (payload_len < 2)
		return 0;
	if (mi[0] != GSM48_IE_MOBILE_ID)
		return 0;
	if (payload_len < mi[1] + 2)
		goto short_read;
	if (gsm_match_mi(ms, mi + 1) > 0)
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_2], 1);

	return 0;
}

/* 9.1.23 PAGING REQUEST 2 message received */
static int gsm48_rr_rx_pag_req_2(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_paging2 *pa = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*pa);
	uint8_t *mi;
	int chan_1, chan_2, chan_3;

	/* 3.3.1.1.2: ignore paging while not camping on a cell */
	if (rr->state != GSM48_RR_ST_IDLE || !cs->selected
	 || (cs->state != GSM322_C3_CAMPED_NORMALLY
	  && cs->state != GSM322_C7_CAMPED_ANY_CELL)) {
		LOGP(DRR, LOGL_INFO, "PAGING ignored, we are not camping "
			"normally.\n");
		return 0;
	}
	LOGP(DPAG, LOGL_INFO, "PAGING REQUEST 2\n");

	if (payload_len < 0) {
		short_read:
		LOGP(DRR, LOGL_NOTICE, "Short read of PAGING REQUEST 2 "
			"message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_1 = pa->cneed1;
	chan_2 = pa->cneed2;
	/* first MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi1)
	 && ms->subscr.tmsi_valid) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi1));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_1], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi1));
	/* second MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi2)
	 && ms->subscr.tmsi_valid) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi2));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_2], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi2));
	/* third MI */
	mi = pa->data;
	if (payload_len < 2)
		return 0;
	if (mi[0] != GSM48_IE_MOBILE_ID)
		return 0;
	if (payload_len < mi[1] + 2 + 1) /* must include "channel needed" */
		goto short_read;
	chan_3 = mi[mi[1] + 2] & 0x03; /* channel needed */
	if (gsm_match_mi(ms, mi + 1) > 0)
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_3], 1);

	return 0;
}

/* 9.1.24 PAGING REQUEST 3 message received */
static int gsm48_rr_rx_pag_req_3(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_paging3 *pa = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*pa);
	int chan_1, chan_2, chan_3, chan_4;

	/* 3.3.1.1.2: ignore paging while not camping on a cell */
	if (rr->state != GSM48_RR_ST_IDLE || !cs->selected
	 || (cs->state != GSM322_C3_CAMPED_NORMALLY
	  && cs->state != GSM322_C7_CAMPED_ANY_CELL)) {
		LOGP(DRR, LOGL_INFO, "PAGING ignored, we are not camping "
			"normally.\n");
		return 0;
	}
	LOGP(DPAG, LOGL_INFO, "PAGING REQUEST 3\n");

	if (payload_len < 0) { /* must include "channel needed", part of *pa */
		LOGP(DRR, LOGL_NOTICE, "Short read of PAGING REQUEST 3 "
			"message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_1 = pa->cneed1;
	chan_2 = pa->cneed2;
	chan_3 = pa->cneed3;
	chan_4 = pa->cneed4;
	/* first MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi1)
	 && ms->subscr.tmsi_valid) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi1));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_1], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi1));
	/* second MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi2)
	 && ms->subscr.tmsi_valid) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi2));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_2], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi2));
	/* thrid MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi3)
	 && ms->subscr.tmsi_valid) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi3));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_3], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi3));
	/* fourth MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi4)
	 && ms->subscr.tmsi_valid) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi4));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_4], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi4));

	return 0;
}

/*
 * (immediate) assignment
 */

/* match request reference agains request history */
static int gsm48_match_ra(struct osmocom_ms *ms, struct gsm48_req_ref *ref)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	int i;

	for (i = 0; i < 3; i++) {
		if (rr->cr_hist[i] >= 0
		 && ref->ra == rr->cr_hist[i]) {
		 	LOGP(DRR, LOGL_INFO, "request %02x matches\n", ref->ra);
		 	// todo: match timeslot
			return 1;
		}
	}

	return 0;
}

/* 9.1.3 sending ASSIGNMENT COMPLETE */
static int gsm48_rr_tx_ass_cpl(struct osmocom_ms *ms, uint8_t cause)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_ass_cpl *ac;

	LOGP(DRR, LOGL_INFO, "ASSIGNMENT COMPLETE (cause #%d)\n", cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	ac = (struct gsm48_ass_cpl *) msgb_put(nmsg, sizeof(*ac));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_ASS_COMPL;

	/* RR_CAUSE */
	ac->rr_cause = cause;

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/* 9.1.4 sending ASSIGNMENT FAILURE */
static int gsm48_rr_tx_ass_fail(struct osmocom_ms *ms, uint8_t cause)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_ass_fail *af;

	LOGP(DRR, LOGL_INFO, "ASSIGNMENT FAILURE (cause #%d)\n", cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	af = (struct gsm48_ass_fail *) msgb_put(nmsg, sizeof(*af));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_ASS_COMPL;

	/* RR_CAUSE */
	af->rr_cause = cause;

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/* 9.1.18 IMMEDIATE ASSIGNMENT is received */
static int gsm48_rr_rx_imm_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_imm_ass *ia = msgb_l3(msg);
	int ma_len = msgb_l3len(msg) - sizeof(*ia);
	uint8_t ch_type, ch_subch, ch_ts;
	struct gsm48_rr_cd cd;
	uint8_t *st, st_len;

	memset(&cd, 0, sizeof(cd));

	if (ma_len < 0 /* mobile allocation IE must be included */
	 || ia->mob_alloc_len > ma_len) { /* short read of IE */
		LOGP(DRR, LOGL_NOTICE, "Short read of IMMEDIATE ASSIGNMENT "
			"message.\n");
		return -EINVAL;
	}
	if (ia->mob_alloc_len > 8) {
		LOGP(DRR, LOGL_NOTICE, "Moble allocation in IMMEDIATE "
			"ASSIGNMENT too large.\n");
		return -EINVAL;
	}

	/* starting time */
	st_len = ma_len - ia->mob_alloc_len;
	st = ia->mob_alloc + ia->mob_alloc_len;
	if (st_len >= 3 && st[0] == GSM48_IE_START_TIME)
		gsm48_decode_start_time(&cd, (struct gsm48_start_time *)(st+1));

	/* decode channel description */
	LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT:");
	cd.chan_nr = ia->chan_desc.chan_nr;
	rsl_dec_chan_nr(cd.chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ia->chan_desc.h0.h) {
		cd.h = 1;
		gsm48_decode_chan_h1(&ia->chan_desc, &cd.tsc, &cd.maio,
			&cd.hsn);
		LOGP(DRR, LOGL_INFO, " (ta %d/%dm ra 0x%02x chan_nr 0x%02x "
			"MAIO %u HSN %u TS %u SS %u TSC %u)\n",
			ia->timing_advance,
			ia->timing_advance * GSM_TA_CM / 100,
			ia->req_ref.ra, ia->chan_desc.chan_nr, cd.maio,
			cd.hsn, ch_ts, ch_subch, cd.tsc);
	} else {
		cd.h = 0;
		gsm48_decode_chan_h0(&ia->chan_desc, &cd.tsc, &cd.arfcn);
		LOGP(DRR, LOGL_INFO, " (ta %d/%dm ra 0x%02x chan_nr 0x%02x "
			"ARFCN %u TS %u SS %u TSC %u)\n",
			ia->timing_advance,
			ia->timing_advance * GSM_TA_CM / 100,
			ia->req_ref.ra, ia->chan_desc.chan_nr, cd.arfcn,
			ch_ts, ch_subch, cd.tsc);
	}

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM48_RR_ST_CONN_PEND || !rr->wait_assign) {
		LOGP(DRR, LOGL_INFO, "Not for us, no request.\n");
		return 0;
	}

	/* request ref */
	if (gsm48_match_ra(ms, &ia->req_ref)) {
		/* channel description */
		memcpy(&rr->cd_now, &cd, sizeof(rr->cd_now));
		/* timing advance */
		rr->cd_now.ta = ia->timing_advance;
		/* mobile allocation */
		memcpy(&rr->cd_now.mob_alloc_lv, &ia->mob_alloc_len, 
			ia->mob_alloc_len + 1);
		rr->wait_assign = 0;
		return gsm48_rr_dl_est(ms);
	}
	LOGP(DRR, LOGL_INFO, "Request, but not for us.\n");

	return 0;
}

/* 9.1.19 IMMEDIATE ASSIGNMENT EXTENDED is received */
static int gsm48_rr_rx_imm_ass_ext(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_imm_ass_ext *ia = msgb_l3(msg);
	int ma_len = msgb_l3len(msg) - sizeof(*ia);
	uint8_t ch_type, ch_subch, ch_ts;
	struct gsm48_rr_cd cd1, cd2;
	uint8_t *st, st_len;

	memset(&cd1, 0, sizeof(cd1));
	memset(&cd2, 0, sizeof(cd2));

	if (ma_len < 0 /* mobile allocation IE must be included */
	 || ia->mob_alloc_len > ma_len) { /* short read of IE */
		LOGP(DRR, LOGL_NOTICE, "Short read of IMMEDIATE ASSIGNMENT "
			"EXTENDED message.\n");
		return -EINVAL;
	}
	if (ia->mob_alloc_len > 4) {
		LOGP(DRR, LOGL_NOTICE, "Moble allocation in IMMEDIATE "
			"ASSIGNMENT EXTENDED too large.\n");
		return -EINVAL;
	}

	/* starting time */
	st_len = ma_len - ia->mob_alloc_len;
	st = ia->mob_alloc + ia->mob_alloc_len;
	if (st_len >= 3 && st[0] == GSM48_IE_START_TIME) {
		gsm48_decode_start_time(&cd1,
			(struct gsm48_start_time *)(st+1));
		memcpy(&cd2, &cd1, sizeof(cd2));
	}

	/* decode channel description */
	LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT EXTENDED:\n");
	cd2.chan_nr = ia->chan_desc1.chan_nr;
	rsl_dec_chan_nr(cd1.chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ia->chan_desc1.h0.h) {
		cd1.h = 1;
		gsm48_decode_chan_h1(&ia->chan_desc1, &cd1.tsc, &cd1.maio,
			&cd1.hsn);
		LOGP(DRR, LOGL_INFO, " assignment 1 (ta %d/%dm ra 0x%02x "
			"chan_nr 0x%02x MAIO %u HSN %u TS %u SS %u TSC %u)\n",
			ia->timing_advance1,
			ia->timing_advance1 * GSM_TA_CM / 100,
			ia->req_ref1.ra, ia->chan_desc1.chan_nr, cd1.maio,
			cd1.hsn, ch_ts, ch_subch, cd1.tsc);
	} else {
		cd1.h = 0;
		gsm48_decode_chan_h0(&ia->chan_desc1, &cd1.tsc, &cd1.arfcn);
		LOGP(DRR, LOGL_INFO, " assignment 1 (ta %d/%dm ra 0x%02x "
			"chan_nr 0x%02x ARFCN %u TS %u SS %u TSC %u)\n",
			ia->timing_advance1,
			ia->timing_advance1 * GSM_TA_CM / 100,
			ia->req_ref1.ra, ia->chan_desc1.chan_nr, cd1.arfcn,
			ch_ts, ch_subch, cd1.tsc);
	}
	cd2.chan_nr = ia->chan_desc2.chan_nr;
	rsl_dec_chan_nr(cd2.chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ia->chan_desc2.h0.h) {
		cd2.h = 1;
		gsm48_decode_chan_h1(&ia->chan_desc2, &cd2.tsc, &cd2.maio,
			&cd2.hsn);
		LOGP(DRR, LOGL_INFO, " assignment 2 (ta %d/%dm ra 0x%02x "
			"chan_nr 0x%02x MAIO %u HSN %u TS %u SS %u TSC %u)\n",
			ia->timing_advance2,
			ia->timing_advance2 * GSM_TA_CM / 100,
			ia->req_ref2.ra, ia->chan_desc2.chan_nr, cd2.maio,
			cd2.hsn, ch_ts, ch_subch, cd2.tsc);
	} else {
		cd2.h = 0;
		gsm48_decode_chan_h0(&ia->chan_desc2, &cd2.tsc, &cd2.arfcn);
		LOGP(DRR, LOGL_INFO, " assignment 2 (ta %d/%dm ra 0x%02x "
			"chan_nr 0x%02x ARFCN %u TS %u SS %u TSC %u)\n",
			ia->timing_advance2,
			ia->timing_advance2 * GSM_TA_CM / 100,
			ia->req_ref2.ra, ia->chan_desc2.chan_nr, cd2.arfcn,
			ch_ts, ch_subch, cd2.tsc);
	}

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM48_RR_ST_CONN_PEND || !rr->wait_assign) {
		LOGP(DRR, LOGL_INFO, "Not for us, no request.\n");
		return 0;
	}

	/* request ref 1 */
	if (gsm48_match_ra(ms, &ia->req_ref1)) {
		/* channel description */
		memcpy(&rr->cd_now, &cd1, sizeof(rr->cd_now));
		/* timing advance */
		rr->cd_now.ta = ia->timing_advance1;
		/* mobile allocation */
		memcpy(&rr->cd_now.mob_alloc_lv, &ia->mob_alloc_len,
			ia->mob_alloc_len + 1);
		rr->wait_assign = 0;
		return gsm48_rr_dl_est(ms);
	}
	/* request ref 1 */
	if (gsm48_match_ra(ms, &ia->req_ref2)) {
		/* channel description */
		memcpy(&rr->cd_now, &cd2, sizeof(rr->cd_now));
		/* timing advance */
		rr->cd_now.ta = ia->timing_advance2;
		/* mobile allocation */
		memcpy(&rr->cd_now.mob_alloc_lv, &ia->mob_alloc_len,
			ia->mob_alloc_len + 1);
		rr->wait_assign = 0;
		return gsm48_rr_dl_est(ms);
	}
	LOGP(DRR, LOGL_INFO, "Request, but not for us.\n");

	return 0;
}

/* 9.1.20 IMMEDIATE ASSIGNMENT REJECT is received */
static int gsm48_rr_rx_imm_ass_rej(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_imm_ass_rej *ia = msgb_l3(msg);
	int payload_len = msgb_l3len(msg) - sizeof(*ia);
	int i;
	struct gsm48_req_ref *req_ref;
	uint8_t t3122_value;

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM48_RR_ST_CONN_PEND || !rr->wait_assign)
		return 0;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of IMMEDIATE ASSIGNMENT "
			"REJECT message.\n");
		return -EINVAL;
	}

	for (i = 0; i < 4; i++) {
		/* request reference */
		req_ref = (struct gsm48_req_ref *)
				(((uint8_t *)&ia->req_ref1) + i * 4);
		LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT REJECT "
			"(ref 0x%02x)\n", req_ref->ra);
		if (gsm48_match_ra(ms, req_ref)) {
			/* wait indication */
			t3122_value = *(((uint8_t *)&ia->wait_ind1) + i * 4);
			if (t3122_value)
				start_rr_t3122(rr, t3122_value, 0);
			/* start timer 3126 if not already */
			if (!bsc_timer_pending(&rr->t3126))
				start_rr_t3126(rr, 5, 0); /* TODO improve! */
			/* stop assignmnet requests */
			rr->n_chan_req = 0;

			/* wait until timer 3126 expires, then release
			 * or wait for channel assignment */
			return 0;
		}
	}

	return 0;
}

/* 9.1.1 ADDITIONAL ASSIGMENT is received  */
static int gsm48_rr_rx_add_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_add_ass *aa = (struct gsm48_add_ass *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*aa);
	struct tlv_parsed tp;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of ADDITIONAL ASSIGNMENT "
			"message.\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}
	tlv_parse(&tp, &gsm48_rr_att_tlvdef, aa->data, payload_len, 0, 0);

	return gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
}

/*
 * measturement reports
 */

static int gsm48_rr_tx_meas_rep(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_rr_meas *meas = &rr->meas;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_meas_res *mr;

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	mr = (struct gsm48_meas_res *) msgb_put(nmsg, sizeof(*mr));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_MEAS_REP;

	/* measurement results */
	mr->rxlev_full = meas->rxlev_full;
	mr->rxlev_sub = meas->rxlev_sub;
	mr->rxqual_full = meas->rxqual_full;
	mr->rxqual_sub = meas->rxqual_sub;
	mr->dtx_used = meas->dtx;
	mr->ba_used = meas->ba;
	mr->meas_valid = meas->meas_valid;
	if (meas->ncell_na) {
		/* no results for serving cells */
		mr->no_nc_n_hi = 1;
		mr->no_nc_n_lo = 3;
	} else {
		mr->no_nc_n_hi = meas->count >> 2;
		mr->no_nc_n_lo = meas->count & 3;
	}
	mr->rxlev_nc1 = meas->rxlev_nc[0];
	mr->rxlev_nc2_hi = meas->rxlev_nc[1] >> 1;
	mr->rxlev_nc2_lo = meas->rxlev_nc[1] & 1;
	mr->rxlev_nc3_hi = meas->rxlev_nc[2] >> 2;
	mr->rxlev_nc3_lo = meas->rxlev_nc[2] & 3;
	mr->rxlev_nc4_hi = meas->rxlev_nc[3] >> 3;
	mr->rxlev_nc4_lo = meas->rxlev_nc[3] & 7;
	mr->rxlev_nc5_hi = meas->rxlev_nc[4] >> 4;
	mr->rxlev_nc5_lo = meas->rxlev_nc[4] & 15;
	mr->rxlev_nc6_hi = meas->rxlev_nc[5] >> 5;
	mr->rxlev_nc6_lo = meas->rxlev_nc[5] & 31;
	mr->bsic_nc1_hi = meas->bsic_nc[0] >> 3;
	mr->bsic_nc1_lo = meas->bsic_nc[0] & 7;
	mr->bsic_nc2_hi = meas->bsic_nc[1] >> 4;
	mr->bsic_nc2_lo = meas->bsic_nc[1] & 15;
	mr->bsic_nc3_hi = meas->bsic_nc[2] >> 5;
	mr->bsic_nc3_lo = meas->bsic_nc[2] & 31;
	mr->bsic_nc4 = meas->bsic_nc[3];
	mr->bsic_nc5 = meas->bsic_nc[4];
	mr->bsic_nc6 = meas->bsic_nc[5];
	mr->bcch_f_nc1 = meas->bcch_f_nc[0];
	mr->bcch_f_nc2 = meas->bcch_f_nc[1];
	mr->bcch_f_nc3 = meas->bcch_f_nc[2];
	mr->bcch_f_nc4 = meas->bcch_f_nc[3];
	mr->bcch_f_nc5_hi = meas->bcch_f_nc[4] >> 1;
	mr->bcch_f_nc5_lo = meas->bcch_f_nc[4] & 1;
	mr->bcch_f_nc6_hi = meas->bcch_f_nc[5] >> 2;
	mr->bcch_f_nc6_lo = meas->bcch_f_nc[5] & 3;

	return gsm48_send_rsl(ms, RSL_MT_UNIT_DATA_REQ, nmsg);
}

/*
 * link establishment and release
 */

/* activate link and send establish request */
static int gsm48_rr_dl_est(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_pag_rsp *pr;
	uint8_t mi[11];
	uint8_t ch_type, ch_subch, ch_ts;

	/* 3.3.1.1.3.1 */
	stop_rr_t3126(rr);

	/* flush pending RACH requests */
#ifdef TODO
	rr->n_chan_req = 0; // just to be safe
	nmsg = msgb_alloc_headroom(20, 16, "RAND_FLUSH");
	if (!nmsg)
		return -ENOMEM;
	gsm48_send_rsl(ms, RSL_MT_RAND_ACC_FLSH, msg);
#else
	if (bsc_timer_pending(&rr->temp_rach_ti))
		bsc_del_timer(&rr->temp_rach_ti);
#endif

	/* send DL_EST_REQ */
	if (rr->rr_est_msg) {
		/* use queued message */
		nmsg = rr->rr_est_msg;
		rr->rr_est_msg = 0;
		LOGP(DRR, LOGL_INFO, "sending establish message\n");
	} else {
		/* create paging response */
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
		pr = (struct gsm48_pag_rsp *) msgb_put(nmsg, sizeof(*pr));
		/* key sequence */
		pr->key_seq = subscr->key_seq;
		/* classmark 2 */
		pr->cm2_len = sizeof(pr->cm2);
		gsm48_rr_enc_cm2(ms, &pr->cm2);
		/* mobile identity */
		if (ms->subscr.tmsi_valid) {
			gsm48_generate_mid_from_tmsi(mi, subscr->tmsi);
			LOGP(DRR, LOGL_INFO, "sending paging response with "
				"TMSI\n");
		} else if (subscr->imsi[0]) {
			gsm48_generate_mid_from_imsi(mi, subscr->imsi);
			LOGP(DRR, LOGL_INFO, "sending paging response with "
				"IMSI\n");
		} else {
			mi[1] = 1;
			mi[2] = 0xf0 | GSM_MI_TYPE_NONE;
			LOGP(DRR, LOGL_INFO, "sending paging response without "
				"TMSI/IMSI\n");
		}
		msgb_put(nmsg, 1 + mi[1]);
		memcpy(pr->data, mi + 1, 1 + mi[1]);
	}

	/* activate channel */
#ifdef TODO
	RSL_MT_ to activate channel with all the cd_now informations
#else
	rsl_dec_chan_nr(rr->cd_now.chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ch_type != RSL_CHAN_SDCCH4_ACCH || ch_ts != 0) {
		printf("Channel type not supported, exitting.\n");
		exit(-ENOTSUP);
	}
	tx_ph_dm_est_req(ms, rr->cd_now.arfcn, rr->cd_now.chan_nr);
#endif

	/* start establishmnet */
	return gsm48_send_rsl(ms, RSL_MT_EST_REQ, nmsg);
}

/* the link is established */
static int gsm48_rr_estab_cnf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *nmsg;

	/* if MM has releases before confirm, we start release */
	if (rr->state == GSM48_RR_ST_IDLE) {
		LOGP(DRR, LOGL_INFO, "MM already released RR.\n");
		/* release message */
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		/* start release */
		return gsm48_send_rsl(ms, RSL_MT_REL_REQ, nmsg);
	}

	/* 3.3.1.1.4 */
	new_rr_state(rr, GSM48_RR_ST_DEDICATED);

	/* send confirm to upper layer */
	nmsg = gsm48_rr_msgb_alloc(
		(rr->rr_est_req) ? GSM48_RR_EST_CNF : GSM48_RR_EST_IND);
	if (!nmsg)
		return -ENOMEM;
	return gsm48_rr_upmsg(ms, nmsg);
}

/* the link is released */
static int gsm48_rr_rel_cnf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	/* deactivate channel */
	LOGP(DRR, LOGL_INFO, "deactivating channel (arfcn %d)\n",
		rr->cd_now.arfcn);
#ifdef TODO
	release and give new arfcn
	tx_ph_dm_rel_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);
#else
	l1ctl_tx_ccch_req(ms, rr->cd_now.arfcn);
#endif

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
static int gsm48_rr_est_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *) msg->data;
	struct gsm48_hdr *gh = msgb_l3(msg);
	uint8_t cause;
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;
	uint16_t acc_class;

	/* 3.3.1.1.3.2 */
	if (bsc_timer_pending(&rr->t3122)) {
		if (rrh->cause != RR_EST_CAUSE_EMERGENCY) {
			LOGP(DRR, LOGL_INFO, "T3122 running, rejecting!\n");
			cause = RR_REL_CAUSE_T3122;
			reject:
			nmsg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
			if (!nmsg)
				return -ENOMEM;
			nrrh = (struct gsm48_rr_hdr *)nmsg->data;
			nrrh->cause = cause;
			return gsm48_rr_upmsg(ms, nmsg);
		}
		LOGP(DRR, LOGL_INFO, "T3122 running, but emergency call\n");
		stop_rr_t3122(rr);
	}

	/* cell selected */
	if (!cs->selected) {
		LOGP(DRR, LOGL_INFO, "No cell selected, rejecting!\n");
		cause = RR_REL_CAUSE_TRY_LATER;
	 	goto reject;
	}

	/* check if camping */
	if (cs->state != GSM322_C3_CAMPED_NORMALLY
	 && rrh->cause != RR_EST_CAUSE_EMERGENCY) {
		LOGP(DRR, LOGL_INFO, "Not camping normally, rejecting!\n");
		cause = RR_REL_CAUSE_EMERGENCY_ONLY;
	 	goto reject;
	}
	if (cs->state != GSM322_C3_CAMPED_NORMALLY
	 && cs->state != GSM322_C7_CAMPED_ANY_CELL) {
		LOGP(DRR, LOGL_INFO, "Not camping, rejecting!\n");
		cause = RR_REL_CAUSE_TRY_LATER;
	 	goto reject;
	}

	/* check for relevant informations */
	if (!s->si3) {
		LOGP(DRR, LOGL_INFO, "Not enough SI, rejecting!\n");
		cause = RR_REL_CAUSE_TRY_LATER;
	 	goto reject;
	}

	/* 3.3.1.1.1 */
	if (!subscr->acc_barr && s->cell_barr) {
		LOGP(DRR, LOGL_INFO, "Cell barred, rejecting!\n");
	 	cause = RR_REL_CAUSE_NOT_AUTHORIZED;
		goto reject;
	}
	if (rrh->cause == RR_EST_CAUSE_EMERGENCY)
		acc_class = subscr->acc_class | 0x0400;
	else
		acc_class = subscr->acc_class & 0xfbff;
	if (!subscr->acc_barr && !(acc_class & (s->class_barr ^ 0xffff))) {
		LOGP(DRR, LOGL_INFO, "Cell barred for our access class (access "
			"%04x barred %04x)!\n", acc_class, s->class_barr);
	 	cause = RR_REL_CAUSE_NOT_AUTHORIZED;
		goto reject;
	}

	/* requested by RR */
	rr->rr_est_req = 1;

	/* clone and store REQUEST message */
	if (!gh) {
		LOGP(DRR, LOGL_ERROR, "Error, missing l3 message\n");
		return -EINVAL;
	}
	rr->rr_est_msg = gsm48_l3_msgb_alloc();
	if (!rr->rr_est_msg)
		return -ENOMEM;
	memcpy(msgb_put(rr->rr_est_msg, msgb_l3len(msg)),
		msgb_l3(msg), msgb_l3len(msg));

	/* request channel */
	return gsm48_rr_chan_req(ms, rrh->cause, 0);
}

/* send all queued messages down to layer 2 */
static int gsm48_rr_dequeue_down(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *msg;

	while((msg = msgb_dequeue(&rr->downqueue))) {
		LOGP(DRR, LOGL_INFO, "Sending queued message.\n");
		gsm48_send_rsl(ms, RSL_MT_DATA_REQ, msg);
	}

	return 0;
}

/* 3.4.2 transfer data in dedicated mode */
static int gsm48_rr_data_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	if (rr->state != GSM48_RR_ST_DEDICATED) {
		msgb_free(msg);
		return -EINVAL;
	}
	
	/* pull RR header */
	msgb_pull(msg, sizeof(struct gsm48_rr_hdr));

	/* queue message, during handover or assignment procedure */
	if (rr->hando_susp_state || rr->assign_susp_state) {
		LOGP(DRR, LOGL_INFO, "Queueing message during suspend.\n");
		msgb_enqueue(&rr->downqueue, msg);
		return 0;
	}

	/* forward message */
	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, msg);
}

/*
 * data indications from data link
 */

/* 3.4.2 data from layer 2 to RR and upper layer*/
static int gsm48_rr_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_rr_hdr *rrh;
	uint8_t pdisc = gh->proto_discr & 0x0f;

	if (pdisc == GSM48_PDISC_RR) {
		int rc = -EINVAL;
		uint8_t skip_ind = (gh->proto_discr & 0xf0) >> 4;

		/* ignore if skip indicator is not B'0000' */
		if (skip_ind)
			return 0;

		switch(gh->msg_type) {
		case GSM48_MT_RR_ADD_ASS:
			rc = gsm48_rr_rx_add_ass(ms, msg);
			break;
#if 0
		case GSM48_MT_RR_ASS_CMD:
			rc = gsm48_rr_rx_ass_cmd(ms, msg);
			break;
		case GSM48_MT_RR_CIP_MODE_CMD:
			rc = gsm48_rr_rx_cip_mode_cmd(ms, msg);
			break;
#endif
		case GSM48_MT_RR_CLSM_ENQ:
			rc = gsm48_rr_rx_cm_enq(ms, msg);
			break;
#if 0
		case GSM48_MT_RR_HANDO_CMD:
			rc = gsm48_rr_rx_hando_cmd(ms, msg);
			break;
		case GSM48_MT_RR_FREQ_REDEF:
			rc = gsm48_rr_rx_freq_redef(ms, msg);
			break;
#endif
		default:
			LOGP(DRR, LOGL_NOTICE, "Message type 0x%02x unknown.\n",
				gh->msg_type);

			/* status message */
			gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N);
		}

		msgb_free(msg);
		return rc;
	}

	/* pull off RSL header up to L3 message */
	msgb_pull(msg, (long)msgb_l3(msg) - (long)msg->data);

	/* push RR header */
	msgb_push(msg, sizeof(struct gsm48_rr_hdr));
	rrh = (struct gsm48_rr_hdr *)msg->data;
	rrh->msg_type = GSM48_RR_DATA_IND;

	return gsm48_rr_upmsg(ms, msg);
}

/* receive BCCH at RR layer */
static int gsm48_rr_rx_bcch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);

	switch (sih->system_information) {
	case GSM48_MT_RR_SYSINFO_1:
		return gsm48_rr_rx_sysinfo1(ms, msg);
	case GSM48_MT_RR_SYSINFO_2:
		return gsm48_rr_rx_sysinfo2(ms, msg);
	case GSM48_MT_RR_SYSINFO_2bis:
		return gsm48_rr_rx_sysinfo2bis(ms, msg);
	case GSM48_MT_RR_SYSINFO_2ter:
		return gsm48_rr_rx_sysinfo2ter(ms, msg);
	case GSM48_MT_RR_SYSINFO_3:
		return gsm48_rr_rx_sysinfo3(ms, msg);
	case GSM48_MT_RR_SYSINFO_4:
		return gsm48_rr_rx_sysinfo4(ms, msg);
	default:
#if 0
		LOGP(DRR, LOGL_NOTICE, "BCCH message type 0x%02x not sup.\n",
			sih->system_information);
#endif
		return -EINVAL;
	}
}

/* receive CCCH at RR layer */
static int gsm48_rr_rx_pch_agch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);

	switch (sih->system_information) {
	case GSM48_MT_RR_SYSINFO_5:
		return gsm48_rr_rx_sysinfo5(ms, msg);
	case GSM48_MT_RR_SYSINFO_5bis:
		return gsm48_rr_rx_sysinfo5bis(ms, msg);
	case GSM48_MT_RR_SYSINFO_5ter:
		return gsm48_rr_rx_sysinfo5ter(ms, msg);
	case GSM48_MT_RR_SYSINFO_6:
		return gsm48_rr_rx_sysinfo6(ms, msg);

	case GSM48_MT_RR_PAG_REQ_1:
		return gsm48_rr_rx_pag_req_1(ms, msg);
	case GSM48_MT_RR_PAG_REQ_2:
		return gsm48_rr_rx_pag_req_2(ms, msg);
	case GSM48_MT_RR_PAG_REQ_3:
		return gsm48_rr_rx_pag_req_3(ms, msg);

	case GSM48_MT_RR_IMM_ASS:
		return gsm48_rr_rx_imm_ass(ms, msg);
	case GSM48_MT_RR_IMM_ASS_EXT:
		return gsm48_rr_rx_imm_ass_ext(ms, msg);
	case GSM48_MT_RR_IMM_ASS_REJ:
		return gsm48_rr_rx_imm_ass_rej(ms, msg);
	default:
#if 0
		LOGP(DRR, LOGL_NOTICE, "CCCH message type 0x%02x unknown.\n",
			sih->system_information);
#endif
		return -EINVAL;
	}
}

/* unit data from layer 2 to RR layer */
static int gsm48_rr_unit_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct tlv_parsed tv;
	
	DEBUGP(DRSL, "RSLms UNIT DATA IND chan_nr=0x%02x link_id=0x%02x\n",
		rllh->chan_nr, rllh->link_id);

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));
	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		DEBUGP(DRSL, "UNIT_DATA_IND without L3 INFO ?!?\n");
		return -EIO;
	}
	msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);

	if (cs->ccch_state != GSM322_CCCH_ST_SYNC
	 && cs->ccch_state != GSM322_CCCH_ST_DATA)
	 	return -EINVAL;

	/* temporary moved here until confirm is fixed */
	if (cs->ccch_state != GSM322_CCCH_ST_DATA) {
		LOGP(DCS, LOGL_INFO, "Channel provides data.\n");
		cs->ccch_state = GSM322_CCCH_ST_DATA;

		/* in dedicated mode */
		if (ms->rrlayer.state == GSM48_RR_ST_CONN_PEND)
			return gsm48_rr_tx_rand_acc(ms, NULL);

		/* set timer for reading BCCH */
		if (cs->state == GSM322_C2_STORED_CELL_SEL
		 || cs->state == GSM322_C1_NORMAL_CELL_SEL
		 || cs->state == GSM322_C6_ANY_CELL_SEL
		 || cs->state == GSM322_C4_NORMAL_CELL_RESEL
		 || cs->state == GSM322_C8_ANY_CELL_RESEL
		 || cs->state == GSM322_C5_CHOOSE_CELL
		 || cs->state == GSM322_C9_CHOOSE_ANY_CELL
		 || cs->state == GSM322_PLMN_SEARCH
		 || cs->state == GSM322_HPLMN_SEARCH)
			start_cs_timer(cs, ms->support.scan_to, 0);
				// TODO: timer depends on BCCH config
	}

	switch (rllh->chan_nr) {
	case RSL_CHAN_PCH_AGCH:
		return gsm48_rr_rx_pch_agch(ms, msg);
	case RSL_CHAN_BCCH:
#if 0
#warning testing corrupt frames
{int i;
if (ms->cellsel.state == GSM322_C7_CAMPED_ANY_CELL)
for(i=0;i<msgb_l3len(msg);i++)
 msg->l3h[i] = random();
 }
#endif
		return gsm48_rr_rx_bcch(ms, msg);
	default:
		LOGP(DRSL, LOGL_NOTICE, "RSL with chan_nr 0x%02x unknown.\n",
			rllh->chan_nr);
		return -EINVAL;
	}
}

/*
 * state machines
 */

/* state trasitions for link layer messages (lower layer) */
static struct dldatastate {
	uint32_t	states;
	int		type;
	const char	*type_name;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} dldatastatelist[] = {
	{SBIT(GSM48_RR_ST_IDLE) | SBIT(GSM48_RR_ST_CONN_PEND),
	 RSL_MT_UNIT_DATA_IND, "UNIT_DATA_IND", gsm48_rr_unit_data_ind},
	{SBIT(GSM48_RR_ST_DEDICATED), /* 3.4.2 */
	 RSL_MT_DATA_IND, "DATA_IND", gsm48_rr_data_ind},
	{SBIT(GSM48_RR_ST_IDLE) | SBIT(GSM48_RR_ST_CONN_PEND),
	 RSL_MT_EST_CONF, "EST_CONF", gsm48_rr_estab_cnf},
#if 0
	{SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_EST_CONF, "EST_CONF", gsm48_rr_estab_cnf_dedicated},
	{SBIT(GSM_RRSTATE),
	 RSL_MT_CONNECT_CNF, "CONNECT_CNF", gsm48_rr_connect_cnf},
	{SBIT(GSM_RRSTATE),
	 RSL_MT_RELEASE_IND, "REL_IND", gsm48_rr_rel_ind},
#endif
	{SBIT(GSM48_RR_ST_IDLE) | SBIT(GSM48_RR_ST_CONN_PEND),
	 RSL_MT_REL_CONF, "REL_CONF", gsm48_rr_rel_cnf},
#if 0
	{SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_REL_CONF, "REL_CONF", gsm48_rr_rel_cnf_dedicated},
#endif
	{SBIT(GSM48_RR_ST_CONN_PEND), /* 3.3.1.1.2 */
	 RSL_MT_CHAN_CNF, "CHAN_CNF", gsm48_rr_tx_rand_acc},
#if 0
	{SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_CHAN_CNF, "CHAN_CNF", gsm48_rr_rand_acc_cnf_dedicated},
	{SBIT(GSM_RRSTATE),
	 RSL_MT_MDL_ERROR_IND, "MDL_ERROR_IND", gsm48_rr_mdl_error_ind},
#endif
};

#define DLDATASLLEN \
	(sizeof(dldatastatelist) / sizeof(struct dldatastate))

static int gsm48_rcv_rsl(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int msg_type = rllh->c.msg_type;
	int i;
	int rc;

	/* find function for current state and message */
	for (i = 0; i < DLDATASLLEN; i++)
		if ((msg_type == dldatastatelist[i].type)
		 && ((1 << rr->state) & dldatastatelist[i].states))
			break;
	if (i == DLDATASLLEN) {
		LOGP(DRSL, LOGL_NOTICE, "RSLms message 0x%02x unhandled at "
		"state %s.\n", msg_type, gsm48_rr_state_names[rr->state]);
		msgb_free(msg);
		return 0;
	}
	LOGP(DRSL, LOGL_INFO, "(ms %s) Received 'RSL_MT_%s' from RSL in state "
		"%s\n", ms->name, dldatastatelist[i].type_name,
		gsm48_rr_state_names[rr->state]);

	rc = dldatastatelist[i].rout(ms, msg);

	/* free msgb unless it is forwarded */
	if (dldatastatelist[i].rout != gsm48_rr_data_ind)
#warning HACK!!!!!!
return rc;
		msgb_free(msg);

	return rc;
}

/* state trasitions for RR-SAP messages from up */
static struct rrdownstate {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} rrdownstatelist[] = {
	{SBIT(GSM48_RR_ST_IDLE), /* 3.3.1.1 */
	 GSM48_RR_EST_REQ, gsm48_rr_est_req},
	{SBIT(GSM48_RR_ST_DEDICATED), /* 3.4.2 */
	 GSM48_RR_DATA_REQ, gsm48_rr_data_req},
#if 0
	{SBIT(GSM48_RR_ST_CONN_PEND) | SBIT(GSM48_RR_ST_DEDICATED),
	 GSM48_RR_ABORT_REQ, gsm48_rr_abort_req},
	{SBIT(GSM48_RR_ST_DEDICATED),
	 GSM48_RR_ACT_REQ, gsm48_rr_act_req},
#endif
};

#define RRDOWNSLLEN \
	(sizeof(rrdownstatelist) / sizeof(struct rrdownstate))

int gsm48_rr_downmsg(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_rr_hdr *rrh = (struct gsm48_rr_hdr *) msg->data;
	int msg_type = rrh->msg_type;
	int i;
	int rc;

	LOGP(DRR, LOGL_INFO, "(ms %s) Message '%s' received in state %s\n",
		ms->name, get_rr_name(msg_type),
		gsm48_rr_state_names[rr->state]);

	/* find function for current state and message */
	for (i = 0; i < RRDOWNSLLEN; i++)
		if ((msg_type == rrdownstatelist[i].type)
		 && ((1 << rr->state) & rrdownstatelist[i].states))
			break;
	if (i == RRDOWNSLLEN) {
		LOGP(DRR, LOGL_NOTICE, "Message unhandled at this state.\n");
		msgb_free(msg);
		return 0;
	}

	rc = rrdownstatelist[i].rout(ms, msg);

	/* free msgb uless it is forwarded */
	if (rrdownstatelist[i].rout != gsm48_rr_data_req)
		msgb_free(msg);

	return rc;
}

/*
 * init/exit
 */

int gsm48_rr_init(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	memset(rr, 0, sizeof(*rr));
	rr->ms = ms;

	LOGP(DRR, LOGL_INFO, "init Radio Ressource process\n");

	INIT_LLIST_HEAD(&rr->rsl_upqueue);
	INIT_LLIST_HEAD(&rr->downqueue);
	/* downqueue is handled here, so don't add_work */

	osmol2_register_handler(ms, &gsm48_rx_rsl);

	return 0;
}

int gsm48_rr_exit(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *msg;

	LOGP(DRR, LOGL_INFO, "exit Radio Ressource process\n");

	/* flush queues */
	while ((msg = msgb_dequeue(&rr->rsl_upqueue)))
		msgb_free(msg);
	while ((msg = msgb_dequeue(&rr->downqueue)))
		msgb_free(msg);

	if (rr->rr_est_msg) {
		msgb_free(rr->rr_est_msg);
		rr->rr_est_msg = NULL;
	}

	stop_rr_t3122(rr);
	stop_rr_t3126(rr);

	return 0;
}

#if 0

the process above is complete
------------------------------------------------------------------------------
incomplete



















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
#include <osmocore/utils.h>
#include <osmocore/gsm48.h>

static int gsm48_rr_abort_req(struct osmocom_ms *ms, struct gsm48_rr *rrmsg)
{
	struct gsm48_rrlayer *rr = ms->rrlayer;
	stop_rr_t3126(rr);
	if (rr->state == GSM48_RR_ST_DEDICATED) {
		struct gsm_dl dlmsg;

		memset(&dlmsg, 0, sizeof(dlmsg));
		return gsm48_send_rsl(ms, RSL_MT_REL_REQ, nmsg);
	}
	new_rr_state(rr, GSM48_RR_ST_IDLE);
}

static int gsm48_rr_act_req(struct osmocom_ms *ms, struct gsm48_rr *rrmsg)
{
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

static int gsm48_rr_rx_ass_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_ass_cmd *ac = (struct gsm48_ass_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*ac);
	struct tlv_parsed tp;
	struct gsm48_rr_chan_desc cd;
	struct msgb *nmsg;

	memset(&cd, 0, sizeof(cd));

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of ASSIGNMENT COMMAND message.\n");
		return gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}
	tlv_parse(&tp, &gsm48_rr_att_tlvdef, ac->data, payload_len, 0, 0);

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
		LOGP(DRR, LOGL_INFO, "No current cell allocation available.\n");
		return gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_NO_CELL_ALLOC_A);
	}
	
	if (not supported) {
		LOGP(DRR, LOGL_INFO, "New channel is not supported.\n");
		return gsm48_rr_tx_rr_status(ms, RR_CAUSE_CHAN_MODE_UNACCEPT);
	}

	if (freq not supported) {
		LOGP(DRR, LOGL_INFO, "New frequency is not supported.\n");
		return gsm48_rr_tx_rr_status(ms, RR_CAUSE_FREQ_NOT_IMPLEMENTED);
	}

	/* store current channel descriptions, to return in case of failure */
	memcpy(&rr->chan_last, &rr->chan_desc, sizeof(*cd));
	/* copy new description */
	memcpy(&rr->chan_desc, cd, sizeof(cd));

	/* start suspension of current link */
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gsm48_send_rsl(ms, RSL_MT_SUSP_REQ, msg);

	/* change into special assignment suspension state */
	rr->assign_susp_state = 1;
	rr->resume_last_state = 0;

	return 0;
}

/* decode "BA Range" (10.5.2.1a) */
static int gsm48_decode_ba_range(uint8_t *ba, uint8_t, ba_len, uint32_t *range,
	uint8_t *ranges, int max_ranges)
{
	/* ba = pointer to IE without IE type and length octets
	 * ba_len = number of octets
	 * range = pointer to store decoded range
	 * ranges = number of ranges decoded
	 * max_ranges = maximum number of decoded ranges that can be stored
	 */
	uint16_t lower, higher;
	int i, n, required_octets;
	
	/* find out how much ba ranges will be decoded */
	n = *ba++;
	ba_len --;
	required_octets = 5 * (n >> 1) + 3 * (n & 1);
	if (required_octets > n) {
		*ranges = 0;
		return -EINVAL;
	}
	if (max_ranges > n)
		n = max_ranges;

	/* decode ranges */
	for (i = 0; i < n; i++) {
		if (!(i & 1)) {
			/* decode even range number */
			lower = *ba++ << 2;
			lower |= (*ba >> 6);
			higher = (*ba++ & 0x3f) << 4;
			higher |= *ba >> 4;
		} else {
			lower = (*ba++ & 0x0f) << 6;
			lower |= *ba >> 2;
			higher = (*ba++ & 0x03) << 8;
			higher |= *ba++;
			/* decode odd range number */
		}
		*range++ = (higher << 16) | lower;
	}
	*ranges = n;

	return 0;
}


/* decode "Cell Description" (10.5.2.2) */
static int gsm48_decode_cell_desc(struct gsm48_cell_desc *cd, uint16_t *arfcn, uint8_t *ncc uint8_t *bcc)
{
	*arfcn = (cd->bcch_hi << 8) + cd->bcch_lo;
	*ncc = cd->ncc;
	*bcc = cd->bcc;
}

/* decode "Power Command" (10.5.2.28) and (10.5.2.28a) */
static int gsm48_decode_power_cmd_acc(struct gsm48_power_cmd *pc, uint8_t *power_level uint8_t *atc)
{
	*power_level = pc->power_level;
	if (atc) /* only in case of 10.5.2.28a */
		*atc = pc->atc;
}

/* decode "Synchronization Indication" (10.5.2.39) */
static int gsm48_decode_power_cmd_acc(struct gsm48_rrlayer *rr, struct gsm48_rr_sync_ind *si)
{
	rr->ho_sync_ind = si->si;
	rr->ho_rot = si->rot;
	rr->ho_nci = si->nci;
}

/* receiving HANDOVER COMMAND message (9.1.15) */
static int gsm48_rr_rx_hando_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_ho_cmd *ho = (struct gsm48_ho_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - wirklich sizeof(*ho);
	struct tlv_parsed tp;
	struct gsm48_rr_chan_desc cd;
	struct msgb *nmsg;

	memset(&cd, 0, sizeof(cd));

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of HANDOVER COMMAND message.\n");
		return gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}
	tlv_parse(&tp, &gsm48_rr_att_tlvdef, ho->data, payload_len, 0, 0);

	/* decode Cell Description */
	gsm_decode_cell_desc(&ho->cell_desc, &cd.bcch_arfcn, &cd.ncc, &cd.bcc);
	/* Channel Description */
	memcpy(&rr->chan_desc.chan_desc, ho->chan_desc, 3);
	/* Handover Reference */
	rr->hando_ref = ho->ho_ref;
	/* Power Command and access type */
	gsm_decode_power_cmd_acc((struct gsm48_power_cmd *)&ho->power_command,
		&cd.power_level, cd.atc);
	/* Synchronization Indication */
	if (TLVP_PRESENT(&tp, GSM48_IE_SYNC_IND))
		gsm48_decode_sync_ind(rr,
			TLVP_VAL(&tp, GSM48_IE_MOBILE_ALLOC)-1, &cd);
	/* Frequency Sort List */
	if (TLVP_PRESENT(&tp, GSM48_IE_FREQ_SHORT_LIST))
		gsm48_decode_freq_list(&ms->support, s->freq,
			TLVP_VAL(&tp, GSM48_IE_MOBILE_ALLOC),
			*(TLVP_VAL(&tp, GSM48_IE_MOBILE_ALLOC)-1),
				0xce, FREQ_TYPE_SERV);


today: more IE parsing

	/* store current channel descriptions, to return in case of failure */
	memcpy(&rr->chan_last, &rr->chan_desc, sizeof(*cd));
	/* copy new description */
	memcpy(&rr->chan_desc, cd, sizeof(cd));

	/* start suspension of current link */
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gsm48_send_rsl(ms, RSL_MT_SUSP_REQ, msg);

	/* change into special handover suspension state */
	rr->hando_susp_state = 1;
	rr->resume_last_state = 0;

	return 0;
}

static int gsm48_rr_estab_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	if (rr->hando_susp_state || rr->assign_susp_state) {
		if (rr->resume_last_state) {
			rr->resume_last_state = 0;
			gsm48_rr_tx_ass_cpl(ms, GSM48_RR_CAUSE_NORMAL);
		} else {
			gsm48_rr_tx_ass_fail(ms, RR_CAUSE_PROTO_ERR_UNSPEC);
		}
		/* transmit queued frames during ho / ass transition */
		gsm48_rr_dequeue_down(ms);
	}

	return 0;
}

static int gsm48_rr_connect_cnf(struct osmocom_ms *ms, struct msgbl *msg)
{
}

static int gsm48_rr_rel_ind(struct osmocom_ms *ms, struct msgb *msg)
{
}

static int gsm48_rr_rel_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = ms->rrlayer;
	struct msgb *nmsg;

	if (rr->hando_susp_state || rr->assign_susp_state) {
		struct msgb *msg;

		/* change radio to new channel */
		tx_ph_dm_est_req(ms, rr->cd_now.arfcn, rr->cd_now.chan_nr);

		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		/* send DL-ESTABLISH REQUEST */
		gsm48_send_rsl(ms, RSL_MT_EST_REQ, nmsg);

	}
	if (rr->hando_susp_state) {
		gsm48_rr_tx_hando_access(ms);
		rr->hando_acc_left = 3;
	}
	return 0;
}

static int gsm48_rr_mdl_error_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = ms->rrlayer;
	struct msgb *nmsg;
	struct gsm_rr_hdr *nrrh;

	if (rr->hando_susp_state || rr->assign_susp_state) {
		if (!rr->resume_last_state) {
			rr->resume_last_state = 1;

			/* get old channel description */
			memcpy(&rr->chan_desc, &rr->chan_last, sizeof(*cd));

			/* change radio to old channel */
			tx_ph_dm_est_req(ms, rr->cd_now.arfcn,
				rr->cd_now.chan_nr);

			/* re-establish old link */
			nmsg = gsm48_l3_msgb_alloc();
			if (!nmsg)
				return -ENOMEM;
			return gsm48_send_rsl(ms, RSL_MT_EST_REQ, nmsg);
		}
		rr->resume_last_state = 0;
	}

	/* deactivate channel */
	tx_ph_dm_rel_req(ms, arfcn, rr->chan_desc.chan_desc.chan_nr);

	/* send abort ind to upper layer */
	nmsg = gsm48_mm_msgb_alloc();

	if (!msg)
		return -ENOMEM;
	nrrh = (struct gsm_mm_hdr *)nmsg->data;
	nrrh->msg_type = RR_ABORT_IND;
	nrrh->cause = GSM_MM_CAUSE_LINK_FAILURE;
	return gsm48_rr_upmsg(ms, msg);
}

static void timeout_rr_t3124(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct msgb *nmsg;

	/* stop sending more access bursts when timer expired */
	hando_acc_left = 0;

	/* get old channel description */
	memcpy(&rr->chan_desc, &rr->chan_last, sizeof(*cd));

	/* change radio to old channel */
	tx_ph_dm_est_req(ms, rr->cd_now.arfcn, rr->cd_now.chan_nr);

	/* re-establish old link */
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	return gsm48_send_rsl(ms, RSL_MT_EST_REQ, nmsg);

	todo
}

/* send HANDOVER ACCESS burst (9.1.14) */
static int gsm48_rr_tx_hando_access(struct osmocom_ms *ms)
{
	nmsg = msgb_alloc_headroom(20, 16, "HAND_ACCESS");
	if (!nmsg)
		return -ENOMEM;
	*msgb_put(nmsg, 1) = rr->hando_ref;
	todo burst
	return gsm48_send_rsl(ms, RSL_MT_RAND_ACC_REQ, nmsg);
}

/* send next channel request in dedicated state */
static int gsm48_rr_rand_acc_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *nmsg;
	int s;

	if (!rr->hando_susp_state) {
		LOGP(DRR, LOGL_NOTICE, "Random acces confirm, but not in handover state.\n");
		return 0;
	}

	/* send up to four handover access bursts */
	if (rr->hando_acc_left) {
		rr->hando_acc_left--;
		gsm48_rr_tx_hando_access(ms);
		return;
	}

	/* start timer for sending next HANDOVER ACCESS bursts afterwards */
	if (!bsc_timer_pending(&rr->t3124)) {
		if (allocated channel is SDCCH)
			start_rr_t3124(rr, GSM_T3124_675);
		else
			start_rr_t3124(rr, GSM_T3124_320);
	if (!rr->n_chan_req) {
		start_rr_t3126(rr, 5, 0); /* TODO improve! */
		return 0;
	}
	rr->n_chan_req--;

	/* wait for PHYSICAL INFORMATION message or T3124 timeout */
	return 0;

}

#endif


