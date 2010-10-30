#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocore/msgb.h>
#include <osmocore/rsl.h>
#include <osmocore/tlv.h>
#include <osmocore/gsm48_ie.h>
#include <osmocore/protocol/gsm_04_08.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/lapdm.h>
#include <osmocom/bb/misc/rslms.h>
#include <osmocom/bb/misc/layer3.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>

static struct {
	int has_si1;
	int ccch_mode;
	int ccch_enabled;
	int rach_count;
	struct gsm_sysinfo_freq cell_arfcns[1024];
} app_state;


static void dump_bcch(struct osmocom_ms *ms, uint8_t tc, const uint8_t *data)
{
	struct gsm48_system_information_type_header *si_hdr;
	si_hdr = (struct gsm48_system_information_type_header *) data;

	/* GSM 05.02 §6.3.1.3 Mapping of BCCH data */
	switch (si_hdr->system_information) {
	case GSM48_MT_RR_SYSINFO_1:
		fprintf(stderr, "\tSI1");
#ifdef BCCH_TC_CHECK
		if (tc != 0)
			fprintf(stderr, " on wrong TC");
#endif
		if (!app_state.has_si1) {
			struct gsm48_system_information_type_1 *si1 =
				(struct gsm48_system_information_type_1 *)data;

			gsm48_decode_freq_list(&app_state.cell_arfcns,
			                       si1->cell_channel_description,
					       sizeof(si1->cell_channel_description),
					       0xff, 0x01);

			app_state.has_si1 = 1;
		}
		break;
	case GSM48_MT_RR_SYSINFO_2:
		fprintf(stderr, "\tSI2");
#ifdef BCCH_TC_CHECK
		if (tc != 1)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_3:
		fprintf(stderr, "\tSI3");
#ifdef BCCH_TC_CHECK
		if (tc != 2 && tc != 6)
			fprintf(stderr, " on wrong TC");
#endif
		if (app_state.ccch_mode == CCCH_MODE_NONE) {
			struct gsm48_system_information_type_3 *si3 =
				(struct gsm48_system_information_type_3 *)data;

			if (si3->control_channel_desc.ccch_conf == RSL_BCCH_CCCH_CONF_1_C)
				app_state.ccch_mode = CCCH_MODE_COMBINED;
			else
				app_state.ccch_mode = CCCH_MODE_NON_COMBINED;

			l1ctl_tx_ccch_mode_req(ms, app_state.ccch_mode);
		}
		break;
	case GSM48_MT_RR_SYSINFO_4:
		fprintf(stderr, "\tSI4");
#ifdef BCCH_TC_CHECK
		if (tc != 3 && tc != 7)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_5:
		fprintf(stderr, "\tSI5");
		break;
	case GSM48_MT_RR_SYSINFO_6:
		fprintf(stderr, "\tSI6");
		break;
	case GSM48_MT_RR_SYSINFO_7:
		fprintf(stderr, "\tSI7");
#ifdef BCCH_TC_CHECK
		if (tc != 7)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_8:
		fprintf(stderr, "\tSI8");
#ifdef BCCH_TC_CHECK
		if (tc != 3)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_9:
		fprintf(stderr, "\tSI9");
#ifdef BCCH_TC_CHECK
		if (tc != 4)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_13:
		fprintf(stderr, "\tSI13");
#ifdef BCCH_TC_CHECK
		if (tc != 4 && tc != 0)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_16:
		fprintf(stderr, "\tSI16");
#ifdef BCCH_TC_CHECK
		if (tc != 6)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_17:
		fprintf(stderr, "\tSI17");
#ifdef BCCH_TC_CHECK
		if (tc != 2)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_2bis:
		fprintf(stderr, "\tSI2bis");
#ifdef BCCH_TC_CHECK
		if (tc != 5)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_2ter:
		fprintf(stderr, "\tSI2ter");
#ifdef BCCH_TC_CHECK
		if (tc != 5 && tc != 4)
			fprintf(stderr, " on wrong TC");
#endif
		break;
	case GSM48_MT_RR_SYSINFO_5bis:
		fprintf(stderr, "\tSI5bis");
		break;
	case GSM48_MT_RR_SYSINFO_5ter:
		fprintf(stderr, "\tSI5ter");
		break;
	default:
		fprintf(stderr, "\tUnknown SI");
		break;
	};

	fprintf(stderr, "\n");
}


/* send location updating request * (as part of RSLms EST IND /
   LAPDm SABME) */
static int gsm48_tx_loc_upd_req(struct osmocom_ms *ms, uint8_t chan_nr)
{
	struct msgb *msg = msgb_alloc_headroom(256, 16, "loc_upd_req");
	struct gsm48_hdr *gh;
	struct gsm48_loc_upd_req *lu_r;

	DEBUGP(DMM, "chan_nr=%u\n", chan_nr);

	msg->l3h = msgb_put(msg, sizeof(*gh));
	gh = (struct gsm48_hdr *) msg->l3h;
	gh->proto_discr = GSM48_PDISC_MM;
	gh->msg_type = GSM48_MT_MM_LOC_UPD_REQUEST;
	lu_r = (struct gsm48_loc_upd_req *) msgb_put(msg, sizeof(*lu_r));
	lu_r->type = GSM48_LUPD_IMSI_ATT;
	lu_r->key_seq = 0;
	/* FIXME: set LAI and CM1 */
	/* FIXME: set MI */
	lu_r->mi_len = 0;

	return rslms_tx_rll_req_l3(ms, RSL_MT_EST_REQ, chan_nr, 0, msg);
}

