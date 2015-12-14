/* GSM LAPDm (TS 04.06) implementation */

/* (C) 2010-2011 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010-2011 by Andreas Eversberg <jolly@eversberg.eu>
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

/*! \addtogroup lapdm
 *  @{
 */

/*! \file lapdm.c */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>

#include <osmocom/gsm/tlv.h>
#include <osmocom/gsm/rsl.h>
#include <osmocom/gsm/prim.h>
#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/lapdm.h>

#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>

/* TS 04.06 Figure 4 / Section 3.2 */
#define LAPDm_LPD_NORMAL  0
#define LAPDm_LPD_SMSCB	  1
#define LAPDm_SAPI_NORMAL 0
#define LAPDm_SAPI_SMS	  3
#define LAPDm_ADDR(lpd, sapi, cr) ((((lpd) & 0x3) << 5) | (((sapi) & 0x7) << 2) | (((cr) & 0x1) << 1) | 0x1)

#define LAPDm_ADDR_LPD(addr) (((addr) >> 5) & 0x3)
#define LAPDm_ADDR_SAPI(addr) (((addr) >> 2) & 0x7)
#define LAPDm_ADDR_CR(addr) (((addr) >> 1) & 0x1)
#define LAPDm_ADDR_EA(addr) ((addr) & 0x1)

/* TS 04.06 Table 3 / Section 3.4.3 */
#define LAPDm_CTRL_I(nr, ns, p)	((((nr) & 0x7) << 5) | (((p) & 0x1) << 4) | (((ns) & 0x7) << 1))
#define LAPDm_CTRL_S(nr, s, p)	((((nr) & 0x7) << 5) | (((p) & 0x1) << 4) | (((s) & 0x3) << 2) | 0x1)
#define LAPDm_CTRL_U(u, p)	((((u) & 0x1c) << (5-2)) | (((p) & 0x1) << 4) | (((u) & 0x3) << 2) | 0x3)

#define LAPDm_CTRL_is_I(ctrl)	(((ctrl) & 0x1) == 0)
#define LAPDm_CTRL_is_S(ctrl)	(((ctrl) & 0x3) == 1)
#define LAPDm_CTRL_is_U(ctrl)	(((ctrl) & 0x3) == 3)

#define LAPDm_CTRL_U_BITS(ctrl)	((((ctrl) & 0xC) >> 2) | ((ctrl) & 0xE0) >> 3)
#define LAPDm_CTRL_PF_BIT(ctrl)	(((ctrl) >> 4) & 0x1)

#define LAPDm_CTRL_S_BITS(ctrl)	(((ctrl) & 0xC) >> 2)

#define LAPDm_CTRL_I_Ns(ctrl)	(((ctrl) & 0xE) >> 1)
#define LAPDm_CTRL_Nr(ctrl)	(((ctrl) & 0xE0) >> 5)

#define LAPDm_LEN(len)	((len << 2) | 0x1)
#define LAPDm_MORE	0x2
#define LAPDm_EL	0x1

#define LAPDm_U_UI	0x0

/* TS 04.06 Section 5.8.3 */
#define N201_AB_SACCH		18
#define N201_AB_SDCCH		20
#define N201_AB_FACCH		20
#define N201_Bbis		23
#define N201_Bter_SACCH		21
#define N201_Bter_SDCCH		23
#define N201_Bter_FACCH		23
#define N201_B4			19

/* 5.8.2.1 N200 during establish and release */
#define N200_EST_REL		5
/* 5.8.2.1 N200 during timer recovery state */
#define N200_TR_SACCH		5
#define N200_TR_SDCCH		23
#define N200_TR_FACCH_FR	34
#define N200_TR_EFACCH_FR	48
#define N200_TR_FACCH_HR	29
/* FIXME: set N200 depending on chan_nr */
#define N200 N200_TR_SDCCH

enum lapdm_format {
	LAPDm_FMT_A,
	LAPDm_FMT_B,
	LAPDm_FMT_Bbis,
	LAPDm_FMT_Bter,
	LAPDm_FMT_B4,
};

static int lapdm_send_ph_data_req(struct lapd_msg_ctx *lctx, struct msgb *msg);
static int send_rslms_dlsap(struct osmo_dlsap_prim *dp,
	struct lapd_msg_ctx *lctx);
static int update_pending_frames(struct lapd_msg_ctx *lctx);

static void lapdm_dl_init(struct lapdm_datalink *dl,
			  struct lapdm_entity *entity, int t200)
{
	memset(dl, 0, sizeof(*dl));
	dl->entity = entity;
	lapd_dl_init(&dl->dl, 1, 8, 200);
	dl->dl.reestablish = 0; /* GSM uses no reestablish */
	dl->dl.send_ph_data_req = lapdm_send_ph_data_req;
	dl->dl.send_dlsap = send_rslms_dlsap;
	dl->dl.update_pending_frames = update_pending_frames;
	dl->dl.n200_est_rel = N200_EST_REL;
	dl->dl.n200 = N200;
	dl->dl.t203_sec = 0; dl->dl.t203_usec = 0;
	dl->dl.t200_sec = t200; dl->dl.t200_usec = 0;
}

/*! \brief initialize a LAPDm entity and all datalinks inside
 *  \param[in] le LAPDm entity
 *  \param[in] mode \ref lapdm_mode (BTS/MS)
 */
void lapdm_entity_init(struct lapdm_entity *le, enum lapdm_mode mode, int t200)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(le->datalink); i++)
		lapdm_dl_init(&le->datalink[i], le, t200);

	lapdm_entity_set_mode(le, mode);
}

/*! \brief initialize a LAPDm channel and all its channels
 *  \param[in] lc \ref lapdm_channel to be initialized
 *  \param[in] mode \ref lapdm_mode (BTS/MS)
 *
 * This really is a convenience wrapper around calling \ref
 * lapdm_entity_init twice.
 */
void lapdm_channel_init(struct lapdm_channel *lc, enum lapdm_mode mode)
{
	lapdm_entity_init(&lc->lapdm_acch, mode, 2);
	/* FIXME: this depends on chan type */
	lapdm_entity_init(&lc->lapdm_dcch, mode, 1);
}

