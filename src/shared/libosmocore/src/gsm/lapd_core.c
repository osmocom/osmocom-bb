/* LAPD core implementation */

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

/*! \addtogroup lapd
 *  @{
 */

/*! \file lapd.c */

/*!
 * Notes on Buffering: rcv_buffer, tx_queue, tx_hist, send_buffer, send_queue
 *
 * RX data is stored in the rcv_buffer (pointer). If the message is complete, it
 * is removed from rcv_buffer pointer and forwarded to L3. If the RX data is
 * received while there is an incomplete rcv_buffer, it is appended to it.
 *
 * TX data is stored in the send_queue first. When transmitting a frame,
 * the first message in the send_queue is moved to the send_buffer. There it
 * resides until all fragments are acknowledged. Fragments to be sent by I
 * frames are stored in the tx_hist buffer for resend, if required. Also the
 * current fragment is copied into the tx_queue. There it resides until it is
 * forwarded to layer 1.
 *
 * In case we have SAPI 0, we only have a window size of 1, so the unack-
 * nowledged message resides always in the send_buffer. In case of a suspend,
 * it can be written back to the first position of the send_queue.
 *
 * The layer 1 normally sends a PH-READY-TO-SEND. But because we use
 * asynchronous transfer between layer 1 and layer 2 (serial link), we must
 * send a frame before layer 1 reaches the right timeslot to send it. So we
 * move the tx_queue to layer 1 when there is not already a pending frame, and
 * wait until acknowledge after the frame has been sent. If we receive an
 * acknowledge, we can send the next frame from the buffer, if any.
 *
 * The moving of tx_queue to layer 1 may also trigger T200, if desired. Also it
 * will trigger next I frame, if possible.
 *
 * T203 is optional. It will be stated when entering MF EST state. It will also
 * be started when I or S frame is received in that state . It will be
 * restarted in the lapd_acknowledge() function, in case outstanding frames
 * will not trigger T200. It will be stoped, when T200 is started in MF EST
 * state. It will also be stoped when leaving MF EST state.
 *
 */

/* Enable this to test content resolution on network side:
 * - The first SABM is received, UA is dropped.
 * - The phone repeats SABM, but it's content is wrong, so it is ignored
 * - The phone repeats SABM again, content is right, so UA is sent.
 */
//#define TEST_CONTENT_RESOLUTION_NETWORK

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include <osmocom/core/logging.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/talloc.h>
#include <osmocom/gsm/lapd_core.h>

/* TS 04.06 Table 4 / Section 3.8.1 */
#define LAPD_U_SABM	0x7
#define LAPD_U_SABME	0xf
#define LAPD_U_DM	0x3
#define LAPD_U_UI	0x0
#define LAPD_U_DISC	0x8
#define LAPD_U_UA	0xC
#define LAPD_U_FRMR	0x11

#define LAPD_S_RR	0x0
#define LAPD_S_RNR	0x1
#define LAPD_S_REJ	0x2

#define CR_USER2NET_CMD		0
#define CR_USER2NET_RESP	1
#define CR_NET2USER_CMD		1
#define CR_NET2USER_RESP	0

#define LAPD_HEADROOM	56

#define SBIT(a) (1 << a)
#define ALL_STATES 0xffffffff

static void lapd_t200_cb(void *data);
static void lapd_t203_cb(void *data);
static int lapd_send_i(struct lapd_msg_ctx *lctx, int line);
static int lapd_est_req(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx);

/* UTILITY FUNCTIONS */

struct msgb *lapd_msgb_alloc(int length, const char *name)
{
	/* adding space for padding, FIXME: add as an option */
	if (length < 21)
		length = 21;
	return msgb_alloc_headroom(length + LAPD_HEADROOM, LAPD_HEADROOM, name);
}

static inline uint8_t do_mod(uint8_t x, uint8_t m)
{
	return x & (m - 1);
}

static inline uint8_t inc_mod(uint8_t x, uint8_t m)
{
	return (x + 1) & (m - 1);
}

static inline uint8_t add_mod(uint8_t x, uint8_t y, uint8_t m)
{
	return (x + y) & (m - 1);
}

static inline uint8_t sub_mod(uint8_t x, uint8_t y, uint8_t m)
{
	return (x - y) & (m - 1); /* handle negative results correctly */
}

static void lapd_dl_flush_send(struct lapd_datalink *dl)
{
	struct msgb *msg;

	/* Flush send-queue */
	while ((msg = msgb_dequeue(&dl->send_queue)))
		msgb_free(msg);

	/* Clear send-buffer */
	if (dl->send_buffer) {
		msgb_free(dl->send_buffer);
		dl->send_buffer = NULL;
	}
}

static void lapd_dl_flush_hist(struct lapd_datalink *dl)
{
	unsigned int i;

	for (i = 0; i < dl->range_hist; i++) {
		if (dl->tx_hist[i].msg) {
			msgb_free(dl->tx_hist[i].msg);
			dl->tx_hist[i].msg = NULL;
		}
	}
}

static void lapd_dl_flush_tx(struct lapd_datalink *dl)
{
	struct msgb *msg;

	while ((msg = msgb_dequeue(&dl->tx_queue)))
		msgb_free(msg);
	lapd_dl_flush_hist(dl);
}

/* Figure B.2/Q.921 */
const char *lapd_state_names[] = {
	"LAPD_STATE_NULL",
	"LAPD_STATE_TEI_UNASS",
	"LAPD_STATE_ASS_TEI_WAIT",
	"LAPD_STATE_EST_TEI_WAIT",
	"LAPD_STATE_IDLE",
	"LAPD_STATE_SABM_SENT",
	"LAPD_STATE_DISC_SENT",
	"LAPD_STATE_MF_EST",
	"LAPD_STATE_TIMER_RECOV",

};

static void lapd_start_t200(struct lapd_datalink *dl)
{
	if (osmo_timer_pending(&dl->t200))
		return;
	LOGP(DLLAPD, LOGL_INFO, "start T200\n");
	osmo_timer_schedule(&dl->t200, dl->t200_sec, dl->t200_usec);
}

static void lapd_start_t203(struct lapd_datalink *dl)
{
	if (osmo_timer_pending(&dl->t203))
		return;
	LOGP(DLLAPD, LOGL_INFO, "start T203\n");
	osmo_timer_schedule(&dl->t203, dl->t203_sec, dl->t203_usec);
}

static void lapd_stop_t200(struct lapd_datalink *dl)
{
	if (!osmo_timer_pending(&dl->t200))
		return;
	LOGP(DLLAPD, LOGL_INFO, "stop T200\n");
	osmo_timer_del(&dl->t200);
}

static void lapd_stop_t203(struct lapd_datalink *dl)
{
	if (!osmo_timer_pending(&dl->t203))
		return;
	LOGP(DLLAPD, LOGL_INFO, "stop T203\n");
	osmo_timer_del(&dl->t203);
}

static void lapd_dl_newstate(struct lapd_datalink *dl, uint32_t state)
{
	LOGP(DLLAPD, LOGL_INFO, "new state %s -> %s\n",
		lapd_state_names[dl->state], lapd_state_names[state]);

	if (state != LAPD_STATE_MF_EST && dl->state == LAPD_STATE_MF_EST) {
		/* stop T203 on leaving MF EST state, if running */
		lapd_stop_t203(dl);
		/* remove content res. (network side) on leaving MF EST state */
		if (dl->cont_res) {
			msgb_free(dl->cont_res);
			dl->cont_res = NULL;
		}
	}

	/* start T203 on entering MF EST state, if enabled */
	if ((dl->t203_sec || dl->t203_usec)
	 && state == LAPD_STATE_MF_EST && dl->state != LAPD_STATE_MF_EST)
		lapd_start_t203(dl);

	dl->state = state;
}

static void *tall_lapd_ctx = NULL;

/* init datalink instance and allocate history */
void lapd_dl_init(struct lapd_datalink *dl, uint8_t k, uint8_t v_range,
	int maxf)
{
	int m;

	memset(dl, 0, sizeof(*dl));
	INIT_LLIST_HEAD(&dl->send_queue);
	INIT_LLIST_HEAD(&dl->tx_queue);
	dl->reestablish = 1;
	dl->n200_est_rel = 3;
	dl->n200 = 3;
	dl->t200_sec = 1;
	dl->t200_usec = 0;
	dl->t200.data = dl;
	dl->t200.cb = &lapd_t200_cb;
	dl->t203_sec = 10;
	dl->t203_usec = 0;
	dl->t203.data = dl;
	dl->t203.cb = &lapd_t203_cb;
	dl->maxf = maxf;
	if (k > v_range - 1)
		k = v_range - 1;
	dl->k = k;
	dl->v_range = v_range;

	/* Calculate modulo for history array:
	 * - The history range must be at least k+1.
	 * - The history range must be 2^x, where x is as low as possible.
	 */
	k++;
	for (m = 0x80; m; m >>= 1) {
		if ((m & k)) {
			if (k > m)
				m <<= 1;
			dl->range_hist = m;
			break;
		}
	}

	LOGP(DLLAPD, LOGL_INFO, "Init DL layer: sequence range = %d, k = %d, "
		"history range = %d\n", dl->v_range, dl->k, dl->range_hist);

	lapd_dl_newstate(dl, LAPD_STATE_IDLE);

	if (!tall_lapd_ctx)
		tall_lapd_ctx = talloc_named_const(NULL, 1, "lapd context");
	dl->tx_hist = (struct lapd_history *) talloc_zero_array(tall_lapd_ctx,
					struct log_info, dl->range_hist);
}

/* reset to IDLE state */
void lapd_dl_reset(struct lapd_datalink *dl)
{
	if (dl->state == LAPD_STATE_IDLE)
		return;
	LOGP(DLLAPD, LOGL_INFO, "Resetting LAPDm instance\n");
	/* enter idle state (and remove eventual cont_res) */
	lapd_dl_newstate(dl, LAPD_STATE_IDLE);
	/* flush buffer */
	lapd_dl_flush_tx(dl);
	lapd_dl_flush_send(dl);
	/* Discard partly received L3 message */
	if (dl->rcv_buffer) {
		msgb_free(dl->rcv_buffer);
		dl->rcv_buffer = NULL;
	}
	/* stop Timers */
	lapd_stop_t200(dl);
	lapd_stop_t203(dl);
}

