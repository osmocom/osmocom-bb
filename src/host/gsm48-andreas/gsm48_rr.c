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

todo:
flush rach msg in all cases: during sending, after its done, and when aborted
stop timers on abort
debugging. (wenn dies todo erledigt ist, bitte in den anderen code moven)
wird beim abbruch immer der gepufferte cm-service-request entfernt, auch beim verschicken?:
measurement reports
todo rr_sync_ind

todo change procedures, release procedure

#include <osmocore/protocol/gsm_04_08.h>
#include <osmocore/msgb.h>
#include <osmocore/gsm48.h>

static int gsm_rr_chan2cause[4] = {
	RR_EST_CAUSE_ANS_PAG_ANY,
	RR_EST_CAUSE_ANS_PAG_SDCCH,
	RR_EST_CAUSE_ANS_PAG_TCH_F,
	RR_EST_CAUSE_ANS_PAG_TCH_ANY
};

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

static int rr_recvmsg(struct osmocom_ms *ms,
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

static void stop_rr_t3126(struct gsm_rrlayer *rr)
{
	if (bsc_timer_pending(rr->t3126)) {
		DEBUGP(DRR, "stopping pending timer T3126\n");
		bsc_del_timer(&rr->t3126);
	}
}

static void stop_rr_t3122(struct gsm_rrlayer *rr)
{
	if (bsc_timer_pending(rr->t3122)) {
		DEBUGP(DRR, "stopping pending timer T3122\n");
		bsc_del_timer(&rr->t3122);
	}
	rr->t3122_running = 0;
}

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

static int gsm_match_ra(struct osmocom_ms *ms, struct gsm48_req_ref *req)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	int i;

	for (i = 0; i < 3; i++) {
		if (rr->cr_hist[i] >= 0
		 && ref->ra == rr->cr_hist[i]) {
		 	todo: match timeslot
			return 1;
		}
	}

	return 0;
}

static int gsm_rr_tx_chan_req(struct osmocom_ms *ms, int cause)
{
	struct gsm_rrlayer *rr = ms->rrlayer;

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
		struct gsm_rr rrmsg;

		DEBUGP(DRR, "CHANNEL REQUEST: with unknown establishment cause: %d\n", rrmsg->cause);
		memset(rrmsg, 0, sizeof(rrmsg));
		rrmsg->rr_cause = RR_REL_CAUSE_UNDEFINED;
		rr_recvmsg(ms, RR_REL_IND, rrmsg);
		new_rr_state(rr, GSM_RRSTATE_IDLE);
		return -EINVAL;
	}

	rr->wait_assign = 1;

	/* create and send RACH msg */
	memset(&dlmsg, 0, sizeof(dlmsg));
	dlmsg->msg = msgb_alloc(1, "CHAN_REQ");
	if (!dlmsg->msg)
		return -ENOMEM;
	*msgb_put(dlmsg->msg, 1) = chan_req;
	rr->chan_req = chan_req;
	t = ms->si.tx_integer;
	if (t < 8)
		t = 8;
	dlmsg->delay = random() % t;
	rr->cr_hist[3] = -1;
	rr->cr_hist[2] = -1;
	rr->cr_hist[1] = chan_req;
	return gsm_send_dl(ms, DL_RANDOM_ACCESS_REQ, dlmsg);
}

static int gsm_rr_est_req(struct osmocom_ms *ms, struct gsm_rr *rrmsg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm_dl dlmsg;

	/* 3.3.1.1.3.2 */
	if (rr->t3122_running) {
		if (rrmsg->cause != RR_EST_CAUSE_EMERGENCY) {
			struct gsm_rr newmsg;

			memset(newmsg, 0, sizeof(newmsg));
			newmsg->rr_cause = RR_REL_CAUSE_T3122_PENDING;
			return rr_recvmsg(ms, RR_REL_IND, newmsg);
		} else
			stop_rr_t3122(rr);
	}

	bsc_schedule_timer(&rr->t3122, sec, micro);
	/* 3.3.1.1.1 */
	if (rrmsg->cause != RR_EST_CAUSE_EMERGENCY) {
		if (!(ms->access_class & ms->si.access_class)) {
			reject:
			if (!ms->opt.access_class_override) {
				struct gsm_rr newmsg;

				memset(newmsg, 0, sizeof(newmsg));
				newmsg->rr_cause = RR_REL_CAUSE_NOT_AUTHORIZED;
				return rr_recvmsg(ms, RR_REL_IND, newmsg);
			}
		}
	} else {
		if (!(ms->access_class & ms->si.access_class)
		 && !ms->si.emergency)
		 	goto reject;
	}

	/* requested by RR */
	rr->rr_est_req = 1;

	/* store REQUEST message */
	rr->rr_est_msg = rrmsg->msg;

	return gsm_rr_tx_chan_req(ms, rrmsg->cause);
}

