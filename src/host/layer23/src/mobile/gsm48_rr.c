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

/* Testing delayed (immediate) assigment / handover
 *
 * When enabled, the starting time will be set by given frames in the future.
 * If a starting time is given by the network, this time is ignored.
 */
//#define TEST_STARTING_TIMER 140

/* Testing if frequency modification works correctly "after time".
 *
 * When enabled, the starting time will be set in the future.
 * A wrong channel is defined "before time", so noise is received until
 * starting time elapses.
 * If a starting time is given by the network, this time is ignored.
 * Also channel definitions "before time" are ignored.
 *
 * NOTE: TEST_STARTING_TIMER MUST be defined also.
 */
//#define TEST_FREQUENCY_MOD

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

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/networks.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/mobile/vty.h>

static void start_rr_t_meas(struct gsm48_rrlayer *rr, int sec, int micro);
static void stop_rr_t_meas(struct gsm48_rrlayer *rr);
static void stop_rr_t_starting(struct gsm48_rrlayer *rr);
static void stop_rr_t3124(struct gsm48_rrlayer *rr);
static int gsm48_rcv_rsl(struct osmocom_ms *ms, struct msgb *msg);
static int gsm48_rr_dl_est(struct osmocom_ms *ms);
static int gsm48_rr_tx_meas_rep(struct osmocom_ms *ms);
static int gsm48_rr_set_mode(struct osmocom_ms *ms, uint8_t chan_nr,
	uint8_t mode);

/*
 * support
 */

#define MIN(a, b) ((a < b) ? a : b)

int gsm48_decode_lai(struct gsm48_loc_area_id *lai, uint16_t *mcc,
	uint16_t *mnc, uint16_t *lac)
{
	*mcc = ((lai->digits[0] & 0x0f) << 8)
	     | (lai->digits[0] & 0xf0)
	     | (lai->digits[1] & 0x0f);
	*mnc = ((lai->digits[2] & 0x0f) << 8)
	     | (lai->digits[2] & 0xf0)
	     | ((lai->digits[1] & 0xf0) >> 4);
	*lac = ntohs(lai->lac);

	return 0;
}

int gsm48_encode_lai(struct gsm48_loc_area_id *lai, uint16_t mcc,
	uint16_t mnc, uint16_t lac)
{
	lai->digits[0] = (mcc >> 8) | (mcc & 0xf0);
	lai->digits[1] = (mcc & 0x0f) | (mnc << 4);
	lai->digits[2] = (mnc >> 8) | (mnc & 0xf0);
	lai->lac = htons(lac);

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

/* decode "Power Command" (10.5.2.28) and (10.5.2.28a) */
static int gsm48_decode_power_cmd_acc(struct gsm48_power_cmd *pc,
	uint8_t *power_level, uint8_t *atc)
{
	*power_level = pc->power_level;
	if (atc) /* only in case of 10.5.2.28a */
		*atc = pc->atc;

	return 0;
}

/* 10.5.2.38 decode Starting time IE */
static int gsm48_decode_start_time(struct gsm48_rr_cd *cd,
	struct gsm48_start_time *st)
{
	cd->start = 1;
	cd->start_tm.t1 = st->t1;
	cd->start_tm.t2 = st->t2;
	cd->start_tm.t3 = (st->t3_high << 3) | st->t3_low;
	cd->start_tm.fn = gsm_gsmtime2fn(&cd->start_tm);

	return 0;
}

/* decode "BA Range" (10.5.2.1a) */
static int gsm48_decode_ba_range(const uint8_t *ba, uint8_t ba_len,
	uint32_t *range, uint8_t *ranges, int max_ranges)
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
	if (required_octets > ba_len) {
		LOGP(DRR, LOGL_NOTICE, "BA range IE too short: %d ranges "
			"require %d octets, but only %d octets remain.\n",
			n, required_octets, ba_len);
		*ranges = 0;
		return -EINVAL;
	}
	if (max_ranges > n)
		LOGP(DRR, LOGL_NOTICE, "BA range %d exceed the maximum number "
			"of ranges supported by this mobile (%d).\n",
			n, max_ranges);
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
static int gsm48_decode_cell_desc(struct gsm48_cell_desc *cd, uint16_t *arfcn,
	uint8_t *ncc, uint8_t *bcc)
{
	*arfcn = (cd->arfcn_hi << 8) + cd->arfcn_lo;
	*ncc = cd->ncc;
	*bcc = cd->bcc;

	return 0;
}

/* decode "Synchronization Indication" (10.5.2.39) */
static int gsm48_decode_sync_ind(struct gsm48_rrlayer *rr,
	struct gsm48_sync_ind *si)
{
	rr->hando_sync_ind = si->si;
	rr->hando_rot = si->rot;
	rr->hando_nci = si->nci;

	return 0;
}

/* 3.1.4.3 set sequence number and increment */
static int gsm48_apply_v_sd(struct gsm48_rrlayer *rr, struct msgb *msg)
{
	struct gsm48_hdr *gh = msgb_l3(msg);
	uint8_t pdisc = gh->proto_discr & 0x0f;
	uint8_t v_sd;

	switch (pdisc) {
	case GSM48_PDISC_MM:
	case GSM48_PDISC_CC:
	case GSM48_PDISC_NC_SS:
		/* all thre pdiscs share the same V(SD) */
		pdisc = GSM48_PDISC_MM;
		// fall through
	case GSM48_PDISC_GROUP_CC:
	case GSM48_PDISC_BCAST_CC:
	case GSM48_PDISC_PDSS1:
	case GSM48_PDISC_PDSS2:
		/* extract v_sd(pdisc) */
		v_sd = (rr->v_sd >> pdisc) & 1;

		/* replace bit 7 vy v_sd */
		gh->msg_type &= 0xbf;
		gh->msg_type |= (v_sd << 6);

		/* increment V(SD) */
		rr->v_sd ^= (1 << pdisc);
		LOGP(DRR, LOGL_INFO, "Using and incrementing V(SD) = %d "
			"(pdisc %x)\n", v_sd, pdisc);
		break;
	case GSM48_PDISC_RR:
	case GSM48_PDISC_SMS:
		/* no V(VSD) is required */
		break;
	default:
		LOGP(DRR, LOGL_ERROR, "Error, V(SD) of pdisc %x not handled\n",
			pdisc);
		return -ENOTSUP;
	}

	return 0;
}

/* set channel mode if supported, or return error cause */
static uint8_t gsm48_rr_check_mode(struct osmocom_ms *ms, uint8_t chan_nr,
	uint8_t mode)
{
	struct gsm_support *sup = &ms->support;
	uint8_t ch_type, ch_subch, ch_ts;

	/* only complain if we use TCH/F or TCH/H */
	rsl_dec_chan_nr(chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ch_type != RSL_CHAN_Bm_ACCHs
	 && ch_type != RSL_CHAN_Lm_ACCHs)
		return 0;

	switch (mode) {
	case GSM48_CMODE_SIGN:
		LOGP(DRR, LOGL_INFO, "Mode: signalling\n");
		break;
	case GSM48_CMODE_SPEECH_V1:
		if (ch_type == RSL_CHAN_Bm_ACCHs) {
			if (!sup->full_v1) {
				LOGP(DRR, LOGL_NOTICE, "Not supporting "
					"full-rate speech V1\n");
				return GSM48_RR_CAUSE_CHAN_MODE_UNACCT;
			}
			LOGP(DRR, LOGL_INFO, "Mode: full-rate speech V1\n");
		} else {
			if (!sup->half_v1) {
				LOGP(DRR, LOGL_NOTICE, "Not supporting "
					"half-rate speech V1\n");
				return GSM48_RR_CAUSE_CHAN_MODE_UNACCT;
			}
			LOGP(DRR, LOGL_INFO, "Mode: half-rate speech V1\n");
		}
		break;
	case GSM48_CMODE_SPEECH_EFR:
		if (ch_type == RSL_CHAN_Bm_ACCHs) {
			if (!sup->full_v2) {
				LOGP(DRR, LOGL_NOTICE, "Not supporting "
					"full-rate speech V2\n");
				return GSM48_RR_CAUSE_CHAN_MODE_UNACCT;
			}
			LOGP(DRR, LOGL_INFO, "Mode: full-rate speech V2\n");
		} else {
			LOGP(DRR, LOGL_NOTICE, "Not supporting "
					"half-rate speech V2\n");
			return GSM48_RR_CAUSE_CHAN_MODE_UNACCT;
		}
		break;
	case GSM48_CMODE_SPEECH_AMR:
		if (ch_type == RSL_CHAN_Bm_ACCHs) {
			if (!sup->full_v3) {
				LOGP(DRR, LOGL_NOTICE, "Not supporting "
					"full-rate speech V3\n");
				return GSM48_RR_CAUSE_CHAN_MODE_UNACCT;
			}
			LOGP(DRR, LOGL_INFO, "Mode: full-rate speech V3\n");
		} else {
			if (!sup->half_v3) {
				LOGP(DRR, LOGL_NOTICE, "Not supporting "
					"half-rate speech V3\n");
				return GSM48_RR_CAUSE_CHAN_MODE_UNACCT;
			}
			LOGP(DRR, LOGL_INFO, "Mode: half-rate speech V3\n");
		}
		break;
	default:
		LOGP(DRR, LOGL_ERROR, "Mode 0x%02x not supported!\n", mode);
		return GSM48_RR_CAUSE_CHAN_MODE_UNACCT;
	}

	return 0;
}

/*
 * state transition
 */

const char *gsm48_rr_state_names[] = {
	"idle",
	"connection pending",
	"dedicated",
	"release pending",
};

static void new_rr_state(struct gsm48_rrlayer *rr, int state)
{
	if (state < 0 || state >= 
		(sizeof(gsm48_rr_state_names) / sizeof(char *)))
		return;

	/* must check against equal state */
	if (rr->state == state) {
		LOGP(DRR, LOGL_INFO, "equal state ? %s\n",
			gsm48_rr_state_names[rr->state]);
		return;
	}

	LOGP(DRR, LOGL_INFO, "new state %s -> %s\n",
		gsm48_rr_state_names[rr->state], gsm48_rr_state_names[state]);

	/* abort handover, in case of release of dedicated mode */
	if (rr->state == GSM48_RR_ST_DEDICATED) {
		/* disable handover / assign state */
		rr->modify_state = GSM48_RR_MOD_NONE;
		/* stop start_time_timer */
		stop_rr_t_starting(rr);
		/* stop handover timer */
		stop_rr_t3124(rr);
	}

	rr->state = state;

	if (state == GSM48_RR_ST_IDLE) {
		struct msgb *msg, *nmsg;
		struct gsm322_msg *em;

		/* release dedicated mode, if any */
		l1ctl_tx_dm_rel_req(rr->ms);
		rr->dm_est = 0;
		l1ctl_tx_reset_req(rr->ms, L1CTL_RES_T_FULL);
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
		/* reset ciphering */
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
		/* return to same cell after LOC.UPD. */
		if (rr->est_cause == RR_EST_CAUSE_LOC_UPD) {
			em = (struct gsm322_msg *) nmsg->data;
			em->same_cell = 1;
		}
		gsm322_c_event(rr->ms, nmsg);
		msgb_free(nmsg);
		/* reset any BA range */
		rr->ba_ranges = 0;
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

/* allocate GSM 04.06 layer 2 RSL message */
struct msgb *gsm48_rsl_msgb_alloc(void)
{
	struct msgb *msg;

	msg = msgb_alloc_headroom(RSL_ALLOC_SIZE+RSL_ALLOC_HEADROOM,
		RSL_ALLOC_HEADROOM, "GSM 04.06 RSL");
	if (!msg)
		return NULL;
	msg->l2h = msg->data;

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

/* push rsl header + release mode and send (RSL-SAP) */
static int gsm48_send_rsl_rel(struct osmocom_ms *ms, uint8_t msg_type,
				struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	rsl_rll_push_hdr(msg, msg_type, rr->cd_now.chan_nr,
		rr->cd_now.link_id, 1);

	return rslms_recvmsg(msg, ms);
}

/* enqueue messages (RSL-SAP) */
static int gsm48_rx_rsl(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	msgb_enqueue(&rr->rsl_upqueue, msg);

	return 0;
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

int gsm48_rr_start_monitor(struct osmocom_ms *ms)
{
	ms->rrlayer.monitor = 1;

	return 0;
}

int gsm48_rr_stop_monitor(struct osmocom_ms *ms)
{
	ms->rrlayer.monitor = 0;

	return 0;
}

/*
 * timers handling
 */

/* special timer to monitor measurements */
static void timeout_rr_meas(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct gsm322_cellsel *cs = &rr->ms->cellsel;
	struct rx_meas_stat *meas = &rr->ms->meas;
	struct gsm_settings *set = &rr->ms->settings;
	int rxlev, berr, snr;
	uint8_t ch_type, ch_subch, ch_ts;
	char text[256];

	if (!cs->selected) {
		return;
	} else if (!meas->frames) {
		sprintf(text, "MON: no cell info");
	} else {
		rxlev = meas->rxlev / meas->frames;
		berr = meas->berr / meas->frames;
		snr = meas->snr / meas->frames;
		sprintf(text, "MON: f=%d lev=%s snr=%2d ber=%3d "
			"LAI=%s %s %04x ID=%04x", cs->sel_arfcn,
			gsm_print_rxlev(rxlev), berr, snr, 
			gsm_print_mcc(cs->sel_mcc),
			gsm_print_mnc(cs->sel_mnc), cs->sel_lac, cs->sel_id);
		if (rr->state == GSM48_RR_ST_DEDICATED) {
			rsl_dec_chan_nr(rr->cd_now.chan_nr, &ch_type,
				&ch_subch, &ch_ts);
			sprintf(text + strlen(text), " TA=%d pwr=%d TS=%d",
			rr->cd_now.ind_ta - set->alter_delay,
			(set->alter_tx_power) ? set->alter_tx_power_value
					: rr->cd_now.ind_tx_power, ch_ts);
			if (ch_type == RSL_CHAN_SDCCH8_ACCH
			 || ch_type == RSL_CHAN_SDCCH4_ACCH)
				sprintf(text + strlen(text), "/%d", ch_subch);
		}
	}
	LOGP(DRR, LOGL_INFO, "%s\n", text);
	if (rr->monitor)
		vty_notify(rr->ms, "%s\n", text);

	if (rr->dm_est)
		gsm48_rr_tx_meas_rep(rr->ms);
	memset(meas, 0, sizeof(*meas));
	start_rr_t_meas(rr, 1, 0);
}

/* special timer to assign / handover when starting time is reached */
static void timeout_rr_t_starting(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct msgb *nmsg;

	LOGP(DRR, LOGL_INFO, "starting timer has fired\n");

	/* open channel when starting timer of IMM.ASS has fired */
	if (rr->modify_state == GSM48_RR_MOD_IMM_ASS) {
		rr->modify_state = GSM48_RR_MOD_NONE;
		gsm48_rr_dl_est(rr->ms);
		return;
	}

	/* start suspension of current link */
	LOGP(DRR, LOGL_INFO, "request suspension of data link\n");
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return;
	gsm48_send_rsl(rr->ms, RSL_MT_SUSP_REQ, nmsg);
}

/* special timer to ensure that UA is sent before disconnecting channel */
static void timeout_rr_t_rel_wait(void *arg)
{
	struct gsm48_rrlayer *rr = arg;

	LOGP(DRR, LOGL_INFO, "L2 release timer has fired, done waiting\n");

	/* return to idle now */
	new_rr_state(rr, GSM48_RR_ST_IDLE);
}

/* 3.4.13.1.1: Timeout of T3110 */
static void timeout_rr_t3110(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct osmocom_ms *ms = rr->ms;
	struct msgb *nmsg;
	uint8_t *mode;

	LOGP(DRR, LOGL_INFO, "timer T3110 has fired, release locally\n");

	new_rr_state(rr, GSM48_RR_ST_REL_PEND);

	/* disconnect the main signalling link */
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return;
	mode = msgb_put(nmsg, 2);
	mode[0] = RSL_IE_RELEASE_MODE;
	mode[1] = 1; /* local release */
	gsm48_send_rsl_rel(ms, RSL_MT_REL_REQ, nmsg);

	return;
}

static void timeout_rr_t3122(void *arg)
{
	LOGP(DRR, LOGL_INFO, "timer T3122 has fired\n");
}

static void timeout_rr_t3124(void *arg)
{
	LOGP(DRR, LOGL_INFO, "timer T3124 has fired\n");
}

static void timeout_rr_t3126(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct osmocom_ms *ms = rr->ms;

	LOGP(DRR, LOGL_INFO, "timer T3126 has fired\n");
	if (rr->rr_est_req) {
		struct msgb *msg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
		struct gsm48_rr_hdr *rrh;

		LOGP(DSUM, LOGL_INFO, "Requesting channel failed\n");
		if (!msg)
			return;
		rrh = (struct gsm48_rr_hdr *)msg->data;
		rrh->cause = RR_REL_CAUSE_RA_FAILURE;
		gsm48_rr_upmsg(ms, msg);
	}

	new_rr_state(rr, GSM48_RR_ST_IDLE);
}

static void start_rr_t_meas(struct gsm48_rrlayer *rr, int sec, int micro)
{
	rr->t_meas.cb = timeout_rr_meas;
	rr->t_meas.data = rr;
	bsc_schedule_timer(&rr->t_meas, sec, micro);
}

static void start_rr_t_rel_wait(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T_rel_wait with %d.%03d seconds\n", sec,
		micro / 1000);
	rr->t_rel_wait.cb = timeout_rr_t_rel_wait;
	rr->t_rel_wait.data = rr;
	bsc_schedule_timer(&rr->t_rel_wait, sec, micro);
}

static void start_rr_t_starting(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T_starting with %d.%03d seconds\n", sec,
		micro / 1000);
	rr->t_starting.cb = timeout_rr_t_starting;
	rr->t_starting.data = rr;
	bsc_schedule_timer(&rr->t_starting, sec, micro);
}

static void start_rr_t3110(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T3110 with %d.%03d seconds\n", sec,
		micro / 1000);
	rr->t3110.cb = timeout_rr_t3110;
	rr->t3110.data = rr;
	bsc_schedule_timer(&rr->t3110, sec, micro);
}

static void start_rr_t3122(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T3122 with %d.%03d seconds\n", sec,
		micro / 1000);
	rr->t3122.cb = timeout_rr_t3122;
	rr->t3122.data = rr;
	bsc_schedule_timer(&rr->t3122, sec, micro);
}

static void start_rr_t3124(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T3124 with %d.%03d seconds\n", sec,
		micro / 1000);
	rr->t3124.cb = timeout_rr_t3124;
	rr->t3124.data = rr;
	bsc_schedule_timer(&rr->t3124, sec, micro);
}