/* reset and de-allocate history buffer */
void lapd_dl_exit(struct lapd_datalink *dl)
{
	/* free all ressources except history buffer */
	lapd_dl_reset(dl);
	/* free history buffer list */
	talloc_free(dl->tx_hist);
}

/*! \brief Set the \ref lapdm_mode of a LAPDm entity */
int lapd_set_mode(struct lapd_datalink *dl, enum lapd_mode mode)
{
	switch (mode) {
	case LAPD_MODE_USER:
		dl->cr.loc2rem.cmd = CR_USER2NET_CMD;
		dl->cr.loc2rem.resp = CR_USER2NET_RESP;
		dl->cr.rem2loc.cmd = CR_NET2USER_CMD;
		dl->cr.rem2loc.resp = CR_NET2USER_RESP;
		break;
	case LAPD_MODE_NETWORK:
		dl->cr.loc2rem.cmd = CR_NET2USER_CMD;
		dl->cr.loc2rem.resp = CR_NET2USER_RESP;
		dl->cr.rem2loc.cmd = CR_USER2NET_CMD;
		dl->cr.rem2loc.resp = CR_USER2NET_RESP;
		break;
	default:
		return -EINVAL;
	}
	dl->mode = mode;

	return 0;
}

/* send DL message with optional msgb */
static int send_dl_l3(uint8_t prim, uint8_t op, struct lapd_msg_ctx *lctx,
	struct msgb *msg)
{
	struct lapd_datalink *dl = lctx->dl;
	struct osmo_dlsap_prim dp;

	osmo_prim_init(&dp.oph, 0, prim, op, msg);
	return dl->send_dlsap(&dp, lctx);
}

/* send simple DL message */
static inline int send_dl_simple(uint8_t prim, uint8_t op,
	struct lapd_msg_ctx *lctx)
{
	struct msgb *msg = lapd_msgb_alloc(0, "DUMMY");

	return send_dl_l3(prim, op, lctx, msg);
}

/* send MDL-ERROR INDICATION */
static int mdl_error(uint8_t cause, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct osmo_dlsap_prim dp;

	LOGP(DLLAPD, LOGL_NOTICE, "sending MDL-ERROR-IND cause %d\n",
		cause);
	osmo_prim_init(&dp.oph, 0, PRIM_MDL_ERROR, PRIM_OP_INDICATION, NULL);
	dp.u.error_ind.cause = cause;
	return dl->send_dlsap(&dp, lctx);
}

/* send UA response */
static int lapd_send_ua(struct lapd_msg_ctx *lctx, uint8_t len, uint8_t *data)
{
	struct msgb *msg = lapd_msgb_alloc(len, "LAPD UA");
	struct lapd_msg_ctx nctx;
	struct lapd_datalink *dl = lctx->dl;

	memcpy(&nctx, lctx, sizeof(nctx));
	msg->l3h = msgb_put(msg, len);
	if (len)
		memcpy(msg->l3h, data, len);
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.resp;
	nctx.format = LAPD_FORM_U;
	nctx.s_u = LAPD_U_UA;
	/* keep nctx.p_f */
	nctx.length = len;
	nctx.more = 0;

	return dl->send_ph_data_req(&nctx, msg);
}

