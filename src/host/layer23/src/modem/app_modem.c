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

#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/signal.h>
#include <osmocom/core/application.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/tun.h>
#include <osmocom/vty/vty.h>

#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/gsm48_ie.h>
#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gprs/llc/llc.h>
#include <osmocom/gprs/llc/llc_prim.h>
#include <osmocom/gprs/rlcmac/csn1_defs.h>
#include <osmocom/gprs/rlcmac/rlcmac_prim.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/common/l1l2_interface.h>
#include <osmocom/bb/common/sysinfo.h>
#include <osmocom/bb/common/apn.h>
#include <osmocom/bb/modem/rlcmac.h>
#include <osmocom/bb/modem/llc.h>
#include <osmocom/bb/modem/sndcp.h>
#include <osmocom/bb/modem/vty.h>

#include <l1ctl_proto.h>

#include "config.h"

static struct {
	struct osmocom_ms *ms;
	enum ccch_mode ccch_mode;
	struct gsm48_sysinfo si;

	/* TODO: use mobile->rrlayer API instead */
	struct {
		bool ref_valid;
		struct gsm48_req_ref ref;
	} chan_req;
} app_data;

/* Local network-originated IP packet, needs to be sent via SNDCP/LLC (GPRS) towards GSM network */
static int modem_tun_data_ind_cb(struct osmo_tundev *tun, struct msgb *msg)
{
	struct osmobb_apn *apn = (struct osmobb_apn *)osmo_tundev_get_priv_data(tun);
	struct osmo_sockaddr dst;
	struct iphdr *iph = (struct iphdr *)msgb_data(msg);
	struct ip6_hdr *ip6h = (struct ip6_hdr *)msgb_data(msg);
	size_t pkt_len = msgb_length(msg);
	uint8_t pref_offset;
	char addrstr[INET6_ADDRSTRLEN];
	int rc = 0;

	switch (iph->version) {
	case 4:
		if (pkt_len < sizeof(*iph) || pkt_len < 4*iph->ihl)
			return -1;
		dst.u.sin.sin_family = AF_INET;
		dst.u.sin.sin_addr.s_addr = iph->daddr;
		break;
	case 6:
		/* Due to the fact that 3GPP requires an allocation of a
		 * /64 prefix to each MS, we must instruct
		 * ippool_getip() below to match only the leading /64
		 * prefix, i.e. the first 8 bytes of the address. If the ll addr
		 * is used, then the match should be done on the trailing 64
		 * bits. */
		dst.u.sin6.sin6_family = AF_INET6;
		pref_offset = IN6_IS_ADDR_LINKLOCAL(&ip6h->ip6_dst) ? 8 : 0;
		memcpy(&dst.u.sin6.sin6_addr, ((uint8_t *)&ip6h->ip6_dst) + pref_offset, 8);
		break;
	default:
		LOGTUN(LOGL_NOTICE, tun, "non-IPv%u packet received\n", iph->version);
		rc = -1;
		goto free_ret;
	}

	LOGPAPN(LOGL_DEBUG, apn, "system wants to transmit IPv%c pkt to %s (%zu bytes)\n",
		iph->version == 4 ? '4' : '6', osmo_sockaddr_ntop(&dst.u.sa, addrstr), pkt_len);

	rc = modem_sndcp_sn_unitdata_req(apn, msgb_data(msg), pkt_len);

free_ret:
	msgb_free(msg);
	return rc;
}

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

	if (!app_data.si.si1 || !app_data.si.si13)
		return -EAGAIN;
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
	struct osmo_gprs_rlcmac_prim *rlcmac_prim;

	if (msgb_l3len(msg) != GSM_MACBLOCK_LEN)
		return -EINVAL;
	if (!memcmp(&app_data.si.si13_msg[0], msgb_l3(msg), msgb_l3len(msg)))
		return 0; /* this message is already handled */

	rc = gsm48_decode_sysinfo13(&app_data.si, msgb_l3(msg), msgb_l3len(msg));
	if (rc != 0)
		return rc;

	/* Forward SI13 to RLC/MAC layer */
	rlcmac_prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_ccch_data_ind(0 /* TODO: fn */, msgb_l3(msg));
	rc = osmo_gprs_rlcmac_prim_lower_up(rlcmac_prim);
	return rc;
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


/*
GSM A-I/F DTAP - Attach Request
 Protocol Discriminator: GPRS mobility management messages (8)
 DTAP GPRS Mobility Management Message Type: Attach Request (0x01)
 MS Network Capability
  Length: 2
  1... .... = GEA/1: Encryption algorithm available
  .1.. .... = SM capabilities via dedicated channels: Mobile station supports mobile terminated point to point SMS via dedicated signalling channels
  ..1. .... = SM capabilities via GPRS channels: Mobile station supports mobile terminated point to point SMS via GPRS packet data channels
  ...0 .... = UCS2 support: The ME has a preference for the default alphabet (defined in 3GPP TS 23.038 [8b]) over UCS2
  .... 01.. = SS Screening Indicator: capability of handling of ellipsis notation and phase 2 error handling (0x1)
  .... ..0. = SoLSA Capability: The ME does not support SoLSA
  .... ...1 = Revision level indicator: Used by a mobile station supporting R99 or later versions of the protocol
  1... .... = PFC feature mode: Mobile station does support BSS packet flow procedures
  .110 000. = Extended GEA bits: 0x30
  .... ...0 = LCS VA capability: LCS value added location request notification capability not supported
 Attach Type
 Ciphering Key Sequence Number
 DRX Parameter
 Mobile Identity - TMSI/P-TMSI (0xf43cec71)
 Routing Area Identification - Old routing area identification - RAI: 234-70-5-0
 MS Radio Access Capability
 GPRS Timer - Ready Timer
  Element ID: 0x17
  GPRS Timer: 10 sec
  000. .... = Unit: value is incremented in multiples of 2 seconds (0)
  ...0 0101 = Timer value: 5

*/
static uint8_t pdu_gmmm_attach_req[] =  {
	0x08, 0x01, 0x02, 0xe5, 0xe0, 0x01, 0x0a, 0x00, 0x05, 0xf4, 0xf4, 0x3c, 0xec, 0x71, 0x32, 0xf4,
	0x07, 0x00, 0x05, 0x00, 0x17, 0x19, 0x33, 0x43, 0x2b, 0x37, 0x15, 0x9e, 0xf9, 0x88, 0x79, 0xcb,
	0xa2, 0x8c, 0x66, 0x21, 0xe7, 0x26, 0x88, 0xb1, 0x98, 0x87, 0x9c, 0x00, 0x17, 0x05
};