static void start_rr_t3126(struct gsm48_rrlayer *rr, int sec, int micro)
{
	LOGP(DRR, LOGL_INFO, "starting T3126 with %d.%03d seconds\n", sec,
		micro / 1000);
	rr->t3126.cb = timeout_rr_t3126;
	rr->t3126.data = rr;
	bsc_schedule_timer(&rr->t3126, sec, micro);
}

static void stop_rr_t_meas(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t_meas)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T_meas\n");
		bsc_del_timer(&rr->t_meas);
	}
}

static void stop_rr_t_starting(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t_starting)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T_starting\n");
		bsc_del_timer(&rr->t_starting);
	}
}

static void stop_rr_t_rel_wait(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t_rel_wait)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T_rel_wait\n");
		bsc_del_timer(&rr->t_rel_wait);
	}
}

static void stop_rr_t3110(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t3110)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T3110\n");
		bsc_del_timer(&rr->t3110);
	}
}

static void stop_rr_t3122(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t3122)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T3122\n");
		bsc_del_timer(&rr->t3122);
	}
}

static void stop_rr_t3124(struct gsm48_rrlayer *rr)
{
	if (bsc_timer_pending(&rr->t3124)) {
		LOGP(DRR, LOGL_INFO, "stopping pending timer T3124\n");
		bsc_del_timer(&rr->t3124);
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
	struct gsm_settings *set = &ms->settings;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_rr_hdr *nrrh;
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
		gsm48_generate_mid_from_imsi(buf, set->imeisv);
		/* alter MI type */
	        buf[2] = (buf[2] & ~GSM_MI_TYPE_MASK) | GSM_MI_TYPE_IMEISV;
		tlv = msgb_put(nmsg, 2 + buf[1]);
		memcpy(tlv, buf, 2 + buf[1]);
	}

	gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);

	/* send RR_SYNC_IND(ciphering) */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_SYNC_IND);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *)nmsg->data;
	nrrh->cause = RR_SYNC_CAUSE_CIPHERING;
	return gsm48_rr_upmsg(ms, nmsg);
}

/* receive ciphering mode command */
static int gsm48_rr_rx_cip_mode_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_subscriber *subscr = &ms->subscr;
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

	if (!sc)
		LOGP(DRR, LOGL_INFO, "CIPHERING MODE COMMAND (sc=%u, cr=%u)\n",
			sc, cr);
	else
		LOGP(DRR, LOGL_INFO, "CIPHERING MODE COMMAND (sc=%u, "
			"algo=A5/%d cr=%u)\n", sc, alg_id + 1, cr);

	/* 3.4.7.2 */
	if (rr->cipher_on && sc) {
		LOGP(DRR, LOGL_NOTICE, "chiphering already applied\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}

	/* check if we actually support this cipher */
	if (sc && ((alg_id == GSM_CIPHER_A5_1 && !sup->a5_1)
		|| (alg_id == GSM_CIPHER_A5_2 && !sup->a5_2)
		|| (alg_id == GSM_CIPHER_A5_3 && !sup->a5_3)
		|| (alg_id == GSM_CIPHER_A5_4 && !sup->a5_4)
		|| (alg_id == GSM_CIPHER_A5_5 && !sup->a5_5)
		|| (alg_id == GSM_CIPHER_A5_6 && !sup->a5_6)
		|| (alg_id == GSM_CIPHER_A5_7 && !sup->a5_7))) {
		LOGP(DRR, LOGL_NOTICE, "algo not supported\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}

	/* check if we have no key */
	if (sc && subscr->key_seq == 7) {
		LOGP(DRR, LOGL_NOTICE, "no key available\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}

	/* change to ciphering */
	rr->cipher_on = sc;
	rr->cipher_type = alg_id;
	if (rr->cipher_on)
		l1ctl_tx_crypto_req(ms, rr->cipher_type + 1, subscr->key, 8);
	else
		l1ctl_tx_crypto_req(ms, 0, NULL, 0);

	/* response (using the new mode) */
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
	cm->a5_1 = !sup->a5_1;
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

/* start random access */
static int gsm48_rr_chan_req(struct osmocom_ms *ms, int cause, int paging)
{
	struct gsm_support *sup = &ms->support;
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;
	uint8_t chan_req_val, chan_req_mask;
	int rc;

	LOGP(DSUM, LOGL_INFO, "Establish radio link due to %s request\n",
		(paging) ? "paging" : "mobility management");

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

	/* set assignment state */
	rr->wait_assign = 0;

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
		switch (sup->ch_cap) {
		case GSM_CAP_SDCCH:
			chan_req_mask = 0x0f;
			chan_req_val = 0x10;
			break;
		case GSM_CAP_SDCCH_TCHF:
			chan_req_mask = 0x1f;
			chan_req_val = 0x80;
			break;
		default:
			chan_req_mask = 0x0f;
			chan_req_val = 0x20;
			break;
		}
		LOGP(DRR, LOGL_INFO, "CHANNEL REQUEST: %02x (PAGING TCH/F)\n",
			chan_req_val);
		break;
	case RR_EST_CAUSE_ANS_PAG_TCH_ANY:
		switch (sup->ch_cap) {
		case GSM_CAP_SDCCH:
			chan_req_mask = 0x0f;
			chan_req_val = 0x10;
			break;
		case GSM_CAP_SDCCH_TCHF:
			chan_req_mask = 0x1f;
			chan_req_val = 0x80;
			break;
		default:
			chan_req_mask = 0x0f;
			chan_req_val = 0x30;
			break;
		}
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
		LOGP(DSUM, LOGL_INFO, "Requesting channel failed\n");

		nmsg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
		if (!nmsg)
			return -ENOMEM;
		nrrh = (struct gsm48_rr_hdr *)nmsg->data;
		nrrh->cause = RR_REL_CAUSE_UNDEFINED;
		gsm48_rr_upmsg(ms, nmsg);
		new_rr_state(rr, GSM48_RR_ST_IDLE);
		return -EINVAL;
	}

	/* store value, mask and history */
	rr->chan_req_val = chan_req_val;
	rr->chan_req_mask = chan_req_mask;
	rr->cr_hist[2].valid = 0;
	rr->cr_hist[1].valid = 0;
	rr->cr_hist[0].valid = 0;

	/* store establishment cause, so 'choose cell' selects the last cell
	 * after location updating */
	rr->est_cause = cause;

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
	struct gsm48_sysinfo *s = &ms->cellsel.sel_si;
	struct gsm_settings *set = &ms->settings;
	struct msgb *nmsg;
	struct abis_rsl_cchan_hdr *ncch;
	int slots;
	uint8_t chan_req;
	uint8_t tx_power;

	/* already assigned */
	if (rr->wait_assign == 2)
		return 0;

	/* store frame number */
	if (msg) {
		struct abis_rsl_cchan_hdr *ch = msgb_l2(msg);
		struct gsm48_req_ref *ref =
				(struct gsm48_req_ref *) (ch->data + 1);

		if (msgb_l2len(msg) < sizeof(*ch) + sizeof(*ref)) {
			LOGP(DRR, LOGL_ERROR, "CHAN_CNF too slort\n");
			return -EINVAL;
		}

		/* shift history and store */
		memcpy(&(rr->cr_hist[2]), &(rr->cr_hist[1]),
			sizeof(struct gsm48_cr_hist));
		memcpy(&(rr->cr_hist[1]), &(rr->cr_hist[0]),
			sizeof(struct gsm48_cr_hist));
		rr->cr_hist[0].valid = 1;
		rr->cr_hist[0].ref.ra = rr->cr_ra;
		rr->cr_hist[0].ref.t1 = ref->t1;
		rr->cr_hist[0].ref.t2 = ref->t2;
		rr->cr_hist[0].ref.t3_low = ref->t3_low;
		rr->cr_hist[0].ref.t3_high = ref->t3_high;
	}

	if (cs->ccch_state != GSM322_CCCH_ST_DATA) {
		LOGP(DRR, LOGL_INFO, "CCCH channel activation failed.\n");

		if (rr->rr_est_req) {
			struct msgb *msg =
				gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
			struct gsm48_rr_hdr *rrh;

			LOGP(DSUM, LOGL_INFO, "Requesting channel failed\n");
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

	LOGP(DRR, LOGL_INFO, "RANDOM ACCESS (requests left %d)\n",
		rr->n_chan_req);

	if (!rr->n_chan_req) {
		LOGP(DRR, LOGL_INFO, "Done with sending RANDOM ACCESS "
			"bursts\n");
		if (!bsc_timer_pending(&rr->t3126))
			start_rr_t3126(rr, 5, 0); /* TODO improve! */
		return 0;
	}
	rr->n_chan_req--;

	if (rr->wait_assign == 0) {
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
			break;
		case 4: case 9: case 16:
			if (s->ccch_conf != 1)
				slots = 76;
			else
				slots = 52;
			break;
		case 5: case 10: case 20:
			if (s->ccch_conf != 1)
				slots = 109;
			else
				slots = 58;
			break;
		case 6: case 11: case 25:
			if (s->ccch_conf != 1)
				slots = 163;
			else
				slots = 86;
			break;
		default:
			if (s->ccch_conf != 1)
				slots = 217;
			else
				slots = 115;
			break;
		}
	}

	chan_req = random();
	chan_req &= rr->chan_req_mask;
	chan_req |= rr->chan_req_val;

	LOGP(DRR, LOGL_INFO, "RANDOM ACCESS (Tx-integer %d combined %s "
		"S(lots) %d ra 0x%02x)\n", s->tx_integer,
		(s->ccch_conf == 1) ? "yes": "no", slots, chan_req);

	/* (re)send CHANNEL RQD with new randiom */
	nmsg = gsm48_rsl_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	ncch = (struct abis_rsl_cchan_hdr *) msgb_put(nmsg, sizeof(*ncch)
							+ 4 + 2 + 2);
	rsl_init_cchan_hdr(ncch, RSL_MT_CHAN_RQD);
	ncch->chan_nr = RSL_CHAN_RACH;
	ncch->data[0] = RSL_IE_REQ_REFERENCE;
	ncch->data[1] = chan_req;
#warning HACK: fn51 and fn_off
	ncch->data[2] = (s->ccch_conf == 1) ? 27 : 50;
	ncch->data[3] = 1 + ((random() % s->tx_integer) + slots) / 51;
	ncch->data[4] = RSL_IE_ACCESS_DELAY;
	ncch->data[5] = set->alter_delay; /* (-)=earlier (+)=later */
	ncch->data[6] = RSL_IE_MS_POWER;
	if (set->alter_tx_power) {
		tx_power = set->alter_tx_power_value;
		LOGP(DRR, LOGL_INFO, "Use alternative tx-power %d (%d dBm)\n",
			tx_power,
			ms_pwr_dbm(gsm_arfcn2band(cs->arfcn), tx_power));
	} else {
		tx_power = s->ms_txpwr_max_cch;
		/* power offset in case of DCS1800 */
		if (s->po && cs->arfcn >= 512 && cs->arfcn <= 885) {
			LOGP(DRR, LOGL_INFO, "Use MS-TXPWR-MAX-CCH power value "
				"%d (%d dBm) with offset %d dBm\n", tx_power,
				ms_pwr_dbm(gsm_arfcn2band(cs->arfcn), tx_power),
				s->po_value * 2);
			/* use reserved bits 7,8 for offset (+ X * 2dB) */
			tx_power |= s->po_value << 6;
		} else
			LOGP(DRR, LOGL_INFO, "Use MS-TXPWR-MAX-CCH power value "
				"%d (%d dBm)\n", tx_power,
				ms_pwr_dbm(gsm_arfcn2band(cs->arfcn),
							tx_power));
	}
	ncch->data[7] = tx_power;

	/* set initial indications */
	rr->cd_now.ind_tx_power = s->ms_txpwr_max_cch;
	rr->cd_now.ind_ta = set->alter_delay;

	/* store ra until confirmed, then copy it with time into cr_hist */
	rr->cr_ra = chan_req;

	return rslms_recvmsg(nmsg, ms);
}

/*
 * system information
 */

/* decode "Cell Channel Description" (10.5.2.1b) and other frequency lists */
static int decode_freq_list(struct gsm_support *sup,
	struct gsm_sysinfo_freq *f, uint8_t *cd, uint8_t len, uint8_t mask,
	uint8_t frqt)
{
	/* only Bit map 0 format for P-GSM */
	if ((cd[0] & 0xc0 & mask) != 0x00 &&
	    (sup->p_gsm && !sup->e_gsm && !sup->r_gsm && !sup->dcs_1800))
	 	return 0;

	return gsm48_decode_freq_list(f, cd, len, mask, frqt);
}

/* decode "Cell Selection Parameters" (10.5.2.4) */
static int gsm48_decode_cell_sel_param(struct gsm48_sysinfo *s,
	struct gsm48_cell_sel_par *cs)
{
#ifdef TODO
	convert ms_txpwr_max_ccch dependant on the current frequenc and support
	to the right powe level
#endif
	s->ms_txpwr_max_cch = cs->ms_txpwr_max_ccch;
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
static int gsm48_decode_mobile_alloc(struct gsm_sysinfo_freq *freq,
	uint8_t *ma, uint8_t len, uint16_t *hopping, uint8_t *hopp_len, int si4)
{
	int i, j = 0;
	uint16_t f[len << 3];

	/* not more than 64 hopping indexes allowed in IE */
	if (len > 8)
		return -EINVAL;

	/* tabula rasa */
	*hopp_len = 0;
	if (si4) {
		for (i = 0; i < 1024; i++)
			freq[i].mask &= ~FREQ_TYPE_HOPP;
	}

	/* generating list of all frequencies (1..1023,0) */
	for (i = 1; i <= 1024; i++) {
		if ((freq[i & 1023].mask & FREQ_TYPE_SERV)) {
			LOGP(DRR, LOGL_INFO, "Serving cell ARFCN #%d: %d\n",
				j, i & 1023);
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
			LOGP(DRR, LOGL_INFO, "Hopping ARFCN: %d (bit %d)\n",
				i, f[i]);
			/* index higher than entries in list ? */
			if (i >= j) {
				LOGP(DRR, LOGL_NOTICE, "Mobile Allocation "
					"hopping index %d exceeds maximum "
					"number of cell frequencies. (%d)\n",
					i + 1, j);
				break;
			}
			hopping[(*hopp_len)++] = f[i];
			if (si4)
				freq[f[i]].mask |= FREQ_TYPE_HOPP;
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
	struct bitvec bv;

	memset(&bv, 0, sizeof(bv));
	bv.data_len = len;
	bv.data = si;

	/* Optional Selection Parameters */
	if (bitvec_get_bit_high(&bv) == H) {
		s->sp = 1;
		s->sp_cbq = bitvec_get_uint(&bv, 1);
		s->sp_cro = bitvec_get_uint(&bv, 6);
		s->sp_to = bitvec_get_uint(&bv, 3);
		s->sp_pt = bitvec_get_uint(&bv, 5);
	}
	/* Optional Power Offset */
	if (bitvec_get_bit_high(&bv) == H) {
		s->po = 1;
		s->po_value = bitvec_get_uint(&bv, 3);
	}
	/* System Onformation 2ter Indicator */
	if (bitvec_get_bit_high(&bv) == H)
		s->si2ter_ind = 1;
	/* Early Classark Sending Control */
	if (bitvec_get_bit_high(&bv) == H)
		s->ecsm = 1;
	/* Scheduling if and where */
	if (bitvec_get_bit_high(&bv) == H) {
		s->sched = 1;
		s->sched_where = bitvec_get_uint(&bv, 3);
	}
	/* GPRS Indicator */
	s->gi_ra_colour = bitvec_get_uint(&bv, 3);
	s->gi_si13_pos = bitvec_get_uint(&bv, 1);
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
static int gsm48_new_sysinfo(struct osmocom_ms *ms, uint8_t type)
{
	struct gsm48_sysinfo *s = ms->cellsel.si;
	struct msgb *nmsg;
	struct gsm322_msg *em;

	/* update list of measurements, if BA(SACCH) is complete and new */
	if (s
	 && (type == GSM48_MT_RR_SYSINFO_5
	  || type == GSM48_MT_RR_SYSINFO_5bis
	  || type == GSM48_MT_RR_SYSINFO_5ter)
	 && s->si5
	 && (!s->nb_ext_ind_si5
	  || (s->si5bis && s->nb_ext_ind_si5 && !s->nb_ext_ind_si5bis)
	  || (s->si5bis && s->si5ter && s->nb_ext_ind_si5
		&& s->nb_ext_ind_si5bis))) {
		struct gsm48_rr_meas *rrmeas = &ms->rrlayer.meas;
		int n = 0, i;

		LOGP(DRR, LOGL_NOTICE, "Complete set of SI5* for BA(%d)\n",
			s->nb_ba_ind_si5);
		rrmeas->nc_num = 0;
		for (i = 1; i <= 1024; i++) {
			if ((s->freq[i & 1023].mask & FREQ_TYPE_REP)) {
				if (n == 32) {
					LOGP(DRR, LOGL_NOTICE, "SI5* report "
						"exceeds 32 BCCHs\n");
					break;
				}
				LOGP(DRR, LOGL_NOTICE, "SI5* report arfcn %d\n",
					i & 1023);
				rrmeas->nc_arfcn[n] = i & 1023;
				rrmeas->nc_rxlev[n] = -128;
				n++;
			}
		}
		rrmeas->nc_num = n;
	}

	/* send sysinfo event to other layers */
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

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 1 "
			"ignored\n");
		return -EINVAL;
	}

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
	decode_freq_list(&ms->support, s->freq,
		si->cell_channel_description,
		sizeof(si->cell_channel_description), 0xce, FREQ_TYPE_SERV);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_param(s, &si->rach_control);
	/* SI 1 Rest Octets */
	if (payload_len)
		gsm48_decode_si1_rest(s, si->rest_octets, payload_len);

	s->si1 = 1;

	return gsm48_new_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 2" message (9.1.32) */
static int gsm48_rr_rx_sysinfo2(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2 *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 2 "
			"ignored\n");
		return -EINVAL;
	}

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
	decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_NCELL_2);
	/* NCC Permitted */
	s->nb_ncc_permitted_si2 = si->ncc_permitted;
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, &si->rach_control);

	s->si2 = 1;

	return gsm48_new_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 2bis" message (9.1.33) */
static int gsm48_rr_rx_sysinfo2bis(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2bis *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 2bis"
			" ignored\n");
		return -EINVAL;
	}

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
	decode_freq_list(&ms->support, s->freq,
		si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_NCELL_2bis);
	/* RACH Control Parameter */
	gsm48_decode_rach_ctl_neigh(s, &si->rach_control);

	s->si2bis = 1;

	return gsm48_new_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 2ter" message (9.1.34) */
static int gsm48_rr_rx_sysinfo2ter(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_2ter *si = msgb_l3(msg);
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 2ter"
			" ignored\n");
		return -EINVAL;
	}

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
	s->nb_ba_ind_si2ter = (si->ext_bcch_frequency_list[0] >> 5) & 1;
	decode_freq_list(&ms->support, s->freq,
		si->ext_bcch_frequency_list,
		sizeof(si->ext_bcch_frequency_list), 0x8e,
			FREQ_TYPE_NCELL_2ter);

	s->si2ter = 1;

	return gsm48_new_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 3" message (9.1.35) */
static int gsm48_rr_rx_sysinfo3(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_3 *si = msgb_l3(msg);
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm48_sysinfo *s = cs->si;
	int payload_len = msgb_l3len(msg) - sizeof(*si);

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 3 "
			"ignored\n");
		return -EINVAL;
	}

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

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 3 (mcc %s mnc %s "
		"lac 0x%04x)\n", gsm_print_mcc(s->mcc),
		gsm_print_mnc(s->mnc), s->lac);

	s->si3 = 1;

	if (cs->ccch_mode == CCCH_MODE_NONE) {
		cs->ccch_mode = (s->ccch_conf == 1) ? CCCH_MODE_COMBINED :
			CCCH_MODE_NON_COMBINED;
		LOGP(DRR, LOGL_NOTICE, "Changing CCCH_MODE to %d\n",
			cs->ccch_mode);
		l1ctl_tx_ccch_mode_req(ms, cs->ccch_mode);
	}

	return gsm48_new_sysinfo(ms, si->header.system_information);
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

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 4 "
			"ignored\n");
		return -EINVAL;
	}

	if (payload_len < 0) {
		short_read:
		LOGP(DRR, LOGL_NOTICE, "Short read of SYSTEM INFORMATION 4 "
			"message.\n");
		return -EINVAL;
	}

	if (!s->si1) {
		LOGP(DRR, LOGL_NOTICE, "Ignoring SYSTEM INFORMATION 4 "
			"until SI 1 is received.\n");
		return -EBUSY;
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
		gsm48_decode_mobile_alloc(s->freq, data + 2, si->data[1],
			s->hopping, &s->hopp_len, 1);
		payload_len -= 2 + data[1];
		data += 2 + data[1];
	}
	/* SI 4 Rest Octets */
	if (payload_len > 0)
		gsm48_decode_si4_rest(s, data, payload_len);

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 4 (mcc %s mnc %s "
		"lac 0x%04x)\n", gsm_print_mcc(s->mcc),
		gsm_print_mnc(s->mnc), s->lac);

	s->si4 = 1;

	return gsm48_new_sysinfo(ms, si->header.system_information);
}

