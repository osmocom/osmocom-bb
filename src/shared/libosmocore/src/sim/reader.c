/* Card reader abstraction for libosmosim */
/*
 * (C) 2012 by Harald Welte <laforge@gnumonks.org>
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


#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <netinet/in.h>

#include <osmocom/core/msgb.h>
#include <osmocom/sim/sim.h>


#include "sim_int.h"

/* remove the SW from end of the message */
static int get_sw(struct msgb *resp)
{
	int ret;

	if (!msgb_apdu_de(resp) || msgb_apdu_le(resp) < 2)
		return -EIO;

	ret = msgb_get_u16(resp);

	return ret;
}

/* According to ISO7816-4 Annex A */
static int transceive_apdu_t0(struct osim_card_hdl *st, struct msgb *amsg)
{
	struct osim_reader_hdl *rh = st->reader;
	struct msgb *tmsg = msgb_alloc(1024, "TPDU");
	struct osim_apdu_cmd_hdr *tpduh;
	uint8_t *cur;
	uint16_t sw;
	int rc, num_resp = 0;

	if (!tmsg)
		return -ENOMEM;

	/* create TPDU header from APDU header */
	tpduh = (struct osim_apdu_cmd_hdr *) msgb_put(tmsg, sizeof(*tpduh));
	memcpy(tpduh, msgb_apdu_h(amsg), sizeof(*tpduh));

	switch (msgb_apdu_case(amsg)) {
	case APDU_CASE_1:
		tpduh->p3 = 0x00;
		break;
	case APDU_CASE_2S:
		tpduh->p3 = msgb_apdu_le(amsg);
		break;
	case APDU_CASE_2E:
		if (msgb_apdu_le(amsg) <= 256) {
			/* case 2E.1 */
			tpduh->p3 = msgb_apdu_le(amsg) & 0xff;
		} else {
			/* case 2E.2 */
			tpduh->p3 = 0;
			msgb_put_u16(tmsg, msgb_apdu_le(amsg));
		}
		break;
	case APDU_CASE_3S:
	case APDU_CASE_4S:
		tpduh->p3 = msgb_apdu_lc(amsg);
		cur = msgb_put(tmsg, tpduh->p3);
		memcpy(cur, msgb_apdu_dc(amsg), tpduh->p3);
		break;
	case APDU_CASE_3E:
	case APDU_CASE_4E:
		if (msgb_apdu_lc(amsg) < 256) {
			/* Case 3E.1 */
			tpduh->p3 = msgb_apdu_lc(amsg);
		} else {
			/* Case 3E.2 */
			/* FXIME: Split using ENVELOPE! */
			return -1;
		}
		break;
	}

transceive_again:

	/* store pointer to start of response */
	tmsg->l3h = tmsg->tail;

	/* transceive */
	rc = rh->ops->transceive(st->reader, tmsg);
	if (rc < 0) {
		msgb_free(tmsg);
		return rc;
	}
	msgb_apdu_sw(tmsg) = get_sw(tmsg);

	/* increase number of responsese received */
	num_resp++;

	/* save SW */
	sw = msgb_apdu_sw(tmsg);
	printf("sw = 0x%04x\n", sw);
	msgb_apdu_sw(amsg) = sw;

	switch (msgb_apdu_case(amsg)) {
	case APDU_CASE_1:
	case APDU_CASE_3S:
		/* just copy SW */
		break;
	case APDU_CASE_2S:
case_2s:
		switch (sw >> 8) {
		case 0x67: /* Case 2S.2: Le definitely not accepted */
			break;
		case 0x6c: /* Case 2S.3: Le not accepted, La indicated */
			tpduh->p3 = sw & 0xff;
			/* re-issue the command with La as */
			goto transceive_again;
			break;
		case 0x90:
			/* Case 2S.1, fall-through */
		case 0x91: case 0x92: case 0x93: case 0x94: case 0x95:
		case 0x96: case 0x97: case 0x98: case 0x99: case 0x9a:
		case 0x9b: case 0x9c: case 0x9d: case 0x9e: case 0x9f:
			/* Case 2S.4 */
			/* copy response data over */
			cur = msgb_put(amsg, msgb_l3len(tmsg));
			memcpy(cur, tmsg->l3h, msgb_l3len(tmsg));
		}
		break;
	case APDU_CASE_4S:
		/* FIXME: this is 4S.2 only for 2nd... response: */
		if (num_resp >= 2)
			goto case_2s;

		switch (sw >> 8) {
		case 0x60: case 0x62: case 0x63: case 0x64: case 0x65:
		case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a:
		case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
			/* Case 4S.1: Command not accepted: just copy SW */
			break;
		case 0x90:
			/* case 4S.2: Command accepted */
			tpduh->ins = 0xC0;
			tpduh->p1 = tpduh->p2 = 0;
			tpduh->p3 = msgb_apdu_le(amsg);
			/* strip off current result */
			msgb_get(tmsg, msgb_length(tmsg)-sizeof(*tpduh));
			goto transceive_again;
			break;
		case 0x61: /* Case 4S.3: command accepted with info added */
		case 0x9F: /* FIXME: This is specific to SIM cards */
			tpduh->ins = 0xC0;
			tpduh->p1 = tpduh->p2 = 0;
			tpduh->p3 = OSMO_MIN(msgb_apdu_le(amsg), sw & 0xff);
			/* strip off current result */
			msgb_get(tmsg, msgb_length(tmsg)-sizeof(*tpduh));
			goto transceive_again;
			break;
		}
		/* Case 4S.2: Command accepted: just copy SW */
		/* Case 4S.4: Just copy SW */
		break;
	case APDU_CASE_2E:
		if (msgb_apdu_le(amsg) <= 256) {
			/* Case 2E.1: Le <= 256 */
			goto case_2s;
		}
		switch (sw >> 8) {
		case 0x67:
			/* Case 2E.2a: wrong length, abort */
			break;
		case 0x6c:
			/* Case 2E.2b: wrong length, La given */
			tpduh->p3 = sw & 0xff;
			/* re-issue the command with La as given */
			goto transceive_again;
			break;
		case 0x90:
			/* Case 2E.2c: */
			break;
		case 0x61:
			/* Case 2E.2d: more data available */
			/* FIXME: issue yet another GET RESPONSE */
			break;
		}
		break;
	case APDU_CASE_3E:
		/* FIXME: handling for ENVELOPE splitting */
		break;
	case APDU_CASE_4E:
		break;
	}

	msgb_free(tmsg);

	/* compute total length of response data */
	msgb_apdu_le(amsg) = amsg->tail - msgb_apdu_de(amsg);

	return sw;
}