/*! \brief flush and release all resoures in LAPDm entity */
void lapdm_entity_exit(struct lapdm_entity *le)
{
	unsigned int i;
	struct lapdm_datalink *dl;

	for (i = 0; i < ARRAY_SIZE(le->datalink); i++) {
		dl = &le->datalink[i];
		lapd_dl_exit(&dl->dl);
	}
}

/* \brief lfush and release all resources in LAPDm channel
 *
 * A convenience wrapper calling \ref lapdm_entity_exit on both
 * entities inside the \ref lapdm_channel
 */
void lapdm_channel_exit(struct lapdm_channel *lc)
{
	lapdm_entity_exit(&lc->lapdm_acch);
	lapdm_entity_exit(&lc->lapdm_dcch);
}

struct lapdm_datalink *lapdm_datalink_for_sapi(struct lapdm_entity *le, uint8_t sapi)
{
	switch (sapi) {
	case LAPDm_SAPI_NORMAL:
		return &le->datalink[0];
	case LAPDm_SAPI_SMS:
		return &le->datalink[1];
	default:
		return NULL;
	}
}

/* Append padding (if required) */
static void lapdm_pad_msgb(struct msgb *msg, uint8_t n201)
{
	int pad_len = n201 - msgb_l2len(msg);
	uint8_t *data;

	if (pad_len < 0) {
		LOGP(DLLAPD, LOGL_ERROR,
		     "cannot pad message that is already too big!\n");
		return;
	}

	data = msgb_put(msg, pad_len);
	memset(data, 0x2B, pad_len);
}

/* input function that L2 calls when sending messages up to L3 */
static int rslms_sendmsg(struct msgb *msg, struct lapdm_entity *le)
{
	if (!le->l3_cb) {
		msgb_free(msg);
		return -EIO;
	}

	/* call the layer2 message handler that is registered */
	return le->l3_cb(msg, le, le->l3_ctx);
}

/* write a frame into the tx queue */
static int tx_ph_data_enqueue(struct lapdm_datalink *dl, struct msgb *msg,
				uint8_t chan_nr, uint8_t link_id, uint8_t pad)
{
	struct lapdm_entity *le = dl->entity;
	struct osmo_phsap_prim pp;

	/* if there is a pending message, queue it */
	if (le->tx_pending || le->flags & LAPDM_ENT_F_POLLING_ONLY) {
		*msgb_push(msg, 1) = pad;
		*msgb_push(msg, 1) = link_id;
		*msgb_push(msg, 1) = chan_nr;
		msgb_enqueue(&dl->dl.tx_queue, msg);
		return -EBUSY;
	}

	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_DATA,
			PRIM_OP_REQUEST, msg);
	pp.u.data.chan_nr = chan_nr;
	pp.u.data.link_id = link_id;

	/* send the frame now */
	le->tx_pending = 0; /* disabled flow control */
	lapdm_pad_msgb(msg, pad);

	return le->l1_prim_cb(&pp.oph, le->l1_ctx);
}

static struct msgb *tx_dequeue_msgb(struct lapdm_entity *le)
{
	struct lapdm_datalink *dl;
	int last = le->last_tx_dequeue;
	int i = last, n = ARRAY_SIZE(le->datalink);
	struct msgb *msg = NULL;

	/* round-robin dequeue */
	do {
		/* next */
		i = (i + 1) % n;
		dl = &le->datalink[i];
		if ((msg = msgb_dequeue(&dl->dl.tx_queue)))
			break;
	} while (i != last);

	if (msg) {
		/* Set last dequeue position */
		le->last_tx_dequeue = i;
	}

	return msg;
}

/*! \brief dequeue a msg that's pending transmission via L1 and wrap it into
 * a osmo_phsap_prim */
int lapdm_phsap_dequeue_prim(struct lapdm_entity *le, struct osmo_phsap_prim *pp)
{
	struct msgb *msg;
	uint8_t pad;

	msg = tx_dequeue_msgb(le);
	if (!msg)
		return -ENODEV;

	/* if we have a message, send PH-DATA.req */
	osmo_prim_init(&pp->oph, SAP_GSM_PH, PRIM_PH_DATA,
			PRIM_OP_REQUEST, msg);

	/* Pull chan_nr and link_id */
	pp->u.data.chan_nr = *msg->data;
	msgb_pull(msg, 1);
	pp->u.data.link_id = *msg->data;
	msgb_pull(msg, 1);
	pad = *msg->data;
	msgb_pull(msg, 1);

	/* Pad the frame, we can transmit now */
	lapdm_pad_msgb(msg, pad);

	return 0;
}

/* get next frame from the tx queue. because the ms has multiple datalinks,
 * each datalink's queue is read round-robin.
 */
static int l2_ph_data_conf(struct msgb *msg, struct lapdm_entity *le)
{
	struct osmo_phsap_prim pp;

	/* we may send again */
	le->tx_pending = 0;

	/* free confirm message */
	if (msg)
		msgb_free(msg);

	if (lapdm_phsap_dequeue_prim(le, &pp) < 0) {
		/* no message in all queues */

		/* If user didn't request PH-EMPTY_FRAME.req, abort */
		if (!(le->flags & LAPDM_ENT_F_EMPTY_FRAME))
			return 0;

		/* otherwise, send PH-EMPTY_FRAME.req */
		osmo_prim_init(&pp.oph, SAP_GSM_PH,
				PRIM_PH_EMPTY_FRAME,
				PRIM_OP_REQUEST, NULL);
	} else {
		le->tx_pending = 1;
	}

	return le->l1_prim_cb(&pp.oph, le->l1_ctx);
}

/* Create RSLms various RSLms messages */
static int send_rslms_rll_l3(uint8_t msg_type, struct lapdm_msg_ctx *mctx,
			     struct msgb *msg)
{
	/* Add the RSL + RLL header */
	rsl_rll_push_l3(msg, msg_type, mctx->chan_nr, mctx->link_id, 1);

	/* send off the RSLms message to L3 */
	return rslms_sendmsg(msg, mctx->dl->entity);
}

