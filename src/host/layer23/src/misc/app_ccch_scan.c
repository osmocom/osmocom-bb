/* CCCH passive sniffer */
/* (C) 2010-2011 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
 */

#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm48_ie.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/core/signal.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/misc/rslms.h>
#include <osmocom/bb/misc/layer3.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/ms.h>

#include <l1ctl_proto.h>

static struct {
	struct osmocom_ms *ms;
	int ccch_mode;
} app_state;

static int bcch_check_tc(uint8_t si_type, uint8_t tc)
{
	/* FIXME: there is no tc information (always 0) */
	return 0;

	switch (si_type) {
	case GSM48_MT_RR_SYSINFO_1:
		if (tc != 0)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_2:
		if (tc != 1)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_3:
		if (tc != 2 && tc != 6)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_4:
		if (tc != 3 && tc != 7)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_7:
		if (tc != 7)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_8:
		if (tc != 3)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_9:
		if (tc != 4)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_13:
		if (tc != 4 && tc != 0)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_16:
		if (tc != 6)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_17:
		if (tc != 2)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_2bis:
		if (tc != 5)
			return -EINVAL;
		break;
	case GSM48_MT_RR_SYSINFO_2ter:
		if (tc != 5 && tc != 4)
			return -EINVAL;
		break;

	/* The following types are used on SACCH only */
	case GSM48_MT_RR_SYSINFO_5:
	case GSM48_MT_RR_SYSINFO_6:
	case GSM48_MT_RR_SYSINFO_5bis:
	case GSM48_MT_RR_SYSINFO_5ter:
		break;

	/* Unknown SI type */
	default:
		LOGP(DRR, LOGL_INFO, "Unknown SI (type=0x%02x)\n", si_type);
		return -ENOTSUP;
	};

	return 0;
}

static void handle_si3(struct osmocom_ms *ms,
	struct gsm48_system_information_type_3 *si)
{
	if (app_state.ccch_mode != CCCH_MODE_NONE)
		return;

	if (si->control_channel_desc.ccch_conf == RSL_BCCH_CCCH_CONF_1_C)
		app_state.ccch_mode = CCCH_MODE_COMBINED;
	else
		app_state.ccch_mode = CCCH_MODE_NON_COMBINED;

	l1ctl_tx_ccch_mode_req(ms, app_state.ccch_mode);
}

static void dump_bcch(struct osmocom_ms *ms, uint8_t tc, const uint8_t *data)
{
	struct gsm48_system_information_type_header *si_hdr;
	si_hdr = (struct gsm48_system_information_type_header *) data;
	uint8_t si_type = si_hdr->system_information;

	LOGP(DRR, LOGL_INFO, "BCCH message (type=0x%02x): %s\n",
		si_type, gsm48_rr_msg_name(si_type));

	if (bcch_check_tc(si_type, tc) == -EINVAL)
		LOGP(DRR, LOGL_INFO, "SI on wrong tc=%u\n", tc);

	/* GSM 05.02 §6.3.1.3 Mapping of BCCH data */
	switch (si_type) {
	case GSM48_MT_RR_SYSINFO_3:
		handle_si3(ms,
			(struct gsm48_system_information_type_3 *) data);
		break;

	default:
		/* We don't care about other types of SI */
		break; /* thus there is nothing to do */
	};
}


/**
 * This method used to send a l1ctl_tx_dm_est_req_h0 or
 * a l1ctl_tx_dm_est_req_h1 to the layer1 to follow this
 * assignment. The code has been removed.
 */
static int gsm48_rx_imm_ass(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_imm_ass *ia = msgb_l3(msg);
	uint8_t ch_type, ch_subch, ch_ts;

	/* Discard packet TBF assignment */
	if (ia->page_mode & 0xf0)
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

		LOGP(DRR, LOGL_NOTICE, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
			"ARFCN=%u, TS=%u, SS=%u, TSC=%u)\n", ia->req_ref.ra,
			ia->chan_desc.chan_nr, arfcn, ch_ts, ch_subch,
			ia->chan_desc.h0.tsc);

	} else {
		/* Hopping */
		uint8_t maio, hsn;

		hsn = ia->chan_desc.h1.hsn;
		maio = ia->chan_desc.h1.maio_low | (ia->chan_desc.h1.maio_high << 2);

		LOGP(DRR, LOGL_NOTICE, "GSM48 IMM ASS (ra=0x%02x, chan_nr=0x%02x, "
			"HSN=%u, MAIO=%u, TS=%u, SS=%u, TSC=%u)\n", ia->req_ref.ra,
			ia->chan_desc.chan_nr, hsn, maio, ch_ts, ch_subch,
			ia->chan_desc.h1.tsc);
	}

	return 0;
}