/* FIXME: T=1 According to ISO7816-4 Annex B */

int osim_transceive_apdu(struct osim_chan_hdl *st, struct msgb *amsg)
{
	switch (st->card->proto) {
	case OSIM_PROTO_T0:
		return transceive_apdu_t0(st->card, amsg);
	default:
		return -ENOTSUP;
	}
}

struct osim_reader_hdl *osim_reader_open(enum osim_reader_driver driver, int idx,
					 const char *name, void *ctx)
{
	const struct osim_reader_ops *ops;
	struct osim_reader_hdl *rh;

	switch (driver) {
	case OSIM_READER_DRV_PCSC:
		ops = &pcsc_reader_ops;
		break;
	default:
		return NULL;
	}

	rh = ops->reader_open(idx, name, ctx);
	if (!rh)
		return NULL;
	rh->ops = ops;

	/* FIXME: for now we only do T=0 on all readers */
	rh->proto_supported = (1 << OSIM_PROTO_T0);

	return rh;
}

struct osim_card_hdl *osim_card_open(struct osim_reader_hdl *rh, enum osim_proto proto)
{
	struct osim_card_hdl *ch;

	if (!(rh->proto_supported & (1 << proto)))
		return NULL;

	ch = rh->ops->card_open(rh, proto);
	if (!ch)
		return NULL;

	ch->proto = proto;

	return ch;
}
