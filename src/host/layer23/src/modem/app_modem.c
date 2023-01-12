/* modem app (gprs) */

/* (C) 2022 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>
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
#include <errno.h>
#include <stdio.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/application.h>

#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm48_ie.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gprs/rlcmac/gprs_rlcmac.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/sysinfo.h>

#include <l1ctl_proto.h>

static struct {
	enum ccch_mode ccch_mode;
	struct gsm48_sysinfo si;

	/* TODO: use mobile->rrlayer API instead */
	struct {
		bool ref_valid;
		struct gsm48_req_ref ref;
	} chan_req;
} app_data;

/* Generate a 8-bit CHANNEL REQUEST message as per 3GPP TS 44.018, 9.1.8 */
static uint8_t gen_chan_req(bool single_block)
{
	uint8_t rnd = (uint8_t)rand();

	if (single_block) /* 01110xxx */
		return 0x70 | (rnd & 0x07);

	/* 011110xx or 01111x0x or 01111xx0 */
	if ((rnd & 0x07) == 0x07)
		return 0x78;
	return 0x78 | (rnd & 0x07);
}

static int modem_tx_chan_req(struct osmocom_ms *ms, bool single_block)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	OSMO_ASSERT(rr->state == GSM48_RR_ST_IDLE);

	if (!app_data.si.si1)
		return -EBUSY;
	if (!app_data.si.gprs.supported)
		return -ENOTSUP;

	rr->cr_ra = gen_chan_req(single_block);
	LOGP(DRR, LOGL_NOTICE, "Sending CHANNEL REQUEST (0x%02x)\n", rr->cr_ra);
	l1ctl_tx_rach_req(ms, RSL_CHAN_RACH, 0x00, rr->cr_ra, 0,
			  app_data.ccch_mode == CCCH_MODE_COMBINED);

	rr->state = GSM48_RR_ST_CONN_PEND;
	return 0;
}

static int handle_si1(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&app_data.si.si1_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo1(&app_data.si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to decode SI1 message\n");
		return rc;
	}

	return 0;
}

static int handle_si3(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&app_data.si.si3_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo3(&app_data.si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to decode SI3 message\n");
		return rc;
	}

	if (app_data.ccch_mode == CCCH_MODE_NONE) {
		if (app_data.si.ccch_conf == RSL_BCCH_CCCH_CONF_1_C)
			app_data.ccch_mode = CCCH_MODE_COMBINED;
		else
			app_data.ccch_mode = CCCH_MODE_NON_COMBINED;
		l1ctl_tx_ccch_mode_req(ms, app_data.ccch_mode);
	}

	if (!app_data.si.gprs.supported) {
		LOGP(DRR, LOGL_NOTICE, "SI3 Rest Octets IE contains no GPRS Indicator\n");
		return 0;
	}

	LOGP(DRR, LOGL_NOTICE, "Found GPRS Indicator (RA Colour %u, SI13 on BCCH %s)\n",
	     app_data.si.gprs.ra_colour, app_data.si.gprs.si13_pos ? "Ext" : "Norm");

	return 0;
}

static int handle_si4(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&app_data.si.si4_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo4(&app_data.si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to decode SI4 message\n");
		return rc;
	}

	if (!app_data.si.gprs.supported) {
		LOGP(DRR, LOGL_NOTICE, "SI4 Rest Octets IE contains no GPRS Indicator\n");
		return 0;
	}

	LOGP(DRR, LOGL_NOTICE, "Found GPRS Indicator (RA Colour %u, SI13 on BCCH %s)\n",
	     app_data.si.gprs.ra_colour, app_data.si.gprs.si13_pos ? "Ext" : "Norm");

	return 0;
}

static int handle_si13(struct osmocom_ms *ms, struct msgb *msg)
{
	int rc;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&app_data.si.si13_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo13(&app_data.si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0)
		return rc;

	return 0;
}