/* send DM response */
static int lapd_send_dm(struct lapd_msg_ctx *lctx)
{
	struct msgb *msg = lapd_msgb_alloc(0, "LAPD DM");
	struct lapd_msg_ctx nctx;
	struct lapd_datalink *dl = lctx->dl;

	memcpy(&nctx, lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.resp;
	nctx.format = LAPD_FORM_U;
	nctx.s_u = LAPD_U_DM;
	/* keep nctx.p_f */
	nctx.length = 0;
	nctx.more = 0;

	return dl->send_ph_data_req(&nctx, msg);
}

/* send RR response / command */
static int lapd_send_rr(struct lapd_msg_ctx *lctx, uint8_t f_bit, uint8_t cmd)
{
	struct msgb *msg = lapd_msgb_alloc(0, "LAPD RR");
	struct lapd_msg_ctx nctx;
	struct lapd_datalink *dl = lctx->dl;

	memcpy(&nctx, lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = (cmd) ? dl->cr.loc2rem.cmd : dl->cr.loc2rem.resp;
	nctx.format = LAPD_FORM_S;
	nctx.s_u = LAPD_S_RR;
	nctx.p_f = f_bit;
	nctx.n_recv = dl->v_recv;
	nctx.length = 0;
	nctx.more = 0;

	return dl->send_ph_data_req(&nctx, msg);
}

/* send RNR response / command */
static int lapd_send_rnr(struct lapd_msg_ctx *lctx, uint8_t f_bit, uint8_t cmd)
{
	struct msgb *msg = lapd_msgb_alloc(0, "LAPD RNR");
	struct lapd_msg_ctx nctx;
	struct lapd_datalink *dl = lctx->dl;

	memcpy(&nctx, lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = (cmd) ? dl->cr.loc2rem.cmd : dl->cr.loc2rem.resp;
	nctx.format = LAPD_FORM_S;
	nctx.s_u = LAPD_S_RNR;
	nctx.p_f = f_bit;
	nctx.n_recv = dl->v_recv;
	nctx.length = 0;
	nctx.more = 0;

	return dl->send_ph_data_req(&nctx, msg);
}

/* send REJ response */
static int lapd_send_rej(struct lapd_msg_ctx *lctx, uint8_t f_bit)
{
	struct msgb *msg = lapd_msgb_alloc(0, "LAPD REJ");
	struct lapd_msg_ctx nctx;
	struct lapd_datalink *dl = lctx->dl;

	memcpy(&nctx, lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.resp;
	nctx.format = LAPD_FORM_S;
	nctx.s_u = LAPD_S_REJ;
	nctx.p_f = f_bit;
	nctx.n_recv = dl->v_recv;
	nctx.length = 0;
	nctx.more = 0;

	return dl->send_ph_data_req(&nctx, msg);
}

/* resend SABM or DISC message */
static int lapd_send_resend(struct lapd_datalink *dl)
{
	struct msgb *msg;
	uint8_t h = do_mod(dl->v_send, dl->range_hist);
	int length = dl->tx_hist[h].msg->len;
	struct lapd_msg_ctx nctx;

	/* assemble message */
	memcpy(&nctx, &dl->lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.cmd;
	nctx.format = LAPD_FORM_U;
	if (dl->state == LAPD_STATE_SABM_SENT)
		nctx.s_u = (dl->use_sabme) ? LAPD_U_SABME : LAPD_U_SABM;
	else
		nctx.s_u = LAPD_U_DISC;
	nctx.p_f = 1;
	nctx.length = length;
	nctx.more = 0;

	/* Resend SABM/DISC from tx_hist */
	msg = lapd_msgb_alloc(length, "LAPD resend");
	msg->l3h = msgb_put(msg, length);
	if (length)
		memcpy(msg->l3h, dl->tx_hist[h].msg->data, length);

	return dl->send_ph_data_req(&nctx, msg);
}

/* reestablish link */
static int lapd_reestablish(struct lapd_datalink *dl)
{
	struct osmo_dlsap_prim dp;
	struct msgb *msg;

	msg = lapd_msgb_alloc(0, "DUMMY");
	osmo_prim_init(&dp.oph, 0, PRIM_DL_EST, PRIM_OP_REQUEST, msg);
	
	return lapd_est_req(&dp, &dl->lctx);
}

/* Timer callback on T200 expiry */
static void lapd_t200_cb(void *data)
{
	struct lapd_datalink *dl = data;

	LOGP(DLLAPD, LOGL_INFO, "Timeout T200 (%p) state=%d\n", dl,
		(int) dl->state);

	switch (dl->state) {
	case LAPD_STATE_SABM_SENT:
		/* 5.4.1.3 */
		if (dl->retrans_ctr + 1 >= dl->n200_est_rel + 1) {
			/* send RELEASE INDICATION to L3 */
			send_dl_simple(PRIM_DL_REL, PRIM_OP_INDICATION,
				&dl->lctx);
			/* send MDL ERROR INIDCATION to L3 */
			mdl_error(MDL_CAUSE_T200_EXPIRED, &dl->lctx);
			/* flush tx and send buffers */
			lapd_dl_flush_tx(dl);
			lapd_dl_flush_send(dl);
			/* go back to idle state */
			lapd_dl_newstate(dl, LAPD_STATE_IDLE);
			/* NOTE: we must not change any other states or buffers
			 * and queues, since we may reconnect after handover
			 * failure. the buffered messages is replaced there */
			break;
		}
		/* retransmit SABM command */
		lapd_send_resend(dl);
		/* increment re-transmission counter */
		dl->retrans_ctr++;
		/* restart T200 (PH-READY-TO-SEND) */
		lapd_start_t200(dl);
		break;
	case LAPD_STATE_DISC_SENT:
		/* 5.4.4.3 */
		if (dl->retrans_ctr + 1 >= dl->n200_est_rel + 1) {
			/* send RELEASE INDICATION to L3 */
			send_dl_simple(PRIM_DL_REL, PRIM_OP_CONFIRM, &dl->lctx);
			/* send MDL ERROR INIDCATION to L3 */
			mdl_error(MDL_CAUSE_T200_EXPIRED, &dl->lctx);
			/* flush tx and send buffers */
			lapd_dl_flush_tx(dl);
			lapd_dl_flush_send(dl);
			/* go back to idle state */
			lapd_dl_newstate(dl, LAPD_STATE_IDLE);
			/* NOTE: we must not change any other states or buffers
			 * and queues, since we may reconnect after handover
			 * failure. the buffered messages is replaced there */
			break;
		}
		/* retransmit DISC command */
		lapd_send_resend(dl);
		/* increment re-transmission counter */
		dl->retrans_ctr++;
		/* restart T200 (PH-READY-TO-SEND) */
		lapd_start_t200(dl);
		break;
	case LAPD_STATE_MF_EST:
		/* 5.5.7 */
		dl->retrans_ctr = 0;
		lapd_dl_newstate(dl, LAPD_STATE_TIMER_RECOV);
		/* fall through */
	case LAPD_STATE_TIMER_RECOV:
		dl->retrans_ctr++;
		if (dl->retrans_ctr < dl->n200) {
			uint8_t vs = sub_mod(dl->v_send, 1, dl->v_range);
			uint8_t h = do_mod(vs, dl->range_hist);
			/* retransmit I frame (V_s-1) with P=1, if any */
			if (dl->tx_hist[h].msg) {
				struct msgb *msg;
				int length = dl->tx_hist[h].msg->len;
				struct lapd_msg_ctx nctx;

				LOGP(DLLAPD, LOGL_INFO, "retransmit last frame"
					" V(S)=%d\n", vs);
				/* Create I frame (segment) from tx_hist */
				memcpy(&nctx, &dl->lctx, sizeof(nctx));
				/* keep nctx.ldp */
				/* keep nctx.sapi */
				/* keep nctx.tei */
				nctx.cr = dl->cr.loc2rem.cmd;
				nctx.format = LAPD_FORM_I;
				nctx.p_f = 1;
				nctx.n_send = vs;
				nctx.n_recv = dl->v_recv;
				nctx.length = length;
				nctx.more = dl->tx_hist[h].more;
				msg = lapd_msgb_alloc(length, "LAPD I resend");
				msg->l3h = msgb_put(msg, length);
				memcpy(msg->l3h, dl->tx_hist[h].msg->data,
					length);
				dl->send_ph_data_req(&nctx, msg);
			} else {
			/* OR send appropriate supervision frame with P=1 */
				if (!dl->own_busy && !dl->seq_err_cond) {
					lapd_send_rr(&dl->lctx, 1, 1);
					/* NOTE: In case of sequence error
					 * condition, the REJ frame has been
					 * transmitted when entering the
					 * condition, so it has not be done
					 * here
				 	 */
				} else if (dl->own_busy) {
					lapd_send_rnr(&dl->lctx, 1, 1);
				} else {
					LOGP(DLLAPD, LOGL_INFO, "unhandled, "
						"pls. fix\n");
				}
			}
			/* restart T200 (PH-READY-TO-SEND) */
			lapd_start_t200(dl);
		} else {
			/* send MDL ERROR INIDCATION to L3 */
			mdl_error(MDL_CAUSE_T200_EXPIRED, &dl->lctx);
			/* reestablish */
			if (!dl->reestablish)
				break;
			LOGP(DLLAPD, LOGL_NOTICE, "N200 reached, performing "
				"reestablishment.\n");
			lapd_reestablish(dl);
		}
		break;
	default:
		LOGP(DLLAPD, LOGL_INFO, "T200 expired in unexpected "
			"dl->state %d\n", (int) dl->state);
	}
}

/* Timer callback on T203 expiry */
static void lapd_t203_cb(void *data)
{
	struct lapd_datalink *dl = data;

	LOGP(DLLAPD, LOGL_INFO, "Timeout T203 (%p) state=%d\n", dl,
		(int) dl->state);

	if (dl->state != LAPD_STATE_MF_EST) {
		LOGP(DLLAPD, LOGL_ERROR, "T203 fired outside MF EST state, "
			"please fix!\n");
		return;
	}

	/* set retransmission counter to 0 */
	dl->retrans_ctr = 0;
	/* enter timer recovery state */
	lapd_dl_newstate(dl, LAPD_STATE_TIMER_RECOV);
	/* transmit a supervisory command with P bit set to 1 as follows: */
	if (!dl->own_busy) {
		LOGP(DLLAPD, LOGL_INFO, "transmit an RR poll command\n");
		/* Send RR with P=1 */
		lapd_send_rr(&dl->lctx, 1, 1);
	} else {
		LOGP(DLLAPD, LOGL_INFO, "transmit an RNR poll command\n");
		/* Send RNR with P=1 */
		lapd_send_rnr(&dl->lctx, 1, 1);
	}
	/* start T200 */
	lapd_start_t200(dl);
}

/* 5.5.3.1: Common function to acknowlege frames up to the given N(R) value */
static void lapd_acknowledge(struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	uint8_t nr = lctx->n_recv;
	int s = 0, rej = 0, t200_reset = 0;
	int i, h;

	/* supervisory frame ? */
	if (lctx->format == LAPD_FORM_S)
		s = 1;
	/* REJ frame ? */
	if (s && lctx->s_u == LAPD_S_REJ)
	 	rej = 1;

	/* Flush all transmit buffers of acknowledged frames */
	for (i = dl->v_ack; i != nr; i = inc_mod(i, dl->v_range)) {
		h = do_mod(i, dl->range_hist);
		if (dl->tx_hist[h].msg) {
			msgb_free(dl->tx_hist[h].msg);
			dl->tx_hist[h].msg = NULL;
			LOGP(DLLAPD, LOGL_INFO, "ack frame %d\n", i);
		}
	}

	if (dl->state != LAPD_STATE_TIMER_RECOV) {
		/* When not in the timer recovery condition, the data
		 * link layer entity shall reset the timer T200 on
		 * receipt of a valid I frame with N(R) higher than V(A),
		 * or an REJ with an N(R) equal to V(A). */
		if ((!rej && nr != dl->v_ack)
		 || (rej && nr == dl->v_ack)) {
		 	t200_reset = 1;
			lapd_stop_t200(dl);
			/* 5.5.3.1 Note 1 + 2 imply timer recovery cond. */
		}
		/* 5.7.4: N(R) sequence error
		 * N(R) is called valid, if and only if
		 * (N(R)-V(A)) mod 8 <= (V(S)-V(A)) mod 8.
		 */
		if (sub_mod(nr, dl->v_ack, dl->v_range)
				> sub_mod(dl->v_send, dl->v_ack, dl->v_range)) {
			LOGP(DLLAPD, LOGL_NOTICE, "N(R) sequence error\n");
			mdl_error(MDL_CAUSE_SEQ_ERR, lctx);
		}
	}

	/* V(A) shall be set to the value of N(R) */
	dl->v_ack = nr;

	/* If T200 has been stopped by the receipt of an I, RR or RNR frame,
	 * and if there are outstanding I frames, restart T200 */
	if (t200_reset && !rej) {
		if (dl->tx_hist[sub_mod(dl->v_send, 1, dl->range_hist)].msg) {
			LOGP(DLLAPD, LOGL_INFO, "start T200, due to unacked I "
				"frame(s)\n");
			lapd_start_t200(dl);
		}
	}

	/* This also does a restart, when I or S frame is received */

	/* Stop T203, if running */
	lapd_stop_t203(dl);
	/* Start T203, if T200 is not running in MF EST state, if enabled */
	if (!osmo_timer_pending(&dl->t200)
	 && (dl->t203_sec || dl->t203_usec)
	 && (dl->state == LAPD_STATE_MF_EST)) {
		lapd_start_t203(dl);
	}
}

/* L1 -> L2 */

/* Receive a LAPD U (Unnumbered) message from L1 */
static int lapd_rx_u(struct msgb *msg, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	int length = lctx->length;
	int rc = 0;
	uint8_t prim, op;

	switch (lctx->s_u) {
	case LAPD_U_SABM:
	case LAPD_U_SABME:
		prim = PRIM_DL_EST;
		op = PRIM_OP_INDICATION;

		LOGP(DLLAPD, LOGL_INFO, "SABM(E) received in state %s\n",
			lapd_state_names[dl->state]);
		/* 5.7.1 */
		dl->seq_err_cond = 0;
		/* G.2.2 Wrong value of the C/R bit */
		if (lctx->cr == dl->cr.rem2loc.resp) {
			LOGP(DLLAPD, LOGL_NOTICE, "SABM response error\n");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
			return -EINVAL;
		}

		/* G.4.5 If SABM is received with L>N201 or with M bit
		 * set, AN MDL-ERROR-INDICATION is sent to MM.
		 */
		if (lctx->more || length > lctx->n201) {
			LOGP(DLLAPD, LOGL_NOTICE, "SABM too large error\n");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_UFRM_INC_PARAM, lctx);
			return -EIO;
		}

		switch (dl->state) {
		case LAPD_STATE_IDLE:
			break;
		case LAPD_STATE_MF_EST:
			LOGP(DLLAPD, LOGL_INFO, "SABM command, multiple "
				"frame established state\n");
			/* If link is lost on the remote side, we start over
			 * and send DL-ESTABLISH indication again. */
			if (dl->v_send != dl->v_recv) {
				LOGP(DLLAPD, LOGL_INFO, "Remote reestablish\n");
				mdl_error(MDL_CAUSE_SABM_MF, lctx);
				break;
			}
			/* Ignore SABM if content differs from first SABM. */
			if (dl->mode == LAPD_MODE_NETWORK && length
			 && dl->cont_res) {
#ifdef TEST_CONTENT_RESOLUTION_NETWORK
				dl->cont_res->data[0] ^= 0x01;
#endif
				if (memcmp(dl->cont_res, msg->data, length)) {
					LOGP(DLLAPD, LOGL_INFO, "Another SABM "
						"with diffrent content - "
						"ignoring!\n");
					msgb_free(msg);
					return 0;
				}
			}
			/* send UA again */
			lapd_send_ua(lctx, length, msg->l3h);
			msgb_free(msg);
			return 0;
		case LAPD_STATE_DISC_SENT:
			/* 5.4.6.2 send DM with F=P */
			lapd_send_dm(lctx);
			/* stop Timer T200 */
			lapd_stop_t200(dl);
			msgb_free(msg);
			return send_dl_simple(prim, op, lctx);
		default:
			/* collision: Send UA, but still wait for rx UA, then
			 * change to MF_EST state.
			 */
			/* check for contention resoultion */
			if (dl->tx_hist[0].msg && dl->tx_hist[0].msg->len) {
				LOGP(DLLAPD, LOGL_NOTICE, "SABM not allowed "
					"during contention resolution\n");
				mdl_error(MDL_CAUSE_SABM_INFO_NOTALL, lctx);
			}
			lapd_send_ua(lctx, length, msg->l3h);
			msgb_free(msg);
			return 0;
		}
		/* save message context for further use */
		memcpy(&dl->lctx, lctx, sizeof(dl->lctx));
#ifndef TEST_CONTENT_RESOLUTION_NETWORK
		/* send UA response */
		lapd_send_ua(lctx, length, msg->l3h);
#endif
		/* set Vs, Vr and Va to 0 */
		dl->v_send = dl->v_recv = dl->v_ack = 0;
		/* clear tx_hist */
		lapd_dl_flush_hist(dl);
		/* enter multiple-frame-established state */
		lapd_dl_newstate(dl, LAPD_STATE_MF_EST);
		/* store content resolution data on network side
		 * Note: cont_res will be removed when changing state again,
		 * so it must be allocated AFTER lapd_dl_newstate(). */
		if (dl->mode == LAPD_MODE_NETWORK && length) {
			dl->cont_res = lapd_msgb_alloc(length, "CONT RES");
			memcpy(msgb_put(dl->cont_res, length), msg->l3h,
				length);
			LOGP(DLLAPD, LOGL_NOTICE, "Store content res.\n");
		}
		/* send notification to L3 */
		if (length == 0) {
			/* 5.4.1.2 Normal establishment procedures */
			rc = send_dl_simple(prim, op, lctx);
			msgb_free(msg);
		} else {
			/* 5.4.1.4 Contention resolution establishment */
			rc = send_dl_l3(prim, op, lctx, msg);
		}
		break;
	case LAPD_U_DM:
		LOGP(DLLAPD, LOGL_INFO, "DM received in state %s\n",
			lapd_state_names[dl->state]);
		/* G.2.2 Wrong value of the C/R bit */
		if (lctx->cr == dl->cr.rem2loc.cmd) {
			LOGP(DLLAPD, LOGL_NOTICE, "DM command error\n");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
			return -EINVAL;
		}
		if (!lctx->p_f) {
			/* 5.4.1.2 DM responses with the F bit set to "0"
			 * shall be ignored.
			 */
			msgb_free(msg);
			return 0;
		}
		switch (dl->state) {
		case LAPD_STATE_SABM_SENT:
			break;
		case LAPD_STATE_MF_EST:
			if (lctx->p_f) {
				LOGP(DLLAPD, LOGL_INFO, "unsolicited DM "
					"response\n");
				mdl_error(MDL_CAUSE_UNSOL_DM_RESP, lctx);
			} else {
				LOGP(DLLAPD, LOGL_INFO, "unsolicited DM "
					"response, multiple frame established "
					"state\n");
				mdl_error(MDL_CAUSE_UNSOL_DM_RESP_MF, lctx);
				/* reestablish */
				if (!dl->reestablish) {
					msgb_free(msg);
					return 0;
				}
				LOGP(DLLAPD, LOGL_NOTICE, "Performing "
					"reestablishment.\n");
				lapd_reestablish(dl);
			}
			msgb_free(msg);
			return 0;
		case LAPD_STATE_TIMER_RECOV:
			/* FP = 0 (DM is normal in case PF = 1) */
			if (!lctx->p_f) {
				LOGP(DLLAPD, LOGL_INFO, "unsolicited DM "
					"response, multiple frame established "
					"state\n");
				mdl_error(MDL_CAUSE_UNSOL_DM_RESP_MF, lctx);
				msgb_free(msg);
				/* reestablish */
				if (!dl->reestablish)
					return 0;
				LOGP(DLLAPD, LOGL_NOTICE, "Performing "
					"reestablishment.\n");
				return lapd_reestablish(dl);
			}
			break;
		case LAPD_STATE_DISC_SENT:
			/* stop Timer T200 */
			lapd_stop_t200(dl);
			/* go to idle state */
			lapd_dl_flush_tx(dl);
			lapd_dl_flush_send(dl);
			lapd_dl_newstate(dl, LAPD_STATE_IDLE);
			rc = send_dl_simple(PRIM_DL_REL, PRIM_OP_CONFIRM, lctx);
			msgb_free(msg);
			return 0;
		case LAPD_STATE_IDLE:
			/* 5.4.5 all other frame types shall be discarded */
		default:
			LOGP(DLLAPD, LOGL_INFO, "unsolicited DM response! "
				"(discarding)\n");
			msgb_free(msg);
			return 0;
		}
		/* stop timer T200 */
		lapd_stop_t200(dl);
		/* go to idle state */
		lapd_dl_newstate(dl, LAPD_STATE_IDLE);
		rc = send_dl_simple(PRIM_DL_REL, PRIM_OP_INDICATION, lctx);
		msgb_free(msg);
		break;
	case LAPD_U_UI:
		LOGP(DLLAPD, LOGL_INFO, "UI received\n");
		/* G.2.2 Wrong value of the C/R bit */
		if (lctx->cr == dl->cr.rem2loc.resp) {
			LOGP(DLLAPD, LOGL_NOTICE, "UI indicates response "
				"error\n");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
			return -EINVAL;
		}

		/* G.4.5 If UI is received with L>N201 or with M bit
		 * set, AN MDL-ERROR-INDICATION is sent to MM.
		 */
		if (length > lctx->n201 || lctx->more) {
			LOGP(DLLAPD, LOGL_NOTICE, "UI too large error "
				"(%d > N201(%d) or M=%d)\n", length,
				lctx->n201, lctx->more);
			msgb_free(msg);
			mdl_error(MDL_CAUSE_UFRM_INC_PARAM, lctx);
			return -EIO;
		}

		/* do some length checks */
		if (length == 0) {
			/* 5.3.3 UI frames received with the length indicator
			 * set to "0" shall be ignored
			 */
			LOGP(DLLAPD, LOGL_INFO, "length=0 (discarding)\n");
			msgb_free(msg);
			return 0;
		}
		rc = send_dl_l3(PRIM_DL_UNIT_DATA, PRIM_OP_INDICATION, lctx,
				msg);
		break;
	case LAPD_U_DISC:
		prim = PRIM_DL_REL;
		op = PRIM_OP_INDICATION;

		LOGP(DLLAPD, LOGL_INFO, "DISC received in state %s\n",
			lapd_state_names[dl->state]);
		/* flush tx and send buffers */
		lapd_dl_flush_tx(dl);
		lapd_dl_flush_send(dl);
		/* 5.7.1 */
		dl->seq_err_cond = 0;
		/* G.2.2 Wrong value of the C/R bit */
		if (lctx->cr == dl->cr.rem2loc.resp) {
			LOGP(DLLAPD, LOGL_NOTICE, "DISC response error\n");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
			return -EINVAL;
		}
		if (length > 0 || lctx->more) {
			/* G.4.4 If a DISC or DM frame is received with L>0 or
			 * with the M bit set to "1", an MDL-ERROR-INDICATION
			 * primitive with cause "U frame with incorrect
			 * parameters" is sent to the mobile management entity.
			 */
			LOGP(DLLAPD, LOGL_NOTICE,
				"U frame iwth incorrect parameters ");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_UFRM_INC_PARAM, lctx);
			return -EIO;
		}
		switch (dl->state) {
		case LAPD_STATE_IDLE:
			LOGP(DLLAPD, LOGL_INFO, "DISC in idle state\n");
			/* send DM with F=P */
			msgb_free(msg);
			return lapd_send_dm(lctx);
		case LAPD_STATE_SABM_SENT:
			LOGP(DLLAPD, LOGL_INFO, "DISC in SABM state\n");
			/* 5.4.6.2 send DM with F=P */
			lapd_send_dm(lctx);
			/* stop Timer T200 */
			lapd_stop_t200(dl);
			/* go to idle state */
			lapd_dl_newstate(dl, LAPD_STATE_IDLE);
			msgb_free(msg);
			return send_dl_simple(PRIM_DL_REL, PRIM_OP_INDICATION,
				lctx);
		case LAPD_STATE_MF_EST:
		case LAPD_STATE_TIMER_RECOV:
			LOGP(DLLAPD, LOGL_INFO, "DISC in est state\n");
			break;
		case LAPD_STATE_DISC_SENT:
			LOGP(DLLAPD, LOGL_INFO, "DISC in disc state\n");
			prim = PRIM_DL_REL;
			op = PRIM_OP_CONFIRM;
			break;
		default:
			lapd_send_ua(lctx, length, msg->l3h);
			msgb_free(msg);
			return 0;
		}
		/* send UA response */
		lapd_send_ua(lctx, length, msg->l3h);
		/* stop Timer T200 */
		lapd_stop_t200(dl);
		/* enter idle state, keep tx-buffer with UA response */
		lapd_dl_newstate(dl, LAPD_STATE_IDLE);
		/* send notification to L3 */
		rc = send_dl_simple(prim, op, lctx);
		msgb_free(msg);
		break;
	case LAPD_U_UA:
		LOGP(DLLAPD, LOGL_INFO, "UA received in state %s\n",
			lapd_state_names[dl->state]);
		/* G.2.2 Wrong value of the C/R bit */
		if (lctx->cr == dl->cr.rem2loc.cmd) {
			LOGP(DLLAPD, LOGL_NOTICE, "UA indicates command "
				"error\n");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
			return -EINVAL;
		}

		/* G.4.5 If UA is received with L>N201 or with M bit
		 * set, AN MDL-ERROR-INDICATION is sent to MM.
		 */
		if (lctx->more || length > lctx->n201) {
			LOGP(DLLAPD, LOGL_NOTICE, "UA too large error\n");
			msgb_free(msg);
			mdl_error(MDL_CAUSE_UFRM_INC_PARAM, lctx);
			return -EIO;
		}

		if (!lctx->p_f) {
			/* 5.4.1.2 A UA response with the F bit set to "0"
			 * shall be ignored.
			 */
			LOGP(DLLAPD, LOGL_INFO, "F=0 (discarding)\n");
			msgb_free(msg);
			return 0;
		}
		switch (dl->state) {
		case LAPD_STATE_SABM_SENT:
			break;
		case LAPD_STATE_MF_EST:
		case LAPD_STATE_TIMER_RECOV:
			LOGP(DLLAPD, LOGL_INFO, "unsolicited UA response! "
				"(discarding)\n");
			mdl_error(MDL_CAUSE_UNSOL_UA_RESP, lctx);
			msgb_free(msg);
			return 0;
		case LAPD_STATE_DISC_SENT:
			LOGP(DLLAPD, LOGL_INFO, "UA in disconnect state\n");
			/* stop Timer T200 */
			lapd_stop_t200(dl);
			/* go to idle state */
			lapd_dl_flush_tx(dl);
			lapd_dl_flush_send(dl);
			lapd_dl_newstate(dl, LAPD_STATE_IDLE);
			rc = send_dl_simple(PRIM_DL_REL, PRIM_OP_CONFIRM, lctx);
			msgb_free(msg);
			return 0;
		case LAPD_STATE_IDLE:
			/* 5.4.5 all other frame types shall be discarded */
		default:
			LOGP(DLLAPD, LOGL_INFO, "unsolicited UA response! "
				"(discarding)\n");
			msgb_free(msg);
			return 0;
		}
		LOGP(DLLAPD, LOGL_INFO, "UA in SABM state\n");
		/* stop Timer T200 */
		lapd_stop_t200(dl);
		/* compare UA with SABME if contention resolution is applied */
		if (dl->tx_hist[0].msg->len) {
			if (length != (dl->tx_hist[0].msg->len)
			 || !!memcmp(dl->tx_hist[0].msg->data, msg->l3h,
			 					length)) {
				LOGP(DLLAPD, LOGL_INFO, "**** UA response "
					"mismatches ****\n");
				rc = send_dl_simple(PRIM_DL_REL,
					PRIM_OP_INDICATION, lctx);
				msgb_free(msg);
				/* go to idle state */
				lapd_dl_flush_tx(dl);
				lapd_dl_flush_send(dl);
				lapd_dl_newstate(dl, LAPD_STATE_IDLE);
				return 0;
			}
		}
		/* set Vs, Vr and Va to 0 */
		dl->v_send = dl->v_recv = dl->v_ack = 0;
		/* clear tx_hist */
		lapd_dl_flush_hist(dl);
		/* enter multiple-frame-established state */
		lapd_dl_newstate(dl, LAPD_STATE_MF_EST);
		/* send outstanding frames, if any (resume / reconnect) */
		lapd_send_i(lctx, __LINE__);
		/* send notification to L3 */
		rc = send_dl_simple(PRIM_DL_EST, PRIM_OP_CONFIRM, lctx);
		msgb_free(msg);
		break;
	case LAPD_U_FRMR:
		LOGP(DLLAPD, LOGL_NOTICE, "Frame reject received\n");
		/* send MDL ERROR INIDCATION to L3 */
		mdl_error(MDL_CAUSE_FRMR, lctx);
		msgb_free(msg);
		/* reestablish */
		if (!dl->reestablish)
			break;
		LOGP(DLLAPD, LOGL_NOTICE, "Performing reestablishment.\n");
		rc = lapd_reestablish(dl);
		break;
	default:
		/* G.3.1 */
		LOGP(DLLAPD, LOGL_NOTICE, "Unnumbered frame not allowed.\n");
		msgb_free(msg);
		mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
		return -EINVAL;
	}
	return rc;
}

/* Receive a LAPD S (Supervisory) message from L1 */
static int lapd_rx_s(struct msgb *msg, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	int length = lctx->length;

	if (length > 0 || lctx->more) {
		/* G.4.3 If a supervisory frame is received with L>0 or
		 * with the M bit set to "1", an MDL-ERROR-INDICATION
		 * primitive with cause "S frame with incorrect
		 * parameters" is sent to the mobile management entity. */
		LOGP(DLLAPD, LOGL_NOTICE,
				"S frame with incorrect parameters\n");
		msgb_free(msg);
		mdl_error(MDL_CAUSE_SFRM_INC_PARAM, lctx);
		return -EIO;
	}

	if (lctx->cr == dl->cr.rem2loc.resp
	 && lctx->p_f
	 && dl->state != LAPD_STATE_TIMER_RECOV) {
		/* 5.4.2.2: Inidcate error on supervisory reponse F=1 */
		LOGP(DLLAPD, LOGL_NOTICE, "S frame response with F=1 error\n");
		mdl_error(MDL_CAUSE_UNSOL_SPRV_RESP, lctx);
	}

	switch (dl->state) {
	case LAPD_STATE_IDLE:
		/* if P=1, respond DM with F=1 (5.2.2) */
		/* 5.4.5 all other frame types shall be discarded */
		if (lctx->p_f)
			lapd_send_dm(lctx); /* F=P */
		/* fall though */
	case LAPD_STATE_SABM_SENT:
	case LAPD_STATE_DISC_SENT:
		LOGP(DLLAPD, LOGL_NOTICE, "S frame ignored in this state\n");
		msgb_free(msg);
		return 0;
	}
	switch (lctx->s_u) {
	case LAPD_S_RR:
		LOGP(DLLAPD, LOGL_INFO, "RR received in state %s\n",
			lapd_state_names[dl->state]);
		/* 5.5.3.1: Acknowlege all tx frames up the the N(R)-1 */
		lapd_acknowledge(lctx);

		/* 5.5.3.2 */
		if (lctx->cr == dl->cr.rem2loc.cmd
		 && lctx->p_f) {
		 	if (!dl->own_busy && !dl->seq_err_cond) {
				LOGP(DLLAPD, LOGL_INFO, "RR frame command "
					"with polling bit set and we are not "
					"busy, so we reply with RR frame "
					"response\n");
				lapd_send_rr(lctx, 1, 0);
				/* NOTE: In case of sequence error condition,
				 * the REJ frame has been transmitted when
				 * entering the condition, so it has not be
				 * done here
				 */
			} else if (dl->own_busy) {
				LOGP(DLLAPD, LOGL_INFO, "RR frame command "
					"with polling bit set and we are busy, "
					"so we reply with RR frame response\n");
				lapd_send_rnr(lctx, 1, 0);
			}
		} else if (lctx->cr == dl->cr.rem2loc.resp
			&& lctx->p_f
			&& dl->state == LAPD_STATE_TIMER_RECOV) {
			LOGP(DLLAPD, LOGL_INFO, "RR response with F==1, "
				"and we are in timer recovery state, so "
				"we leave that state\n");
			/* V(S) to the N(R) in the RR frame */
			dl->v_send = lctx->n_recv;
			/* stop Timer T200 */
			lapd_stop_t200(dl);
			/* 5.5.7 Clear timer recovery condition */
			lapd_dl_newstate(dl, LAPD_STATE_MF_EST);
		}
		/* Send message, if possible due to acknowledged data */
		lapd_send_i(lctx, __LINE__);

		break;
	case LAPD_S_RNR:
		LOGP(DLLAPD, LOGL_INFO, "RNR received in state %s\n",
			lapd_state_names[dl->state]);
		/* 5.5.3.1: Acknowlege all tx frames up the the N(R)-1 */
		lapd_acknowledge(lctx);

		/* 5.5.5 */
		/* Set peer receiver busy condition */
		dl->peer_busy = 1;

		if (lctx->p_f) {
			if (lctx->cr == dl->cr.rem2loc.cmd) {
				if (!dl->own_busy) {
					LOGP(DLLAPD, LOGL_INFO, "RNR poll "
						"command and we are not busy, "
						"so we reply with RR final "
						"response\n");
					/* Send RR with F=1 */
					lapd_send_rr(lctx, 1, 0);
				} else {
					LOGP(DLLAPD, LOGL_INFO, "RNR poll "
						"command and we are busy, so "
						"we reply with RNR final "
						"response\n");
					/* Send RNR with F=1 */
					lapd_send_rnr(lctx, 1, 0);
				}
			} else if (dl->state == LAPD_STATE_TIMER_RECOV) {
				LOGP(DLLAPD, LOGL_INFO, "RNR poll response "
					"and we in timer recovery state, so "
					"we leave that state\n");
				/* 5.5.7 Clear timer recovery condition */
				lapd_dl_newstate(dl, LAPD_STATE_MF_EST);
				/* V(S) to the N(R) in the RNR frame */
				dl->v_send = lctx->n_recv;
			}
		} else
			LOGP(DLLAPD, LOGL_INFO, "RNR not polling/final state "
				"received\n");

		/* Send message, if possible due to acknowledged data */
		lapd_send_i(lctx, __LINE__);

		break;
	case LAPD_S_REJ:
		LOGP(DLLAPD, LOGL_INFO, "REJ received in state %s\n",
			lapd_state_names[dl->state]);
		/* 5.5.3.1: Acknowlege all tx frames up the the N(R)-1 */
		lapd_acknowledge(lctx);

		/* 5.5.4.1 */
		if (dl->state != LAPD_STATE_TIMER_RECOV) {
			/* Clear an existing peer receiver busy condition */
			dl->peer_busy = 0;
			/* V(S) and V(A) to the N(R) in the REJ frame */
			dl->v_send = dl->v_ack = lctx->n_recv;
			/* stop Timer T200 */
			lapd_stop_t200(dl);
			/* 5.5.3.2 */
			if (lctx->cr == dl->cr.rem2loc.cmd && lctx->p_f) {
				if (!dl->own_busy && !dl->seq_err_cond) {
					LOGP(DLLAPD, LOGL_INFO, "REJ poll "
						"command not in timer recovery "
						"state and not in own busy "
						"condition received, so we "
						"respond with RR final "
						"response\n");
					lapd_send_rr(lctx, 1, 0);
					/* NOTE: In case of sequence error
					 * condition, the REJ frame has been
					 * transmitted when entering the
					 * condition, so it has not be done
					 * here
				 	 */
				} else if (dl->own_busy) {
					LOGP(DLLAPD, LOGL_INFO, "REJ poll "
						"command not in timer recovery "
						"state and in own busy "
						"condition received, so we "
						"respond with RNR final "
						"response\n");
					lapd_send_rnr(lctx, 1, 0);
				}
			} else
				LOGP(DLLAPD, LOGL_INFO, "REJ response or not "
					"polling command not in timer recovery "
					"state received\n");
			/* send MDL ERROR INIDCATION to L3 */
			if (lctx->cr == dl->cr.rem2loc.resp && lctx->p_f) {
				mdl_error(MDL_CAUSE_UNSOL_SPRV_RESP, lctx);
			}

		} else if (lctx->cr == dl->cr.rem2loc.resp && lctx->p_f) {
			LOGP(DLLAPD, LOGL_INFO, "REJ poll response in timer "
				"recovery state received\n");
			/* Clear an existing peer receiver busy condition */
			dl->peer_busy = 0;
			/* V(S) and V(A) to the N(R) in the REJ frame */
			dl->v_send = dl->v_ack = lctx->n_recv;
			/* stop Timer T200 */
			lapd_stop_t200(dl);
			/* 5.5.7 Clear timer recovery condition */
			lapd_dl_newstate(dl, LAPD_STATE_MF_EST);
		} else {
			/* Clear an existing peer receiver busy condition */
			dl->peer_busy = 0;
			/* V(S) and V(A) to the N(R) in the REJ frame */
			dl->v_send = dl->v_ack = lctx->n_recv;
			/* 5.5.3.2 */
			if (lctx->cr == dl->cr.rem2loc.cmd && lctx->p_f) {
				if (!dl->own_busy && !dl->seq_err_cond) {
					LOGP(DLLAPD, LOGL_INFO, "REJ poll "
						"command in timer recovery "
						"state and not in own busy "
						"condition received, so we "
						"respond with RR final "
						"response\n");
					lapd_send_rr(lctx, 1, 0);
					/* NOTE: In case of sequence error
					 * condition, the REJ frame has been
					 * transmitted when entering the
					 * condition, so it has not be done
					 * here
				 	 */
				} else if (dl->own_busy) {
					LOGP(DLLAPD, LOGL_INFO, "REJ poll "
						"command in timer recovery "
						"state and in own busy "
						"condition received, so we "
						"respond with RNR final "
						"response\n");
					lapd_send_rnr(lctx, 1, 0);
				}
			} else
				LOGP(DLLAPD, LOGL_INFO, "REJ response or not "
					"polling command in timer recovery "
					"state received\n");
		}

		/* FIXME: 5.5.4.2 2) */

		/* Send message, if possible due to acknowledged data */
		lapd_send_i(lctx, __LINE__);

		break;
	default:
		/* G.3.1 */
		LOGP(DLLAPD, LOGL_NOTICE, "Supervisory frame not allowed.\n");
		msgb_free(msg);
		mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
		return -EINVAL;
	}
	msgb_free(msg);
	return 0;
}

/* Receive a LAPD I (Information) message from L1 */
static int lapd_rx_i(struct msgb *msg, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	//uint8_t nr = lctx->n_recv;
	uint8_t ns = lctx->n_send;
	int length = lctx->length;
	int rc;

	LOGP(DLLAPD, LOGL_INFO, "I received in state %s\n",
		lapd_state_names[dl->state]);
		
	/* G.2.2 Wrong value of the C/R bit */
	if (lctx->cr == dl->cr.rem2loc.resp) {
		LOGP(DLLAPD, LOGL_NOTICE, "I frame response not allowed\n");
		msgb_free(msg);
		mdl_error(MDL_CAUSE_FRM_UNIMPL, lctx);
		return -EINVAL;
	}

	if (length == 0 || length > lctx->n201) {
		/* G.4.2 If the length indicator of an I frame is set
		 * to a numerical value L>N201 or L=0, an MDL-ERROR-INDICATION
		 * primitive with cause "I frame with incorrect length"
		 * is sent to the mobile management entity. */
		LOGP(DLLAPD, LOGL_NOTICE, "I frame length not allowed\n");
		msgb_free(msg);
		mdl_error(MDL_CAUSE_IFRM_INC_LEN, lctx);
		return -EIO;
	}

	/* G.4.2 If the numerical value of L is L<N201 and the M
	 * bit is set to "1", then an MDL-ERROR-INDICATION primitive with
	 * cause "I frame with incorrect use of M bit" is sent to the
	 * mobile management entity. */
	if (lctx->more && length < lctx->n201) {
		LOGP(DLLAPD, LOGL_NOTICE, "I frame with M bit too short\n");
		msgb_free(msg);
		mdl_error(MDL_CAUSE_IFRM_INC_MBITS, lctx);
		return -EIO;
	}

	switch (dl->state) {
	case LAPD_STATE_IDLE:
		/* if P=1, respond DM with F=1 (5.2.2) */
		/* 5.4.5 all other frame types shall be discarded */
		if (lctx->p_f)
			lapd_send_dm(lctx); /* F=P */
		/* fall though */
	case LAPD_STATE_SABM_SENT:
	case LAPD_STATE_DISC_SENT:
		LOGP(DLLAPD, LOGL_NOTICE, "I frame ignored in this state\n");
		msgb_free(msg);
		return 0;
	}

	/* 5.7.1: N(s) sequence error */
	if (ns != dl->v_recv) {
		LOGP(DLLAPD, LOGL_NOTICE, "N(S) sequence error: N(S)=%u, "
		     "V(R)=%u\n", ns, dl->v_recv);
		/* discard data */
		msgb_free(msg);
		if (dl->seq_err_cond != 1) {
			/* FIXME: help me understand what exactly todo here
			*/
			dl->seq_err_cond = 1;
			lapd_send_rej(lctx, lctx->p_f);
		} else {
			/* If there are two subsequent sequence errors received,
			 * ignore it. (Ignore every second subsequent error.)
			 * This happens if our reply with the REJ is too slow,
			 * so the remote gets a T200 timeout and sends another
			 * frame with a sequence error.
			 * Test showed that replying with two subsequent REJ
			 * messages could the remote L2 process to abort.
			 * Replying too slow shouldn't happen, but may happen
			 * over serial link between BB and LAPD.
			 */
			dl->seq_err_cond = 2;
		}
		/* Even if N(s) sequence error, acknowledge to N(R)-1 */
		/* 5.5.3.1: Acknowlege all transmitted frames up the N(R)-1 */
		lapd_acknowledge(lctx); /* V(A) is also set here */

		/* Send message, if possible due to acknowledged data */
		lapd_send_i(lctx, __LINE__);

		return 0;
	}
	dl->seq_err_cond = 0;

	/* Increment receiver state */
	dl->v_recv = inc_mod(dl->v_recv, dl->v_range);
	LOGP(DLLAPD, LOGL_INFO, "incrementing V(R) to %u\n", dl->v_recv);

	/* 5.5.3.1: Acknowlege all transmitted frames up the the N(R)-1 */
	lapd_acknowledge(lctx); /* V(A) is also set here */

	/* Only if we are not in own receiver busy condition */
	if (!dl->own_busy) {
		/* if the frame carries a complete segment */
		if (!lctx->more && !dl->rcv_buffer) {
			LOGP(DLLAPD, LOGL_INFO, "message in single I frame\n");
			/* send a DATA INDICATION to L3 */
			msg->len = length;
			msg->tail = msg->data + length;
			rc = send_dl_l3(PRIM_DL_DATA, PRIM_OP_INDICATION, lctx,
				msg);
		} else {
			/* create rcv_buffer */
			if (!dl->rcv_buffer) {
				LOGP(DLLAPD, LOGL_INFO, "message in multiple "
					"I frames (first message)\n");
				dl->rcv_buffer = lapd_msgb_alloc(dl->maxf,
					"LAPD RX");
				dl->rcv_buffer->l3h = dl->rcv_buffer->data;
			}
			/* concat. rcv_buffer */
			if (msgb_l3len(dl->rcv_buffer) + length > dl->maxf) {
				LOGP(DLLAPD, LOGL_NOTICE, "Received frame "
					"overflow!\n");
			} else {
				memcpy(msgb_put(dl->rcv_buffer, length),
					msg->l3h, length);
			}
			/* if the last segment was received */
			if (!lctx->more) {
				LOGP(DLLAPD, LOGL_INFO, "message in multiple "
					"I frames (last message)\n");
				rc = send_dl_l3(PRIM_DL_DATA,
					PRIM_OP_INDICATION, lctx,
					dl->rcv_buffer);
				dl->rcv_buffer = NULL;
			} else
				LOGP(DLLAPD, LOGL_INFO, "message in multiple "
					"I frames (next message)\n");
			msgb_free(msg);

		}
	} else
		LOGP(DLLAPD, LOGL_INFO, "I frame ignored during own receiver "
			"busy condition\n");

	/* Check for P bit */
	if (lctx->p_f) {
		/* 5.5.2.1 */
		/* check if we are not in own receiver busy */
		if (!dl->own_busy) {
			LOGP(DLLAPD, LOGL_INFO, "we are not busy, send RR\n");
			/* Send RR with F=1 */
			rc = lapd_send_rr(lctx, 1, 0);
		} else {
			LOGP(DLLAPD, LOGL_INFO, "we are busy, send RNR\n");
			/* Send RNR with F=1 */
			rc = lapd_send_rnr(lctx, 1, 0);
		}
	} else {
		/* 5.5.2.2 */
		/* check if we are not in own receiver busy */
		if (!dl->own_busy) {
			/* NOTE: V(R) is already set above */
			rc = lapd_send_i(lctx, __LINE__);
			if (rc) {
				LOGP(DLLAPD, LOGL_INFO, "we are not busy and "
					"have no pending data, send RR\n");
				/* Send RR with F=0 */
				return lapd_send_rr(lctx, 0, 0);
			}
			/* all I or one RR is sent, we are done */
			return 0;
		} else {
			LOGP(DLLAPD, LOGL_INFO, "we are busy, send RNR\n");
			/* Send RNR with F=0 */
			rc = lapd_send_rnr(lctx, 0, 0);
		}
	}

	/* Send message, if possible due to acknowledged data */
	lapd_send_i(lctx, __LINE__);

	return rc;
}

/* Receive a LAPD message from L1 */
int lapd_ph_data_ind(struct msgb *msg, struct lapd_msg_ctx *lctx)
{
	int rc;

	switch (lctx->format) {
	case LAPD_FORM_U:
		rc = lapd_rx_u(msg, lctx);
		break;
	case LAPD_FORM_S:
		rc = lapd_rx_s(msg, lctx);
		break;
	case LAPD_FORM_I:
		rc = lapd_rx_i(msg, lctx);
		break;
	default:
		LOGP(DLLAPD, LOGL_NOTICE, "unknown LAPD format\n");
		msgb_free(msg);
		rc = -EINVAL;
	}
	return rc;
}

/* L3 -> L2 */

/* send unit data */
static int lapd_udata_req(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg = dp->oph.msg;
	struct lapd_msg_ctx nctx;

	memcpy(&nctx, lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.cmd;
	nctx.format = LAPD_FORM_U;
	nctx.s_u = LAPD_U_UI;
	/* keep nctx.p_f */
	nctx.length = msg->len;
	nctx.more = 0;

	return dl->send_ph_data_req(&nctx, msg);
}

/* request link establishment */
static int lapd_est_req(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg = dp->oph.msg;
	struct lapd_msg_ctx nctx;

	if (msg->len)
		LOGP(DLLAPD, LOGL_INFO, "perform establishment with content "
			"(SABM)\n");
	else
		LOGP(DLLAPD, LOGL_INFO, "perform normal establishm. (SABM)\n");

	/* Flush send-queue */
	/* Clear send-buffer */
	lapd_dl_flush_send(dl);
	/* be sure that history is empty */
	lapd_dl_flush_hist(dl);

	/* save message context for further use */
	memcpy(&dl->lctx, lctx, sizeof(dl->lctx));

	/* Discard partly received L3 message */
	if (dl->rcv_buffer) {
		msgb_free(dl->rcv_buffer);
		dl->rcv_buffer = NULL;
	}

	/* assemble message */
	memcpy(&nctx, &dl->lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.cmd;
	nctx.format = LAPD_FORM_U;
	nctx.s_u = (dl->use_sabme) ? LAPD_U_SABME : LAPD_U_SABM;
	nctx.p_f = 1;
	nctx.length = msg->len;
	nctx.more = 0;

	/* Transmit-buffer carries exactly one segment */
	dl->tx_hist[0].msg = lapd_msgb_alloc(msg->len, "HIST");
	msgb_put(dl->tx_hist[0].msg, msg->len);
	if (msg->len)
		memcpy(dl->tx_hist[0].msg->data, msg->l3h, msg->len);
	dl->tx_hist[0].more = 0;
	/* set Vs to 0, because it is used as index when resending SABM */
	dl->v_send = 0;
	
	/* Set states */
	dl->own_busy = dl->peer_busy = 0;
	dl->retrans_ctr = 0;
	lapd_dl_newstate(dl, LAPD_STATE_SABM_SENT);

	/* Tramsmit and start T200 */
	dl->send_ph_data_req(&nctx, msg);
	lapd_start_t200(dl);

	return 0;
}

/* send data */
static int lapd_data_req(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg = dp->oph.msg;

	if (msgb_l3len(msg) == 0) {
		LOGP(DLLAPD, LOGL_ERROR,
			"writing an empty message is not possible.\n");
		msgb_free(msg);
		return -1;
	}

	LOGP(DLLAPD, LOGL_INFO,
	     "writing message to send-queue: l3len: %d\n", msgb_l3len(msg));

	/* Write data into the send queue */
	msgb_enqueue(&dl->send_queue, msg);

	/* Send message, if possible */
	lapd_send_i(&dl->lctx, __LINE__);

	return 0;
}

/* Send next I frame from queued/buffered data */
static int lapd_send_i(struct lapd_msg_ctx *lctx, int line)
{
	struct lapd_datalink *dl = lctx->dl;
	uint8_t k = dl->k;
	uint8_t h;
	struct msgb *msg;
	int length, left;
	int rc = - 1; /* we sent nothing */
	struct lapd_msg_ctx nctx;


	LOGP(DLLAPD, LOGL_INFO, "%s() called from line %d\n", __func__, line);

	next_frame:

	if (dl->peer_busy) {
		LOGP(DLLAPD, LOGL_INFO, "peer busy, not sending\n");
		return rc;
	}

	if (dl->state == LAPD_STATE_TIMER_RECOV) {
		LOGP(DLLAPD, LOGL_INFO, "timer recovery, not sending\n");
		return rc;
	}

	/* If the send state variable V(S) is equal to V(A) plus k
	 * (where k is the maximum number of outstanding I frames - see
	 * subclause 5.8.4), the data link layer entity shall not transmit any
	 * new I frames, but shall retransmit an I frame as a result
	 * of the error recovery procedures as described in subclauses 5.5.4 and
	 * 5.5.7. */
	if (dl->v_send == add_mod(dl->v_ack, k, dl->v_range)) {
		LOGP(DLLAPD, LOGL_INFO, "k frames outstanding, not sending "
			"more (k=%u V(S)=%u V(A)=%u)\n", k, dl->v_send,
			dl->v_ack);
		return rc;
	}

	h = do_mod(dl->v_send, dl->range_hist);

	/* if we have no tx_hist yet, we create it */
	if (!dl->tx_hist[h].msg) {
		/* Get next message into send-buffer, if any */
		if (!dl->send_buffer) {
			next_message:
			dl->send_out = 0;
			dl->send_buffer = msgb_dequeue(&dl->send_queue);
			/* No more data to be sent */
			if (!dl->send_buffer)
				return rc;
			LOGP(DLLAPD, LOGL_INFO, "get message from "
				"send-queue\n");
		}

		/* How much is left in the send-buffer? */
		left = msgb_l3len(dl->send_buffer) - dl->send_out;
		/* Segment, if data exceeds N201 */
		length = left;
		if (length > lctx->n201)
			length = lctx->n201;
		LOGP(DLLAPD, LOGL_INFO, "msg-len %d sent %d left %d N201 %d "
			"length %d first byte %02x\n",
			msgb_l3len(dl->send_buffer), dl->send_out, left,
			lctx->n201, length, dl->send_buffer->l3h[0]);
		/* If message in send-buffer is completely sent */
		if (left == 0) {
			msgb_free(dl->send_buffer);
			dl->send_buffer = NULL;
			goto next_message;
		}

		LOGP(DLLAPD, LOGL_INFO, "send I frame %sV(S)=%d\n",
			(left > length) ? "segment " : "", dl->v_send);

		/* Create I frame (segment) and transmit-buffer content */
		msg = lapd_msgb_alloc(length, "LAPD I");
		msg->l3h = msgb_put(msg, length);
		/* assemble message */
		memcpy(&nctx, &dl->lctx, sizeof(nctx));
		/* keep nctx.ldp */
		/* keep nctx.sapi */
		/* keep nctx.tei */
		nctx.cr = dl->cr.loc2rem.cmd;
		nctx.format = LAPD_FORM_I;
		nctx.p_f = 0;
		nctx.n_send = dl->v_send;
		nctx.n_recv = dl->v_recv;
		nctx.length = length;
		if (left > length)
			nctx.more = 1;
		else
			nctx.more = 0;
		if (length)
			memcpy(msg->l3h, dl->send_buffer->l3h + dl->send_out,
				length);
		/* store in tx_hist */
		dl->tx_hist[h].msg = lapd_msgb_alloc(msg->len, "HIST");
		msgb_put(dl->tx_hist[h].msg, msg->len);
		if (length)
			memcpy(dl->tx_hist[h].msg->data, msg->l3h, msg->len);
		dl->tx_hist[h].more = nctx.more;
		/* Add length to track how much is already in the tx buffer */
		dl->send_out += length;
	} else {
		LOGP(DLLAPD, LOGL_INFO, "resend I frame from tx buffer "
			"V(S)=%d\n", dl->v_send);

		/* Create I frame (segment) from tx_hist */
		length = dl->tx_hist[h].msg->len;
		msg = lapd_msgb_alloc(length, "LAPD I resend");
		msg->l3h = msgb_put(msg, length);
		/* assemble message */
		memcpy(&nctx, &dl->lctx, sizeof(nctx));
		/* keep nctx.ldp */
		/* keep nctx.sapi */
		/* keep nctx.tei */
		nctx.cr = dl->cr.loc2rem.cmd;
		nctx.format = LAPD_FORM_I;
		nctx.p_f = 0;
		nctx.n_send = dl->v_send;
		nctx.n_recv = dl->v_recv;
		nctx.length = length;
		nctx.more = dl->tx_hist[h].more;
		if (length)
			memcpy(msg->l3h, dl->tx_hist[h].msg->data, length);
	}

	/* The value of the send state variable V(S) shall be incremented by 1
	 * at the end of the transmission of the I frame */
	dl->v_send = inc_mod(dl->v_send, dl->v_range);

	/* If timer T200 is not running at the time right before transmitting a
	 * frame, when the PH-READY-TO-SEND primitive is received from the
	 * physical layer., it shall be set. */
	if (!osmo_timer_pending(&dl->t200)) {
		/* stop Timer T203, if running */
		lapd_stop_t203(dl);
		/* start Timer T200 */
		lapd_start_t200(dl);
	}

	dl->send_ph_data_req(&nctx, msg);

	rc = 0; /* we sent something */
	goto next_frame;
}

/* request link suspension */
static int lapd_susp_req(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg = dp->oph.msg;

	LOGP(DLLAPD, LOGL_INFO, "perform suspension\n");

	/* put back the send-buffer to the send-queue (first position) */
	if (dl->send_buffer) {
		LOGP(DLLAPD, LOGL_INFO, "put frame in sendbuffer back to "
			"queue\n");
		llist_add(&dl->send_buffer->list, &dl->send_queue);
		dl->send_buffer = NULL;
	} else
		LOGP(DLLAPD, LOGL_INFO, "no frame in sendbuffer\n");

	/* Clear transmit buffer, but keep send buffer */
	lapd_dl_flush_tx(dl);
	/* Stop timers (there is no state change, so we must stop all timers */
	lapd_stop_t200(dl);
	lapd_stop_t203(dl);

	msgb_free(msg);

	return send_dl_simple(PRIM_DL_SUSP, PRIM_OP_CONFIRM, &dl->lctx);
}

/* requesst resume or reconnect of link */
static int lapd_res_req(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg = dp->oph.msg;
	struct lapd_msg_ctx nctx;

	LOGP(DLLAPD, LOGL_INFO, "perform re-establishment (SABM) length=%d\n",
		msg->len);
	
	/* be sure that history is empty */
	lapd_dl_flush_hist(dl);

	/* save message context for further use */
	memcpy(&dl->lctx, lctx, sizeof(dl->lctx));

	/* Replace message in the send-buffer (reconnect) */
	if (dl->send_buffer)
		msgb_free(dl->send_buffer);
	dl->send_out = 0;
	if (msg && msg->len)
		/* Write data into the send buffer, to be sent first */
		dl->send_buffer = msg;
	else
		dl->send_buffer = NULL;

	/* Discard partly received L3 message */
	if (dl->rcv_buffer) {
		msgb_free(dl->rcv_buffer);
		dl->rcv_buffer = NULL;
	}

	/* Create new msgb (old one is now free) */
	msg = lapd_msgb_alloc(0, "LAPD SABM");
	msg->l3h = msg->data;
	/* assemble message */
	memcpy(&nctx, &dl->lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.cmd;
	nctx.format = LAPD_FORM_U;
	nctx.s_u = (dl->use_sabme) ? LAPD_U_SABME : LAPD_U_SABM;
	nctx.p_f = 1;
	nctx.length = 0;
	nctx.more = 0;

	dl->tx_hist[0].msg = lapd_msgb_alloc(msg->len, "HIST");
	msgb_put(dl->tx_hist[0].msg, msg->len);
	if (msg->len)
		memcpy(dl->tx_hist[0].msg->data, msg->l3h, msg->len);
	dl->tx_hist[0].more = 0;
	/* set Vs to 0, because it is used as index when resending SABM */
	dl->v_send = 0;

	/* Set states */
	dl->own_busy = dl->peer_busy = 0;
	dl->retrans_ctr = 0;
	lapd_dl_newstate(dl, LAPD_STATE_SABM_SENT);

	/* Tramsmit and start T200 */
	dl->send_ph_data_req(&nctx, msg);
	lapd_start_t200(dl);

	return 0;
}

/* requesst release of link */
static int lapd_rel_req(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg = dp->oph.msg;
	struct lapd_msg_ctx nctx;

	/* local release */
	if (dp->u.rel_req.mode) {
		LOGP(DLLAPD, LOGL_INFO, "perform local release\n");
		msgb_free(msg);
		/* stop Timer T200 */
		lapd_stop_t200(dl);
		/* enter idle state, T203 is stopped here, if running */
		lapd_dl_newstate(dl, LAPD_STATE_IDLE);
		/* flush buffers */
		lapd_dl_flush_tx(dl);
		lapd_dl_flush_send(dl);
		/* send notification to L3 */
		return send_dl_simple(PRIM_DL_REL, PRIM_OP_CONFIRM, &dl->lctx);
	}

	/* in case we are already disconnecting */
	if (dl->state == LAPD_STATE_DISC_SENT)
		return -EBUSY;

	/* flush tx_hist */
	lapd_dl_flush_hist(dl);

	LOGP(DLLAPD, LOGL_INFO, "perform normal release (DISC)\n");

	/* Push LAPD header on msgb */
	/* assemble message */
	memcpy(&nctx, &dl->lctx, sizeof(nctx));
	/* keep nctx.ldp */
	/* keep nctx.sapi */
	/* keep nctx.tei */
	nctx.cr = dl->cr.loc2rem.cmd;
	nctx.format = LAPD_FORM_U;
	nctx.s_u = LAPD_U_DISC;
	nctx.p_f = 1;
	nctx.length = 0;
	nctx.more = 0;

	dl->tx_hist[0].msg = lapd_msgb_alloc(msg->len, "HIST");
	msgb_put(dl->tx_hist[0].msg, msg->len);
	if (msg->len)
		memcpy(dl->tx_hist[0].msg->data, msg->l3h, msg->len);
	dl->tx_hist[0].more = 0;
	/* set Vs to 0, because it is used as index when resending DISC */
	dl->v_send = 0;
	
	/* Set states */
	dl->own_busy = dl->peer_busy = 0;
	dl->retrans_ctr = 0;
	lapd_dl_newstate(dl, LAPD_STATE_DISC_SENT);

	/* Tramsmit and start T200 */
	dl->send_ph_data_req(&nctx, msg);
	lapd_start_t200(dl);

	return 0;
}

/* request release of link in idle state */
static int lapd_rel_req_idle(struct osmo_dlsap_prim *dp,
	struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	struct msgb *msg = dp->oph.msg;

	msgb_free(msg);

	/* send notification to L3 */
	return send_dl_simple(PRIM_DL_REL, PRIM_OP_CONFIRM, &dl->lctx);
}

/* statefull handling for DL SAP messages from L3 */
static struct l2downstate {
	uint32_t	states;
	int		prim, op;
	const char 	*name;
	int		(*rout) (struct osmo_dlsap_prim *dp,
					struct lapd_msg_ctx *lctx);
} l2downstatelist[] = {
	/* create and send UI command */
	{ALL_STATES,
	 PRIM_DL_UNIT_DATA, PRIM_OP_REQUEST, 
	 "DL-UNIT-DATA-REQUEST", lapd_udata_req},

	/* create and send SABM command */
	{SBIT(LAPD_STATE_IDLE),
	 PRIM_DL_EST, PRIM_OP_REQUEST,
	 "DL-ESTABLISH-REQUEST", lapd_est_req},

	/* create and send I command */
	{SBIT(LAPD_STATE_MF_EST) |
	 SBIT(LAPD_STATE_TIMER_RECOV),
	 PRIM_DL_DATA, PRIM_OP_REQUEST,
	 "DL-DATA-REQUEST", lapd_data_req},

	/* suspend datalink */
	{SBIT(LAPD_STATE_MF_EST) |
	 SBIT(LAPD_STATE_TIMER_RECOV),
	 PRIM_DL_SUSP, PRIM_OP_REQUEST,
	 "DL-SUSPEND-REQUEST", lapd_susp_req},

	/* create and send SABM command (resume) */
	{SBIT(LAPD_STATE_MF_EST) |
	 SBIT(LAPD_STATE_TIMER_RECOV),
	 PRIM_DL_RES, PRIM_OP_REQUEST,
	 "DL-RESUME-REQUEST", lapd_res_req},

	/* create and send SABM command (reconnect) */
	{SBIT(LAPD_STATE_IDLE) |
	 SBIT(LAPD_STATE_MF_EST) |
	 SBIT(LAPD_STATE_TIMER_RECOV),
	 PRIM_DL_RECON, PRIM_OP_REQUEST,
	 "DL-RECONNECT-REQUEST", lapd_res_req},

	/* create and send DISC command */
	{SBIT(LAPD_STATE_SABM_SENT) |
	 SBIT(LAPD_STATE_MF_EST) |
	 SBIT(LAPD_STATE_TIMER_RECOV) |
	 SBIT(LAPD_STATE_DISC_SENT),
	 PRIM_DL_REL, PRIM_OP_REQUEST,
	 "DL-RELEASE-REQUEST", lapd_rel_req},

	/* release in idle state */
	{SBIT(LAPD_STATE_IDLE),
	 PRIM_DL_REL, PRIM_OP_REQUEST,
	 "DL-RELEASE-REQUEST", lapd_rel_req_idle},
};

#define L2DOWNSLLEN \
	(sizeof(l2downstatelist) / sizeof(struct l2downstate))

int lapd_recv_dlsap(struct osmo_dlsap_prim *dp, struct lapd_msg_ctx *lctx)
{
	struct lapd_datalink *dl = lctx->dl;
	int i, supported = 0;
	struct msgb *msg = dp->oph.msg;
	int rc;

	/* find function for current state and message */
	for (i = 0; i < L2DOWNSLLEN; i++) {
		if (dp->oph.primitive == l2downstatelist[i].prim
		 && dp->oph.operation == l2downstatelist[i].op) {
			supported = 1;
		 	if ((SBIT(dl->state) & l2downstatelist[i].states))
				break;
		}
	}
	if (!supported) {
		LOGP(DLLAPD, LOGL_NOTICE, "Message %u/%u unsupported.\n",
			dp->oph.primitive, dp->oph.operation);
		msgb_free(msg);
		return 0;
	}
	if (i == L2DOWNSLLEN) {
		LOGP(DLLAPD, LOGL_NOTICE, "Message %u/%u unhandled at this "
			"state %s.\n", dp->oph.primitive, dp->oph.operation,
			lapd_state_names[dl->state]);
		msgb_free(msg);
		return 0;
	}

	LOGP(DLLAPD, LOGL_INFO, "Message %s received in state %s\n",
		l2downstatelist[i].name, lapd_state_names[dl->state]);

	rc = l2downstatelist[i].rout(dp, lctx);

	return rc;
}

