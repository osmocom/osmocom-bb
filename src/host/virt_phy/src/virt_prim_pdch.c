/*
 * (C) 2023 by sysmocom - s.f.m.c. GmbH <info@sysmocom.de>
 * Author: Vadim Yanitskiy <vyanitskiy@sysmocom.de>
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

#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/gsm/gsm0502.h>

#include <osmocom/bb/virtphy/l1ctl_sap.h>
#include <osmocom/bb/virtphy/virt_l1_sched.h>
#include <osmocom/bb/virtphy/gsmtapl1_if.h>
#include <osmocom/bb/virtphy/logging.h>

#include <osmocom/bb/l1ctl_proto.h>
#include <osmocom/bb/l1gprs.h>

void l1ctl_rx_gprs_uldl_tbf_cfg_req(struct l1_model_ms *ms, struct msgb *msg)
{
	const struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;

	if (OSMO_UNLIKELY(ms->gprs == NULL)) {
		LOGPMS(DL1C, LOGL_ERROR, ms, "l1gprs is not initialized\n");
		return;
	}

	msg->l1h = msgb_pull(msg, sizeof(*l1h));

	if (l1h->msg_type == L1CTL_GPRS_UL_TBF_CFG_REQ)
		l1gprs_handle_ul_tbf_cfg_req(ms->gprs, msg);
	else
		l1gprs_handle_dl_tbf_cfg_req(ms->gprs, msg);
}

void l1ctl_rx_gprs_ul_block_req(struct l1_model_ms *ms, struct msgb *msg)
{
	const struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1gprs_prim_ul_block_req req;

	if (OSMO_UNLIKELY(ms->gprs == NULL)) {
		LOGPMS(DL1P, LOGL_ERROR, ms, "l1gprs is not initialized\n");
		msgb_free(msg);
		return;
	}

	msg->l1h = (void *)l1h->data;
	if (l1gprs_handle_ul_block_req(ms->gprs, &req, msg) != 0) {
		msgb_free(msg);
		return;
	}
	msg->l2h = (void *)&req.data[0];

	virt_l1_sched_schedule(ms, msg, req.hdr.fn, req.hdr.tn,
			       &gsmtapl1_tx_to_virt_um_inst);
}

void l1ctl_tx_gprs_dl_block_ind(struct l1_model_ms *ms, const struct msgb *msg,
				uint32_t fn, uint8_t tn, uint8_t rxlev)
{
	struct l1gprs_prim_dl_block_ind ind;
	struct msgb *nmsg;
	uint8_t usf = 0xff;

	if (ms->gprs == NULL)
		return;

	ind = (struct l1gprs_prim_dl_block_ind) {
		.hdr = {
			.fn = fn,
			.tn = tn,
		},
		.meas = {
			.ber10k = 0, /* perfect Um, no errors */
			.ci_cb = 180, /* 18 dB */
			.rx_lev = rxlev,
		},
		.data = msgb_data(msg),
		.data_len = msgb_length(msg),
	};

	nmsg = l1gprs_handle_dl_block_ind(ms->gprs, &ind, &usf);
	if (nmsg != NULL)
		l1ctl_sap_tx_to_l23_inst(ms, nmsg);
	/* Every fn % 13 == 12 we have either a PTCCH or an IDLE slot, thus
	 * every fn % 13 ==  8 we add 5 frames, or 4 frames othrwise.  The
	 * resulting value is first fn of the next block. */
	const uint32_t rts_fn = GSM_TDMA_FN_SUM(fn, (fn % 13 == 8) ? 5 : 4);
	nmsg = l1gprs_handle_rts_ind(ms->gprs, rts_fn, tn, usf);
	if (nmsg != NULL)
		l1ctl_sap_tx_to_l23_inst(ms, nmsg);
}