static int gsm_rr_data_req(struct osmocom_ms *ms, struct gsm_rr *rrmsg)
{
	struct gsm_dl dlmsg;

	/* 3.4.2 */
	memset(&dlmsg, 0, sizeof(dlmsg));
	dlmsg->msg = rrmsg->msg;
	return gsm_send_dl(ms, DL_RELEASE_REQ, dlmsg);
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

static int gsm_send_rr(struct osmocom_ms *ms, struct gsm_rr *rrmsg)
{
	int msg_type = rrmsg->msg_type;

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

	return rc;
}

static int gsm_rr_rx_pag_req_1(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	int chan_first, chan_second;

	/* 3.3.1.1.2: ignore paging while establishing */
	if (rr->state != GSM_RRSTATE_IDLE)
		return 0;

	if (payload_len < 1 + 2) {
		short:
		DEBUGP(DRR, "Short read of paging request 1 message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_first = *gh->data & 0x03;
	chan_second = (*gh->data >> 2) & 0x03;
	/* first MI */
	mi = gh->data + 1;
	if (payload_len - 1 < mi[0] + 1)
		goto short;
	if (gsm_match_mi(ms, mi) > 0)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_first]);
	/* second MI */
	payload_len -= 1 + mi[0] - 1;
	mi = gh->data + 1 + mi[0] + 1;
	if (payload_len < 3)
		return 0;
	if (mi[0] != GSM48_IE_MOBILE_ID)
		return 0;
	if (payload_len < mi[1] + 2)
		goto short;
	if (gsm_match_mi(ms, mi + 1) > 0)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_second]);

	return 0;
}

static int gsm_rr_rx_pag_req_2(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	u_int32_t tmsi;
	int chan_first, chan_second, chan_third;

	/* 3.3.1.1.2: ignore paging while establishing */
	if (rr->state != GSM_RRSTATE_IDLE)
		return 0;

	if (payload_len < 1 + 4 + 4) {
		short:
		DEBUGP(DRR, "Short read of paging request 2 message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_first = *gh->data & 0x03;
	chan_second = (*gh->data >> 2) & 0x03;
	/* first MI */
	mi = gh->data + 1;
	memcpy(&tmsi, mi, 4);
	if (ms->subscr.tmsi == ntohl(tmsi)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_first]);
	/* second MI */
	mi = gh->data + 1 + 4;
	memcpy(&tmsi, mi, 4);
	if (ms->subscr.tmsi == ntohl(tmsi)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_second]);
	/* third MI */
	payload_len -= 1 + 4 + 4;
	mi = gh->data + 1 + 4 + 4;
	if (payload_len < 3)
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

static int gsm_rr_rx_pag_req_3(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	u_int32_t tmsi;
	int chan_first, chan_second, chan_third, chan_fourth;

	/* 3.3.1.1.2: ignore paging while establishing */
	if (rr->state != GSM_RRSTATE_IDLE)
		return 0;

	if (payload_len < 1 + 4 + 4 + 4 + 4 + 1) { /* must include "channel needed" */
		short:
		DEBUGP(DRR, "Short read of paging request 3 message .\n");
		return -EINVAL;
	}

	/* channel needed */
	chan_first = *gh->data & 0x03;
	chan_second = (*gh->data >> 2) & 0x03;
	chan_third = gh->data[1 + 4 + 4 + 4 + 4] & 0x03;
	chan_fourth = (gh->data[1 + 4 + 4 + 4 + 4] >> 2) & 0x03;
	/* first MI */
	mi = gh->data + 1;
	memcpy(&tmsi, mi, 4);
	if (ms->subscr.tmsi == ntohl(tmsi)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_first]);
	/* second MI */
	mi = gh->data + 1 + 4;
	memcpy(&tmsi, mi, 4);
	if (ms->subscr.tmsi == ntohl(tmsi)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_second]);
	/* thrid MI */
	mi = gh->data + 1 + 4 + 4;
	memcpy(&tmsi, mi, 4);
	if (ms->subscr.tmsi == ntohl(tmsi)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_third]);
	/* fourth MI */
	mi = gh->data + 1 + 4 + 4 + 4;
	memcpy(&tmsi, mi, 4);
	if (ms->subscr.tmsi == ntohl(tmsi)
	 && ms->subscr.tmsi_valid)
		return gsm_rr_tx_chan_req(ms, gsm_rr_chan2cause[chan_fourth]);

	return 0;
}

