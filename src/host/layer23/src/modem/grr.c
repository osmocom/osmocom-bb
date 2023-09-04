/*
 * (C) 2022-2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Pau Espin Pedrol <pespin@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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
 * along with this program.  If not, see <http://www.gnu.org/lienses/>.
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <osmocom/core/fsm.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/logging.h>

#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/lapdm.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/gsm48_ie.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/gprs/rlcmac/rlcmac_prim.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/sysinfo.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/modem/modem.h>
#include <osmocom/bb/modem/grr.h>

#include <osmocom/bb/mobile/gsm322.h>
#include <osmocom/bb/mobile/gsm48_rr.h>

#include <l1ctl_proto.h>

static uint32_t _gsm48_req_ref2fn(const struct gsm48_req_ref *ref)
{
	const struct gsm_time time = {
		.t3 = ref->t3_high << 3 | ref->t3_low,
		.t2 = ref->t2,
		.t1 = ref->t1,
	};
	return gsm_gsmtime2fn(&time);
}

/* Generate an 8-bit CHANNEL REQUEST message as per 3GPP TS 44.018, 9.1.8 */
uint8_t modem_grr_gen_chan_req(bool single_block)
{
	uint8_t rnd = (uint8_t)rand();

	if (single_block) /* 01110xxx */
		return 0x70 | (rnd & 0x07);

	/* 011110xx or 01111x0x or 01111xx0 */
	if ((rnd & 0x07) == 0x07)
		return 0x78;
	return 0x78 | (rnd & 0x07);
}

static bool grr_match_req_ref(struct osmocom_ms *ms,
			      const struct gsm48_req_ref *ref)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	for (unsigned int i = 0; i < ARRAY_SIZE(rr->cr_hist); i++) {
		const struct gsm48_cr_hist *hist = &rr->cr_hist[i];
		if (!hist->valid)
			continue;
		if (memcmp(&hist->ref, ref, sizeof(*ref)) == 0)
			return true;
	}

	return false;
}

static int forward_to_rlcmac(struct osmocom_ms *ms, struct msgb *msg)
{
	struct osmo_gprs_rlcmac_prim *rlcmac_prim;
	const uint32_t fn = *(uint32_t *)(&msg->cb[0]);

	/* Forward SI13 to RLC/MAC layer */
	rlcmac_prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_ccch_data_ind(fn, msgb_l3(msg));
	return osmo_gprs_rlcmac_prim_lower_up(rlcmac_prim);
}

static int grr_handle_si1(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&cs->sel_si.si1_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo1(&cs->sel_si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to decode SI1 message\n");
		return rc;
	}

	return 0;
}

static int grr_handle_si3(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&cs->sel_si.si3_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo3(&cs->sel_si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to decode SI3 message\n");
		return rc;
	}

	if (cs->ccch_mode == CCCH_MODE_NONE) {
		if (cs->sel_si.ccch_conf == RSL_BCCH_CCCH_CONF_1_C)
			cs->ccch_mode = CCCH_MODE_COMBINED;
		else
			cs->ccch_mode = CCCH_MODE_NON_COMBINED;
		l1ctl_tx_ccch_mode_req(ms, cs->ccch_mode);
	}

	if (!cs->sel_si.gprs.supported) {
		LOGP(DRR, LOGL_NOTICE, "SI3 Rest Octets IE contains no GPRS Indicator\n");
		return 0;
	}

	LOGP(DRR, LOGL_NOTICE, "Found GPRS Indicator (RA Colour %u, SI13 on BCCH %s)\n",
	     cs->sel_si.gprs.ra_colour, cs->sel_si.gprs.si13_pos ? "Ext" : "Norm");

	return 0;
}

static int grr_handle_si4(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&cs->sel_si.si4_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo4(&cs->sel_si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to decode SI4 message\n");
		return rc;
	}

	if (!cs->sel_si.gprs.supported) {
		LOGP(DRR, LOGL_NOTICE, "SI4 Rest Octets IE contains no GPRS Indicator\n");
		return 0;
	}

	LOGP(DRR, LOGL_NOTICE, "Found GPRS Indicator (RA Colour %u, SI13 on BCCH %s)\n",
	     cs->sel_si.gprs.ra_colour, cs->sel_si.gprs.si13_pos ? "Ext" : "Norm");

	return 0;
}