static const char *pag_print_mode(int mode)
{
	switch (mode) {
	case 0:
		return "Normal paging";
	case 1:
		return "Extended paging";
	case 2:
		return "Paging reorganization";
	case 3:
		return "Same as before";
	default:
		return "invalid";
	}
}

static char *chan_need(int need)
{
	switch (need) {
	case 0:
		return "any";
	case 1:
		return "sdch";
	case 2:
		return "tch/f";
	case 3:
		return "tch/h";
	default:
		return "invalid";
	}
}

static char *mi_type_to_string(int type)
{
	switch (type) {
	case GSM_MI_TYPE_NONE:
		return "none";
	case GSM_MI_TYPE_IMSI:
		return "imsi";
	case GSM_MI_TYPE_IMEI:
		return "imei";
	case GSM_MI_TYPE_IMEISV:
		return "imeisv";
	case GSM_MI_TYPE_TMSI:
		return "tmsi";
	default:
		return "invalid";
	}
}

/**
 * This can contain two MIs. The size checking is a bit of a mess.
 */
static int gsm48_rx_paging_p1(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_paging1 *pag;
	int len1, len2, mi_type, tag;
	char mi_string[GSM48_MI_SIZE];

	/* is there enough room for the header + LV? */
	if (msgb_l3len(msg) < sizeof(*pag) + 2) {
		LOGP(DRR, LOGL_ERROR, "PagingRequest is too short.\n");
		return -1;
	}

	pag = msgb_l3(msg);
	len1 = pag->data[0];
	mi_type = pag->data[1] & GSM_MI_TYPE_MASK;

	if (msgb_l3len(msg) < sizeof(*pag) + 2 + len1) {
		LOGP(DRR, LOGL_ERROR, "PagingRequest with wrong MI\n");
		return -1;
	}

	if (mi_type != GSM_MI_TYPE_NONE) {
		gsm48_mi_to_string(mi_string, sizeof(mi_string), &pag->data[1], len1);
		LOGP(DRR, LOGL_NOTICE, "Paging1: %s chan %s to %s M(%s) \n",
		     pag_print_mode(pag->pag_mode),
		     chan_need(pag->cneed1),
		     mi_type_to_string(mi_type),
		     mi_string);
	}

	/* check if we have a MI type in here */
	if (msgb_l3len(msg) < sizeof(*pag) + 2 + len1 + 3)
		return 0;

	tag = pag->data[2 + len1 + 0];
	len2 = pag->data[2 + len1 + 1];
	mi_type = pag->data[2 + len1 + 2] & GSM_MI_TYPE_MASK;
	if (tag == GSM48_IE_MOBILE_ID && mi_type != GSM_MI_TYPE_NONE) {
		if (msgb_l3len(msg) < sizeof(*pag) + 2 + len1 + 3 + len2) {
			LOGP(DRR, LOGL_ERROR, "Optional MI does not fit here.\n");
			return -1;
		}

		gsm48_mi_to_string(mi_string, sizeof(mi_string), &pag->data[2 + len1 + 2], len2);
		LOGP(DRR, LOGL_NOTICE, "Paging2: %s chan %s to %s M(%s) \n",
		     pag_print_mode(pag->pag_mode),
		     chan_need(pag->cneed2),
		     mi_type_to_string(mi_type),
		     mi_string);
	}
	return 0;
}

static int gsm48_rx_paging_p2(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_paging2 *pag;
	int tag, len, mi_type;
	char mi_string[GSM48_MI_SIZE];

	if (msgb_l3len(msg) < sizeof(*pag)) {
		LOGP(DRR, LOGL_ERROR, "Paging2 message is too small.\n");
		return -1;
	}

	pag = msgb_l3(msg);
	LOGP(DRR, LOGL_NOTICE, "Paging1: %s chan %s to TMSI M(0x%x) \n",
		     pag_print_mode(pag->pag_mode),
		     chan_need(pag->cneed1), pag->tmsi1);
	LOGP(DRR, LOGL_NOTICE, "Paging2: %s chan %s to TMSI M(0x%x) \n",
		     pag_print_mode(pag->pag_mode),
		     chan_need(pag->cneed2), pag->tmsi2);

	/* no optional element */
	if (msgb_l3len(msg) < sizeof(*pag) + 3)
		return 0;

	tag = pag->data[0];
	len = pag->data[1];
	mi_type = pag->data[2] & GSM_MI_TYPE_MASK;

	if (tag != GSM48_IE_MOBILE_ID)
		return 0;

	if (msgb_l3len(msg) < sizeof(*pag) + 3 + len) {
		LOGP(DRR, LOGL_ERROR, "Optional MI does not fit in here\n");
		return -1;
	}

	gsm48_mi_to_string(mi_string, sizeof(mi_string), &pag->data[2], len);
	LOGP(DRR, LOGL_NOTICE, "Paging3: %s chan %s to %s M(%s) \n",
	     pag_print_mode(pag->pag_mode),
	     "n/a ",
	     mi_type_to_string(mi_type),
	     mi_string);

	return 0;
}

