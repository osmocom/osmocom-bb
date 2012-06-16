/* GPRS BSSGP protocol implementation as per 3GPP TS 08.18 */

/* (C) 2009-2012 by Harald Welte <laforge@gnumonks.org>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <errno.h>
#include <stdint.h>

#include <netinet/in.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/tlv.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gprs/gprs_bssgp.h>
#include <osmocom/gprs/gprs_ns.h>

#include "common_vty.h"

struct gprs_ns_inst *bssgp_nsi;

/* BSSGP Protocol specific, not implementation specific */
/* FIXME: This needs to go into libosmocore after finished */

/* Chapter 11.3.9 / Table 11.10: Cause coding */
static const struct value_string bssgp_cause_strings[] = {
	{ BSSGP_CAUSE_PROC_OVERLOAD,	"Processor overload" },
	{ BSSGP_CAUSE_EQUIP_FAIL,	"Equipment Failure" },
	{ BSSGP_CAUSE_TRASIT_NET_FAIL,	"Transit netowkr service failure" },
	{ BSSGP_CAUSE_CAPA_GREATER_0KPBS,"Transmission capacity modified" },
	{ BSSGP_CAUSE_UNKNOWN_MS,	"Unknown MS" },
	{ BSSGP_CAUSE_UNKNOWN_BVCI,	"Unknown BVCI" },
	{ BSSGP_CAUSE_CELL_TRAF_CONG,	"Cell traffic congestion" },
	{ BSSGP_CAUSE_SGSN_CONG,	"SGSN congestion" },
	{ BSSGP_CAUSE_OML_INTERV,	"O&M intervention" },
	{ BSSGP_CAUSE_BVCI_BLOCKED,	"BVCI blocked" },
	{ BSSGP_CAUSE_PFC_CREATE_FAIL,	"PFC create failure" },
	{ BSSGP_CAUSE_SEM_INCORR_PDU,	"Semantically incorrect PDU" },
	{ BSSGP_CAUSE_INV_MAND_INF,	"Invalid mandatory information" },
	{ BSSGP_CAUSE_MISSING_MAND_IE,	"Missing mandatory IE" },
	{ BSSGP_CAUSE_MISSING_COND_IE,	"Missing conditional IE" },
	{ BSSGP_CAUSE_UNEXP_COND_IE,	"Unexpected conditional IE" },
	{ BSSGP_CAUSE_COND_IE_ERR,	"Conditional IE error" },
	{ BSSGP_CAUSE_PDU_INCOMP_STATE,	"PDU incompatible with protocol state" },
	{ BSSGP_CAUSE_PROTO_ERR_UNSPEC,	"Protocol error - unspecified" },
	{ BSSGP_CAUSE_PDU_INCOMP_FEAT, 	"PDU not compatible with feature set" },
	{ 0, NULL },
};

const char *bssgp_cause_str(enum gprs_bssgp_cause cause)
{
	return get_value_string(bssgp_cause_strings, cause);
}


struct msgb *bssgp_msgb_alloc(void)
{
	return msgb_alloc_headroom(4096, 128, "BSSGP");
}

/* Transmit a simple response such as BLOCK/UNBLOCK/RESET ACK/NACK */
int bssgp_tx_simple_bvci(uint8_t pdu_type, uint16_t nsei,
			 uint16_t bvci, uint16_t ns_bvci)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));
	uint16_t _bvci;

	msgb_nsei(msg) = nsei;
	msgb_bvci(msg) = ns_bvci;

	bgph->pdu_type = pdu_type;
	_bvci = htons(bvci);
	msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &_bvci);

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}

/* Chapter 10.4.14: Status */
int bssgp_tx_status(uint8_t cause, uint16_t *bvci, struct msgb *orig_msg)
{
	struct msgb *msg = bssgp_msgb_alloc();
	struct bssgp_normal_hdr *bgph =
			(struct bssgp_normal_hdr *) msgb_put(msg, sizeof(*bgph));

	LOGP(DBSSGP, LOGL_NOTICE, "BSSGP BVCI=%u Tx STATUS, cause=%s\n",
		bvci ? *bvci : 0, bssgp_cause_str(cause));
	msgb_nsei(msg) = msgb_nsei(orig_msg);
	msgb_bvci(msg) = 0;

	bgph->pdu_type = BSSGP_PDUT_STATUS;
	msgb_tvlv_put(msg, BSSGP_IE_CAUSE, 1, &cause);
	if (bvci) {
		uint16_t _bvci = htons(*bvci);
		msgb_tvlv_put(msg, BSSGP_IE_BVCI, 2, (uint8_t *) &_bvci);
	}
	msgb_tvlv_put(msg, BSSGP_IE_PDU_IN_ERROR,
		      msgb_bssgp_len(orig_msg), msgb_bssgph(orig_msg));

	return gprs_ns_sendmsg(bssgp_nsi, msg);
}