/* Take a B4 format message from L1 and create RSLms UNIT DATA IND */
static int send_rslms_rll_l3_ui(struct lapdm_msg_ctx *mctx, struct msgb *msg)
{
	uint8_t l3_len = msg->tail - (uint8_t *)msgb_l3(msg);
	struct abis_rsl_rll_hdr *rllh;

	/* Add the RSL + RLL header */
	msgb_tv16_push(msg, RSL_IE_L3_INFO, l3_len);
	msgb_push(msg, 2 + 2);
	rsl_rll_push_hdr(msg, RSL_MT_UNIT_DATA_IND, mctx->chan_nr,
		mctx->link_id, 1);
	rllh = (struct abis_rsl_rll_hdr *)msgb_l2(msg);

	rllh->data[0] = RSL_IE_TIMING_ADVANCE;
	rllh->data[1] = mctx->ta_ind;

	rllh->data[2] = RSL_IE_MS_POWER;
	rllh->data[3] = mctx->tx_power_ind;
	
	return rslms_sendmsg(msg, mctx->dl->entity);
}

static int send_rll_simple(uint8_t msg_type, struct lapdm_msg_ctx *mctx)
{
	struct msgb *msg;

	msg = rsl_rll_simple(msg_type, mctx->chan_nr, mctx->link_id, 1);

	/* send off the RSLms message to L3 */
	return rslms_sendmsg(msg, mctx->dl->entity);
}

static int rsl_rll_error(uint8_t cause, struct lapdm_msg_ctx *mctx)
{
	struct msgb *msg;

	LOGP(DLLAPD, LOGL_NOTICE, "sending MDL-ERROR-IND %d\n", cause);
	msg = rsl_rll_simple(RSL_MT_ERROR_IND, mctx->chan_nr, mctx->link_id, 1);
	msgb_tlv_put(msg, RSL_IE_RLM_CAUSE, 1, &cause);
	return rslms_sendmsg(msg, mctx->dl->entity);
}

/* DLSAP L2 -> L3 (RSLms) */
static int send_rslms_dlsap(struct osmo_dlsap_prim *dp,
	struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct lapdm_datalink *mdl =
		container_of(dl, struct lapdm_datalink, dl);
	struct lapdm_msg_ctx *mctx = &mdl->mctx;
	uint8_t rll_msg = 0;

	switch (OSMO_PRIM_HDR(&dp->oph)) {
	case OSMO_PRIM(PRIM_DL_EST, PRIM_OP_INDICATION):
		if (dp->oph.msg && dp->oph.msg->len == 0) {
			/* omit L3 info by freeing message */
			msgb_free(dp->oph.msg);
			dp->oph.msg = NULL;
		}
		rll_msg = RSL_MT_EST_IND;
		break;
	case OSMO_PRIM(PRIM_DL_EST, PRIM_OP_CONFIRM):
		rll_msg = RSL_MT_EST_CONF;
		break;
	case OSMO_PRIM(PRIM_DL_DATA, PRIM_OP_INDICATION):
		rll_msg = RSL_MT_DATA_IND;
		break;
	case OSMO_PRIM(PRIM_DL_UNIT_DATA, PRIM_OP_INDICATION):
		return send_rslms_rll_l3_ui(mctx, dp->oph.msg);
	case OSMO_PRIM(PRIM_DL_REL, PRIM_OP_INDICATION):
		rll_msg = RSL_MT_REL_IND;
		break;
	case OSMO_PRIM(PRIM_DL_REL, PRIM_OP_CONFIRM):
		rll_msg = RSL_MT_REL_CONF;
		break;
	case OSMO_PRIM(PRIM_DL_SUSP, PRIM_OP_CONFIRM):
		rll_msg = RSL_MT_SUSP_CONF;
		break;
	case OSMO_PRIM(PRIM_MDL_ERROR, PRIM_OP_INDICATION):
		rsl_rll_error(dp->u.error_ind.cause, mctx);
		if (dp->oph.msg)
			msgb_free(dp->oph.msg);
		return 0;
	}

	if (!rll_msg) {
		LOGP(DLLAPD, LOGL_ERROR, "Unsupported op %d, prim %d. Please "
			"fix!\n", dp->oph.primitive, dp->oph.operation);
		return -EINVAL;
	}

	if (!dp->oph.msg)
		return send_rll_simple(rll_msg, mctx);

	return send_rslms_rll_l3(rll_msg, mctx, dp->oph.msg);
}

/* send a data frame to layer 1 */
static int lapdm_send_ph_data_req(struct lapd_msg_ctx *lctx, struct msgb *msg)
{
	uint8_t l3_len = msg->tail - msg->data;
	struct lapd_datalink *dl = lctx->dl;
	struct lapdm_datalink *mdl =
		container_of(dl, struct lapdm_datalink, dl);
	struct lapdm_msg_ctx *mctx = &mdl->mctx;
	int format = lctx->format;

	/* prepend l2 header */
	msg->l2h = msgb_push(msg, 3);
	msg->l2h[0] = LAPDm_ADDR(lctx->lpd, lctx->sapi, lctx->cr);
					/* EA is set here too */
	switch (format) {
	case LAPD_FORM_I:
		msg->l2h[1] = LAPDm_CTRL_I(lctx->n_recv, lctx->n_send,
						lctx->p_f);
		break;
	case LAPD_FORM_S:
		msg->l2h[1] = LAPDm_CTRL_S(lctx->n_recv, lctx->s_u, lctx->p_f);
		break;
	case LAPD_FORM_U:
		msg->l2h[1] = LAPDm_CTRL_U(lctx->s_u, lctx->p_f);
		break;
	default:
		msgb_free(msg);
		return -EINVAL;
	}
	msg->l2h[2] = LAPDm_LEN(l3_len); /* EL is set here too */
	if (lctx->more)
		msg->l2h[2] |= LAPDm_MORE;

	/* add ACCH header with last indicated tx-power and TA */
	if ((mctx->link_id & 0x40)) {
		struct lapdm_entity *le = mdl->entity;

		msg->l2h = msgb_push(msg, 2);
		msg->l2h[0] = le->tx_power;
		msg->l2h[1] = le->ta;
	}

	return tx_ph_data_enqueue(mctx->dl, msg, mctx->chan_nr, mctx->link_id,
			23);
}

static int update_pending_frames(struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg;
	int rc = -1;

	llist_for_each_entry(msg, &dl->tx_queue, list) {
		if (LAPDm_CTRL_is_I(msg->l2h[1])) {
			msg->l2h[1] = LAPDm_CTRL_I(dl->v_recv, LAPDm_CTRL_I_Ns(msg->l2h[1]),
					LAPDm_CTRL_PF_BIT(msg->l2h[1]));
			rc = 0;
		} else if (LAPDm_CTRL_is_S(msg->l2h[1])) {
			LOGP(DLLAPD, LOGL_ERROR, "Supervisory frame in queue, this shouldn't happen\n");
		}
	}

	return rc;
}

