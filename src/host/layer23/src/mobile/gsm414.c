/*
 * (C) 2020 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
 *
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-2.0+
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

#include <osmocom/core/logging.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/msgb.h>

#include <osmocom/gsm/gsm48.h>
#include <osmocom/gsm/rsl.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_04_14.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/common/ms.h>
#include <osmocom/bb/common/logging.h>
#include <osmocom/bb/mobile/gsm48_rr.h>

#include <l1ctl_proto.h>

int gsm48_rr_tx_rr_status(struct osmocom_ms *ms, uint8_t cause);
int gsm48_send_rsl(struct osmocom_ms *ms, uint8_t msg_type,
		   struct msgb *msg, uint8_t link_id);
struct msgb *gsm48_l3_msgb_alloc(void);

#define loop_mode_name(mode) \
	get_value_string(loop_mode_names, mode)

static const struct value_string loop_mode_names[] = {
	{ L1CTL_TCH_LOOP_OPEN,	"(OPEN)" },
	{ L1CTL_TCH_LOOP_A,	"A" },
	{ L1CTL_TCH_LOOP_B,	"B" },
	{ L1CTL_TCH_LOOP_C,	"C" },
	{ L1CTL_TCH_LOOP_D,	"D" },
	{ L1CTL_TCH_LOOP_E,	"E" },
	{ L1CTL_TCH_LOOP_F,	"F" },
	{ L1CTL_TCH_LOOP_I,	"I" },
	{ 0, NULL }
};

static struct msgb *alloc_gsm414_msg(uint8_t msg_type)
{
	struct gsm48_hdr *ngh;
	struct msgb *nmsg;

	nmsg = gsm48_l3_msgb_alloc();
	if (nmsg == NULL)
		return NULL;

	ngh = (struct gsm48_hdr *) msgb_put(nmsg, sizeof(*ngh));
	ngh->proto_discr = GSM48_PDISC_TEST;
	ngh->msg_type = msg_type;

	return nmsg;
}

static int handle_close_tch_loop(struct osmocom_ms *ms, const struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	const struct gsm48_hdr *gh = msgb_l3(msg);
	unsigned int msg_len = msgb_l3len(msg);
	struct msgb *nmsg;

	/* Make sure that we have an active connection */
	if (rr->state != GSM48_RR_ST_DEDICATED) {
		LOGP(DMM, LOGL_NOTICE, "TCH loop requires an active connection\n");
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT);
		return -EINVAL;
	}

	/* Make sure that the established channel is either TCH/F or TCH/H */
	if ((rr->cd_now.chan_nr & 0xf8) != RSL_CHAN_Bm_ACCHs
	 && (rr->cd_now.chan_nr & 0xf0) != RSL_CHAN_Lm_ACCHs) {
		LOGP(DMM, LOGL_NOTICE, "TCH loop requires a TCH/F or TCH/H connection\n");
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT);
		return -EINVAL;
	}

	/* Check if a loop is already closed */
	if (rr->tch_loop_mode != L1CTL_TCH_LOOP_OPEN) {
		LOGP(DMM, LOGL_NOTICE, "TCH loop has already been closed\n");
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT);
		return -EINVAL;
	}

	if ((msg_len - sizeof(*gh)) < 1)
		return -EINVAL;

	/* Parse type of the TCH test loop, convert to L1CTL format */
	uint8_t gsm414_loop_mode = (gh->data[0] >> 1) & 0x1f;

	/* NOTE: some bits are not specified, so they can be 0 or 1 */
	if (gsm414_loop_mode == GSM414_LOOP_A)
		rr->tch_loop_mode = L1CTL_TCH_LOOP_A;
	else if (gsm414_loop_mode == GSM414_LOOP_B)
		rr->tch_loop_mode = L1CTL_TCH_LOOP_B;
	else if ((gsm414_loop_mode & 0x1e) == GSM414_LOOP_C)
		rr->tch_loop_mode = L1CTL_TCH_LOOP_C;
	else if ((gsm414_loop_mode & 0x1c) == GSM414_LOOP_D)
		rr->tch_loop_mode = L1CTL_TCH_LOOP_D;
	else if ((gsm414_loop_mode & 0x1c) == GSM414_LOOP_E)
		rr->tch_loop_mode = L1CTL_TCH_LOOP_E;
	else if ((gsm414_loop_mode & 0x1c) == GSM414_LOOP_F)
		rr->tch_loop_mode = L1CTL_TCH_LOOP_F;
	else if ((gsm414_loop_mode & 0x1c) == GSM414_LOOP_I)
		rr->tch_loop_mode = L1CTL_TCH_LOOP_I;
	else {
		LOGP(DMM, LOGL_NOTICE, "Unhandled 3GPP TS 44.014 TCH loop "
		     "mode=0x%02x => rejecting\n", gsm414_loop_mode);
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N);
		return -ENOTSUP;
	}

	LOGP(DMM, LOGL_NOTICE, "(%s) Closing 3GPP TS 44.014 TCH loop mode '%s'\n",
	     rsl_chan_nr_str(rr->cd_now.chan_nr), loop_mode_name(rr->tch_loop_mode));

	/* Instruct the L1 to enable received TCH loopback mode
	 * FIXME: delay applying this mode, so we can send the ACK first */
	l1ctl_tx_tch_mode_req(ms, rr->cd_now.mode, rr->audio_mode, rr->tch_loop_mode);

	/* Craft and send the ACKnowledgement */
	nmsg = alloc_gsm414_msg(GSM414_MT_CLOSE_TCH_LOOP_ACK);
	if (nmsg == NULL)
		return -ENOMEM;

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg, 0);
}