/* receive "SYSTEM INFORMATION 5" message (9.1.37) */
static int gsm48_rr_rx_sysinfo5(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_5 *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 5 "
			"ignored\n");
		return -EINVAL;
	}

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
	decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5);

	s->si5 = 1;

	return gsm48_new_sysinfo(ms, si->system_information);
}

/* receive "SYSTEM INFORMATION 5bis" message (9.1.38) */
static int gsm48_rr_rx_sysinfo5bis(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_5bis *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 5bis"
			" ignored\n");
		return -EINVAL;
	}

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
	decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0xce, FREQ_TYPE_REP_5bis);

	s->si5bis = 1;

	return gsm48_new_sysinfo(ms, si->system_information);
}

/* receive "SYSTEM INFORMATION 5ter" message (9.1.39) */
static int gsm48_rr_rx_sysinfo5ter(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_5ter *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 5ter"
			" ignored\n");
		return -EINVAL;
	}

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
	s->nb_multi_rep_si5ter = (si->bcch_frequency_list[0] >> 6) & 3;
	s->nb_ba_ind_si5ter = (si->bcch_frequency_list[0] >> 5) & 1;
	decode_freq_list(&ms->support, s->freq, si->bcch_frequency_list,
		sizeof(si->bcch_frequency_list), 0x8e, FREQ_TYPE_REP_5ter);

	s->si5ter = 1;

	return gsm48_new_sysinfo(ms, si->system_information);
}

/* receive "SYSTEM INFORMATION 6" message (9.1.39) */
static int gsm48_rr_rx_sysinfo6(struct osmocom_ms *ms, struct msgb *msg)
{
	/* NOTE: pseudo length is not in this structure, so we skip */
	struct gsm48_system_information_type_6 *si = msgb_l3(msg) + 1;
	struct gsm48_sysinfo *s = ms->cellsel.si;
	int payload_len = msgb_l3len(msg) - sizeof(*si) - 1;

	if (!s) {
		LOGP(DRR, LOGL_INFO, "No cell selected, SYSTEM INFORMATION 6 "
			"ignored\n");
		return -EINVAL;
	}

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
	s->nb_ncc_permitted_si6 = si->ncc_permitted;
	/* SI 6 Rest Octets */
	if (payload_len >= 4)
		gsm48_decode_si6_rest(s, si->rest_octets, payload_len);

	LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 6 (mcc %s mnc %s "
		"lac 0x%04x SACCH-timeout %d)\n", gsm_print_mcc(s->mcc),
		gsm_print_mnc(s->mnc), s->lac, s->sacch_radio_link_timeout);

	s->si6 = 1;

	return gsm48_new_sysinfo(ms, si->system_information);
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
	struct gsm322_cellsel *cs = &ms->cellsel;
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
		 && ms->subscr.mcc == cs->sel_mcc
		 && ms->subscr.mnc == cs->sel_mnc
		 && ms->subscr.lac == cs->sel_lac) {
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
		LOGP(DRR, LOGL_INFO, "PAGING ignored, we are not camping.\n");

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
		LOGP(DRR, LOGL_INFO, "PAGING ignored, we are not camping.\n");

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
	 && ms->subscr.mcc == cs->sel_mcc
	 && ms->subscr.mnc == cs->sel_mnc
	 && ms->subscr.lac == cs->sel_lac) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi1));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_1], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi1));
	/* second MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi2)
	 && ms->subscr.mcc == cs->sel_mcc
	 && ms->subscr.mnc == cs->sel_mnc
	 && ms->subscr.lac == cs->sel_lac) {
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
		LOGP(DRR, LOGL_INFO, "PAGING ignored, we are not camping.\n");

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
	 && ms->subscr.mcc == cs->sel_mcc
	 && ms->subscr.mnc == cs->sel_mnc
	 && ms->subscr.lac == cs->sel_lac) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi1));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_1], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi1));
	/* second MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi2)
	 && ms->subscr.mcc == cs->sel_mcc
	 && ms->subscr.mnc == cs->sel_mnc
	 && ms->subscr.lac == cs->sel_lac) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi2));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_2], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi2));
	/* thrid MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi3)
	 && ms->subscr.mcc == cs->sel_mcc
	 && ms->subscr.mnc == cs->sel_mnc
	 && ms->subscr.lac == cs->sel_lac) {
		LOGP(DPAG, LOGL_INFO, "TMSI %08x matches\n", ntohl(pa->tmsi3));
		return gsm48_rr_chan_req(ms, gsm48_rr_chan2cause[chan_3], 1);
	} else
		LOGP(DPAG, LOGL_INFO, "TMSI %08x (not for us)\n",
			ntohl(pa->tmsi3));
	/* fourth MI */
	if (ms->subscr.tmsi == ntohl(pa->tmsi4)
	 && ms->subscr.mcc == cs->sel_mcc
	 && ms->subscr.mnc == cs->sel_mnc
	 && ms->subscr.lac == cs->sel_lac) {
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
	uint8_t ia_t1, ia_t2, ia_t3;
	uint8_t cr_t1, cr_t2, cr_t3;

	for (i = 0; i < 3; i++) {
		/* filter confirmed RACH requests only */
		if (rr->cr_hist[i].valid && ref->ra == rr->cr_hist[i].ref.ra) {
		 	ia_t1 = ref->t1;
		 	ia_t2 = ref->t2;
		 	ia_t3 = (ref->t3_high << 3) | ref->t3_low;
			ref = &rr->cr_hist[i].ref;
		 	cr_t1 = ref->t1;
		 	cr_t2 = ref->t2;
		 	cr_t3 = (ref->t3_high << 3) | ref->t3_low;
			if (ia_t1 == cr_t1 && ia_t2 == cr_t2
			 && ia_t3 == cr_t3) {
		 		LOGP(DRR, LOGL_INFO, "request %02x matches "
					"(fn=%d,%d,%d)\n", ref->ra, ia_t1,
					ia_t2, ia_t3);
				return 1;
			} else
		 		LOGP(DRR, LOGL_INFO, "request %02x matches "
					"but not frame number (IMM.ASS "
					"fn=%d,%d,%d != RACH fn=%d,%d,%d)\n",
					ref->ra, ia_t1, ia_t2, ia_t3,
					cr_t1, cr_t2, cr_t3);
		}
	}

	return 0;
}

/* 9.1.18 IMMEDIATE ASSIGNMENT is received */
static int gsm48_rr_rx_imm_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_imm_ass *ia = msgb_l3(msg);
	int ma_len = msgb_l3len(msg) - sizeof(*ia);
	uint8_t ch_type, ch_subch, ch_ts;
	struct gsm48_rr_cd cd;
#ifndef TEST_STARTING_TIMER
	uint8_t *st, st_len;
#endif

	memset(&cd, 0, sizeof(cd));
	cd.ind_tx_power = rr->cd_now.ind_tx_power;

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
#ifdef TEST_STARTING_TIMER
	cd.start = 1;
	cd.start_tm.fn = (ms->meas.last_fn + TEST_STARTING_TIMER) % 42432;
	LOGP(DRR, LOGL_INFO, " TESTING: starting time ahead\n");
#else
	st_len = ma_len - ia->mob_alloc_len;
	st = ia->mob_alloc + ia->mob_alloc_len;
	if (st_len >= 3 && st[0] == GSM48_IE_START_TIME)
		gsm48_decode_start_time(&cd, (struct gsm48_start_time *)(st+1));
#endif

	/* decode channel description */
	LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT:\n");
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
	if (rr->state != GSM48_RR_ST_CONN_PEND || rr->wait_assign == 0) {
		LOGP(DRR, LOGL_INFO, "Not for us, no request.\n");
		return 0;
	}

	if (rr->wait_assign == 2) {
		LOGP(DRR, LOGL_INFO, "Ignoring, channel already assigned.\n");
		return 0;
	}

	/* request ref */
	if (gsm48_match_ra(ms, &ia->req_ref)) {
		/* channel description */
		memcpy(&rr->cd_now, &cd, sizeof(rr->cd_now));
		/* timing advance */
		rr->cd_now.ind_ta = ia->timing_advance;
		/* mobile allocation */
		memcpy(&rr->cd_now.mob_alloc_lv, &ia->mob_alloc_len, 
			ia->mob_alloc_len + 1);
		rr->wait_assign = 2;
		/* reset scheduler */
		LOGP(DRR, LOGL_INFO, "resetting scheduler\n");
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_SCHED);

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
#ifndef TEST_STARTING_TIMER
	uint8_t *st, st_len;