static int modem_rx_bcch(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct gsm48_system_information_type_header *si_hdr = msgb_l3(msg);
	const uint8_t si_type = si_hdr->system_information;

	LOGP(DRR, LOGL_INFO, "BCCH message (type=0x%02x): %s\n",
	     si_type, gsm48_rr_msg_name(si_type));

	/* HACK: request an Uplink TBF here (one phase access) */
	if (ms->rrlayer.state == GSM48_RR_ST_IDLE)
		modem_tx_chan_req(ms, false);

	switch (si_type) {
	case GSM48_MT_RR_SYSINFO_1:
		return handle_si1(ms, msg);
	case GSM48_MT_RR_SYSINFO_3:
		return handle_si3(ms, msg);
	case GSM48_MT_RR_SYSINFO_4:
		return handle_si4(ms, msg);
	case GSM48_MT_RR_SYSINFO_13:
		return handle_si13(ms, msg);
	default:
		return 0;
	};
}

static int modem_rx_imm_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct gsm48_imm_ass *ia = msgb_l3(msg);
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	uint8_t ch_type, ch_subch, ch_ts;
	int rc;

	/* Discard CS channel assignment */
	if ((ia->page_mode >> 4) == 0)
		return 0;

	if (rr->state != GSM48_RR_ST_CONN_PEND)
		return 0;
	if (!app_data.chan_req.ref_valid)
		return 0;
	if (memcmp(&ia->req_ref, &app_data.chan_req.ref, sizeof(ia->req_ref)))
		return 0;

	if (rsl_dec_chan_nr(ia->chan_desc.chan_nr, &ch_type, &ch_subch, &ch_ts) != 0) {
		LOGP(DRR, LOGL_ERROR,
		     "%s(): rsl_dec_chan_nr(chan_nr=0x%02x) failed\n",
		     __func__, ia->chan_desc.chan_nr);
		return -EINVAL;
	}

	if (!ia->chan_desc.h0.h) {
		/* Non-hopping */
		uint16_t arfcn;

		arfcn = ia->chan_desc.h0.arfcn_low | (ia->chan_desc.h0.arfcn_high << 8);

		LOGP(DRR, LOGL_INFO, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
		     "ARFCN=%u, TS=%u, SS=%u, TSC=%u)\n", ia->req_ref.ra,
		     ia->chan_desc.chan_nr, arfcn, ch_ts, ch_subch,
		     ia->chan_desc.h0.tsc);

		l1ctl_tx_dm_est_req_h0(ms, arfcn, RSL_CHAN_OSMO_PDCH,
				       ia->chan_desc.h0.tsc, GSM48_CMODE_SIGN, 0);
	} else {
		/* Hopping */
		uint8_t maio, hsn, ma_len;
		uint16_t ma[64];

		hsn = ia->chan_desc.h1.hsn;
		maio = ia->chan_desc.h1.maio_low | (ia->chan_desc.h1.maio_high << 2);

		LOGP(DRR, LOGL_INFO, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
		     "HSN=%u, MAIO=%u, TS=%u, SS=%u, TSC=%u)\n", ia->req_ref.ra,
		     ia->chan_desc.chan_nr, hsn, maio, ch_ts, ch_subch,
		     ia->chan_desc.h1.tsc);

		for (unsigned int i = 1, j = 0; i <= 1024; i++) {
			unsigned int arfcn = i & 1023;
			unsigned int k;

			if (~app_data.si.freq[arfcn].mask & 0x01)
				continue;

			k = ia->mob_alloc_len - (j >> 3) - 1;
			if (ia->mob_alloc[k] & (1 << (j & 7)))
				ma[ma_len++] = arfcn;
			j++;
		}

		l1ctl_tx_dm_est_req_h1(ms, maio, hsn, &ma[0], ma_len, RSL_CHAN_OSMO_PDCH,
				       ia->chan_desc.h1.tsc, GSM48_CMODE_SIGN, 0);
	}

	const uint8_t *data = msgb_l3(msg) + sizeof(*ia) + ia->mob_alloc_len;
	size_t data_len = msgb_l3len(msg) - (sizeof(*ia) + ia->mob_alloc_len);
	IA_RestOctets_t iaro;

	rc = osmo_gprs_rlcmac_decode_imm_ass_ro(&iaro, data, data_len);
	if (rc != 0) {
		LOGP(DRR, LOGL_ERROR, "Failed to decode IA Rest Octets IE\n");
		return rc;
	}

	/* TODO: deliver decoded params to the RLC/MAC layer */

	rr->state = GSM48_RR_ST_DEDICATED;
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

