/* CBCH passive sniffer */

/* (C) 2010 by Holger Hans Peter Freyther
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Alex Badea <vamposdecampos@gmail.com>
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

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/common/l23_app.h>
#include <osmocom/bb/misc/layer3.h>

#include <osmocom/core/msgb.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/select.h>
#include <osmocom/core/signal.h>
#include <osmocom/gsm/rsl.h>

#include <l1ctl_proto.h>

struct osmocom_ms *g_ms;
struct gsm48_sysinfo g_sysinfo = {};

static int try_cbch(struct osmocom_ms *ms, struct gsm48_sysinfo *s)
{
	if (!s->si1 || !s->si4)
		return 0;
	if (!s->chan_nr) {
		LOGP(DRR, LOGL_INFO, "no CBCH chan_nr found\n");
		return 0;
	}

	if (s->h) {
		LOGP(DRR, LOGL_INFO, "chan_nr = 0x%02x TSC = %d  MAIO = %d  "
			"HSN = %d  hseq (%d): %s\n",
			s->chan_nr, s->tsc, s->maio, s->hsn,
			s->hopp_len,
			osmo_hexdump((unsigned char *) s->hopping, s->hopp_len * 2));
		return l1ctl_tx_dm_est_req_h1(ms,
			s->maio, s->hsn, s->hopping, s->hopp_len,
			s->chan_nr, s->tsc,
			GSM48_CMODE_SIGN, 0);
	} else {
		LOGP(DRR, LOGL_INFO, "chan_nr = 0x%02x TSC = %d  ARFCN = %d\n",
			s->chan_nr, s->tsc, s->arfcn);
		return l1ctl_tx_dm_est_req_h0(ms, s->arfcn,
			s->chan_nr, s->tsc, GSM48_CMODE_SIGN, 0);
	}
}


/* receive BCCH at RR layer */
static int bcch(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm48_system_information_type_header *sih = msgb_l3(msg);
	struct gsm48_sysinfo *s = &g_sysinfo;

	if (msgb_l3len(msg) != 23) {
		LOGP(DRR, LOGL_NOTICE, "Invalid BCCH message length\n");
		return -EINVAL;
	}
	switch (sih->system_information) {
	case GSM48_MT_RR_SYSINFO_1:
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 1\n");
		gsm48_decode_sysinfo1(s,
			(struct gsm48_system_information_type_1 *) sih,
			msgb_l3len(msg));
		return try_cbch(ms, s);
	case GSM48_MT_RR_SYSINFO_4:
		LOGP(DRR, LOGL_INFO, "New SYSTEM INFORMATION 4\n");
		gsm48_decode_sysinfo4(s,
			(struct gsm48_system_information_type_4 *) sih,
			msgb_l3len(msg));
		return try_cbch(ms, s);
	default:
		return 0;
	}
}

static int unit_data_ind(struct osmocom_ms *ms, struct msgb *msg)
{
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

	rsl_dec_chan_nr(rllh->chan_nr, &ch_type, &ch_subch, &ch_ts);
	switch (ch_type) {
	case RSL_CHAN_BCCH:
		return bcch(ms, msg);
	default:
		return 0;
	}
}

static int rcv_rll(struct osmocom_ms *ms, struct msgb *msg)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int msg_type = rllh->c.msg_type;

	if (msg_type == RSL_MT_UNIT_DATA_IND) {
		unit_data_ind(ms, msg);
	} else
		LOGP(DRSL, LOGL_NOTICE, "RSLms message unhandled\n");

	msgb_free(msg);

	return 0;
}

static int rcv_rsl(struct msgb *msg, struct lapdm_entity *le, void *l3ctx)
{
	struct osmocom_ms *ms = l3ctx;
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = rcv_rll(ms, msg);
		break;
	default:
		LOGP(DRSL, LOGL_NOTICE, "unknown RSLms msg_discr 0x%02x\n",
			rslh->msg_discr);
		msgb_free(msg);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int signal_cb(unsigned int subsys, unsigned int signal,
		     void *handler_data, void *signal_data)
{
	struct osmocom_ms *ms;

	if (subsys != SS_L1CTL)
		return 0;

	switch (signal) {
	case S_L1CTL_RESET:
	case S_L1CTL_FBSB_ERR:
		ms = g_ms;
		return l1ctl_tx_fbsb_req(ms, ms->test_arfcn,
			L1CTL_FBSB_F_FB01SB, 100, 0, CCCH_MODE_COMBINED,
			dbm2rxlev(-85));
	case S_L1CTL_FBSB_RESP:
		return 0;
	}
	return 0;
}

int l23_app_init(struct osmocom_ms *ms)
{
	/* don't do layer3_init() as we don't want an actualy L3 */

	g_ms = ms;
	lapdm_channel_set_l3(&ms->lapdm_channel, &rcv_rsl, ms);

	l1ctl_tx_reset_req(ms, L1CTL_RES_T_FULL);
	/* FIXME: L1CTL_RES_T_FULL doesn't reset dedicated mode
	 * (if previously set), so we release it here. */
	l1ctl_tx_dm_rel_req(ms);
	return osmo_signal_register_handler(SS_L1CTL, &signal_cb, NULL);
}

static struct l23_app_info info = {
	.copyright	= "Copyright (C) 2010 Harald Welte <laforge@gnumonks.org>\n",
	.contribution	= "Contributions by Holger Hans Peter Freyther\n",
};

struct l23_app_info *l23_app_info()
{
	return &info;
}