#endif

	memset(&cd1, 0, sizeof(cd1));
	cd1.ind_tx_power = rr->cd_now.ind_tx_power;
	memset(&cd2, 0, sizeof(cd2));
	cd2.ind_tx_power = rr->cd_now.ind_tx_power;

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

#ifdef TEST_STARTING_TIMER
	cd1.start = 1;
	cd2.start_tm.fn = (ms->meas.last_fn + TEST_STARTING_TIMER) % 42432;
	memcpy(&cd2, &cd1, sizeof(cd2));
	LOGP(DRR, LOGL_INFO, " TESTING: starting time ahead\n");
#else
	/* starting time */
	st_len = ma_len - ia->mob_alloc_len;
	st = ia->mob_alloc + ia->mob_alloc_len;
	if (st_len >= 3 && st[0] == GSM48_IE_START_TIME) {
		gsm48_decode_start_time(&cd1,
			(struct gsm48_start_time *)(st+1));
		memcpy(&cd2, &cd1, sizeof(cd2));
	}
#endif

	/* decode channel description */
	LOGP(DRR, LOGL_INFO, "IMMEDIATE ASSIGNMENT EXTENDED:\n");
	cd1.chan_nr = ia->chan_desc1.chan_nr;
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
	if (rr->state != GSM48_RR_ST_CONN_PEND || rr->wait_assign == 0) {
		LOGP(DRR, LOGL_INFO, "Not for us, no request.\n");
		return 0;
	}

	if (rr->wait_assign == 2) {
		LOGP(DRR, LOGL_INFO, "Ignoring, channel already assigned.\n");
		return 0;
	}

	/* request ref 1 */
	if (gsm48_match_ra(ms, &ia->req_ref1)) {
		/* channel description */
		memcpy(&rr->cd_now, &cd1, sizeof(rr->cd_now));
		/* timing advance */
		rr->cd_now.ind_ta = ia->timing_advance1;
		/* mobile allocation */
		memcpy(&rr->cd_now.mob_alloc_lv, &ia->mob_alloc_len,
			ia->mob_alloc_len + 1);
		rr->wait_assign = 2;
		/* reset scheduler */
		LOGP(DRR, LOGL_INFO, "resetting scheduler\n");
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_SCHED);

		return gsm48_rr_dl_est(ms);
	}
	/* request ref 2 */
	if (gsm48_match_ra(ms, &ia->req_ref2)) {
		/* channel description */
		memcpy(&rr->cd_now, &cd2, sizeof(rr->cd_now));
		/* timing advance */
		rr->cd_now.ind_ta = ia->timing_advance2;
		/* mobile allocation */
		memcpy(&rr->cd_now.mob_alloc_lv, &ia->mob_alloc_len,
			ia->mob_alloc_len + 1);
		rr->wait_assign = 2;
		/* reset scheduler */
		LOGP(DRR, LOGL_INFO, "resetting scheduler\n");
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_SCHED);

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
	if (rr->state != GSM48_RR_ST_CONN_PEND || rr->wait_assign == 0)
		return 0;

	if (rr->wait_assign == 2) {
		return 0;
	}

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

/* 9.1.1 ADDITIONAL ASSIGMENT is received */
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
	struct gsm48_sysinfo *s = ms->cellsel.si;
	struct rx_meas_stat *meas = &rr->ms->meas;
	struct gsm48_rr_meas *rrmeas = &rr->meas;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_meas_res *mr;
	uint8_t serv_rxlev_full = 0, serv_rxlev_sub = 0, serv_rxqual_full = 0,
		serv_rxqual_sub = 0;
	uint8_t ta, tx_power;
	uint8_t rep_ba = 0, rep_valid = 0, meas_valid = 0, multi_rep = 0;
	uint8_t n = 0, rxlev_nc[6], bsic_nc[6], bcch_f_nc[6];

	/* just in case! */
	if (!s)
		return -EINVAL;

	/* check if SI5* is completely received, check BA-IND */
	if (s->si5
	 && (!s->nb_ext_ind_si5
	  || (s->si5bis && s->nb_ext_ind_si5 && !s->nb_ext_ind_si5bis)
	  || (s->si5bis && s->si5ter && s->nb_ext_ind_si5
			&& s->nb_ext_ind_si5bis))) {
		rep_ba = s->nb_ba_ind_si5;
		if ((s->si5bis && s->nb_ext_ind_si5
		  && s->nb_ba_ind_si5bis != rep_ba)
		 || (s->si5bis && s->si5ter && s->nb_ext_ind_si5
		  && s->nb_ext_ind_si5bis && s->nb_ba_ind_si5ter != rep_ba)) {
			LOGP(DRR, LOGL_NOTICE, "BA-IND missmatch on SI5*");
		} else
			rep_valid = 1;
	}

	/* check for valid measurements, any frame must exist */
	if (meas->frames) {
		meas_valid = 1;
		serv_rxlev_full = serv_rxlev_sub = meas->rxlev / meas->frames;
		serv_rxqual_full = serv_rxqual_sub = 0; // FIXME
	}

	memset(&rxlev_nc, 0, sizeof(rxlev_nc));
	memset(&bsic_nc, 0, sizeof(bsic_nc));
	memset(&bcch_f_nc, 0, sizeof(bcch_f_nc));
	if (rep_valid) {
		int8_t strongest, current;
		uint8_t ncc;
		int i, index;

		/* multiband reporting, if not: 0 = normal reporting */
		if (s->si5 && s->si5bis && s->si5ter && s->nb_ext_ind_si5
		 && s->nb_ext_ind_si5bis)
			multi_rep = s->nb_multi_rep_si5ter;

		/* get 6 strongest measurements */
		// FIXME: multiband report
		strongest = 127; /* infinite */
		for (n = 0; n < 6; n++) {
			current = -128; /* -infinite */
			index = 0;
			for (i = 0; i < rrmeas->nc_num; i++) {
				/* only check if NCC is permitted */
				ncc = rrmeas->nc_bsic[i] >> 3;
				if ((s->nb_ncc_permitted_si6 & (1 << ncc))
				 && rrmeas->nc_rxlev[i] > current
				 && rrmeas->nc_rxlev[i] < strongest) {
					current = rrmeas->nc_rxlev[i];
					index = i;
				}
			}
			if (current == -128) /* no more found */
				break;
			rxlev_nc[n] = rrmeas->nc_rxlev[index] + 110;
			bsic_nc[n] = rrmeas->nc_bsic[index];
			bcch_f_nc[n] = index;
		}
	}

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;

	/* use indicated tx-power and TA (not the altered ones) */
	tx_power = rr->cd_now.ind_tx_power;
	// FIXME: degrade power to the max supported level
	ta = rr->cd_now.ind_ta;

	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	mr = (struct gsm48_meas_res *) msgb_put(nmsg, sizeof(*mr));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_MEAS_REP;

	/* measurement results */
	mr->rxlev_full = serv_rxlev_full;
	mr->rxlev_sub = serv_rxlev_sub;
	mr->rxqual_full = serv_rxqual_full;
	mr->rxqual_sub = serv_rxqual_sub;
	mr->dtx_used = 0; // FIXME: no DTX yet
	mr->ba_used = rep_ba;
	mr->meas_valid = !meas_valid; /* 0 = valid */
	if (rep_valid) {
		mr->no_nc_n_hi = n >> 2;
		mr->no_nc_n_lo = n & 3;
	} else {
		/* no results for serving cells */
		mr->no_nc_n_hi = 1;
		mr->no_nc_n_lo = 3;
	}
	mr->rxlev_nc1 = rxlev_nc[0];
	mr->rxlev_nc2_hi = rxlev_nc[1] >> 1;
	mr->rxlev_nc2_lo = rxlev_nc[1] & 1;
	mr->rxlev_nc3_hi = rxlev_nc[2] >> 2;
	mr->rxlev_nc3_lo = rxlev_nc[2] & 3;
	mr->rxlev_nc4_hi = rxlev_nc[3] >> 3;
	mr->rxlev_nc4_lo = rxlev_nc[3] & 7;
	mr->rxlev_nc5_hi = rxlev_nc[4] >> 4;
	mr->rxlev_nc5_lo = rxlev_nc[4] & 15;
	mr->rxlev_nc6_hi = rxlev_nc[5] >> 5;
	mr->rxlev_nc6_lo = rxlev_nc[5] & 31;
	mr->bsic_nc1_hi = bsic_nc[0] >> 3;
	mr->bsic_nc1_lo = bsic_nc[0] & 7;
	mr->bsic_nc2_hi = bsic_nc[1] >> 4;
	mr->bsic_nc2_lo = bsic_nc[1] & 15;
	mr->bsic_nc3_hi = bsic_nc[2] >> 5;
	mr->bsic_nc3_lo = bsic_nc[2] & 31;
	mr->bsic_nc4 = bsic_nc[3];
	mr->bsic_nc5 = bsic_nc[4];
	mr->bsic_nc6 = bsic_nc[5];
	mr->bcch_f_nc1 = bcch_f_nc[0];
	mr->bcch_f_nc2 = bcch_f_nc[1];
	mr->bcch_f_nc3 = bcch_f_nc[2];
	mr->bcch_f_nc4 = bcch_f_nc[3];
	mr->bcch_f_nc5_hi = bcch_f_nc[4] >> 1;
	mr->bcch_f_nc5_lo = bcch_f_nc[4] & 1;
	mr->bcch_f_nc6_hi = bcch_f_nc[5] >> 2;
	mr->bcch_f_nc6_lo = bcch_f_nc[5] & 3;

	LOGP(DRR, LOGL_INFO, "MEAS REP: pwr=%d TA=%d meas-invalid=%d "
		"rxlev-full=%d rxlev-sub=%d rxqual-full=%d rxqual-sub=%d "
		"dtx %d ba %d no-ncell-n %d\n", tx_power, ta, mr->meas_valid,
		mr->rxlev_full - 110, mr->rxlev_sub - 110,
		mr->rxqual_full, mr->rxqual_sub, mr->dtx_used, mr->ba_used,
		(mr->no_nc_n_hi << 2) | mr->no_nc_n_lo);

	msgb_tv16_push(nmsg, RSL_IE_L3_INFO,
		nmsg->tail - (uint8_t *)msgb_l3(nmsg));
	msgb_push(nmsg, 2 + 2);
	nmsg->data[0] = RSL_IE_ACCESS_DELAY;
	nmsg->data[1] = ta;
	nmsg->data[2] = RSL_IE_MS_POWER;
	nmsg->data[3] = tx_power;
	rsl_rll_push_hdr(nmsg, RSL_MT_UNIT_DATA_REQ, rr->cd_now.chan_nr,
		0x40, 1);

	return rslms_recvmsg(nmsg, ms);
}

/*
 * link establishment and release
 */

/* process "Loss Of Signal" */
int gsm48_rr_los(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	uint8_t *mode;
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;

	LOGP(DSUM, LOGL_INFO, "Radio link lost signal\n");

	/* stop T3211 if running */
	stop_rr_t3110(rr);

	switch(rr->state) {
	case GSM48_RR_ST_CONN_PEND:
		LOGP(DRR, LOGL_INFO, "LOS during RACH request\n");

		/* stop pending RACH timer */
		stop_rr_t3126(rr);
		break;
	case GSM48_RR_ST_DEDICATED:
		LOGP(DRR, LOGL_INFO, "LOS during dedicated mode, release "
			"locally\n");

		new_rr_state(rr, GSM48_RR_ST_REL_PEND);

		/* release message */
		rel_local:
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		mode = msgb_put(nmsg, 2);
		mode[0] = RSL_IE_RELEASE_MODE;
		mode[1] = 1; /* local release */
		/* start release */
		return gsm48_send_rsl_rel(ms, RSL_MT_REL_REQ, nmsg);
	case GSM48_RR_ST_REL_PEND:
		LOGP(DRR, LOGL_INFO, "LOS during RR release procedure, release "
			"locally\n");

		/* stop pending RACH timer */
		stop_rr_t3110(rr);

		/* release locally */
		goto rel_local;
	default:
		/* this should not happen */
		LOGP(DRR, LOGL_ERROR, "LOS in IDLE state, ignoring\n");
		return -EINVAL;
	}

	/* send inication to upper layer */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *)nmsg->data;
	nrrh->cause = RR_REL_CAUSE_LOST_SIGNAL;
	gsm48_rr_upmsg(ms, nmsg);

	/* return idle */
	new_rr_state(rr, GSM48_RR_ST_IDLE);
	return 0;
}

/* activation of channel in dedicated mode */
static int gsm48_rr_activate_channel(struct osmocom_ms *ms,
	struct gsm48_rr_cd *cd, uint16_t *ma, uint8_t ma_len)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_settings *set = &ms->settings;
 	struct gsm48_sysinfo *s = ms->cellsel.si;
 	struct rx_meas_stat *meas = &ms->meas;
	uint8_t ch_type, ch_subch, ch_ts;

	/* setting (new) timing advance */
	LOGP(DRR, LOGL_INFO, "setting indicated TA %d (actual TA %d)\n",
		cd->ind_ta, cd->ind_ta - set->alter_delay);
	l1ctl_tx_param_req(ms, cd->ind_ta - set->alter_delay,
			(set->alter_tx_power) ? set->alter_tx_power_value
						: cd->ind_tx_power);
	/* setting initial (invalid) measurement report, resetting SI5* */
	if (s) {
		memset(s->si5_msg, 0, sizeof(s->si5_msg));
		memset(s->si5b_msg, 0, sizeof(s->si5b_msg));
		memset(s->si5t_msg, 0, sizeof(s->si5t_msg));
		memset(meas, 0, sizeof(*meas));
	}
	rr->meas.nc_num = 0;
	stop_rr_t_meas(rr);
	start_rr_t_meas(rr, 1, 0);
	gsm48_rr_tx_meas_rep(ms);

	/* establish */
	LOGP(DRR, LOGL_INFO, "establishing channel in dedicated mode\n");
	rsl_dec_chan_nr(cd->chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ch_ts >= 6) {
		printf("Channel type %d, subch %d, ts %d not supported, "
			"exitting.\n", ch_type, ch_subch, ch_ts);
		exit(-ENOTSUP);
	}
	printf("Channel type %d, subch %d, ts %d\n", ch_type, ch_subch, ch_ts);
	if (cd->h)
		l1ctl_tx_dm_est_req_h1(ms, cd->maio, cd->hsn,
			ma, ma_len, cd->chan_nr, cd->tsc);
	else
		l1ctl_tx_dm_est_req_h0(ms, cd->arfcn, cd->chan_nr, cd->tsc);
	rr->dm_est = 1;

	if (rr->cipher_on)
		l1ctl_tx_crypto_req(ms, rr->cipher_type + 1, subscr->key, 8);

	gsm48_rr_set_mode(ms, cd->chan_nr, cd->mode);

	return 0;
}

/* frequency change of channel "after time" */
static int gsm48_rr_channel_after_time(struct osmocom_ms *ms,
	struct gsm48_rr_cd *cd, uint16_t *ma, uint8_t ma_len, uint16_t fn)
{
	struct gsm_subscriber *subscr = &ms->subscr;
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	if (cd->h)
		l1ctl_tx_dm_freq_req_h1(ms, cd->maio, cd->hsn,
			ma, ma_len, cd->tsc, fn);
	else
		l1ctl_tx_dm_freq_req_h0(ms, cd->arfcn, cd->tsc, fn);

	if (rr->cipher_on)
		l1ctl_tx_crypto_req(ms, rr->cipher_type + 1, subscr->key, 8);

	gsm48_rr_set_mode(ms, cd->chan_nr, cd->mode);