static int grr_handle_si13(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm322_cellsel *cs = &ms->cellsel;
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&cs->sel_si.si13_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo13(&cs->sel_si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0)
		return rc;

	/* Forward SI13 to RLC/MAC layer */
	return forward_to_rlcmac(ms, msg);
}

static int grr_rx_bcch(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct gsm48_system_information_type_header *si_hdr = msgb_l3(msg);
	const uint8_t si_type = si_hdr->system_information;
	const uint32_t fn = *(uint32_t *)(&msg->cb[0]);

	LOGP(DRR, LOGL_INFO, "BCCH message (type=0x%02x, fn=%u): %s\n",
	     si_type, fn, gsm48_rr_msg_name(si_type));

	switch (si_type) {
	case GSM48_MT_RR_SYSINFO_1:
		return grr_handle_si1(ms, msg);
	case GSM48_MT_RR_SYSINFO_3:
		return grr_handle_si3(ms, msg);
	case GSM48_MT_RR_SYSINFO_4:
		return grr_handle_si4(ms, msg);
	case GSM48_MT_RR_SYSINFO_13:
		return grr_handle_si13(ms, msg);
	default:
		return 0;
	};
}

static int grr_rx_imm_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct gsm48_imm_ass *ia = msgb_l3(msg);
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	/* 3GPP TS 44.018, section 10.5.2.25b "Dedicated mode or TBF".
	 * As per table 9.1.18.1, only the value part (4 bits) is present in the
	 * IMMEDIATE ASSIGNMENT message.  In struct gsm48_imm_ass it's combined
	 * with the Page Mode IE, perhaps due to historical reasons. */
	const uint8_t dm_or_tbf = ia->page_mode >> 4;

	/* T/D flag: discard dedicated channel assignment */
	if ((dm_or_tbf & (1 << 0)) == 0) {
		LOGP(DRR, LOGL_INFO,
		     "%s(): Discarding IMM ASS: dedicated channel assignment\n",
		     __func__);
		return 0;
	}
	/* NRA flag: discard No Resource Allocated */
	if ((dm_or_tbf & (1 << 3)) != 0) {
		LOGP(DRR, LOGL_INFO,
		     "%s(): Discarding IMM ASS: NRA flag is set\n",
		     __func__);
		return 0;
	}

	/* If this is an Uplink TBF assignment, check the Request Reference IE.
	 * Checking this IE in Downlink TBF assignment makes no sense because
	 * no CHANNEL REQUEST was sent by the MS prior to it. */
	if ((dm_or_tbf & (1 << 1)) == 0) {
		if (rr->state != GSM48_RR_ST_CONN_PEND) {
			LOGP(DRR, LOGL_INFO,
			     "%s(): rr_state != GSM48_RR_ST_CONN_PEND\n", __func__);
			return 0;
		}
		if (!grr_match_req_ref(ms, &ia->req_ref)) {
			LOGP(DRR, LOGL_INFO,
			     "%s(): req_ref mismatch (RA=0x%02x, T1=%u, T3=%u, T2=%u, FN=%u)\n",
			     __func__, ia->req_ref.ra, ia->req_ref.t1,
			     ia->req_ref.t3_high << 3 | ia->req_ref.t3_low, ia->req_ref.t2,
			     _gsm48_req_ref2fn(&ia->req_ref));
			return 0;
		}
	}

	return forward_to_rlcmac(ms, msg);
}

/* TS 44.018 9.1.22 "Paging request type 1" */
static int grr_rx_pag_req_1(struct osmocom_ms *ms, struct msgb *msg)
{
	LOGP(DRR, LOGL_INFO, "Rx Paging Request Type 1\n");

	return forward_to_rlcmac(ms, msg);
}