static int gsm48_rx_paging_p3(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_paging3 *pag;

	if (msgb_l3len(msg) < sizeof(*pag)) {
		LOGP(DRR, LOGL_ERROR, "Paging3 message is too small.\n");
		return -1;
	}

	pag = msgb_l3(msg);
	LOGP(DRR, LOGL_NOTICE, "Paging1: %s chan %s to TMSI M(0x%x) \n",
		     pag_print_mode(pag->pag_mode),
		     chan_need(pag->cneed1), pag->tmsi1);
	LOGP(DRR, LOGL_NOTICE, "Paging2: %s chan %s to TMSI M(0x%x) \n",
		     pag_print_mode(pag->pag_mode),
		     chan_need(pag->cneed2), pag->tmsi2);
	LOGP(DRR, LOGL_NOTICE, "Paging3: %s chan %s to TMSI M(0x%x) \n",
		     pag_print_mode(pag->pag_mode),
		     "n/a ", pag->tmsi3);
	LOGP(DRR, LOGL_NOTICE, "Paging4: %s chan %s to TMSI M(0x%x) \n",
		     pag_print_mode(pag->pag_mode),
		     "n/a ", pag->tmsi4);

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
static bool is_fill_frame(struct msgb *msg)
{
	size_t l2_len = msgb_l3len(msg);
	uint8_t *l2 = msgb_l3(msg);

	OSMO_ASSERT(l2_len == GSM_MACBLOCK_LEN);

	if (!memcmp(l2, paging_fill, sizeof(paging_fill)))
		return true;
	if (!memcmp(l2, lapdm_fill, sizeof(lapdm_fill)))
		return true;

	return false;
}

int gsm48_rx_ccch(struct msgb *msg, struct osmocom_ms *ms)
{
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);
	int rc = 0;

	/* Skip frames with wrong length */
	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN) {
		LOGP(DRR, LOGL_ERROR, "Rx CCCH message with odd length=%u: %s\n",
		     msgb_l3len(msg), msgb_hexdump_l3(msg));
		return -EINVAL;
	}

	/* Skip dummy (fill) frames */
	if (is_fill_frame(msg))
		return 0;

	if (sih->rr_protocol_discriminator != GSM48_PDISC_RR)
		LOGP(DRR, LOGL_ERROR, "PCH pdisc (%s) != RR\n",
			gsm48_pdisc_name(sih->rr_protocol_discriminator));

	switch (sih->system_information) {
	case GSM48_MT_RR_PAG_REQ_1:
		gsm48_rx_paging_p1(msg, ms);
		break;
	case GSM48_MT_RR_PAG_REQ_2:
		gsm48_rx_paging_p2(msg, ms);
		break;
	case GSM48_MT_RR_PAG_REQ_3:
		gsm48_rx_paging_p3(msg, ms);
		break;
	case GSM48_MT_RR_IMM_ASS:
		gsm48_rx_imm_ass(msg, ms);
		break;
	case GSM48_MT_RR_NOTIF_NCH:
		/* notification for voice call groups and such */
		break;
	case 0x07:
		/* wireshark know that this is SI2 quater and for 3G interop */
		break;
	default:
		LOGP(DRR, LOGL_NOTICE, "Unknown PCH/AGCH message "
			"(type 0x%02x): %s\n", sih->system_information,
			msgb_hexdump_l3(msg));
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

	return 0;
}

void layer3_app_reset(void)
{
	/* Reset state */
	app_state.ccch_mode = CCCH_MODE_NONE;
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
		break;
	}
	return 0;
}

static int _ccch_scan_start(void)
{
	int rc;

	rc = layer2_open(app_state.ms, app_state.ms->settings.layer2_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during layer2_open()\n");
		return rc;
	}

	l1ctl_tx_reset_req(app_state.ms, L1CTL_RES_T_FULL);
	return 0;
}

int l23_app_init(void)
{
	l23_app_start = _ccch_scan_start;

	app_state.ms = osmocom_ms_alloc(l23_ctx, "1");
	OSMO_ASSERT(app_state.ms);

	osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);
	return layer3_init(app_state.ms);
}

const struct l23_app_info l23_app_info = {
	.copyright	= "Copyright (C) 2010 Harald Welte <laforge@gnumonks.org>\n",
	.contribution	= "Contributions by Holger Hans Peter Freyther\n",
	.opt_supported = L23_OPT_ARFCN | L23_OPT_TAP | L23_OPT_DBG,
};