	return 0;
}

/* render list of hopping channels from channel description elements */
static int gsm48_rr_render_ma(struct osmocom_ms *ms, struct gsm48_rr_cd *cd,
	uint16_t *ma, uint8_t *ma_len)
{
	struct gsm48_sysinfo *s = ms->cellsel.si;
	struct gsm_support *sup = &ms->support;
	int i;
	uint16_t arfcn;

	/* no hopping (no MA), channel description is valid */
	if (!cd->h) {
		ma_len = 0;
		return 0;
	}

	/* decode mobile allocation */
	if (cd->mob_alloc_lv[0]) {
		struct gsm_sysinfo_freq *freq = s->freq;
	
		LOGP(DRR, LOGL_INFO, "decoding mobile allocation\n");

		if (cd->cell_desc_lv[0]) {
			LOGP(DRR, LOGL_INFO, "using cell channel descr.\n");
			if (cd->cell_desc_lv[0] != 16) {
				LOGP(DRR, LOGL_ERROR, "cell channel descr. "
					"has invalid lenght\n");
				return GSM48_RR_CAUSE_ABNORMAL_UNSPEC;
			}
			gsm48_decode_freq_list(freq, cd->cell_desc_lv + 1, 16,
				0xce, FREQ_TYPE_SERV);
		}

		gsm48_decode_mobile_alloc(freq, cd->mob_alloc_lv + 1,
			cd->mob_alloc_lv[0], ma, ma_len, 0);
		if (*ma_len < 1) {
			LOGP(DRR, LOGL_NOTICE, "mobile allocation with no "
				"frequency available\n");
			return GSM48_RR_CAUSE_NO_CELL_ALLOC_A;

		}
	} else
	/* decode frequency list */
	if (cd->freq_list_lv[0]) {
		struct gsm_sysinfo_freq f[1024];
		int j = 0;

		LOGP(DRR, LOGL_INFO, "decoding frequency list\n");

		/* get bitmap */
		if (gsm48_decode_freq_list(f, cd->freq_list_lv + 1,
			cd->freq_list_lv[0], 0xce, FREQ_TYPE_SERV)) {
			LOGP(DRR, LOGL_NOTICE, "frequency list invalid\n");
			return GSM48_RR_CAUSE_ABNORMAL_UNSPEC;
		}

		/* collect channels from bitmap (1..1023,0) */
		for (i = 1; i <= 1024; i++) {
			if ((f[i & 1023].mask & FREQ_TYPE_SERV)) {
				LOGP(DRR, LOGL_INFO, "Listed ARFCN #%d: %d\n",
					j, i);
				if (j == 64) {
					LOGP(DRR, LOGL_NOTICE, "frequency list "
						"exceeds 64 entries!\n");
					return GSM48_RR_CAUSE_ABNORMAL_UNSPEC;
				}
				ma[j++] = i;
			}
		}
		*ma_len = j;
		return 0;
	} else
	/* decode frequency channel sequence */
	if (cd->freq_seq_lv[0]) {
		int j = 0, inc;

		LOGP(DRR, LOGL_INFO, "decoding frequency channel sequence\n");

		if (cd->freq_seq_lv[0] != 9) {
			LOGP(DRR, LOGL_NOTICE, "invalid frequency channel "
				"sequence\n");
			return GSM48_RR_CAUSE_ABNORMAL_UNSPEC;
		}
		arfcn = cd->freq_seq_lv[1] & 0x7f;
		LOGP(DRR, LOGL_INFO, "Listed Sequence ARFCN #%d: %d\n", j,
			arfcn);
		ma[j++] = arfcn;
		for (i = 0; i <= 16; i++) {
			if ((i & 1))
				inc = cd->freq_seq_lv[2 + (i >> 1)] & 0x0f;
			else
				inc = cd->freq_seq_lv[2 + (i >> 1)] >> 4;
			if (inc) {
				arfcn += inc;
				LOGP(DRR, LOGL_INFO, "Listed Sequence ARFCN "
					"#%d: %d\n", j, arfcn);
				ma[j++] = arfcn;
			} else
				arfcn += 15;
		}
		*ma_len = j;
		return 0;
	} else {
		LOGP(DRR, LOGL_NOTICE, "hopping, but nothing that tells us "
			"a sequence\n");
		return GSM48_RR_CAUSE_ABNORMAL_UNSPEC;
	}

	/* check for unsported frequency */
	for (i = 0; i < *ma_len; i++) {
		arfcn = ma[i];
		if (!(sup->freq_map[arfcn >> 3] & (1 << (arfcn & 7)))) {
			LOGP(DRR, LOGL_NOTICE, "Hopping frequency %d not "
				"supported\n", arfcn);
			return GSM48_RR_CAUSE_FREQ_NOT_IMPL;
		}
	}

	return 0;
}

/* activate link and send establish request */
static int gsm48_rr_dl_est(struct osmocom_ms *ms)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct gsm_subscriber *subscr = &ms->subscr;
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_pag_rsp *pr;
	uint8_t mi[11];
	uint16_t ma[64];
	uint8_t ma_len;

	/* check if we have to change channel at starting time (we delay) */
	if (rr->cd_now.start) {
		int32_t now, start, diff;
		uint32_t start_mili = 0;

		/* how much time do we have left? */
		now = ms->meas.last_fn % 42432;
		start = rr->cd_now.start_tm.fn % 42432;
		diff = start - now;
		if (diff < 0)
			diff += 42432;
		LOGP(DRR, LOGL_INFO, " (Tnow %d Tstart %d diff %d)\n",
			now, start, diff);
		start_mili = (uint32_t)diff * 19580 / 42432 * 10;
		if (diff >= 32024 || !start_mili) {
			LOGP(DRR, LOGL_INFO, " -> Start time already "
				"elapsed\n");
			rr->cd_now.start = 0;
		} else {
			LOGP(DRR, LOGL_INFO, " -> Start time is %d ms in the "
				"future\n", start_mili);
		}

#ifndef TEST_FREQUENCY_MOD
		/* schedule start of IMM.ASS */
		rr->modify_state = GSM48_RR_MOD_IMM_ASS;
		start_rr_t_starting(rr, start_mili / 1000,
			(start_mili % 1000) * 1000);
		/* when timer fires, start time is already elapsed */
		rr->cd_now.start = 0;

		return 0;
#endif
	}

	/* get hopping sequence, if required */
	if (gsm48_rr_render_ma(ms, &rr->cd_now, ma, &ma_len))
		return -EINVAL;

	/* 3.3.1.1.3.1 */
	stop_rr_t3126(rr);

	/* clear all sequence numbers for all possible PDs */
	rr->v_sd = 0;

	/* send DL_EST_REQ */
	if (rr->rr_est_msg) {
		LOGP(DRR, LOGL_INFO, "sending establish message\n");

		/* use queued message */
		nmsg = rr->rr_est_msg;
		rr->rr_est_msg = 0;

		/* set sequence number and increment */
		gsm48_apply_v_sd(rr, nmsg);
	} else {
		/* create paging response */
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
		gh->proto_discr = GSM48_PDISC_RR;
		gh->msg_type = GSM48_MT_RR_PAG_RESP;
		pr = (struct gsm48_pag_rsp *) msgb_put(nmsg, sizeof(*pr));
		/* key sequence */
		pr->key_seq = subscr->key_seq;
		/* classmark 2 */
		pr->cm2_len = sizeof(pr->cm2);
		gsm48_rr_enc_cm2(ms, &pr->cm2);
		/* mobile identity */
		if (ms->subscr.tmsi != 0xffffffff
		 && ms->subscr.mcc == cs->sel_mcc
		 && ms->subscr.mnc == cs->sel_mnc
		 && ms->subscr.lac == cs->sel_lac) {
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

#ifdef TEST_FREQUENCY_MOD
	LOGP(DRR, LOGL_INFO, " TESTING: frequency modify IMM.ASS\n");
	memcpy(&rr->cd_before, &rr->cd_now, sizeof(rr->cd_before));
	rr->cd_before.h = 0;
	rr->cd_before.arfcn = 0;
	/* activate channel */
	gsm48_rr_activate_channel(ms, &rr->cd_before, ma, ma_len);
	/* render channel "after time" */
	gsm48_rr_render_ma(ms, &rr->cd_now, ma, &ma_len);
	/* schedule change of channel */
	gsm48_rr_channel_after_time(ms, &rr->cd_now, ma, ma_len,
		rr->cd_now.start_tm.fn);
#else
	/* activate channel */
	gsm48_rr_activate_channel(ms, &rr->cd_now, ma, ma_len);
#endif

	/* start establishmnet */
	return gsm48_send_rsl(ms, RSL_MT_EST_REQ, nmsg);
}

/* the link is established */
static int gsm48_rr_estab_cnf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	uint8_t *mode;
	struct msgb *nmsg;

	/* if MM has releases before confirm, we start release */
	if (rr->state == GSM48_RR_ST_REL_PEND) {
		LOGP(DRR, LOGL_INFO, "MM already released RR.\n");
		/* release message */
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		mode = msgb_put(nmsg, 2);
		mode[0] = RSL_IE_RELEASE_MODE;
		mode[1] = 0; /* normal release */
		/* start release */
		return gsm48_send_rsl_rel(ms, RSL_MT_REL_REQ, nmsg);
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

/* the link is released in pending state (by l2) */
static int gsm48_rr_rel_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;

	LOGP(DSUM, LOGL_INFO, "Radio link is released\n");

	/* send inication to upper layer */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *)nmsg->data;
	nrrh->cause = RR_REL_CAUSE_NORMAL;
	gsm48_rr_upmsg(ms, nmsg);

	/* start release timer, so UA will be transmitted */
	start_rr_t_rel_wait(rr, 1, 500000);

	/* pending release */
	new_rr_state(rr, GSM48_RR_ST_REL_PEND);

	return 0;
}

/* 9.1.7 CHANNEL RELEASE is received  */
static int gsm48_rr_rx_chan_rel(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_chan_rel *cr = (struct gsm48_chan_rel *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*cr);
	struct tlv_parsed tp;
	struct msgb *nmsg;
	uint8_t *mode;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of CHANNEL RELEASE "
			"message.\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}
	tlv_parse(&tp, &gsm48_rr_att_tlvdef, cr->data, payload_len, 0, 0);

	LOGP(DRR, LOGL_INFO, "channel release request with cause 0x%02x)\n",
		cr->rr_cause);

	/* BA range */
	if (TLVP_PRESENT(&tp, GSM48_IE_BA_RANGE)) {
		gsm48_decode_ba_range(TLVP_VAL(&tp, GSM48_IE_BA_RANGE),
			*(TLVP_VAL(&tp, GSM48_IE_BA_RANGE) - 1), rr->ba_range,
			&rr->ba_ranges,
			sizeof(rr->ba_range) / sizeof(rr->ba_range[0]));
		/* NOTE: the ranges are kept until IDLE state is returned
		 * (see new_rr_state)
		 */
	}

	new_rr_state(rr, GSM48_RR_ST_REL_PEND);

	/* start T3110, so that two DISCs can be sent due to T200 timeout */
	start_rr_t3110(rr, 1, 500000);

	/* disconnect the main signalling link */
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	mode = msgb_put(nmsg, 2);
	mode[0] = RSL_IE_RELEASE_MODE;
	mode[1] = 0; /* normal release */
	return gsm48_send_rsl_rel(ms, RSL_MT_REL_REQ, nmsg);
}

/*
 * frequency redefition, chanel mode modify, assignment, and handover
 */

/* set channel mode in case of TCH */
static int gsm48_rr_set_mode(struct osmocom_ms *ms, uint8_t chan_nr,
	uint8_t mode)
{
	uint8_t ch_type, ch_subch, ch_ts;

	/* only apply mode to TCH/F or TCH/H */
	rsl_dec_chan_nr(chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ch_type != RSL_CHAN_Bm_ACCHs
	 && ch_type != RSL_CHAN_Lm_ACCHs)
		return -ENOTSUP;

	/* setting (new) timing advance */
	LOGP(DRR, LOGL_INFO, "setting TCH mode to %d\n", mode);
	l1ctl_tx_tch_mode_req(ms, mode);

	return 0;
}