/* TS 44.018 9.1.23 "Paging request type 2" */
static int grr_rx_pag_req_2(struct osmocom_ms *ms, struct msgb *msg)
{
	LOGP(DRR, LOGL_INFO, "Rx Paging Request Type 2\n");

	return forward_to_rlcmac(ms, msg);
}

/* 9.1.24 Paging request type 3 */
static int grr_rx_pag_req_3(struct osmocom_ms *ms, struct msgb *msg)
{
	LOGP(DRR, LOGL_INFO, "Rx Paging Request Type 3\n");

	/* Paging Request Type 3 contains 4 TMSI/P-TMSI, but P3 Rest Octets
	contain no "Packet Page Indication" IE, hence it cannot be used to page
	for GPRS. Simply ignore it. */
	return 0;
}

/* Dummy Paging Request 1 with "no identity" */
static const uint8_t paging_fill[] = {
	0x15, 0x06, 0x21, 0x00, 0x01, 0xf0, 0x2b,
	/* The rest part may be randomized */
};

/* LAPDm func=UI fill frame (for the BTS side) */
static const uint8_t lapdm_fill[] = {
	0x03, 0x03, 0x01, 0x2b,
	/* The rest part may be randomized */
};

/* TODO: share / generalize this code */
static bool is_fill_frame(const struct msgb *msg)
{
	const uint8_t *l2 = msgb_l3(msg);

	if (!memcmp(l2, paging_fill, sizeof(paging_fill)))
		return true;
	if (!memcmp(l2, lapdm_fill, sizeof(lapdm_fill)))
		return true;

	return false;
}

static int grr_rx_ccch(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct gsm48_system_information_type_header *sih = msgb_l3(msg);

	/* Skip frames with wrong length */
	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN) {
		LOGP(DRR, LOGL_ERROR, "Rx CCCH message with odd length=%u: %s\n",
		     msgb_l3len(msg), msgb_hexdump_l3(msg));
		return -EINVAL;
	}

	/* Skip dummy (fill) frames */
	if (is_fill_frame(msg))
		return 0;

	if (sih->rr_protocol_discriminator != GSM48_PDISC_RR) {
		LOGP(DRR, LOGL_ERROR, "PCH pdisc (%s) != RR\n",
		     gsm48_pdisc_name(sih->rr_protocol_discriminator));
	}

	switch (sih->system_information) {
	case GSM48_MT_RR_IMM_ASS:
		return grr_rx_imm_ass(ms, msg);
	case GSM48_MT_RR_PAG_REQ_1:
		return grr_rx_pag_req_1(ms, msg);
	case GSM48_MT_RR_PAG_REQ_2:
		return grr_rx_pag_req_2(ms, msg);
	case GSM48_MT_RR_PAG_REQ_3:
		return grr_rx_pag_req_3(ms, msg);
	default:
		return 0;
	}
}

static int grr_rx_rslms_rll_ud(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct tlv_parsed tv;

	DEBUGP(DRSL, "RSLms UNIT DATA IND chan_nr=0x%02x link_id=0x%02x\n",
	       rllh->chan_nr, rllh->link_id);

	if (rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg) - sizeof(*rllh)) < 0) {
		LOGP(DRSL, LOGL_ERROR, "%s(): rsl_tlv_parse() failed\n", __func__);
		return -EINVAL;
	}

	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		LOGP(DRSL, LOGL_ERROR, "UNIT_DATA_IND without L3 INFO ?!?\n");
		return -EINVAL;
	}

	msg->l3h = (uint8_t *)TLVP_VAL(&tv, RSL_IE_L3_INFO);

	switch (rllh->chan_nr) {
	case RSL_CHAN_PCH_AGCH:
		return osmo_fsm_inst_dispatch(ms->grr_fi, GRR_EV_PCH_AGCH_BLOCK_IND, msg);
	case RSL_CHAN_BCCH:
		return osmo_fsm_inst_dispatch(ms->grr_fi, GRR_EV_BCCH_BLOCK_IND, msg);
	default:
		return 0;
	}
}