static int modem_tx_gmm_attach_req(struct osmocom_ms *ms)
{
	struct osmo_gprs_llc_prim *llc_prim;
	uint32_t tlli = 0xe1c5d364;
	int rc;

	llc_prim = osmo_gprs_llc_prim_alloc_ll_unitdata_req(tlli, OSMO_GPRS_LLC_SAPI_GMM,
							    pdu_gmmm_attach_req, sizeof(pdu_gmmm_attach_req));
	rc = osmo_gprs_llc_prim_upper_down(llc_prim);
	return rc;
}

static int modem_rx_imm_ass(struct osmocom_ms *ms, struct msgb *msg)
{
	const struct gsm48_imm_ass *ia = msgb_l3(msg);
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	uint8_t ch_type, ch_subch, ch_ts;
	int rc;
	struct osmo_gprs_rlcmac_prim *rlcmac_prim;

	modem_tx_gmm_attach_req(ms);

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

		l1ctl_tx_dm_est_req_h0(ms, arfcn,
				       RSL_CHAN_OSMO_PDCH | ch_ts,
				       ia->chan_desc.h0.tsc, GSM48_CMODE_SIGN, 0);
	} else {
		/* Hopping */
		uint8_t ma_len = 0;
		uint8_t maio, hsn;
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

		l1ctl_tx_dm_est_req_h1(ms, maio, hsn, &ma[0], ma_len,
				       RSL_CHAN_OSMO_PDCH | ch_ts,
				       ia->chan_desc.h1.tsc, GSM48_CMODE_SIGN, 0);
	}

	rlcmac_prim = osmo_gprs_rlcmac_prim_alloc_l1ctl_ccch_data_ind(0 /* TODO: fn */, (uint8_t *)ia);
	rc = osmo_gprs_rlcmac_prim_lower_up(rlcmac_prim);
	if (rc < 0)
		return rc;

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

static int _modem_start(void)
{
	int rc;

	rc = layer2_open(app_data.ms, app_data.ms->settings.layer2_socket_path);
	if (rc < 0) {
		fprintf(stderr, "Failed during layer2_open()\n");
		return rc;
	}

	l1ctl_tx_reset_req(app_data.ms, L1CTL_RES_T_FULL);
	return 0;
}

int l23_app_init(void)
{
	int rc;

	l23_app_start = _modem_start;

	log_set_category_filter(osmo_stderr_target, DLGLOBAL, 1, LOGL_DEBUG);
	log_set_category_filter(osmo_stderr_target, DLCSN1, 1, LOGL_DEBUG);
	log_set_category_filter(osmo_stderr_target, DRR, 1, LOGL_INFO);

	app_data.ms = osmocom_ms_alloc(l23_ctx, "1");
	OSMO_ASSERT(app_data.ms);

	if ((rc = modem_rlcmac_init(app_data.ms))) {
		LOGP(DRLCMAC, LOGL_FATAL, "Failed initializing RLC/MAC layer\n");
		return rc;
	}

	if ((rc = modem_llc_init(app_data.ms, NULL))) {
		LOGP(DLLC, LOGL_FATAL, "Failed initializing LLC layer\n");
		return rc;
	}

	if ((rc = modem_sndcp_init(app_data.ms))) {
		LOGP(DSNDCP, LOGL_FATAL, "Failed initializing SNDCP layer\n");
		return rc;
	}

	osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);
	lapdm_channel_set_l3(&app_data.ms->lapdm_channel, &modem_rslms_cb, app_data.ms);
	return 0;
}

static int l23_cfg_supported(void)
{
	return L23_OPT_ARFCN | L23_OPT_TAP | L23_OPT_VTY | L23_OPT_DBG;
}

static struct vty_app_info _modem_vty_info = {
	.name = "modem",
	.version = PACKAGE_VERSION,
	.go_parent_cb = modem_vty_go_parent,
};

static struct l23_app_info info = {
	.copyright = "Copyright (C) 2022 by sysmocom - s.m.f.c. GmbH <info@sysmocom.de>\n",
	.cfg_supported = &l23_cfg_supported,
	.vty_info = &_modem_vty_info,
	.vty_init = modem_vty_init,
	.tun_data_ind_cb = modem_tun_data_ind_cb,
};

struct l23_app_info *l23_app_info(void)
{
	return &info;
}