/* 9.1.13 FREQUENCY REDEFINITION is received */
static int gsm48_rr_rx_frq_redef(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_frq_redef *fr = msgb_l3(msg);
	int mob_al_len = msgb_l3len(msg) - sizeof(*fr);
	uint8_t ch_type, ch_subch, ch_ts;
	struct gsm48_rr_cd cd;
	uint8_t cause;
	uint8_t *st;
	uint16_t ma[64];
	uint8_t ma_len;

	memcpy(&cd, &rr->cd_now, sizeof(cd));

	if (mob_al_len < 0 /* mobile allocation IE must be included */
	 || fr->mob_alloc_len + 2 > mob_al_len) { /* short read of IE */
		LOGP(DRR, LOGL_NOTICE, "Short read of FREQUENCY REDEFINITION "
			"message.\n");
		return -EINVAL;
	}
	if (fr->mob_alloc_len > 8) {
		LOGP(DRR, LOGL_NOTICE, "Moble allocation in FREQUENCY "
			"REDEFINITION too large.\n");
		return -EINVAL;
	}

	/* decode channel description */
	LOGP(DRR, LOGL_INFO, "FREQUENCY REDEFINITION:\n");
	cd.chan_nr = fr->chan_desc.chan_nr;
	rsl_dec_chan_nr(cd.chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (fr->chan_desc.h0.h) {
		cd.h = 1;
		gsm48_decode_chan_h1(&fr->chan_desc, &cd.tsc, &cd.maio,
			&cd.hsn);
		LOGP(DRR, LOGL_INFO, " (MAIO %u HSN %u TS %u SS %u TSC %u)\n",
			cd.maio, cd.hsn, ch_ts, ch_subch, cd.tsc);
	} else {
		cd.h = 0;
		gsm48_decode_chan_h0(&fr->chan_desc, &cd.tsc, &cd.arfcn);
		LOGP(DRR, LOGL_INFO, " (ARFCN %u TS %u SS %u TSC %u)\n",
			cd.arfcn, ch_ts, ch_subch, cd.tsc);
	}

	/* mobile allocation */
	memcpy(&rr->cd_now.mob_alloc_lv, &fr->mob_alloc_len, 
		fr->mob_alloc_len + 1);

	/* starting time */
	st = fr->mob_alloc + fr->mob_alloc_len;
	gsm48_decode_start_time(&cd, (struct gsm48_start_time *)(st+1));

	/* cell channel description */
	if (mob_al_len >= fr->mob_alloc_len + 2 + 17
	 && fr->mob_alloc[fr->mob_alloc_len + 2] == GSM48_IE_CELL_CH_DESC) {
		const uint8_t *v = fr->mob_alloc + fr->mob_alloc_len + 3 + 1;

		LOGP(DRR, LOGL_INFO, " using cell channel description)\n");
		memcpy(&cd.cell_desc_lv + 1, v, 17);
		cd.cell_desc_lv[0] = 16;
	}

	/* render channel "after time" */
	cause = gsm48_rr_render_ma(ms, &rr->cd_now, ma, &ma_len);
	if (cause)
		return gsm48_rr_tx_rr_status(ms, cause);

	/* update to new channel data */
	memcpy(&rr->cd_now, &cd, sizeof(rr->cd_now));

	/* schedule change of channel */
	gsm48_rr_channel_after_time(ms, &rr->cd_now, ma, ma_len,
		rr->cd_now.start_tm.fn);

	rr->cd_now.start = 0;

	return 0;
}

/* 9.1.6 sending CHANNEL MODE MODIFY ACKNOWLEDGE */
static int gsm48_rr_tx_chan_modify_ack(struct osmocom_ms *ms,
		struct gsm48_chan_desc *cd, uint8_t mode)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_chan_mode_modify *cm;

	LOGP(DRR, LOGL_INFO, "CHAN.MODE.MOD ACKNOWLEDGE\n");

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	cm = (struct gsm48_chan_mode_modify *) msgb_put(nmsg, sizeof(*cm));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_CHAN_MODE_MODIF_ACK;

	/* CD */
	memcpy(&cm->chan_desc, cd, sizeof(struct gsm48_chan_desc));
	/* mode */
	cm->mode = mode;

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/* 9.1.5 CHANNEL MODE MODIFY is received */
static int gsm48_rr_rx_chan_modify(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_chan_mode_modify *cm =
		(struct gsm48_chan_mode_modify *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*cm);
	struct gsm48_rr_cd *cd = &rr->cd_now;
	uint8_t ch_type, ch_subch, ch_ts;
	uint8_t cause;

	LOGP(DRR, LOGL_INFO, "CHANNEL MODE MODIFY\n");


	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of CHANNEL MODE MODIFY "
			"message.\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}

	/* decode channel description */
	cd->chan_nr = cm->chan_desc.chan_nr;
	rsl_dec_chan_nr(cd->chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (cm->chan_desc.h0.h) {
		cd->h = 1;
		gsm48_decode_chan_h1(&cm->chan_desc, &cd->tsc, &cd->maio,
			&cd->hsn);
		LOGP(DRR, LOGL_INFO, " (chan_nr 0x%02x MAIO %u HSN %u TS %u "
			"SS %u TSC %u mode %u)\n", cm->chan_desc.chan_nr,
			cd->maio, cd->hsn, ch_ts, ch_subch, cd->tsc, cm->mode);
	} else {
		cd->h = 0;
		gsm48_decode_chan_h0(&cm->chan_desc, &cd->tsc, &cd->arfcn);
		LOGP(DRR, LOGL_INFO, " (chan_nr 0x%02x ARFCN %u TS %u SS %u "
			"TSC %u mode %u)\n", cm->chan_desc.chan_nr, cd->arfcn,
			ch_ts, ch_subch, cd->tsc, cm->mode);
	}
	/* mode */
	cause = gsm48_rr_check_mode(ms, cd->chan_nr, cm->mode);
	if (cause)
		return gsm48_rr_tx_rr_status(ms, cause);
	cd->mode = cm->mode;
	gsm48_rr_set_mode(ms, cd->chan_nr, cd->mode);

	return gsm48_rr_tx_chan_modify_ack(ms, &cm->chan_desc, cm->mode);
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

/* 9.1.2 ASSIGNMENT COMMAND is received */
static int gsm48_rr_rx_ass_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_ass_cmd *ac = (struct gsm48_ass_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*ac);
	struct tlv_parsed tp;
	struct gsm48_rr_cd *cda = &rr->cd_after;
	struct gsm48_rr_cd *cdb = &rr->cd_before;
	uint8_t ch_type, ch_subch, ch_ts;
	uint8_t before_time = 0;
	uint16_t ma[64];
	uint8_t ma_len;
	uint32_t start_mili = 0;
	uint8_t cause;
	struct msgb *nmsg;


	LOGP(DRR, LOGL_INFO, "ASSIGNMENT COMMAND\n");

	memset(cda, 0, sizeof(*cda));
	cda->ind_tx_power = rr->cd_now.ind_tx_power;
	memset(cdb, 0, sizeof(*cdb));
	cdb->ind_tx_power = rr->cd_now.ind_tx_power;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of ASSIGNMENT COMMAND "
			"message.\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}
	tlv_parse(&tp, &gsm48_rr_att_tlvdef, ac->data, payload_len, 0, 0);

	/* decode channel description (before time) */
	if (TLVP_PRESENT(&tp, GSM48_IE_CH_DESC_1_BEFORE)) {
		struct gsm48_chan_desc *ccd = (struct gsm48_chan_desc *)
			TLVP_VAL(&tp, GSM48_IE_CH_DESC_1_BEFORE);
		cdb->chan_nr = ccd->chan_nr;
		rsl_dec_chan_nr(cdb->chan_nr, &ch_type, &ch_subch, &ch_ts);
		if (ccd->h0.h) {
			cdb->h = 1;
			gsm48_decode_chan_h1(ccd, &cdb->tsc, &cdb->maio,
				&cdb->hsn);
			LOGP(DRR, LOGL_INFO, " before: (chan_nr 0x%02x MAIO %u "
				"HSN %u TS %u SS %u TSC %u)\n", ccd->chan_nr,
				cdb->maio, cdb->hsn, ch_ts, ch_subch, cdb->tsc);
		} else {
			cdb->h = 0;
			gsm48_decode_chan_h0(ccd, &cdb->tsc, &cdb->arfcn);
			LOGP(DRR, LOGL_INFO, " before: (chan_nr 0x%02x "
				"ARFCN %u TS %u SS %u TSC %u)\n", ccd->chan_nr,
				cdb->arfcn, ch_ts, ch_subch, cdb->tsc);
		}
		before_time = 1;
	}

	/* decode channel description (after time) */
	cda->chan_nr = ac->chan_desc.chan_nr;
	rsl_dec_chan_nr(cda->chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ac->chan_desc.h0.h) {
		cda->h = 1;
		gsm48_decode_chan_h1(&ac->chan_desc, &cda->tsc, &cda->maio,
			&cda->hsn);
		LOGP(DRR, LOGL_INFO, " after: (chan_nr 0x%02x MAIO %u HSN %u "
			"TS %u SS %u TSC %u)\n", ac->chan_desc.chan_nr,
			cda->maio, cda->hsn, ch_ts, ch_subch, cda->tsc);
	} else {
		cda->h = 0;
		gsm48_decode_chan_h0(&ac->chan_desc, &cda->tsc, &cda->arfcn);
		LOGP(DRR, LOGL_INFO, " after: (chan_nr 0x%02x ARFCN %u TS %u "
			"SS %u TSC %u)\n", ac->chan_desc.chan_nr,
			cda->arfcn, ch_ts, ch_subch, cda->tsc);
	}

	/* starting time */
#ifdef TEST_STARTING_TIMER
	cda->start = 1;
	cda->start_tm.fn = (ms->meas.last_fn + TEST_STARTING_TIMER) % 42432;
	LOGP(DRR, LOGL_INFO, " TESTING: starting time ahead\n");
#else
	if (TLVP_PRESENT(&tp, GSM48_IE_START_TIME)) {	
		gsm48_decode_start_time(cda, (struct gsm48_start_time *) 
			TLVP_VAL(&tp, GSM48_IE_START_TIME));
		/* 9.1.2.5 "... before time IE is not present..." */
		if (!before_time) {
			LOGP(DRR, LOGL_INFO, " -> channel description after "
				"time only, but starting time\n");
		} else
			LOGP(DRR, LOGL_INFO, " -> channel description before "
				"and after time\n");
	} else {
		/* 9.1.2.5 "... IEs unnecessary in this message." */
		if (before_time) {
			before_time = 0;
			LOGP(DRR, LOGL_INFO, " -> channel description before "
				"time, but no starting time, ignoring!\n");
		}
	}
#endif

	/* mobile allocation / frequency list after time */
	if (cda->h) {
		if (TLVP_PRESENT(&tp, GSM48_IE_MA_AFTER)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_MA_AFTER) - 1;

			LOGP(DRR, LOGL_INFO, " after: hopping required and "
				"mobile allocation available\n");
			if (*lv + 1 > sizeof(cda->mob_alloc_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cda->mob_alloc_lv, lv, *lv + 1);
		} else
		if (TLVP_PRESENT(&tp, GSM48_IE_FREQ_L_AFTER)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_FREQ_L_AFTER) - 1;

			LOGP(DRR, LOGL_INFO, " after: hopping required and "
				"frequency list available\n");
			if (*lv + 1 > sizeof(cda->freq_list_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cda->freq_list_lv, lv, *lv + 1);
		} else {
			LOGP(DRR, LOGL_NOTICE, " after: hopping required, but "
				"no mobile allocation / frequency list\n");
		}
	}

	/* mobile allocation / frequency list before time */
	if (cdb->h) {
		if (TLVP_PRESENT(&tp, GSM48_IE_MA_BEFORE)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_MA_BEFORE) - 1;

			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"mobile allocation available\n");
			if (*lv + 1 > sizeof(cdb->mob_alloc_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cdb->mob_alloc_lv, lv, *lv + 1);
		} else
		if (TLVP_PRESENT(&tp, GSM48_IE_FREQ_L_BEFORE)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_FREQ_L_BEFORE) - 1;

			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"frequency list available\n");
			if (*lv + 1 > sizeof(cdb->freq_list_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cdb->freq_list_lv, lv, *lv + 1);
		} else
		if (TLVP_PRESENT(&tp, GSM48_IE_F_CH_SEQ_BEFORE)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_F_CH_SEQ_BEFORE) - 1;

			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"frequency channel sequence available\n");
			if (*lv + 1 > sizeof(cdb->freq_seq_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cdb->freq_seq_lv, lv, *lv + 1);
		} else
		if (cda->mob_alloc_lv[0]) {
			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"mobile allocation not available, using "
				"mobile allocation after time\n");
			memcpy(&cdb->mob_alloc_lv, &cda->mob_alloc_lv,
				sizeof(cdb->mob_alloc_lv));
		} else
		if (cda->freq_list_lv[0]) {
			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"frequency list not available, using "
				"frequency list after time\n");
			memcpy(&cdb->freq_list_lv, &cda->freq_list_lv,
				sizeof(cdb->freq_list_lv));
		} else {
			LOGP(DRR, LOGL_NOTICE, " before: hopping required, but "
				"no mobile allocation / frequency list\n");
		}
	}

	/* cell channel description */
	if (TLVP_PRESENT(&tp, GSM48_IE_CELL_CH_DESC)) {
		const uint8_t *lv = TLVP_VAL(&tp, GSM48_IE_CELL_CH_DESC) - 1;

		LOGP(DRR, LOGL_INFO, " both: using cell channel description "
			"in case of mobile allocation\n");
		if (*lv + 1 > sizeof(cdb->cell_desc_lv)) {
			LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
			return -ENOMEM;
		}
		memcpy(&cdb->cell_desc_lv, lv, *lv + 1);
		memcpy(&cda->cell_desc_lv, lv, *lv + 1);
	} else {
		/* keep old */
		memcpy(&cdb->cell_desc_lv, &rr->cd_now.cell_desc_lv,
			sizeof(cdb->cell_desc_lv));
		memcpy(&cda->cell_desc_lv, &rr->cd_now.cell_desc_lv,
			sizeof(cda->cell_desc_lv));
	}

	/* channel mode */
	if (TLVP_PRESENT(&tp, GSM48_IE_CHANMODE_1)) {
		cda->mode = cdb->mode = *TLVP_VAL(&tp, GSM48_IE_CHANMODE_1);
		LOGP(DRR, LOGL_INFO, " both: changing channel mode 0x%02x\n",
			cda->mode);
	} else
		cda->mode = cdb->mode = rr->cd_now.mode;

	/* cipher mode setting */
	if (TLVP_PRESENT(&tp, GSM48_IE_CIP_MODE_SET)) {
		cda->cipher = cdb->cipher =
			*TLVP_VAL(&tp, GSM48_IE_CIP_MODE_SET);
		LOGP(DRR, LOGL_INFO, " both: changing cipher mode 0x%02x\n",
			cda->cipher);
	} else
		cda->cipher = cdb->cipher = rr->cd_now.cipher;

	/* power command and TA (before and after time) */
	gsm48_decode_power_cmd_acc(
		(struct gsm48_power_cmd *) &ac->power_command,
		&cda->ind_tx_power, NULL);
	cdb->ind_tx_power = cda->ind_tx_power;
	cda->ind_ta = cdb->ind_ta = rr->cd_now.ind_ta; /* same cell */
	LOGP(DRR, LOGL_INFO, " both: (tx_power %d TA %d)\n", cda->ind_tx_power,
		cda->ind_ta);

	/* check if we have to change channel at starting time */
	if (cda->start) {
		int32_t now, start, diff;

		/* how much time do we have left? */
		now = ms->meas.last_fn % 42432;
		start = cda->start_tm.fn % 42432;
		diff = start - now;
		if (diff < 0)
			diff += 42432;
		LOGP(DRR, LOGL_INFO, " after: (Tnow %d Tstart %d diff %d)\n",
			now, start, diff);
		start_mili = (uint32_t)diff * 19580 / 42432 * 10;
		if (diff >= 32024 || !start_mili) {
			LOGP(DRR, LOGL_INFO, " -> Start time already "
				"elapsed\n");
			before_time = 0;
			cda->start = 0;
		} else {
			LOGP(DRR, LOGL_INFO, " -> Start time is %d ms in the "
				"future\n", start_mili);
		}
	}

	/* check if channels are valid */
	cause = gsm48_rr_check_mode(ms, cda->chan_nr, cda->mode);
	if (cause)
		return gsm48_rr_tx_ass_fail(ms, cause);
	if (before_time) {
		cause = gsm48_rr_render_ma(ms, cdb, ma, &ma_len);
		if (cause)
			return gsm48_rr_tx_ass_fail(ms, cause);
	}
	cause = gsm48_rr_render_ma(ms, cda, ma, &ma_len);
	if (cause)
		return gsm48_rr_tx_ass_fail(ms, cause);


#if 0
	if (not supported) {
		LOGP(DRR, LOGL_NOTICE, "New channel is not supported.\n");
		return GSM48_RR_CAUSE_CHAN_MODE_UNACCEPT;
	}
#endif

#ifdef TEST_FREQUENCY_MOD
	LOGP(DRR, LOGL_INFO, " TESTING: frequency modify ASS.CMD\n");
	before_time = 1;
	memcpy(cdb, cda, sizeof(*cdb));
	cdb->h = 0;
	cdb->arfcn = 0;
#endif

	/* schedule start of assignment */
	rr->modify_state = GSM48_RR_MOD_ASSIGN;
	if (!before_time && cda->start) {
		start_rr_t_starting(rr, start_mili / 1000, start_mili % 1000);
		/* when timer fires, start time is already elapsed */
		cda->start = 0;

		return 0;
	}

	/* if no starting time, start suspension of current link directly */
	LOGP(DRR, LOGL_INFO, "request suspension of data link\n");
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gsm48_send_rsl(ms, RSL_MT_SUSP_REQ, nmsg);

	return 0;
}

/* 9.1.16 sending HANDOVER COMPLETE */
static int gsm48_rr_tx_hando_cpl(struct osmocom_ms *ms, uint8_t cause)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_ho_cpl *hc;

	LOGP(DRR, LOGL_INFO, "HANDOVER COMPLETE (cause #%d)\n", cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	hc = (struct gsm48_ho_cpl *) msgb_put(nmsg, sizeof(*hc));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_HANDO_COMPL;

	/* RR_CAUSE */
	hc->rr_cause = cause;

	// FIXME: mobile observed time

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/* 9.1.4 sending HANDOVER FAILURE */
static int gsm48_rr_tx_hando_fail(struct osmocom_ms *ms, uint8_t cause)
{
	struct msgb *nmsg;
	struct gsm48_hdr *gh;
	struct gsm48_ho_fail *hf;

	LOGP(DRR, LOGL_INFO, "HANDOVER FAILURE (cause #%d)\n", cause);

	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*gh));
	hf = (struct gsm48_ho_fail *) msgb_put(nmsg, sizeof(*hf));

	gh->proto_discr = GSM48_PDISC_RR;
	gh->msg_type = GSM48_MT_RR_ASS_COMPL;

	/* RR_CAUSE */
	hf->rr_cause = cause;

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg);
}