/* input into layer2 (from layer 1) */
static int l2_ph_data_ind(struct msgb *msg, struct lapdm_entity *le,
	uint8_t chan_nr, uint8_t link_id)
{
	uint8_t cbits = chan_nr >> 3;
	uint8_t sapi; /* we cannot take SAPI from link_id, as L1 has no clue */
	struct lapdm_msg_ctx mctx;
	struct lapd_msg_ctx lctx;
	int rc = 0;
	int n201;

	/* when we reach here, we have a msgb with l2h pointing to the raw
	 * 23byte mac block. The l1h has already been purged. */

	memset(&mctx, 0, sizeof(mctx));
	mctx.chan_nr = chan_nr;
	mctx.link_id = link_id;

	/* check for L1 chan_nr/link_id and determine LAPDm hdr format */
	if (cbits == 0x10 || cbits == 0x12) {
		/* Format Bbis is used on BCCH and CCCH(PCH, NCH and AGCH) */
		mctx.lapdm_fmt = LAPDm_FMT_Bbis;
		n201 = N201_Bbis;
		sapi = 0;
	} else {
		if (mctx.link_id & 0x40) {
			/* It was received from network on SACCH */

			/* If UI on SACCH sent by BTS, lapdm_fmt must be B4 */
			if (le->mode == LAPDM_MODE_MS
			 && LAPDm_CTRL_is_U(msg->l2h[3])
			 && LAPDm_CTRL_U_BITS(msg->l2h[3]) == 0) {
				mctx.lapdm_fmt = LAPDm_FMT_B4;
				n201 = N201_B4;
				LOGP(DLLAPD, LOGL_INFO, "fmt=B4\n");
			} else {
				mctx.lapdm_fmt = LAPDm_FMT_B;
				n201 = N201_AB_SACCH;
				LOGP(DLLAPD, LOGL_INFO, "fmt=B\n");
			}
			/* SACCH frames have a two-byte L1 header that
			 * OsmocomBB L1 doesn't strip */
			mctx.tx_power_ind = msg->l2h[0] & 0x1f;
			mctx.ta_ind = msg->l2h[1];
			msgb_pull(msg, 2);
			msg->l2h += 2;
			sapi = (msg->l2h[0] >> 2) & 7;
		} else {
			mctx.lapdm_fmt = LAPDm_FMT_B;
			LOGP(DLLAPD, LOGL_INFO, "fmt=B\n");
			n201 = N201_AB_SDCCH;
			sapi = (msg->l2h[0] >> 2) & 7;
		}
	}

	mctx.dl = lapdm_datalink_for_sapi(le, sapi);
	/* G.2.1 No action on frames containing an unallocated SAPI. */
	if (!mctx.dl) {
		LOGP(DLLAPD, LOGL_NOTICE, "Received frame for unsupported "
			"SAPI %d!\n", sapi);
		msgb_free(msg);
		return -EIO;
	}

	switch (mctx.lapdm_fmt) {
	case LAPDm_FMT_A:
	case LAPDm_FMT_B:
	case LAPDm_FMT_B4:
		lctx.dl = &mctx.dl->dl;
		/* obtain SAPI from address field */
		mctx.link_id |= LAPDm_ADDR_SAPI(msg->l2h[0]);
		/* G.2.3 EA bit set to "0" is not allowed in GSM */
		if (!LAPDm_ADDR_EA(msg->l2h[0])) {
			LOGP(DLLAPD, LOGL_NOTICE, "EA bit 0 is not allowed in "
				"GSM\n");
			msgb_free(msg);
			rsl_rll_error(RLL_CAUSE_FRM_UNIMPL, &mctx);
			return -EINVAL;
		}
		/* adress field */
		lctx.lpd = LAPDm_ADDR_LPD(msg->l2h[0]);
		lctx.sapi = LAPDm_ADDR_SAPI(msg->l2h[0]);
		lctx.cr = LAPDm_ADDR_CR(msg->l2h[0]);
		/* command field */
		if (LAPDm_CTRL_is_I(msg->l2h[1])) {
			lctx.format = LAPD_FORM_I;
			lctx.n_send = LAPDm_CTRL_I_Ns(msg->l2h[1]);
			lctx.n_recv = LAPDm_CTRL_Nr(msg->l2h[1]);
		} else if (LAPDm_CTRL_is_S(msg->l2h[1])) {
			lctx.format = LAPD_FORM_S;
			lctx.n_recv = LAPDm_CTRL_Nr(msg->l2h[1]);
			lctx.s_u = LAPDm_CTRL_S_BITS(msg->l2h[1]);
		} else if (LAPDm_CTRL_is_U(msg->l2h[1])) {
			lctx.format = LAPD_FORM_U;
			lctx.s_u = LAPDm_CTRL_U_BITS(msg->l2h[1]);
		} else
			lctx.format = LAPD_FORM_UKN;
		lctx.p_f = LAPDm_CTRL_PF_BIT(msg->l2h[1]);
		if (lctx.sapi != LAPDm_SAPI_NORMAL
		 && lctx.sapi != LAPDm_SAPI_SMS
		 && lctx.format == LAPD_FORM_U
		 && lctx.s_u == LAPDm_U_UI) {
			/* 5.3.3 UI frames with invalid SAPI values shall be
			 * discarded
			 */
			LOGP(DLLAPD, LOGL_INFO, "sapi=%u (discarding)\n",
				lctx.sapi);
			msgb_free(msg);
			return 0;
		}
		if (mctx.lapdm_fmt == LAPDm_FMT_B4) {
			lctx.n201 = n201;
			lctx.length = n201;
			lctx.more = 0;
			msg->l3h = msg->l2h + 2;
			msgb_pull_to_l3(msg);
		} else {
			/* length field */
			if (!(msg->l2h[2] & LAPDm_EL)) {
				/* G.4.1 If the EL bit is set to "0", an
				 * MDL-ERROR-INDICATION primitive with cause
				 * "frame not implemented" is sent to the
				 * mobile management entity. */
				LOGP(DLLAPD, LOGL_NOTICE, "we don't support "
					"multi-octet length\n");
				msgb_free(msg);
				rsl_rll_error(RLL_CAUSE_FRM_UNIMPL, &mctx);
				return -EINVAL;
			}
			lctx.n201 = n201;
			lctx.length = msg->l2h[2] >> 2;
			lctx.more = !!(msg->l2h[2] & LAPDm_MORE);
			msg->l3h = msg->l2h + 3;
			msgb_pull_to_l3(msg);
		}
		/* store context for messages from lapd */
		memcpy(&mctx.dl->mctx, &mctx, sizeof(mctx.dl->mctx));
		/* send to LAPD */
		rc = lapd_ph_data_ind(msg, &lctx);
		break;
	case LAPDm_FMT_Bter:
		/* FIXME */
		msgb_free(msg);
		break;
	case LAPDm_FMT_Bbis:
		/* directly pass up to layer3 */
		LOGP(DLLAPD, LOGL_INFO, "fmt=Bbis UI\n");
		msg->l3h = msg->l2h;
		msgb_pull_to_l3(msg);
		rc = send_rslms_rll_l3(RSL_MT_UNIT_DATA_IND, &mctx, msg);
		break;
	default:
		msgb_free(msg);
	}

	return rc;
}