static int handle_open_tch_loop(struct osmocom_ms *ms, const struct msgb *msg)
{
	struct gsm48_rrlayer *rr = &ms->rrlayer;
	struct msgb *nmsg;

	/* Make sure that we have an active connection */
	if (rr->state != GSM48_RR_ST_DEDICATED) {
		LOGP(DMM, LOGL_NOTICE, "TCH loop requires an active connection\n");
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT);
		return -EINVAL;
	}

	/* Make sure that the established channel is either TCH/F or TCH/H */
	if ((rr->cd_now.chan_nr & 0xf8) != RSL_CHAN_Bm_ACCHs
	 && (rr->cd_now.chan_nr & 0xf0) != RSL_CHAN_Lm_ACCHs) {
		LOGP(DMM, LOGL_NOTICE, "TCH loop requires a TCH/F or TCH/H connection\n");
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT);
		return -EINVAL;
	}

	/* Check if a loop actually needs to be closed */
	if (rr->tch_loop_mode == L1CTL_TCH_LOOP_OPEN) {
		LOGP(DMM, LOGL_NOTICE, "TCH loop has not been closed (already open)\n");
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N_COMPAT);
		return -EINVAL;
	}

	LOGP(DMM, LOGL_NOTICE, "(%s) Opening 3GPP TS 44.014 TCH loop mode '%s'\n",
	     rsl_chan_nr_str(rr->cd_now.chan_nr), loop_mode_name(rr->tch_loop_mode));

	/* Instruct the L1 to disable the TCH loopback mode */
	l1ctl_tx_tch_mode_req(ms, rr->cd_now.mode, rr->audio_mode, L1CTL_TCH_LOOP_OPEN);

	/* Only the loop mode C needs to be ACKnowledged */
	bool needs_ack = rr->tch_loop_mode == L1CTL_TCH_LOOP_C;
	rr->tch_loop_mode = L1CTL_TCH_LOOP_OPEN;
	if (!needs_ack)
		return 0;

	/* Craft and send the ACKnowledgement */
	nmsg = alloc_gsm414_msg(GSM414_MT_OPEN_LOOP_CMD);
	if (nmsg == NULL)
		return -ENOMEM;

	msgb_put_u8(nmsg, GSM414_OPEN_LOOP_ACK_IE);

	return gsm48_send_rsl(ms, RSL_MT_DATA_REQ, nmsg, 0);
}

int gsm414_rcv_test(struct osmocom_ms *ms, const struct msgb *msg)
{
	const struct gsm48_hdr *gh = msgb_l3(msg);

	LOGP(DMM, LOGL_INFO, "Received 3GPP TS 44.014 message '%s' (0x%02x)\n",
	     get_value_string(gsm414_msgt_names, gh->msg_type), gh->msg_type);

	/* TODO: check if the test SIM (special EF.ADM) is inserted */
	switch (gh->msg_type) {
	case GSM414_MT_CLOSE_TCH_LOOP_CMD:
		return handle_close_tch_loop(ms, msg);
	case GSM414_MT_OPEN_LOOP_CMD:
		return handle_open_tch_loop(ms, msg);
	default:
		LOGP(DMM, LOGL_NOTICE, "Unhandled 3GPP TS 44.014 message '%s' (0x%02x)\n",
		     get_value_string(gsm414_msgt_names, gh->msg_type), gh->msg_type);
		gsm48_rr_tx_rr_status(ms, GSM48_RR_CAUSE_MSG_TYPE_N);
		return -ENOTSUP;
	}
}