static int grr_rx_rslms_rll(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);

	switch (rllh->c.msg_type) {
	case RSL_MT_UNIT_DATA_IND:
		return grr_rx_rslms_rll_ud(ms, msg);
	default:
		LOGP(DRSL, LOGL_NOTICE, "Unhandled RSLms RLL message "
		     "(msg_type 0x%02x)\n", rllh->c.msg_type);
		return -EINVAL;
	}
}

static int grr_rx_rslms_cchan(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct abis_rsl_cchan_hdr *ch = msgb_l2(msg);

	switch (ch->c.msg_type) {
	case RSL_MT_CHAN_CONF: /* RACH.conf */
		return osmo_fsm_inst_dispatch(ms->grr_fi, GRR_EV_RACH_CNF,
					      (void *)&ch->data[1]);
	default:
		LOGP(DRSL, LOGL_NOTICE, "Unhandled RSLms CCHAN message "
		     "(msg_type 0x%02x)\n", ch->c.msg_type);
		return -EINVAL;
	}
}

int modem_grr_rslms_cb(struct msgb *msg, struct lapdm_entity *le, void *ctx)
{
	const struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc;

	/* Obtain FN from message context: */
	*(uint32_t *)(&msg->cb[0]) = le->datalink[DL_SAPI0].mctx.fn;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = grr_rx_rslms_rll((struct osmocom_ms *)ctx, msg);
		break;
	case ABIS_RSL_MDISC_COM_CHAN:
		rc = grr_rx_rslms_cchan((struct osmocom_ms *)ctx, msg);
		break;
	default:
		LOGP(DRSL, LOGL_NOTICE, "Unhandled RSLms message "
		     "(msg_discr 0x%02x)\n", rslh->msg_discr);
		rc = -EINVAL;
		break;
	}

	msgb_free(msg);
	return rc;
}

/////////////////////////////////////////////////////////////////////////////////////////

#define S(x)	(1 << (x))

#include <osmocom/gsm/gsm0502.h> // XXX

static void handle_pdch_block_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct l1ctl_gprs_dl_block_ind *ind = (void *)msg->l1h;
	const uint32_t fn = osmo_load32be(&ind->hdr.fn);
	struct osmo_gprs_rlcmac_prim *prim;

	/* FIXME: sadly, rlcmac_prim_l1ctl_alloc() is not exposed */
	prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_pdch_data_ind(0, 0, 0, 0, 0, NULL, 0);
	prim->l1ctl = (struct osmo_gprs_rlcmac_l1ctl_prim) {
		.pdch_data_ind = {
			.fn = fn,
			.ts_nr = ind->hdr.tn,
			.rx_lev = ind->meas.rx_lev,
			.ber10k = osmo_load16be(&ind->meas.ber10k),
			.ci_cb = osmo_load16be(&ind->meas.ci_cb),
			.data_len = msgb_l2len(msg),
			.data = msgb_l2(msg),
		}
	};
	osmo_gprs_rlcmac_prim_lower_up(prim);
}

static void handle_pdch_rts_ind(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct l1ctl_gprs_rts_ind *ind = (void *)msg->l1h;
	const uint32_t fn = osmo_load32be(&ind->fn);
	struct osmo_gprs_rlcmac_prim *prim;

	prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_pdch_rts_ind(ind->tn, fn, ind->usf);
	osmo_gprs_rlcmac_prim_lower_up(prim);
}