/* input into layer2 (from layer 1) */
static int l2_ph_rach_ind(struct lapdm_entity *le, uint8_t ra, uint32_t fn, uint8_t acc_delay)
{
	struct abis_rsl_cchan_hdr *ch;
	struct gsm48_req_ref req_ref;
	struct gsm_time gt;
	struct msgb *msg = msgb_alloc_headroom(512, 64, "RSL CHAN RQD");

	if (!msg)
		return -ENOMEM;

	msg->l2h = msgb_push(msg, sizeof(*ch));
	ch = (struct abis_rsl_cchan_hdr *)msg->l2h;
	rsl_init_cchan_hdr(ch, RSL_MT_CHAN_RQD);
	ch->chan_nr = RSL_CHAN_RACH;

	/* generate a RSL CHANNEL REQUIRED message */
	gsm_fn2gsmtime(&gt, fn);
	req_ref.ra = ra;
	req_ref.t1 = gt.t1; /* FIXME: modulo? */
	req_ref.t2 = gt.t2;
	req_ref.t3_low = gt.t3 & 7;
	req_ref.t3_high = gt.t3 >> 3;

	msgb_tv_fixed_put(msg, RSL_IE_REQ_REFERENCE, 3, (uint8_t *) &req_ref);
	msgb_tv_put(msg, RSL_IE_ACCESS_DELAY, acc_delay);

	return rslms_sendmsg(msg, le);
}

static int l2_ph_chan_conf(struct msgb *msg, struct lapdm_entity *le, uint32_t frame_nr);

/*! \brief Receive a PH-SAP primitive from L1 */
int lapdm_phsap_up(struct osmo_prim_hdr *oph, struct lapdm_entity *le)
{
	struct osmo_phsap_prim *pp = (struct osmo_phsap_prim *) oph;
	int rc = 0;

	if (oph->sap != SAP_GSM_PH) {
		LOGP(DLLAPD, LOGL_ERROR, "primitive for unknown SAP %u\n",
			oph->sap);
		rc = -ENODEV;
		goto free;
	}

	switch (oph->primitive) {
	case PRIM_PH_DATA:
		if (oph->operation != PRIM_OP_INDICATION) {
			LOGP(DLLAPD, LOGL_ERROR, "PH_DATA is not INDICATION %u\n",
				oph->operation);
			rc = -ENODEV;
			goto free;
		}
		rc = l2_ph_data_ind(oph->msg, le, pp->u.data.chan_nr,
				    pp->u.data.link_id);
		break;
	case PRIM_PH_RTS:
		if (oph->operation != PRIM_OP_INDICATION) {
			LOGP(DLLAPD, LOGL_ERROR, "PH_RTS is not INDICATION %u\n",
				oph->operation);
			rc = -ENODEV;
			goto free;
		}
		rc = l2_ph_data_conf(oph->msg, le);
		break;
	case PRIM_PH_RACH:
		switch (oph->operation) {
		case PRIM_OP_INDICATION:
			rc = l2_ph_rach_ind(le, pp->u.rach_ind.ra, pp->u.rach_ind.fn,
					    pp->u.rach_ind.acc_delay);
			break;
		case PRIM_OP_CONFIRM:
			rc = l2_ph_chan_conf(oph->msg, le, pp->u.rach_ind.fn);
			break;
		default:
			rc = -EIO;
			goto free;
		}
		break;
	default:
		LOGP(DLLAPD, LOGL_ERROR, "Unknown primitive %u\n",
			oph->primitive);
		rc = -EINVAL;
		goto free;
	}

	return rc;

free:
	msgb_free(oph->msg);
	return rc;
}


/* L3 -> L2 / RSLMS -> LAPDm */

/* Set LAPDm context for established connection */
static int set_lapdm_context(struct lapdm_datalink *dl, uint8_t chan_nr,
	uint8_t link_id, int n201, uint8_t sapi)
{
	memset(&dl->mctx, 0, sizeof(dl->mctx));
	dl->mctx.dl = dl;
	dl->mctx.chan_nr = chan_nr;
	dl->mctx.link_id = link_id;
	dl->dl.lctx.dl = &dl->dl;
	dl->dl.lctx.n201 = n201;
	dl->dl.lctx.sapi = sapi;

	return 0;
}

