/* RSLms - GSM 08.58 like protocol between L2 and L3 of GSM Um interface */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>

#include <osmocom/bb/common/logging.h>
#include <osmocom/gsm/lapdm.h>
#include <osmocom/bb/misc/rslms.h>
#include <osmocom/bb/misc/layer3.h>
#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/l1ctl.h>

/* Send a 'simple' RLL request to L2 */
int rslms_tx_rll_req(struct osmocom_ms *ms, uint8_t msg_type,
		     uint8_t chan_nr, uint8_t link_id)
{
	struct msgb *msg;

	msg = rsl_rll_simple(msg_type, chan_nr, link_id, 1);

	return lapdm_rslms_recvmsg(msg, &ms->lapdm_channel);
}

/* Send a RLL request (including L3 info) to L2 */
int rslms_tx_rll_req_l3(struct osmocom_ms *ms, uint8_t msg_type,
			uint8_t chan_nr, uint8_t link_id, struct msgb *msg)
{
	rsl_rll_push_l3(msg, msg_type, chan_nr, link_id, 1);

	return lapdm_rslms_recvmsg(msg, &ms->lapdm_channel);
}

static int rslms_rx_udata_ind(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct tlv_parsed tv;
	int rc = 0;
	
	DEBUGP(DRSL, "RSLms UNIT DATA IND chan_nr=0x%02x link_id=0x%02x\n",
		rllh->chan_nr, rllh->link_id);

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));
	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		DEBUGP(DRSL, "UNIT_DATA_IND without L3 INFO ?!?\n");
		return -EIO;
	}
	msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);

	if (rllh->chan_nr == RSL_CHAN_PCH_AGCH) {
		rc = gsm48_rx_ccch(msg, ms);
	} else if (rllh->chan_nr == RSL_CHAN_BCCH) {
		rc = gsm48_rx_bcch(msg, ms);
	}

	return rc;
}

static int rslms_rx_rll(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int rc = 0;

	switch (rllh->c.msg_type) {
	case RSL_MT_DATA_IND:
		DEBUGP(DRSL, "RSLms DATA IND\n");
		/* FIXME: implement this */
		break;
	case RSL_MT_UNIT_DATA_IND:
		rc = rslms_rx_udata_ind(msg, ms);
		break;
	case RSL_MT_EST_IND:
		DEBUGP(DRSL, "RSLms EST IND\n");
		/* FIXME: implement this */
		break;
	case RSL_MT_EST_CONF:
		DEBUGP(DRSL, "RSLms EST CONF\n");
		/* FIXME: implement this */
		break;
	case RSL_MT_REL_CONF:
		DEBUGP(DRSL, "RSLms REL CONF\n");
		/* FIXME: implement this */
		break;
	case RSL_MT_ERROR_IND:
		DEBUGP(DRSL, "RSLms ERR IND\n");
		/* FIXME: implement this */
		break;
	default:
		LOGP(DRSL, LOGL_NOTICE, "unknown RSLms message type "
			"0x%02x\n", rllh->c.msg_type);
		rc = -EINVAL;
		break;
	}
	msgb_free(msg);
	return rc;
}

/* input function that L2 calls when sending messages up to L3 */
static int layer3_from_layer2(struct msgb *msg, struct lapdm_entity *le, void *ctx)
{
	struct osmocom_ms *ms = ctx;
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = rslms_rx_rll(msg, ms);
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

int layer3_init(struct osmocom_ms *ms)
{
	lapdm_channel_set_l3(&ms->lapdm_channel, &layer3_from_layer2, ms);

	return 0;
}