static bool grr_cell_is_usable(const struct osmocom_ms *ms)
{
	const struct gsm322_cellsel *cs = &ms->cellsel;
	const struct gsm48_sysinfo *si = &cs->sel_si;

	if (cs->sync_pending) /* FBSB in process */
		return false;

	if (!si->si1 || !si->si3 || !si->si4 || !si->si13)
		return false;
	if (!si->gprs.supported)
		return false;

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////

static void grr_st_packet_not_ready_action(struct osmo_fsm_inst *fi,
					   uint32_t event, void *data)
{
	struct osmocom_ms *ms = fi->priv;

	switch (event) {
	case GRR_EV_BCCH_BLOCK_IND:
		grr_rx_bcch(ms, (struct msgb *)data);
		if (grr_cell_is_usable(ms)) {
			LOGPFSML(fi, LOGL_NOTICE, "Cell is usable, GRR becomes ready\n");
			osmo_fsm_inst_state_chg(fi, GRR_ST_PACKET_IDLE, 0, 0);
		}
		break;
	case GRR_EV_PCH_AGCH_BLOCK_IND:
		grr_rx_ccch(ms, (struct msgb *)data);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void grr_st_packet_idle_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct osmocom_ms *ms = fi->priv;
	struct osmo_gprs_rlcmac_prim *prim;

	prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_ccch_ready_ind();
	osmo_gprs_rlcmac_prim_lower_up(prim);

	modem_gprs_attach_if_needed(ms);
}

static void grr_st_packet_idle_action(struct osmo_fsm_inst *fi,
				      uint32_t event, void *data)
{
	struct osmocom_ms *ms = fi->priv;

	switch (event) {
	case GRR_EV_BCCH_BLOCK_IND:
		grr_rx_bcch(ms, (struct msgb *)data);
		if (!grr_cell_is_usable(ms)) {
			LOGPFSML(fi, LOGL_NOTICE, "Cell is not usable, GRR becomes not ready\n");
			osmo_fsm_inst_state_chg(fi, GRR_ST_PACKET_NOT_READY, 0, 0);
		}
		break;
	case GRR_EV_PCH_AGCH_BLOCK_IND:
		grr_rx_ccch(ms, (struct msgb *)data);
		break;
	case GRR_EV_RACH_REQ:
	{
		const struct osmo_gprs_rlcmac_l1ctl_prim *lp = data;
		struct gsm48_rrlayer *rr = &ms->rrlayer;

		if (lp->rach_req.is_11bit) { /* TODO: implement 11-bit RACH */
			LOGPFSML(fi, LOGL_ERROR, "11-bit RACH is not supported\n");
			return;
		}

		rr->cr_ra = lp->rach_req.ra;
		memset(&rr->cr_hist[0], 0x00, sizeof(rr->cr_hist));

		LOGPFSML(fi, LOGL_INFO, "Sending CHANNEL REQUEST (0x%02x)\n", rr->cr_ra);
		l1ctl_tx_rach_req(ms, RSL_CHAN_RACH, 0x00, rr->cr_ra, 0,
				  ms->cellsel.ccch_mode == CCCH_MODE_COMBINED);

		rr->state = GSM48_RR_ST_CONN_PEND;
		break;
	}
	case GRR_EV_RACH_CNF:
	{
		struct gsm48_rrlayer *rr = &ms->rrlayer;
		const struct gsm48_req_ref *ref = data;

		LOGPFSML(fi, LOGL_NOTICE,
			 "Rx RACH.conf (RA=0x%02x, T1=%u, T3=%u, T2=%u, FN=%u)\n",
			 rr->cr_ra, ref->t1, ref->t3_high << 3 | ref->t3_low, ref->t2,
			 _gsm48_req_ref2fn(ref));

		if (ms->rrlayer.state != GSM48_RR_ST_CONN_PEND) {
			LOGPFSML(fi, LOGL_ERROR, "Rx unexpected RACH.conf\n");
			return;
		}

		/* shift the CHANNEL REQUEST history buffer */
		memmove(&rr->cr_hist[1], &rr->cr_hist[0], ARRAY_SIZE(rr->cr_hist) - 1);
		/* store the new entry */
		rr->cr_hist[0].ref = *ref;
		rr->cr_hist[0].ref.ra = rr->cr_ra;
		rr->cr_hist[0].valid = 1;
		break;
	}
	case GRR_EV_PDCH_ESTABLISH_REQ:
	{
		const struct osmo_gprs_rlcmac_l1ctl_prim *lp = data;

		if (!lp->pdch_est_req.fh) {
			LOGPFSML(fi, LOGL_INFO,
				 "PDCH Establish.Req: TSC=%u, H0, ARFCN=%u\n",
				 lp->pdch_est_req.tsc, lp->pdch_est_req.arfcn);
			l1ctl_tx_dm_est_req_h0(ms, lp->pdch_est_req.arfcn,
					       RSL_CHAN_OSMO_PDCH | lp->pdch_est_req.ts_nr,
					       lp->pdch_est_req.tsc, GSM48_CMODE_SIGN, 0);
		} else {
			/* Hopping */
			uint8_t ma_len = 0;
			uint16_t ma[64];

			LOGPFSML(fi, LOGL_INFO,
				 "PDCH Establish.Req: TSC=%u, H1, HSN=%u, MAIO=%u\n",
				 lp->pdch_est_req.tsc,
				 lp->pdch_est_req.fhp.hsn,
				 lp->pdch_est_req.fhp.maio);

			for (unsigned int i = 1, j = 0; i <= 1024; i++) {
				unsigned int arfcn = i & 1023;
				unsigned int k;

				if (~ms->cellsel.sel_si.freq[arfcn].mask & 0x01)
					continue;

				k = lp->pdch_est_req.fhp.ma_len - (j >> 3) - 1;
				if (lp->pdch_est_req.fhp.ma[k] & (1 << (j & 7)))
					ma[ma_len++] = arfcn;
				j++;
			}

			l1ctl_tx_dm_est_req_h1(ms,
					       lp->pdch_est_req.fhp.maio,
					       lp->pdch_est_req.fhp.hsn,
					       &ma[0], ma_len,
					       RSL_CHAN_OSMO_PDCH | lp->pdch_est_req.ts_nr,
					       lp->pdch_est_req.tsc, GSM48_CMODE_SIGN, 0);
		}
		osmo_fsm_inst_state_chg(fi, GRR_ST_PACKET_TRANSFER, 0, 0);
		break;
	}
	default:
		OSMO_ASSERT(0);
	}
}

static void grr_st_packet_transfer_onenter(struct osmo_fsm_inst *fi, uint32_t prev_state)
{
	struct osmocom_ms *ms = fi->priv;

	ms->rrlayer.state = GSM48_RR_ST_DEDICATED;
}

static void grr_st_packet_transfer_onleave(struct osmo_fsm_inst *fi, uint32_t next_state)
{
	struct osmocom_ms *ms = fi->priv;

	ms->rrlayer.state = GSM48_RR_ST_IDLE;
}

static void grr_st_packet_transfer_action(struct osmo_fsm_inst *fi,
					  uint32_t event, void *data)
{
	struct osmocom_ms *ms = fi->priv;

	switch (event) {
	case GRR_EV_PDCH_UL_TBF_CFG_REQ:
	{
		const struct osmo_gprs_rlcmac_l1ctl_prim *lp = data;
		l1ctl_tx_gprs_ul_tbf_cfg_req(ms,
					     lp->cfg_ul_tbf_req.ul_tbf_nr,
					     lp->cfg_ul_tbf_req.ul_slotmask,
					     lp->cfg_ul_tbf_req.start_fn);
		break;
	}
	case GRR_EV_PDCH_DL_TBF_CFG_REQ:
	{
		const struct osmo_gprs_rlcmac_l1ctl_prim *lp = data;
		l1ctl_tx_gprs_dl_tbf_cfg_req(ms,
					     lp->cfg_dl_tbf_req.dl_tbf_nr,
					     lp->cfg_dl_tbf_req.dl_slotmask,
					     lp->cfg_ul_tbf_req.start_fn,
					     lp->cfg_dl_tbf_req.dl_tfi);
		break;
	}
	case GRR_EV_PDCH_BLOCK_REQ:
	{
		const struct osmo_gprs_rlcmac_l1ctl_prim *lp = data;
		l1ctl_tx_gprs_ul_block_req(ms,
					   lp->pdch_data_req.fn,
					   lp->pdch_data_req.ts_nr,
					   lp->pdch_data_req.data,
					   lp->pdch_data_req.data_len);
		break;
	}
	case GRR_EV_PDCH_BLOCK_IND:
		handle_pdch_block_ind(ms, (struct msgb *)data);
		break;
	case GRR_EV_PDCH_RTS_IND:
		handle_pdch_rts_ind(ms, (struct msgb *)data);
		break;
	case GRR_EV_PDCH_RELEASE_REQ:
		modem_sync_to_cell(ms);
		osmo_fsm_inst_state_chg(fi, GRR_ST_PACKET_NOT_READY, 0, 0);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static const struct osmo_fsm_state grr_fsm_states[] = {
	[GRR_ST_PACKET_NOT_READY] = {
		.name = "PACKET_NOT_READY",
		.out_state_mask = S(GRR_ST_PACKET_IDLE),
		.in_event_mask  = S(GRR_EV_BCCH_BLOCK_IND)
				| S(GRR_EV_PCH_AGCH_BLOCK_IND),
		.action = &grr_st_packet_not_ready_action,
	},
	[GRR_ST_PACKET_IDLE] = {
		.name = "PACKET_IDLE",
		.out_state_mask = S(GRR_ST_PACKET_NOT_READY)
				| S(GRR_ST_PACKET_TRANSFER),
		.in_event_mask  = S(GRR_EV_BCCH_BLOCK_IND)
				| S(GRR_EV_PCH_AGCH_BLOCK_IND)
				| S(GRR_EV_RACH_REQ)
				| S(GRR_EV_RACH_CNF)
				| S(GRR_EV_PDCH_ESTABLISH_REQ),
		.action = &grr_st_packet_idle_action,
		.onenter = &grr_st_packet_idle_onenter,
	},
	[GRR_ST_PACKET_TRANSFER] = {
		.name = "PACKET_TRANSFER",
		.out_state_mask = S(GRR_ST_PACKET_NOT_READY),
		.in_event_mask  = S(GRR_EV_PDCH_UL_TBF_CFG_REQ)
				| S(GRR_EV_PDCH_DL_TBF_CFG_REQ)
				| S(GRR_EV_PDCH_BLOCK_REQ)
				| S(GRR_EV_PDCH_BLOCK_IND)
				| S(GRR_EV_PDCH_RTS_IND)
				| S(GRR_EV_PDCH_RELEASE_REQ),
		.action = &grr_st_packet_transfer_action,
		.onenter = &grr_st_packet_transfer_onenter,
		.onleave = &grr_st_packet_transfer_onleave,
	},
};

static const struct value_string grr_fsm_event_names[] = {
	OSMO_VALUE_STRING(GRR_EV_BCCH_BLOCK_IND),
	OSMO_VALUE_STRING(GRR_EV_PCH_AGCH_BLOCK_IND),
	OSMO_VALUE_STRING(GRR_EV_RACH_REQ),
	OSMO_VALUE_STRING(GRR_EV_RACH_CNF),
	OSMO_VALUE_STRING(GRR_EV_PDCH_ESTABLISH_REQ),
	OSMO_VALUE_STRING(GRR_EV_PDCH_RELEASE_REQ),
	OSMO_VALUE_STRING(GRR_EV_PDCH_UL_TBF_CFG_REQ),
	OSMO_VALUE_STRING(GRR_EV_PDCH_DL_TBF_CFG_REQ),
	OSMO_VALUE_STRING(GRR_EV_PDCH_BLOCK_REQ),
	OSMO_VALUE_STRING(GRR_EV_PDCH_BLOCK_IND),
	OSMO_VALUE_STRING(GRR_EV_PDCH_RTS_IND),
	{ 0, NULL }
};

struct osmo_fsm grr_fsm_def = {
	.name = "GPRS-RR",
	.log_subsys = DRR,
	.states = grr_fsm_states,
	.num_states = ARRAY_SIZE(grr_fsm_states),
	.event_names = grr_fsm_event_names,
};

static __attribute__((constructor)) void on_dso_load(void)
{
	OSMO_ASSERT(osmo_fsm_register(&grr_fsm_def) == 0);
}