static int gsm_rr_dl_est(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;

	/* 3.3.1.1.3.1 */
	stop_rr_t3126(rr);

	/* flush pending RACH requests */
	rr->n_chan_req = 0; // just to be safe
	memset(&dlmsg, 0, sizeof(dlmsg));
	gsm_send_dl(ms, DL_RANDOM_ACCESS_FLU, dlmsg);

	/* send DL_EST_REQ */
	memset(&dlmsg, 0, sizeof(dlmsg));
	memcpy(dlmsg->channel_description, rr->channel_description, 3);
	memcpy(dlmsg->mobile_alloc_lv, rr->mobile_alloc_lv, 9);
	todo starting time
	if (rr->rr_est_msg) {
		dlmsg->msg = rr->rr_est_msg;
		rr->rr_est_msg= 0;
	} else {
		todo paging response
	}
	gsm_send_dl(ms, DL_ESTABLISH_REQ, dlmsg);
}

static int gsm_rr_rx_imm_ass(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	struct gsm48_req_ref *ref;

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM_RRSTATE_CONN_PEND || !rr->wait_assign)
		return 0;

	if (payload_len < 1 + 3 + 3 + 1 + 1) {
		short:
		DEBUGP(DRR, "Short read of immediate assignment message.\n");
		return -EINVAL;
	}
	if (*gh->data[1 + 3 + 3 + 1] > 8) {
		DEBUGP(DRR, "moble allocation in immediate assignment too large.\n");
		return -EINVAL;
	}

	/* request ref */
	if (gsm_match_ra(ms, (struct gsm48_req_ref *)(gh->data + 1 + 3))) {
		/* channel description */
		memcpy(rr->chan_descr, gh->data + 1, 3);
		/* timing advance */
		rr->timing_advance = *gh->data[1 + 3 + 3];
		/* mobile allocation */
		memcpy(rr->mobile_alloc_lv, gh->data + 1 + 3 + 3 + 1, *gh->data[1 + 3 + 3 + 1] + 1);
		rr->wait_assing = 0;
		return gsm_rr_dl_est(ms);
	}

	return 0;
}

static int gsm_rr_rx_imm_ass_ext(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM_RRSTATE_CONN_PEND || !rr->wait_assign)
		return 0;

	if (payload_len < 1 + 3 + 3 + 1 + 3 + 3 + 1 + 1) {
		short:
		DEBUGP(DRR, "Short read of immediate assignment extended message.\n");
		return -EINVAL;
	}
	if (*gh->data[1 + 3 + 3 + 1 + 3 + 3 + 1] > 4) {
		DEBUGP(DRR, "moble allocation in immediate assignment extended too large.\n");
		return -EINVAL;
	}

	/* request ref */
	if (gsm_match_ra(ms, (struct gsm48_req_ref *)(gh->data + 1 + 3))) {
		/* channel description */
		memcpy(rr->chan_descr, gh->data + 1, 3);
		/* timing advance */
		rr->timing_advance = *gh->data[1 + 3 + 3];
		/* mobile allocation */
		memcpy(rr->mobile_alloc_lv, gh->data + 1 + 3 + 3 + 1 + 3 + 3 + 1, *gh->data[1 + 3 + 3 + 1 + 3 + 3 + 1] + 1);
		rr->wait_assing = 0;
		return gsm_rr_dl_est(ms);
	}
	/* request ref 2  */
	if (gsm_match_ra(ms, (struct gsm48_req_ref *)(gh->data + 1 + 3 + 3 + 1 + 3))) {
		/* channel description */
		memcpy(rr->chan_descr, gh->data + 1 + 3 + 3 + 1, 3);
		/* timing advance */
		rr->timing_advance = *gh->data[1 + 3 + 3 + 1 + 3 + 3];
		/* mobile allocation */
		memcpy(rr->mobile_alloc_lv, gh->data + 1 + 3 + 3 + 1 + 3 + 3 + 1, *gh->data[1 + 3 + 3 + 1 + 3 + 3 + 1] + 1);
		rr->wait_assing = 0;
		return gsm_rr_dl_est(ms);
	}

	return 0;
}