/* L3 requests establishment of data link */
static int rslms_rx_rll_est_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	uint8_t chan_nr = rllh->chan_nr;
	uint8_t link_id = rllh->link_id;
	uint8_t sapi = rllh->link_id & 7;
	struct tlv_parsed tv;
	uint8_t length;
	uint8_t n201 = (rllh->link_id & 0x40) ? N201_AB_SACCH : N201_AB_SDCCH;
	struct osmo_dlsap_prim dp;

	/* Set LAPDm context for established connection */
	set_lapdm_context(dl, chan_nr, link_id, n201, sapi);

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg) - sizeof(*rllh));
	if (TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);
		/* contention resolution establishment procedure */
		if (sapi != 0) {
			/* According to clause 6, the contention resolution
			 * procedure is only permitted with SAPI value 0 */
			LOGP(DLLAPD, LOGL_ERROR, "SAPI != 0 but contention"
				"resolution (discarding)\n");
			msgb_free(msg);
			return send_rll_simple(RSL_MT_REL_IND, &dl->mctx);
		}
		/* transmit a SABM command with the P bit set to "1". The SABM
		 * command shall contain the layer 3 message unit */
		length = TLVP_LEN(&tv, RSL_IE_L3_INFO);
	} else {
		/* normal establishment procedure */
		msg->l3h = msg->l2h + sizeof(*rllh);
		length = 0;
	}

	/* check if the layer3 message length exceeds N201 */
	if (length > n201) {
		LOGP(DLLAPD, LOGL_ERROR, "frame too large: %d > N201(%d) "
			"(discarding)\n", length, n201);
		msgb_free(msg);
		return send_rll_simple(RSL_MT_REL_IND, &dl->mctx);
	}

	/* Remove RLL header from msgb and set length to L3-info */
	msgb_pull_to_l3(msg);
	msgb_trim(msg, length);

	/* prepare prim */
	osmo_prim_init(&dp.oph, 0, PRIM_DL_EST, PRIM_OP_REQUEST, msg);

	/* send to L2 */
	return lapd_recv_dlsap(&dp, &dl->dl.lctx);
}

/* L3 requests transfer of unnumbered information */
static int rslms_rx_rll_udata_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct lapdm_entity *le = dl->entity;
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	uint8_t chan_nr = rllh->chan_nr;
	uint8_t link_id = rllh->link_id;
	int ui_bts = (le->mode == LAPDM_MODE_BTS && (link_id & 0x40));
	uint8_t sapi = link_id & 7;
	struct tlv_parsed tv;
	int length;

	/* check if the layer3 message length exceeds N201 */

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));

	if (TLVP_PRESENT(&tv, RSL_IE_TIMING_ADVANCE)) {
		le->ta = *TLVP_VAL(&tv, RSL_IE_TIMING_ADVANCE);
	}
	if (TLVP_PRESENT(&tv, RSL_IE_MS_POWER)) {
		le->tx_power = *TLVP_VAL(&tv, RSL_IE_MS_POWER);
	}
	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		LOGP(DLLAPD, LOGL_ERROR, "unit data request without message "
			"error\n");
		msgb_free(msg);
		return -EINVAL;
	}
	msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);
	length = TLVP_LEN(&tv, RSL_IE_L3_INFO);
	/* check if the layer3 message length exceeds N201 */
	if (length + ((link_id & 0x40) ? 4 : 2) + !ui_bts > 23) {
		LOGP(DLLAPD, LOGL_ERROR, "frame too large: %d > N201(%d) "
			"(discarding)\n", length,
			((link_id & 0x40) ? 18 : 20) + ui_bts);
		msgb_free(msg);
		return -EIO;
	}

	LOGP(DLLAPD, LOGL_INFO, "sending unit data (tx_power=%d, ta=%d)\n",
		le->tx_power, le->ta);

	/* Remove RLL header from msgb and set length to L3-info */
	msgb_pull_to_l3(msg);
	msgb_trim(msg, length);

	/* Push L1 + LAPDm header on msgb */
	msg->l2h = msgb_push(msg, 2 + !ui_bts);
	msg->l2h[0] = LAPDm_ADDR(LAPDm_LPD_NORMAL, sapi, dl->dl.cr.loc2rem.cmd);
	msg->l2h[1] = LAPDm_CTRL_U(LAPDm_U_UI, 0);
	if (!ui_bts)
		msg->l2h[2] = LAPDm_LEN(length);
	if (link_id & 0x40) {
		msg->l2h = msgb_push(msg, 2);
		msg->l2h[0] = le->tx_power;
		msg->l2h[1] = le->ta;
	}

	/* Tramsmit */
	return tx_ph_data_enqueue(dl, msg, chan_nr, link_id, 23);
}

/* L3 requests transfer of acknowledged information */
static int rslms_rx_rll_data_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct tlv_parsed tv;
	int length;
	struct osmo_dlsap_prim dp;

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));
	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		LOGP(DLLAPD, LOGL_ERROR, "data request without message "
			"error\n");
		msgb_free(msg);
		return -EINVAL;
	}
	msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);
	length = TLVP_LEN(&tv, RSL_IE_L3_INFO);

	/* Remove RLL header from msgb and set length to L3-info */
	msgb_pull_to_l3(msg);
	msgb_trim(msg, length);

	/* prepare prim */
	osmo_prim_init(&dp.oph, 0, PRIM_DL_DATA, PRIM_OP_REQUEST, msg);

	/* send to L2 */
	return lapd_recv_dlsap(&dp, &dl->dl.lctx);
}

/* L3 requests suspension of data link */
static int rslms_rx_rll_susp_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	uint8_t sapi = rllh->link_id & 7;
	struct osmo_dlsap_prim dp;

	if (sapi != 0) {
		LOGP(DLLAPD, LOGL_ERROR, "SAPI != 0 while suspending\n");
		msgb_free(msg);
		return -EINVAL;
	}

	/* prepare prim */
	osmo_prim_init(&dp.oph, 0, PRIM_DL_SUSP, PRIM_OP_REQUEST, msg);

	/* send to L2 */
	return lapd_recv_dlsap(&dp, &dl->dl.lctx);
}

/* L3 requests resume of data link */
static int rslms_rx_rll_res_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int msg_type = rllh->c.msg_type;
	uint8_t chan_nr = rllh->chan_nr;
	uint8_t link_id = rllh->link_id;
	uint8_t sapi = rllh->link_id & 7;
	struct tlv_parsed tv;
	uint8_t length;
	uint8_t n201 = (rllh->link_id & 0x40) ? N201_AB_SACCH : N201_AB_SDCCH;
	struct osmo_dlsap_prim dp;

	/* Set LAPDm context for established connection */
	set_lapdm_context(dl, chan_nr, link_id, n201, sapi);

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));
	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		LOGP(DLLAPD, LOGL_ERROR, "resume without message error\n");
		msgb_free(msg);
		return send_rll_simple(RSL_MT_REL_IND, &dl->mctx);
	}
	msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);
	length = TLVP_LEN(&tv, RSL_IE_L3_INFO);

	/* Remove RLL header from msgb and set length to L3-info */
	msgb_pull_to_l3(msg);
	msgb_trim(msg, length);

	/* prepare prim */
	osmo_prim_init(&dp.oph, 0, (msg_type == RSL_MT_RES_REQ) ? PRIM_DL_RES
		: PRIM_DL_RECON, PRIM_OP_REQUEST, msg);

	/* send to L2 */
	return lapd_recv_dlsap(&dp, &dl->dl.lctx);
}