/* receiving HANDOVER COMMAND message (9.1.15) */
static int gsm48_rr_rx_hando_cmd(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm48_hdr *gh = msgb_l3(msg);
	struct gsm48_ho_cmd *ho = (struct gsm48_ho_cmd *)gh->data;
	int payload_len = msgb_l3len(msg) - sizeof(*gh) - sizeof(*ho);
	struct tlv_parsed tp;
	struct gsm48_rr_cd *cda = &rr->cd_after;
	struct gsm48_rr_cd *cdb = &rr->cd_before;
	uint16_t arfcn;
	uint8_t bcc, ncc;
	uint8_t ch_type, ch_subch, ch_ts;
	uint8_t before_time = 0;
	uint16_t ma[64];
	uint8_t ma_len;
	uint32_t start_mili = 0;
	uint8_t cause;
	struct msgb *nmsg;

	LOGP(DRR, LOGL_INFO, "HANDOVER COMMAND\n");

	memset(cda, 0, sizeof(*cda));
	cda->ind_tx_power = rr->cd_now.ind_tx_power;
	memset(cdb, 0, sizeof(*cdb));
	cdb->ind_tx_power = rr->cd_now.ind_tx_power;

	if (payload_len < 0) {
		LOGP(DRR, LOGL_NOTICE, "Short read of HANDOVER COMMAND "
			"message.\n");
		return gsm48_rr_tx_rr_status(ms,
			GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
	}

	/* cell description */
	gsm48_decode_cell_desc(&ho->cell_desc, &arfcn, &ncc, &bcc);

	/* handover reference */
	rr->chan_req_val = ho->ho_ref;
	rr->chan_req_mask = 0x00;

	/* sync ind */
	if (TLVP_PRESENT(&tp, GSM48_IE_SYNC_IND)) {	
		gsm48_decode_sync_ind(rr, (struct gsm48_sync_ind *) 
			TLVP_VAL(&tp, GSM48_IE_SYNC_IND));
		LOGP(DRR, LOGL_INFO, " (sync_ind=%d rot=%d nci=%d)\n",
			rr->hando_sync_ind, rr->hando_rot, rr->hando_nci);
	}

	tlv_parse(&tp, &gsm48_rr_att_tlvdef, ho->data, payload_len, 0, 0);

	/* decode channel description (before time) */
	if (TLVP_PRESENT(&tp, GSM48_IE_CH_DESC_1_BEFORE)) {
		struct gsm48_chan_desc *ccd = (struct gsm48_chan_desc *)
			TLVP_VAL(&tp, GSM48_IE_CH_DESC_1_BEFORE);
		cdb->chan_nr = ccd->chan_nr;
		rsl_dec_chan_nr(cdb->chan_nr, &ch_type, &ch_subch, &ch_ts);
		if (ccd->h0.h) {
			cdb->h = 1;
			gsm48_decode_chan_h1(ccd, &cdb->tsc, &cdb->maio,
				&cdb->hsn);
			LOGP(DRR, LOGL_INFO, " before: (chan_nr 0x%02x MAIO %u "
				"HSN %u TS %u SS %u TSC %u)\n", ccd->chan_nr,
				cdb->maio, cdb->hsn, ch_ts, ch_subch, cdb->tsc);
		} else {
			cdb->h = 0;
			gsm48_decode_chan_h0(ccd, &cdb->tsc, &cdb->arfcn);
			LOGP(DRR, LOGL_INFO, " before: (chan_nr 0x%02x "
				"ARFCN %u TS %u SS %u TSC %u)\n", ccd->chan_nr,
				cdb->arfcn, ch_ts, ch_subch, cdb->tsc);
		}
		before_time = 1;
	}

	/* decode channel description (after time) */
	cda->chan_nr = ho->chan_desc.chan_nr;
	rsl_dec_chan_nr(cda->chan_nr, &ch_type, &ch_subch, &ch_ts);
	if (ho->chan_desc.h0.h) {
		cda->h = 1;
		gsm48_decode_chan_h1(&ho->chan_desc, &cda->tsc, &cda->maio,
			&cda->hsn);
		LOGP(DRR, LOGL_INFO, " after: (chan_nr 0x%02x MAIO %u HSN %u "
			"TS %u SS %u TSC %u)\n", ho->chan_desc.chan_nr,
			cda->maio, cda->hsn, ch_ts, ch_subch, cda->tsc);
	} else {
		cda->h = 0;
		gsm48_decode_chan_h0(&ho->chan_desc, &cda->tsc, &cda->arfcn);
		LOGP(DRR, LOGL_INFO, " after: (chan_nr 0x%02x ARFCN %u TS %u "
			"SS %u TSC %u)\n", ho->chan_desc.chan_nr,
			cda->arfcn, ch_ts, ch_subch, cda->tsc);
	}

	/* starting time */
#ifdef TEST_STARTING_TIMER
	cda->start = 1;
	cda->start_tm.fn = (ms->meas.last_fn + TEST_STARTING_TIMER) % 42432;
	LOGP(DRR, LOGL_INFO, " TESTING: starting time ahead\n");
#else
	if (TLVP_PRESENT(&tp, GSM48_IE_START_TIME)) {	
		gsm48_decode_start_time(cda, (struct gsm48_start_time *) 
			TLVP_VAL(&tp, GSM48_IE_START_TIME));
		/* 9.1.2.5 "... before time IE is not present..." */
		if (!before_time) {
			LOGP(DRR, LOGL_INFO, " -> channel description after "
				"time only, but starting time\n");
		} else
			LOGP(DRR, LOGL_INFO, " -> channel description before "
				"and after time\n");
	} else {
		/* 9.1.2.5 "... IEs unnecessary in this message." */
		if (before_time) {
			before_time = 0;
			LOGP(DRR, LOGL_INFO, " -> channel description before "
				"time, but no starting time, ignoring!\n");
		}
	}
#endif

	/* mobile allocation / frequency list after time */
	if (cda->h) {
		if (TLVP_PRESENT(&tp, GSM48_IE_MA_AFTER)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_MA_AFTER) - 1;

			LOGP(DRR, LOGL_INFO, " after: hopping required and "
				"mobile allocation available\n");
			if (*lv + 1 > sizeof(cda->mob_alloc_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cda->mob_alloc_lv, lv, *lv + 1);
		} else
		if (TLVP_PRESENT(&tp, GSM48_IE_FREQ_L_AFTER)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_FREQ_L_AFTER) - 1;

			LOGP(DRR, LOGL_INFO, " after: hopping required and "
				"frequency list available\n");
			if (*lv + 1 > sizeof(cda->freq_list_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cda->freq_list_lv, lv, *lv + 1);
		} else {
			LOGP(DRR, LOGL_NOTICE, " after: hopping required, but "
				"no mobile allocation / frequency list\n");
		}
	}

	/* mobile allocation / frequency list before time */
	if (cdb->h) {
		if (TLVP_PRESENT(&tp, GSM48_IE_MA_BEFORE)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_MA_BEFORE) - 1;

			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"mobile allocation available\n");
			if (*lv + 1 > sizeof(cdb->mob_alloc_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cdb->mob_alloc_lv, lv, *lv + 1);
		} else
		if (TLVP_PRESENT(&tp, GSM48_IE_FREQ_L_BEFORE)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_FREQ_L_BEFORE) - 1;

			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"frequency list available\n");
			if (*lv + 1 > sizeof(cdb->freq_list_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cdb->freq_list_lv, lv, *lv + 1);
		} else
		if (TLVP_PRESENT(&tp, GSM48_IE_F_CH_SEQ_BEFORE)) {
			const uint8_t *lv =
				TLVP_VAL(&tp, GSM48_IE_F_CH_SEQ_BEFORE) - 1;

			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"frequency channel sequence available\n");
			if (*lv + 1 > sizeof(cdb->freq_seq_lv)) {
				LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
				return -ENOMEM;
			}
			memcpy(&cdb->freq_seq_lv, lv, *lv + 1);
		} else
		if (cda->mob_alloc_lv[0]) {
			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"mobile allocation not available, using "
				"mobile allocation after time\n");
			memcpy(&cdb->mob_alloc_lv, &cda->mob_alloc_lv,
				sizeof(cdb->mob_alloc_lv));
		} else
		if (cda->freq_list_lv[0]) {
			LOGP(DRR, LOGL_INFO, " before: hopping required and "
				"frequency list not available, using "
				"frequency list after time\n");
			memcpy(&cdb->freq_list_lv, &cda->freq_list_lv,
				sizeof(cdb->freq_list_lv));
		} else {
			LOGP(DRR, LOGL_NOTICE, " before: hopping required, but "
				"no mobile allocation / frequency list\n");
		}
	}

	/* cell channel description */
	if (TLVP_PRESENT(&tp, GSM48_IE_CELL_CH_DESC)) {
		const uint8_t *lv = TLVP_VAL(&tp, GSM48_IE_CELL_CH_DESC) - 1;

		LOGP(DRR, LOGL_INFO, " both: using cell channel description "
			"in case of mobile allocation\n");
		if (*lv + 1 > sizeof(cdb->cell_desc_lv)) {
			LOGP(DRR, LOGL_ERROR, "Error: no LV space!\n");
			return -ENOMEM;
		}
		memcpy(&cdb->cell_desc_lv, lv, *lv + 1);
		memcpy(&cda->cell_desc_lv, lv, *lv + 1);
	} else {
		/* keep old */
		memcpy(&cdb->cell_desc_lv, &rr->cd_now.cell_desc_lv,
			sizeof(cdb->cell_desc_lv));
		memcpy(&cda->cell_desc_lv, &rr->cd_now.cell_desc_lv,
			sizeof(cda->cell_desc_lv));
	}

	/* channel mode */
	if (TLVP_PRESENT(&tp, GSM48_IE_CHANMODE_1)) {
		cda->mode = cdb->mode = *TLVP_VAL(&tp, GSM48_IE_CHANMODE_1);
		LOGP(DRR, LOGL_INFO, " both: changing channel mode 0x%02x\n",
			cda->mode);
	} else
		cda->mode = cdb->mode = rr->cd_now.mode;

	/* cipher mode setting */
	if (TLVP_PRESENT(&tp, GSM48_IE_CIP_MODE_SET)) {
		cda->cipher = cdb->cipher =
			*TLVP_VAL(&tp, GSM48_IE_CIP_MODE_SET);
		LOGP(DRR, LOGL_INFO, " both: changing cipher mode 0x%02x\n",
			cda->cipher);
	} else
		cda->cipher = cdb->cipher = rr->cd_now.cipher;

	/* power command and TA (before and after time) */
	gsm48_decode_power_cmd_acc(
		(struct gsm48_power_cmd *) &ho->power_command,
		&cda->ind_tx_power, &rr->hando_act);
	cdb->ind_tx_power = cda->ind_tx_power;
	cda->ind_ta = cdb->ind_ta = rr->cd_now.ind_ta; /* same cell */
	LOGP(DRR, LOGL_INFO, " both: (tx_power %d TA %d access=%s)\n",
		cda->ind_tx_power, cda->ind_ta,
		(rr->hando_act) ? "optional" : "mandatory");

	/* check if we have to change channel at starting time */
	if (cda->start) {
		int32_t now, start, diff;

		/* how much time do we have left? */
		now = ms->meas.last_fn % 42432;
		start = cda->start_tm.fn % 42432;
		diff = start - now;
		if (diff < 0)
			diff += 42432;
		LOGP(DRR, LOGL_INFO, " after: (Tnow %d Tstart %d diff %d)\n",
			now, start, diff);
		start_mili = (uint32_t)diff * 19580 / 42432 * 10;
		if (diff >= 32024 || !start_mili) {
			LOGP(DRR, LOGL_INFO, " -> Start time already "
				"elapsed\n");
			before_time = 0;
			cda->start = 0;
		} else {
			LOGP(DRR, LOGL_INFO, " -> Start time is %d ms in the "
				"future\n", start_mili);
		}
	}

	/* check if channels are valid */
	if (before_time) {
		cause = gsm48_rr_render_ma(ms, cdb, ma, &ma_len);
		if (cause)
			return gsm48_rr_tx_hando_fail(ms, cause);
	}
	cause = gsm48_rr_render_ma(ms, cda, ma, &ma_len);
	if (cause)
		return gsm48_rr_tx_hando_fail(ms, cause);


#if 0
	if (not supported) {
		LOGP(DRR, LOGL_NOTICE, "New channel is not supported.\n");
		return GSM48_RR_CAUSE_CHAN_MODE_UNACCEPT;
	}
#endif

#ifdef TEST_FREQUENCY_MOD
	LOGP(DRR, LOGL_INFO, " TESTING: frequency modify HANDO.CMD\n");
	before_time = 1;
	memcpy(cdb, cda, sizeof(*cdb));
	cdb->h = 0;
	cdb->arfcn = 0;
#endif

	/* schedule start of handover */
	rr->modify_state = GSM48_RR_MOD_HANDO;
	if (!before_time && cda->start) {
		start_rr_t_starting(rr, start_mili / 1000, start_mili % 1000);
		/* when timer fires, start time is already elapsed */
		cda->start = 0;

		return 0;
	}

	/* if no starting time, start suspension of current link directly */
	LOGP(DRR, LOGL_INFO, "request suspension of data link\n");
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	gsm48_send_rsl(ms, RSL_MT_SUSP_REQ, nmsg);

	return 0;
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

/* channel is resumed in dedicated mode */
static int gsm48_rr_estab_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	LOGP(DRR, LOGL_INFO, "data link is resumed\n");

	switch (rr->modify_state) {
	case GSM48_RR_MOD_ASSIGN:
		gsm48_rr_tx_ass_cpl(ms, GSM48_RR_CAUSE_NORMAL);
		break;
	case GSM48_RR_MOD_HANDO:
		gsm48_rr_tx_hando_cpl(ms, GSM48_RR_CAUSE_NORMAL);
		break;
	case GSM48_RR_MOD_ASSIGN_RESUME:
		gsm48_rr_tx_ass_fail(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
		break;
	case GSM48_RR_MOD_HANDO_RESUME:
		gsm48_rr_tx_hando_fail(ms, GSM48_RR_CAUSE_PROT_ERROR_UNSPC);
		break;
	}

	/* transmit queued frames during ho / ass transition */
	gsm48_rr_dequeue_down(ms);

	rr->modify_state = GSM48_RR_MOD_NONE;

	return 0;
}

