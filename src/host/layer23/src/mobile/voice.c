/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
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

#include <stdlib.h>

#include <osmocom/core/msgb.h>
#include <osmocom/codec/codec.h>

#include <osmocom/bb/common/osmocom_data.h>
#include <osmocom/bb/mobile/mncc.h>
#include <osmocom/bb/mobile/voice.h>


/*
 * receive voice
 */

static int gsm_recv_voice(struct osmocom_ms *ms, struct msgb *msg)
{
	struct gsm_data_frame *mncc;

	/* Drop the l1ctl_info_dl header */
	msgb_pull_to_l2(msg);
	/* push mncc header in front of data */
	mncc = (struct gsm_data_frame *)
		msgb_push(msg, sizeof(struct gsm_data_frame));
	mncc->msg_type = GSM_TCHF_FRAME_EFR;
	mncc->callref =  ms->mncc_entity.ref;

	/* HACK: send voice frame back */
	gsm_send_voice(ms, mncc);

	/* distribute and then free */
	if (ms->mncc_entity.mncc_recv && ms->mncc_entity.ref) {
		ms->mncc_entity.mncc_recv(ms, mncc->msg_type, mncc);
	}

	msgb_free(msg);
	return 0;
}

/*
 * send voice
 */
int gsm_send_voice(struct osmocom_ms *ms, struct gsm_data_frame *data)
{
	struct msgb *nmsg;

	nmsg = msgb_alloc_headroom(GSM_EFR_BYTES + 64, 64, "TCH/F");
	if (!nmsg)
		return -ENOMEM;
	nmsg->l2h = msgb_put(nmsg, GSM_EFR_BYTES);
	memcpy(nmsg->l2h, data->data, GSM_EFR_BYTES);

	return gsm48_rr_tx_voice(ms, nmsg);
}

/*
 * init
 */

int gsm_voice_init(struct osmocom_ms *ms)
{
	ms->l1_entity.l1_traffic_ind = gsm_recv_voice;

	return 0;
}