static int gsm48_rx_imm_ass(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_imm_ass *ia = msgb_l3(msg);
	uint8_t ch_type, ch_subch, ch_ts;
	int rv;

	/* Discard packet TBF assignement */
	if (ia->page_mode & 0xf0)
		return 0;

	/* FIXME: compare RA and GSM time with when we sent RACH req */

	rsl_dec_chan_nr(ia->chan_desc.chan_nr, &ch_type, &ch_subch, &ch_ts);

	if (!ia->chan_desc.h0.h) {
		/* Non-hopping */
		uint16_t arfcn;

		arfcn = ia->chan_desc.h0.arfcn_low | (ia->chan_desc.h0.arfcn_high << 8);

		DEBUGP(DRR, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
			"ARFCN=%u, TS=%u, SS=%u, TSC=%u) ", ia->req_ref.ra,
			ia->chan_desc.chan_nr, arfcn, ch_ts, ch_subch,
			ia->chan_desc.h0.tsc);

		/* request L1 to go to dedicated mode on assigned channel */
		rv = l1ctl_tx_dm_est_req_h0(ms,
			arfcn, ia->chan_desc.chan_nr, ia->chan_desc.h0.tsc,
			GSM48_CMODE_SIGN);
	} else {
		/* Hopping */
		uint8_t maio, hsn, ma_len;
		uint16_t ma[64], arfcn;
		int i, j, k;

		hsn = ia->chan_desc.h1.hsn;
		maio = ia->chan_desc.h1.maio_low | (ia->chan_desc.h1.maio_high << 2);

		DEBUGP(DRR, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
			"HSN=%u, MAIO=%u, TS=%u, SS=%u, TSC=%u) ", ia->req_ref.ra,
			ia->chan_desc.chan_nr, hsn, maio, ch_ts, ch_subch,
			ia->chan_desc.h1.tsc);

		/* decode mobile allocation */
		ma_len = 0;
		for (i=1, j=0; i<=1024; i++) {
			arfcn = i & 1023;
			if (app_state.cell_arfcns[arfcn].mask & 0x01) {
				k = ia->mob_alloc_len - (j>>3) - 1;
				if (ia->mob_alloc[k] & (1 << (j&7))) {
					ma[ma_len++] = arfcn;
				}
				j++;
			}
		}

		/* request L1 to go to dedicated mode on assigned channel */
		rv = l1ctl_tx_dm_est_req_h1(ms,
			maio, hsn, ma, ma_len,
			ia->chan_desc.chan_nr, ia->chan_desc.h1.tsc,
			GSM48_CMODE_SIGN);
	}

	DEBUGPC(DRR, "\n");

	rv = gsm48_tx_loc_upd_req(ms, ia->chan_desc.chan_nr);

	return rv;
}

int gsm48_rx_ccch(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);
	int rc = 0;

	if (sih->rr_protocol_discriminator != GSM48_PDISC_RR)
		LOGP(DRR, LOGL_ERROR, "PCH pdisc != RR\n");
	
	switch (sih->system_information) {
	case GSM48_MT_RR_PAG_REQ_1:
	case GSM48_MT_RR_PAG_REQ_2:
	case GSM48_MT_RR_PAG_REQ_3:
		/* FIXME: implement decoding of paging request */
		break;
	case GSM48_MT_RR_IMM_ASS:
		rc = gsm48_rx_imm_ass(msg, ms);
		break;
	default:
		LOGP(DRR, LOGL_NOTICE, "unknown PCH/AGCH type 0x%02x\n",
			sih->system_information);
		rc = -EINVAL;
	}

	return rc;
}

int gsm48_rx_bcch(struct msgb *msg, struct osmocom_ms *ms)
{
	/* FIXME: we have lost the gsm frame time until here, need to store it
	 * in some msgb context */
	//dump_bcch(dl->time.tc, ccch->data);
	dump_bcch(ms, 0, msg->l3h);

	/* Req channel logic */
	if (app_state.ccch_enabled && (app_state.rach_count < 2)) {
		l1ctl_tx_rach_req(ms, app_state.rach_count, 0,
			app_state.ccch_mode == CCCH_MODE_COMBINED);
		app_state.rach_count++;
	}

	return 0;
}

void layer3_app_reset(void)
{
	/* Reset state */
	app_state.has_si1 = 0;
	app_state.ccch_mode = CCCH_MODE_NONE;
	app_state.ccch_enabled = 0;
	app_state.rach_count = 0;

	memset(&app_state.cell_arfcns, 0x00, sizeof(app_state.cell_arfcns));
}