/* suspend confirm in dedicated mode */
static int gsm48_rr_susp_cnf_dedicated(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	if (rr->modify_state) {
		struct msgb *nmsg;
		uint16_t ma[64];
		uint8_t ma_len;

		/* deactivating dedicated mode */
		LOGP(DRR, LOGL_INFO, "suspension coplete, leaving dedicated "
			"mode\n");
		l1ctl_tx_dm_rel_req(ms);
		rr->dm_est = 0;
		l1ctl_tx_reset_req(ms, L1CTL_RES_T_SCHED);

		/* store current channel descriptions */
		memcpy(&rr->cd_last, &rr->cd_now, sizeof(rr->cd_last));

		/* copy channel description "after time" */
		memcpy(&rr->cd_now, &rr->cd_after, sizeof(rr->cd_now));

		if (rr->cd_after.start) {
			/* render channel "before time" */
			gsm48_rr_render_ma(ms, &rr->cd_before, ma, &ma_len);

			/* activate channel */
			gsm48_rr_activate_channel(ms, &rr->cd_before, ma,
				ma_len);

			/* render channel "after time" */
			gsm48_rr_render_ma(ms, &rr->cd_now, ma, &ma_len);

			/* schedule change of channel */
			gsm48_rr_channel_after_time(ms, &rr->cd_now, ma, ma_len,
				rr->cd_now.start_tm.fn);
		} else {
			/* render channel "after time" */
			gsm48_rr_render_ma(ms, &rr->cd_now, ma, &ma_len);

			/* activate channel */
			gsm48_rr_activate_channel(ms, &rr->cd_now, ma, ma_len);
		}

		/* send DL-RESUME REQUEST */
		LOGP(DRR, LOGL_INFO, "request resume of data link\n");
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		gsm48_send_rsl(ms, RSL_MT_RES_REQ, nmsg);

#ifdef TODO
		/* trigger RACH */
		if (rr->modify_state == GSM48_RR_MOD_HANDO) {
			gsm48_rr_tx_hando_access(ms);
			rr->hando_acc_left = 3;
		}
#endif
	}
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
			LOGP(DSUM, LOGL_INFO, "Establishing radio link not "
				"possible\n");
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

	/* if state is not idle */
	if (rr->state != GSM48_RR_ST_IDLE) {
		LOGP(DRR, LOGL_INFO, "We are not IDLE yet, rejecting!\n");
		cause = RR_REL_CAUSE_TRY_LATER;
	 	goto reject;
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

	/* set sequence number and increment */
	gsm48_apply_v_sd(rr, msg);

	/* queue message, during handover or assignment procedure */
	if (rr->modify_state == GSM48_RR_MOD_ASSIGN
	 || rr->modify_state == GSM48_RR_MOD_HANDO) {
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
		case GSM48_MT_RR_ASS_CMD:
			rc = gsm48_rr_rx_ass_cmd(ms, msg);
			break;
		case GSM48_MT_RR_CIPH_M_CMD:
			rc = gsm48_rr_rx_cip_mode_cmd(ms, msg);
			break;
		case GSM48_MT_RR_CLSM_ENQ:
			rc = gsm48_rr_rx_cm_enq(ms, msg);
			break;
		case GSM48_MT_RR_CHAN_MODE_MODIF:
			rc = gsm48_rr_rx_chan_modify(ms, msg);
			break;
		case GSM48_MT_RR_HANDO_CMD:
			rc = gsm48_rr_rx_hando_cmd(ms, msg);
			break;
		case GSM48_MT_RR_FREQ_REDEF:
			rc = gsm48_rr_rx_frq_redef(ms, msg);
			break;
		case GSM48_MT_RR_CHAN_REL:
			rc = gsm48_rr_rx_chan_rel(ms, msg);
			break;
		case GSM48_MT_RR_APP_INFO:
			LOGP(DRR, LOGL_NOTICE, "APP INFO not supported!\n");
			break;
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

/* receive ACCH at RR layer */
static int gsm48_rr_rx_acch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm_settings *set = &ms->settings;
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);
	uint8_t ind_ta, ind_tx_power;

	if (msgb_l2len(msg) < sizeof(*rllh) + 2 + 2) {
		LOGP(DRR, LOGL_ERROR, "Missing TA and TX_POWER IEs\n");
		return -EINVAL;
	}

	ind_ta = rllh->data[1];
	ind_tx_power = rllh->data[3];
	LOGP(DRR, LOGL_INFO, "Indicated ta %d (actual ta %d)\n",
		ind_ta, ind_ta - set->alter_delay);
	LOGP(DRR, LOGL_INFO, "Indicated tx_power %d\n",
		ind_tx_power);
	if (ind_ta != rr->cd_now.ind_ta
	 || ind_tx_power != rr->cd_now.ind_tx_power) {
		LOGP(DRR, LOGL_INFO, "setting new ta and tx_power\n");
		l1ctl_tx_param_req(ms, ind_ta - set->alter_delay,
			(set->alter_tx_power) ? set->alter_tx_power_value
						: ind_tx_power);
		rr->cd_now.ind_ta = ind_ta;
		rr->cd_now.ind_tx_power = ind_tx_power;
	}

	switch (sih->system_information) {
	case GSM48_MT_RR_SYSINFO_5:
		return gsm48_rr_rx_sysinfo5(ms, msg);
	case GSM48_MT_RR_SYSINFO_5bis:
		return gsm48_rr_rx_sysinfo5bis(ms, msg);
	case GSM48_MT_RR_SYSINFO_5ter:
		return gsm48_rr_rx_sysinfo5ter(ms, msg);
	case GSM48_MT_RR_SYSINFO_6:
		return gsm48_rr_rx_sysinfo6(ms, msg);
	default:
		LOGP(DRR, LOGL_NOTICE, "ACCH message type 0x%02x unknown.\n",
			sih->system_information);
		return -EINVAL;
	}
}

/* unit data from layer 2 to RR layer */
static int gsm48_rr_unit_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct gsm322_cellsel *cs = &ms->cellsel;
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct tlv_parsed tv;
	uint8_t ch_type, ch_subch, ch_ts;
	
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

	/* when camping, start/reset loss timer */
	if (cs->state == GSM322_C3_CAMPED_NORMALLY
	 || cs->state == GSM322_C7_CAMPED_ANY_CELL) {
		struct gsm48_sysinfo *s = &ms->cellsel.sel_si;
#ifdef TODO
	set radio link timeout on layer 1
	it is the number of subsequent BCCH blocks. (about 1/4 seconds)
#else
		/* use maximu loss timer, if to value is not available yet */
		start_loss_timer(cs, ((rr->state == GSM48_RR_ST_DEDICATED)
			? ((s->sacch_radio_link_timeout) ? : 64)
			: s->bcch_radio_link_timeout) / 4, 0);
#endif
	}

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

	rsl_dec_chan_nr(rllh->chan_nr, &ch_type, &ch_subch, &ch_ts);
	switch (ch_type) {
	case RSL_CHAN_PCH_AGCH:
		return gsm48_rr_rx_pch_agch(ms, msg);
	case RSL_CHAN_BCCH:
		return gsm48_rr_rx_bcch(ms, msg);
	case RSL_CHAN_SDCCH4_ACCH:
		return gsm48_rr_rx_acch(ms, msg);
	case RSL_CHAN_SDCCH8_ACCH:
		return gsm48_rr_rx_acch(ms, msg);
	default:
		LOGP(DRSL, LOGL_NOTICE, "RSL with chan_nr 0x%02x unknown.\n",
			rllh->chan_nr);
		return -EINVAL;
	}
}

/* 3.4.13.3 RR abort in dedicated mode (also in conn. pending mode) */
static int gsm48_rr_abort_req(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	uint8_t *mode;

	/* stop pending RACH timer */
	stop_rr_t3126(rr);

	/* release "normally" if we are in dedicated mode */
	if (rr->state == GSM48_RR_ST_DEDICATED) {
		struct msgb *nmsg;

		LOGP(DRR, LOGL_INFO, "Abort in dedicated state, send release "
			"to layer 2.\n");

		new_rr_state(rr, GSM48_RR_ST_REL_PEND);

		/* release message */
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		mode = msgb_put(nmsg, 2);
		mode[0] = RSL_IE_RELEASE_MODE;
		mode[1] = 0; /* normal release */
		return gsm48_send_rsl_rel(ms, RSL_MT_REL_REQ, nmsg);
	}

	LOGP(DRR, LOGL_INFO, "Abort in connection pending state, return to "
		"idle state.\n");
	/* return idle */
	new_rr_state(rr, GSM48_RR_ST_IDLE);

	return 0;
}

/* release confirm */
static int gsm48_rr_rel_cnf(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;

	LOGP(DSUM, LOGL_INFO, "Requesting channel aborted\n");

	/* stop T3211 if running */
	stop_rr_t3110(rr);

	/* send release indication */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_REL_IND);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *)nmsg->data;
	nrrh->cause = RR_REL_CAUSE_NORMAL;
	gsm48_rr_upmsg(ms, nmsg);

	/* return idle */
	new_rr_state(rr, GSM48_RR_ST_IDLE);
	return 0;
}

/* MDL-ERROR */
static int gsm48_rr_mdl_error_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct msgb *nmsg;
	struct gsm48_rr_hdr *nrrh;
	uint8_t *mode;
	uint8_t cause = rllh->data[2];
	uint16_t ma[64];
	uint8_t ma_len;

	switch (cause) {
	case RLL_CAUSE_SEQ_ERR:
	case RLL_CAUSE_UNSOL_DM_RESP_MF:
		break;
	default:
		LOGP(DRR, LOGL_NOTICE, "MDL-Error (cause %d) ignoring\n",
			cause);
	}

	LOGP(DRR, LOGL_NOTICE, "MDL-Error (cause %d) aborting\n", cause);

	/* disconnect the main signalling link */
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	mode = msgb_put(nmsg, 2);
	mode[0] = RSL_IE_RELEASE_MODE;
	mode[1] = 1; /* local release */
	gsm48_send_rsl_rel(ms, RSL_MT_REL_REQ, nmsg);

	/* deactivate channel */
	l1ctl_tx_dm_rel_req(ms);
	rr->dm_est = 0;
	l1ctl_tx_reset_req(ms, L1CTL_RES_T_SCHED);

	switch (rr->modify_state) {
	case GSM48_RR_MOD_ASSIGN:
	case GSM48_RR_MOD_HANDO:
		if (rr->modify_state == GSM48_RR_MOD_ASSIGN)
			rr->modify_state = GSM48_RR_MOD_ASSIGN_RESUME;
		else
			rr->modify_state = GSM48_RR_MOD_HANDO_RESUME;

		/* get old channel description */
		memcpy(&rr->cd_now, &rr->cd_last, sizeof(rr->cd_now));

		/* render and change radio to old channel */
		gsm48_rr_render_ma(ms, &rr->cd_now, ma, &ma_len);
		gsm48_rr_activate_channel(ms, &rr->cd_now, ma, ma_len);

		/* re-establish old link */
		nmsg = gsm48_l3_msgb_alloc();
		if (!nmsg)
			return -ENOMEM;
		return gsm48_send_rsl(ms, RSL_MT_RECON_REQ, nmsg);
	case GSM48_RR_MOD_ASSIGN_RESUME:
	case GSM48_RR_MOD_HANDO_RESUME:
		rr->modify_state = GSM48_RR_MOD_NONE;
		break;
	}

	/* send abort ind to upper layer */
	nmsg = gsm48_rr_msgb_alloc(GSM48_RR_ABORT_IND);
	if (!nmsg)
		return -ENOMEM;
	nrrh = (struct gsm48_rr_hdr *)nmsg->data;
	nrrh->cause = RR_REL_CAUSE_LINK_FAILURE;
	gsm48_rr_upmsg(ms, nmsg);

	/* return idle */
	new_rr_state(rr, GSM48_RR_ST_IDLE);
	return 0;
}

/*
 * state machines
 */

/* state trasitions for link layer messages (lower layer) */
static struct dldatastate {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} dldatastatelist[] = {
	/* data transfer */
	{SBIT(GSM48_RR_ST_IDLE) |
	 SBIT(GSM48_RR_ST_CONN_PEND) |
	 SBIT(GSM48_RR_ST_DEDICATED) |
	 SBIT(GSM48_RR_ST_REL_PEND),
	 RSL_MT_UNIT_DATA_IND, gsm48_rr_unit_data_ind},

	{SBIT(GSM48_RR_ST_DEDICATED), /* 3.4.2 */
	 RSL_MT_DATA_IND, gsm48_rr_data_ind},

	/* esablish */
	{SBIT(GSM48_RR_ST_IDLE) |
	 SBIT(GSM48_RR_ST_CONN_PEND) |
	 SBIT(GSM48_RR_ST_REL_PEND),
	 RSL_MT_EST_CONF, gsm48_rr_estab_cnf},

	/* resume */
	{SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_EST_CONF, gsm48_rr_estab_cnf_dedicated},

	/* release */
	{SBIT(GSM48_RR_ST_CONN_PEND) |
	 SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_REL_IND, gsm48_rr_rel_ind},

	{SBIT(GSM48_RR_ST_REL_PEND),
	 RSL_MT_REL_CONF, gsm48_rr_rel_cnf},

	/* suspenion */
	{SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_SUSP_CONF, gsm48_rr_susp_cnf_dedicated},

#if 0
	{SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_CHAN_CNF, gsm48_rr_rand_acc_cnf_dedicated},
#endif

	{SBIT(GSM48_RR_ST_DEDICATED),
	 RSL_MT_ERROR_IND, gsm48_rr_mdl_error_ind},
};

#define DLDATASLLEN \
	(sizeof(dldatastatelist) / sizeof(struct dldatastate))

static int gsm48_rcv_rll(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int msg_type = rllh->c.msg_type;
	int i;
	int rc;

	if (msg_type != RSL_MT_UNIT_DATA_IND) {
		LOGP(DRSL, LOGL_INFO, "(ms %s) Received '%s' from L2 in state "
			"%s\n", ms->name, get_rsl_name(msg_type),
			gsm48_rr_state_names[rr->state]);
	}

	/* find function for current state and message */
	for (i = 0; i < DLDATASLLEN; i++)
		if ((msg_type == dldatastatelist[i].type)
		 && ((1 << rr->state) & dldatastatelist[i].states))
			break;
	if (i == DLDATASLLEN) {
		LOGP(DRSL, LOGL_NOTICE, "RSLms message unhandled\n");
		msgb_free(msg);
		return 0;
	}

	rc = dldatastatelist[i].rout(ms, msg);

	/* free msgb unless it is forwarded */
	if (dldatastatelist[i].rout != gsm48_rr_data_ind)
		msgb_free(msg);

	return rc;
}

static int gsm48_rcv_cch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct abis_rsl_cchan_hdr *ch = msgb_l2(msg);
	int msg_type = ch->c.msg_type;
	int rc;

	LOGP(DRSL, LOGL_INFO, "(ms %s) Received '%s' from L2 in state "
		"%s\n", ms->name, get_rsl_name(msg_type),
		gsm48_rr_state_names[rr->state]);

	if (rr->state == GSM48_RR_ST_CONN_PEND
	 && msg_type == RSL_MT_CHAN_CONF) {
	 	rc = gsm48_rr_tx_rand_acc(ms, msg);
		msgb_free(msg);
		return rc;
	}

	LOGP(DRSL, LOGL_NOTICE, "RSLms message unhandled\n");
	msgb_free(msg);
	return 0;
}


/* input function for L2 messags up to L3 */
static int gsm48_rcv_rsl(struct osmocom_ms *ms, struct msgb *msg)
{
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = gsm48_rcv_rll(ms, msg);
		break;
	case ABIS_RSL_MDISC_COM_CHAN:
		rc = gsm48_rcv_cch(ms, msg);
		break;
	default:
		/* FIXME: implement this */
		LOGP(DRSL, LOGL_NOTICE, "unknown RSLms msg_discr 0x%02x\n",
			rslh->msg_discr);
		msgb_free(msg);
		rc = -EINVAL;
		break;
	}

	return rc;
}

/* state trasitions for RR-SAP messages from up */
static struct rrdownstate {
	uint32_t	states;
	int		type;
	int		(*rout) (struct osmocom_ms *ms, struct msgb *msg);
} rrdownstatelist[] = {
	/* NOTE: If not IDLE, it is rejected there. */
	{ALL_STATES, /* 3.3.1.1 */
	 GSM48_RR_EST_REQ, gsm48_rr_est_req},

	{SBIT(GSM48_RR_ST_DEDICATED), /* 3.4.2 */
	 GSM48_RR_DATA_REQ, gsm48_rr_data_req},

	{SBIT(GSM48_RR_ST_CONN_PEND) |
	 SBIT(GSM48_RR_ST_DEDICATED), /* 3.4.13.3 */
	 GSM48_RR_ABORT_REQ, gsm48_rr_abort_req},
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

	start_rr_t_meas(rr, 1, 0);

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

	stop_rr_t_meas(rr);
	stop_rr_t_starting(rr);
	stop_rr_t_rel_wait(rr);
	stop_rr_t3110(rr);
	stop_rr_t3122(rr);
	stop_rr_t3124(rr);
	stop_rr_t3126(rr);

	return 0;
}

#if 0

todo rr_sync_ind when receiving ciph, re ass, channel mode modify


static void timeout_rr_t3124(void *arg)
{
	struct gsm48_rrlayer *rr = arg;
	struct msgb *nmsg;

	/* stop sending more access bursts when timer expired */
	hando_acc_left = 0;

	/* get old channel description */
	memcpy(&rr->chan_desc, &rr->chan_last, sizeof(rr->chan_desc));

	/* change radio to old channel */
	tx_ph_dm_est_req(ms, rr->cd_now.arfcn, rr->cd_now.chan_nr,
			 rr->cd_now.tsc);
	rr->dm_est = 1;

	/* re-establish old link */
	nmsg = gsm48_l3_msgb_alloc();
	if (!nmsg)
		return -ENOMEM;
	return gsm48_send_rsl(ms, RSL_MT_REEST_REQ, nmsg);

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

	if (rr->modify_state != GSM48_RR_MOD_HANDO) {
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
	}
	if (!rr->n_chan_req) {
		start_rr_t3126(rr, 5, 0); /* TODO improve! */
		return 0;
	}
	rr->n_chan_req--;

	/* wait for PHYSICAL INFORMATION message or T3124 timeout */
	return 0;

}

#endif