/* L3 requests release of data link */
static int rslms_rx_rll_rel_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	uint8_t mode = 0;
	struct osmo_dlsap_prim dp;

	/* get release mode */
	if (rllh->data[0] == RSL_IE_RELEASE_MODE)
		mode = rllh->data[1] & 1;

	/* Pull rllh */
	msgb_pull_to_l3(msg);

	/* 04.06 3.8.3: No information field is permitted with the DISC
	 * command. */
	msgb_trim(msg, 0);

	/* prepare prim */
	osmo_prim_init(&dp.oph, 0, PRIM_DL_REL, PRIM_OP_REQUEST, msg);
	dp.u.rel_req.mode = mode;

	/* send to L2 */
	return lapd_recv_dlsap(&dp, &dl->dl.lctx);
}

/* L3 requests channel in idle state */
static int rslms_rx_chan_rqd(struct lapdm_channel *lc, struct msgb *msg)
{
	struct abis_rsl_cchan_hdr *cch = msgb_l2(msg);
	void *l1ctx = lc->lapdm_dcch.l1_ctx;
	struct osmo_phsap_prim pp;

	osmo_prim_init(&pp.oph, SAP_GSM_PH, PRIM_PH_RACH,
			PRIM_OP_REQUEST, NULL);

	if (msgb_l2len(msg) < sizeof(*cch) + 4 + 2 + 2) {
		LOGP(DLLAPD, LOGL_ERROR, "Message too short for CHAN RQD!\n");
		return -EINVAL;
	}
	if (cch->data[0] != RSL_IE_REQ_REFERENCE) {
		LOGP(DLLAPD, LOGL_ERROR, "Missing REQ REFERENCE IE\n");
		return -EINVAL;
	}
	pp.u.rach_req.ra = cch->data[1];
	pp.u.rach_req.offset = ((cch->data[2] & 0x7f) << 8) | cch->data[3];
	pp.u.rach_req.is_combined_ccch = cch->data[2] >> 7;

	if (cch->data[4] != RSL_IE_ACCESS_DELAY) {
		LOGP(DLLAPD, LOGL_ERROR, "Missing ACCESS_DELAY IE\n");
		return -EINVAL;
	}
	/* TA = 0 - delay */
	pp.u.rach_req.ta = 0 - cch->data[5];

	if (cch->data[6] != RSL_IE_MS_POWER) {
		LOGP(DLLAPD, LOGL_ERROR, "Missing MS POWER IE\n");
		return -EINVAL;
	}
	pp.u.rach_req.tx_power = cch->data[7];

	msgb_free(msg);

	return lc->lapdm_dcch.l1_prim_cb(&pp.oph, l1ctx);
}

/* L1 confirms channel request */
static int l2_ph_chan_conf(struct msgb *msg, struct lapdm_entity *le, uint32_t frame_nr)
{
	struct abis_rsl_cchan_hdr *ch;
	struct gsm_time tm;
	struct gsm48_req_ref *ref;

	gsm_fn2gsmtime(&tm, frame_nr);

	msgb_pull_to_l3(msg);
	msg->l2h = msgb_push(msg, sizeof(*ch) + sizeof(*ref));
	ch = (struct abis_rsl_cchan_hdr *)msg->l2h;
	rsl_init_cchan_hdr(ch, RSL_MT_CHAN_CONF);
	ch->chan_nr = RSL_CHAN_RACH;
	ch->data[0] = RSL_IE_REQ_REFERENCE;
	ref = (struct gsm48_req_ref *) (ch->data + 1);
	ref->t1 = tm.t1;
	ref->t2 = tm.t2;
	ref->t3_low = tm.t3 & 0x7;
	ref->t3_high = tm.t3 >> 3;
	
	return rslms_sendmsg(msg, le);
}

/* incoming RSLms RLL message from L3 */
static int rslms_rx_rll(struct msgb *msg, struct lapdm_channel *lc)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int msg_type = rllh->c.msg_type;
	uint8_t sapi = rllh->link_id & 7;
	struct lapdm_entity *le;
	struct lapdm_datalink *dl;
	int rc = 0;

	if (msgb_l2len(msg) < sizeof(*rllh)) {
		LOGP(DLLAPD, LOGL_ERROR, "Message too short for RLL hdr!\n");
		msgb_free(msg);
		return -EINVAL;
	}

	if (rllh->link_id & 0x40)
		le = &lc->lapdm_acch;
	else
		le = &lc->lapdm_dcch;

	/* 4.1.1.5 / 4.1.1.6 / 4.1.1.7 all only exist on MS side, not
	 * BTS side */
	if (le->mode == LAPDM_MODE_BTS) {
		switch (msg_type) {
		case RSL_MT_SUSP_REQ:
		case RSL_MT_RES_REQ:
		case RSL_MT_RECON_REQ:
			LOGP(DLLAPD, LOGL_NOTICE, "(%p) RLL Message '%s' unsupported in BTS side LAPDm\n",
				lc->name, rsl_msg_name(msg_type));
			msgb_free(msg);
			return -EINVAL;
			break;
		default:
			break;
		}
	}

	/* G.2.1 No action shall be taken on frames containing an unallocated
	 * SAPI.
	 */
	dl = lapdm_datalink_for_sapi(le, sapi);
	if (!dl) {
		LOGP(DLLAPD, LOGL_ERROR, "No instance for SAPI %d!\n", sapi);
		msgb_free(msg);
		return -EINVAL;
	}

	switch (msg_type) {
	case RSL_MT_DATA_REQ:
	case RSL_MT_SUSP_REQ:
	case RSL_MT_REL_REQ:
		/* This is triggered in abnormal error conditions where
		 * set_lapdm_context() was not called for the channel earlier. */
		if (!dl->dl.lctx.dl) {
			LOGP(DLLAPD, LOGL_NOTICE, "(%p) RLL Message '%s' received without LAPDm context. (sapi %d)\n",
					lc->name, rsl_msg_name(msg_type), sapi);
			msgb_free(msg);
			return -EINVAL;
		}
		break;
	default:
		LOGP(DLLAPD, LOGL_INFO, "(%p) RLL Message '%s' received. (sapi %d)\n",
			lc->name, rsl_msg_name(msg_type), sapi);
	}

	switch (msg_type) {
	case RSL_MT_UNIT_DATA_REQ:
		rc = rslms_rx_rll_udata_req(msg, dl);
		break;
	case RSL_MT_EST_REQ:
		rc = rslms_rx_rll_est_req(msg, dl);
		break;
	case RSL_MT_DATA_REQ:
		rc = rslms_rx_rll_data_req(msg, dl);
		break;
	case RSL_MT_SUSP_REQ:
		rc = rslms_rx_rll_susp_req(msg, dl);
		break;
	case RSL_MT_RES_REQ:
		rc = rslms_rx_rll_res_req(msg, dl);
		break;
	case RSL_MT_RECON_REQ:
		rc = rslms_rx_rll_res_req(msg, dl);
		break;
	case RSL_MT_REL_REQ:
		rc = rslms_rx_rll_rel_req(msg, dl);
		break;
	default:
		LOGP(DLLAPD, LOGL_NOTICE, "Message unsupported.\n");
		msgb_free(msg);
		rc = -EINVAL;
	}

	return rc;
}