static int modem_rx_ccch(struct osmocom_ms *ms, struct msgb *msg)
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
		return modem_rx_imm_ass(ms, msg);
	default:
		return 0;
	}
}

static int modem_rx_rslms_rll_ud(struct osmocom_ms *ms, struct msgb *msg)
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
		return modem_rx_ccch(ms, msg);
	case RSL_CHAN_BCCH:
		return modem_rx_bcch(ms, msg);
	default:
		return 0;
	}
}

static int modem_rx_rslms_rll(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);

	switch (rllh->c.msg_type) {
	case RSL_MT_UNIT_DATA_IND:
		return modem_rx_rslms_rll_ud(ms, msg);
	default:
		LOGP(DRSL, LOGL_NOTICE, "Unhandled RSLms RLL message "
		     "(msg_type 0x%02x)\n", rllh->c.msg_type);
		return -EINVAL;
	}
}

static int modem_rx_rslms_cchan(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct abis_rsl_cchan_hdr *ch = msgb_l2(msg);
	struct gsm48_rrlayer *rr = &ms->rrlayer;

	switch (ch->c.msg_type) {
	case RSL_MT_CHAN_CONF: /* RACH.conf */
		if (rr->state == GSM48_RR_ST_CONN_PEND) {
			const struct gsm48_req_ref *ref = (void *)&ch->data[1];
			LOGP(DRSL, LOGL_NOTICE,
			     "Rx RACH.conf (RA=0x%02x, T1=%u, T3=%u, T2=%u)\n",
			     rr->cr_ra, ref->t1, ref->t3_high << 3 | ref->t3_low, ref->t2);
			memcpy(&app_data.chan_req.ref, ref, sizeof(*ref));
			app_data.chan_req.ref.ra = rr->cr_ra;
			app_data.chan_req.ref_valid = true;
			return 0;
		}
		/* fall-through */
	default:
		LOGP(DRSL, LOGL_NOTICE, "Unhandled RSLms CCHAN message "
		     "(msg_type 0x%02x)\n", ch->c.msg_type);
		return -EINVAL;
	}
}

static int modem_rslms_cb(struct msgb *msg, struct lapdm_entity *le, void *ctx)
{
	const struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = modem_rx_rslms_rll((struct osmocom_ms *)ctx, msg);
		break;
	case ABIS_RSL_MDISC_COM_CHAN:
		rc = modem_rx_rslms_cchan((struct osmocom_ms *)ctx, msg);
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

void layer3_app_reset(void)
{
	memset(&app_data, 0x00, sizeof(app_data));
}

static int signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
		ms = signal_data;
		layer3_app_reset();
		return l1ctl_tx_fbsb_req(ms, ms->test_arfcn,
					 L1CTL_FBSB_F_FB01SB, 100, 0,
					 CCCH_MODE_NONE, dbm2rxlev(-85));
	}

	return 0;
}


int l23_app_init(struct osmocom_ms *ms)
{
	log_set_category_filter(osmo_stderr_target, DLGLOBAL, 1, LOGL_DEBUG);
	log_set_category_filter(osmo_stderr_target, DLCSN1, 1, LOGL_DEBUG);
	log_set_category_filter(osmo_stderr_target, DRR, 1, LOGL_INFO);

	osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);
	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);

	lapdm_channel_set_l3(&ms->lapdm_channel, &modem_rslms_cb, ms);

	return 0;
}

static int l23_cfg_supported(void)
{
	return L23_OPT_ARFCN | L23_OPT_TAP | L23_OPT_DBG;
}

static struct l23_app_info info = {
	.copyright = "Copyright (C) 2022 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>\n",
	.cfg_supported = &l23_cfg_supported,
};

struct l23_app_info *l23_app_info(void)
{
	return &info;
}