static int gsm_rr_rx_imm_ass_rej(struct osmocom_ms *ms, struct gsm_msgb *msg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int payload_len = msgb_l3len(msg) - sizeof(*gh);
	int i;

	/* 3.3.1.1.2: ignore assignment while idle */
	if (rr->state != GSM_RRSTATE_CONN_PEND || !rr->wait_assign)
		return 0;

	if (payload_len < 1 + 4 * (3 + 1)) {
		short:
		DEBUGP(DRR, "Short read of immediate assignment reject message.\n");
		return -EINVAL;
	}

	for (i = 0; i < 4; i++) {
		if (gsm_match_ra(ms, (struct gsm48_req_ref *)(gh->data + 1 + i * (3 + 1)))) {
			rr->wait_assing = 0;
			/* wait indication */
			t3122_value = gh->data[1 + i * (3 + 1) + 3];
			if (t3122_value) {
				start_rr_t3122(rr, t3122_value);
				t3122_running = 1;
			}
			/* release */
			if (rr->rr_est_req) {
				struct gsm_rr rrmsg;

				memset(rrmsg, 0, sizeof(rrmsg));
				rrmsg->rr_cause = RR_REL_CAUSE_CHANNEL_REJECT;
				rr_recvmsg(ms, RR_REL_IND, rrmsg);
			}
			new_rr_state(rr, GSM_RRSTATE_IDLE);
		}
	}

	return 0;
}

static int gsm_rr_unit_data_ind(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(dlmsg->msg);

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
		DEBUGP(DRR, "Message type %d unknown.\n", gh->msg_type);
		return -EINVAL;
	}
}

static int gsm_rr_data_ind(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	struct gsm_rr rrmsg;

	/* 3.4.2 */
	memset(&rrmsg, 0, sizeof(rrmsg));
	rrmsg->msg = dlmsg->msg;
	return rr_recv_msg(ms, DL_RELEASE_REQ, rrmsg);
}

static int gsm_rr_estab_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	struct gsm_rr rrmsg;

	if (rr->state == GSM_RRSTATE_IDLE) {
		struct gsm_dl dlmsg;

		memset(&dlmsg, 0, sizeof(dlmsg));
		return gsm_send_dl(ms, DL_RELEASE_REQ, dlmsg);
	}

	/* 3.3.1.1.4 */
	new_rr_state(rr, GSM_RRSTATE_DEDICATED);

	memset(rrmsg, 0, sizeof(rrmsg));
	return rr_recvmsg(ms, (rr->rr_est_req) ? RR_EST_CNF : RR_EST_IND, rrmsg);
}

static int gsm_rr_suspend_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
}

static int gsm_rr_suspend_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
}

static int gsm_rr_connect_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
}

static int gsm_rr_rel_ind(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
}

static int gsm_rr_rel_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
}

static int gsm_rr_rand_acc_cnf(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
	struct gsm_rrlayer *rr = ms->rrlayer;
	int s;

	if (!rr->n_chan_req) {
		start_rr_t3126(rr, GSM_T3126_MS);
		return 0;
	}
	rr->n_chan_req--;

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
	memset(&dlmsg, 0, sizeof(dlmsg));
	dlmsg->msg = msgb_alloc(1, "CHAN_REQ");
	if (!dlmsg->msg)
		return -ENOMEM;
	*msgb_put(dlmsg->msg, 1) = rr->chan_req;
	dlmsg->delay = (random() % ms->si.tx_integer) + s;
	rr->cr_hist[3] = rr->cr_hist[2];
	rr->cr_hist[2] = rr->cr_hist[1];
	rr->cr_hist[1] = chan_req;
	return gsm_send_dl(ms, DL_RANDOM_ACCESS_REQ, dlmsg);

}

static int gsm_rr_mdl_error_ind(struct osmocom_ms *ms, struct gsm_dl *dlmsg)
{
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
	{SBIT(GSM_RRSTATE),
	DL_SUSPEND_CNF, gsm_rr_suspend_cnf},
	{SBIT(GSM_RRSTATE),
	DL_RESUME_CNF, gsm_rr_suspend_cnf},
	{SBIT(GSM_RRSTATE),
	DL_CONNECT_CNF, gsm_rr_connect_cnf},
	{SBIT(GSM_RRSTATE),
	DL_RELEASE_IND, gsm_rr_rel_ind},
	{SBIT(GSM_RRSTATE),
	DL_RELEASE_CNF, gsm_rr_rel_cnf},
	{SBIT(GSM_RRSTATE_CONN_PEND), /* 3.3.1.1.2 */
	DL_RANDOM_ACCESS_CNF, gsm_rr_rand_acc_cnf},
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

	return rc;
}

static void timeout_rr_t3122(void *arg)
{
	struct gsm_rrlayer *rr = arg;

	rr->t3122_running = 0;
}

static void timeout_rr_t3126(void *arg)
{
	struct gsm_rrlayer *rr = arg;

	if (rr->rr_est_req) {
		struct gsm_rr rrmsg;

		memset(rrmsg, 0, sizeof(rrmsg));
		rrmsg->rr_cause = RR_REL_CAUSE_RA_FAILURE;
		rr_recvmsg(ms, RR_REL_IND, rrmsg);
	}

	new_rr_state(rr, GSM_RRSTATE_IDLE);
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