/* incoming RSLms COMMON CHANNEL message from L3 */
static int rslms_rx_com_chan(struct msgb *msg, struct lapdm_channel *lc)
{
	struct abis_rsl_cchan_hdr *cch = msgb_l2(msg);
	int msg_type = cch->c.msg_type;
	int rc = 0;

	if (msgb_l2len(msg) < sizeof(*cch)) {
		LOGP(DLLAPD, LOGL_ERROR, "Message too short for COM CHAN hdr!\n");
		return -EINVAL;
	}

	switch (msg_type) {
	case RSL_MT_CHAN_RQD:
		/* create and send RACH request */
		rc = rslms_rx_chan_rqd(lc, msg);
		break;
	default:
		LOGP(DLLAPD, LOGL_NOTICE, "Unknown COMMON CHANNEL msg %d!\n",
			msg_type);
		msgb_free(msg);
		return 0;
	}

	return rc;
}

/*! \brief Receive a RSLms \ref msgb from Layer 3 */
int lapdm_rslms_recvmsg(struct msgb *msg, struct lapdm_channel *lc)
{
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	if (msgb_l2len(msg) < sizeof(*rslh)) {
		LOGP(DLLAPD, LOGL_ERROR, "Message too short RSL hdr!\n");
		return -EINVAL;
	}

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = rslms_rx_rll(msg, lc);
		break;
	case ABIS_RSL_MDISC_COM_CHAN:
		rc = rslms_rx_com_chan(msg, lc);
		break;
	default:
		LOGP(DLLAPD, LOGL_ERROR, "unknown RSLms message "
			"discriminator 0x%02x", rslh->msg_discr);
		msgb_free(msg);
		return -EINVAL;
	}

	return rc;
}

/*! \brief Set the \ref lapdm_mode of a LAPDm entity */
int lapdm_entity_set_mode(struct lapdm_entity *le, enum lapdm_mode mode)
{
	int i;
	enum lapd_mode lm;

	switch (mode) {
	case LAPDM_MODE_MS:
		lm = LAPD_MODE_USER;
		break;
	case LAPDM_MODE_BTS:
		lm = LAPD_MODE_NETWORK;
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(le->datalink); i++) {
		lapd_set_mode(&le->datalink[i].dl, lm);
	}

	le->mode = mode;

	return 0;
}

/*! \brief Set the \ref lapdm_mode of a LAPDm channel*/
int lapdm_channel_set_mode(struct lapdm_channel *lc, enum lapdm_mode mode)
{
	int rc;

	rc = lapdm_entity_set_mode(&lc->lapdm_dcch, mode);
	if (rc < 0)
		return rc;

	return lapdm_entity_set_mode(&lc->lapdm_acch, mode);
}

/*! \brief Set the L1 callback and context of a LAPDm channel */
void lapdm_channel_set_l1(struct lapdm_channel *lc, osmo_prim_cb cb, void *ctx)
{
	lc->lapdm_dcch.l1_prim_cb = cb;
	lc->lapdm_acch.l1_prim_cb = cb;
	lc->lapdm_dcch.l1_ctx = ctx;
	lc->lapdm_acch.l1_ctx = ctx;
}

/*! \brief Set the L3 callback and context of a LAPDm channel */
void lapdm_channel_set_l3(struct lapdm_channel *lc, lapdm_cb_t cb, void *ctx)
{
	lc->lapdm_dcch.l3_cb = cb;
	lc->lapdm_acch.l3_cb = cb;
	lc->lapdm_dcch.l3_ctx = ctx;
	lc->lapdm_acch.l3_ctx = ctx;
}

/*! \brief Reset an entire LAPDm entity and all its datalinks */
void lapdm_entity_reset(struct lapdm_entity *le)
{
	struct lapdm_datalink *dl;
	int i;

	for (i = 0; i < ARRAY_SIZE(le->datalink); i++) {
		dl = &le->datalink[i];
		lapd_dl_reset(&dl->dl);
	}
}

/*! \brief Reset a LAPDm channel with all its entities */
void lapdm_channel_reset(struct lapdm_channel *lc)
{
	lapdm_entity_reset(&lc->lapdm_dcch);
	lapdm_entity_reset(&lc->lapdm_acch);
}

/*! \brief Set the flags of a LAPDm entity */
void lapdm_entity_set_flags(struct lapdm_entity *le, unsigned int flags)
{
	le->flags = flags;
}

/*! \brief Set the flags of all LAPDm entities in a LAPDm channel */
void lapdm_channel_set_flags(struct lapdm_channel *lc, unsigned int flags)
{
	lapdm_entity_set_flags(&lc->lapdm_dcch, flags);
	lapdm_entity_set_flags(&lc->lapdm_acch, flags);
}

/*! @} */
