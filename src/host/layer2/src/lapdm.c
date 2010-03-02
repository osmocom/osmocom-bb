/* GSM LAPDm (TS 04.06) implementation */

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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <osmocore/timer.h>
#include <osmocore/msgb.h>
#include <osmocore/tlv.h>
#include <osmocore/utils.h>
#include <osmocore/rsl.h>
#include <osmocore/protocol/gsm_04_08.h>
#include <osmocore/protocol/gsm_08_58.h>

#include <osmocom/debug.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/osmocom_layer2.h>
#include <osmocom/lapdm.h>

#include <l1a_l23_interface.h>

/* TS 04.06 Figure 4 / Section 3.2 */
#define LAPDm_LPD_NORMAL  0
#define LAPDm_LPD_SMSCB	  1
#define LAPDm_SAPI_NORMAL 0
#define LAPDm_SAPI_SMS	  3
#define LAPDm_ADDR(lpd, sapi, cr) (((lpd & 0x3) << 5) | ((sapi & 0x7) << 2) | ((cr & 0x1) << 1) | 0x1)

#define LAPDm_ADDR_SAPI(addr) ((addr >> 2) & 0x7)

/* TS 04.06 Table 3 / Section 3.4.3 */
#define LAPDm_CTRL_I(nr, ns, p)	(((nr & 0x7) << 5) | ((p & 0x1) << 4) | ((ns & 0x7) << 1))
#define LAPDm_CTRL_S(nr, s, p)	(((nr & 0x7) << 5) | ((p & 0x1) << 4) | ((s & 0x3) << 2) | 0x1)
#define LAPDm_CTRL_U(u, p)	(((u & 0x1c) << 5) | ((p & 0x1) << 4) | ((u & 0x3) << 2) | 0x3)

#define LAPDm_CTRL_is_I(ctrl)	((ctrl & 0x1) == 0)
#define LAPDm_CTRL_is_S(ctrl)	((ctrl & 0x3) == 1)
#define LAPDm_CTRL_is_U(ctrl)	((ctrl & 0x3) == 3)

#define LAPDm_CTRL_U_BITS(ctrl)	(((ctrl & 0xC) >> 2) | (ctrl & 0xE) >> 3)
#define LAPDm_CTRL_PF_BIT(ctrl)	((ctrl >> 4) & 0x1)

#define LAPDm_CTRL_S_BITS(ctrl)	((ctrl & 0xC) >> 2)

#define LAPDm_CTRL_I_Ns(ctrl)	((ctrl & 0xE) >> 1)
#define LAPDm_CTRL_I_Nr(ctrl)	((ctrl & 0xE0) >> 5)

/* TS 04.06 Table 4 / Section 3.8.1 */
#define LAPDm_U_SABM	0x7
#define LAPDm_U_DM	0x3
#define LAPDm_U_UI	0x0
#define LAPDm_U_DISC	0x8
#define LAPDm_U_UA	0xC

#define LAPDm_S_RR	0x0
#define LAPDm_S_RNR	0x1
#define LAPDm_S_REJ	0x2

#define LAPDm_LEN(len)	((len << 2) | 0x1)

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
/* FIXME: this depends on chan type */
#define N200	N200_TR_SACCH

#define CR_MS2BS_CMD	0
#define CR_MS2BS_RESP	1
#define CR_BS2MS_CMD	1
#define CR_BS2MS_RESP	0

/* Set T200 to 1 Second (OpenBTS uses 900ms) */
#define T200	1, 0

enum lapdm_format {
	LAPDm_FMT_A,
	LAPDm_FMT_B,
	LAPDm_FMT_Bbis,
	LAPDm_FMT_Bter,
	LAPDm_FMT_B4,
};

struct lapdm_msg_ctx {
	struct lapdm_datalink *dl;
	enum lapdm_format lapdm_fmt;
	uint8_t chan_nr;
	uint8_t link_id;
	uint8_t addr;
	uint8_t ctrl;
};

static void lapdm_t200_cb(void *data);

/* UTILITY FUNCTIONS */

static inline uint8_t inc_mod8(uint8_t x)
{
	return (x + 1) % 8;
}

static void lapdm_dl_init(struct lapdm_datalink *dl,
			  struct lapdm_entity *entity)
{
	memset(dl, 0, sizeof(*dl));
	dl->t200.data = dl;
	dl->t200.cb = &lapdm_t200_cb;
	dl->entity = entity;
}

void lapdm_init(struct lapdm_entity *le, struct osmocom_ms *ms)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(le->datalink); i++)
		lapdm_dl_init(&le->datalink[i], le);

	le->ms = ms;
}

static struct lapdm_datalink *datalink_for_sapi(struct lapdm_entity *le, uint8_t sapi)
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

/* remove the L2 header from a MSGB */
static inline unsigned char *msgb_pull_l2h(struct msgb *msg)
{
	unsigned char *ret = msgb_pull(msg, msg->l3h - msg->l2h);
	msg->l2h = NULL;
	return ret;
}

/* Take a Bbis format message from L1 and create RSLms UNIT DATA IND */
static int send_rslms_rll_l3(uint8_t msg_type, struct lapdm_msg_ctx *mctx,
			     struct msgb *msg)
{
	/* Add the L3 header */
	rsl_rll_push_l3(msg, msg_type, mctx->chan_nr, mctx->link_id);

	/* send off the RSLms message to L3 */
	return rslms_sendmsg(msg, mctx->dl->entity->ms);
}

static int send_rslms_rll_simple(uint8_t msg_type, struct lapdm_msg_ctx *mctx)
{
	struct msgb *msg;

	msg = rsl_rll_simple(msg_type, mctx->chan_nr, mctx->link_id);

	/* send off the RSLms message to L3 */
	return rslms_sendmsg(msg, mctx->dl->entity->ms);
}

static int check_length_ind(uint8_t length_ind)
{
	if (!(length_ind & 0x01)) {
		/* G.4.1 If the EL bit is set to "0", an MDL-ERROR-INDICATION
		 * primitive with cause "frame not implemented" is sent to the
		 * mobile management entity. */
		printf("we don't support multi-octet length\n");
		return -EINVAL;
	}
	if (length_ind & 0x02) {
		printf("we don't support LAPDm fragmentation yet\n");
		return -EINVAL;
	}
	return 0;
}

/* Timer callback on T200 expiry */
static void lapdm_t200_cb(void *data)
{
	struct lapdm_datalink *dl = data;

	switch (dl->state) {
	case LAPDm_STATE_SABM_SENT:
		/* 5.4.1.3 */
		if (dl->retrans_ctr >= N200_EST_REL + 1) {
			/* FIXME: send RELEASE INDICATION to L3 */
			dl->retrans_ctr = 0;
			dl->state = LAPDm_STATE_IDLE;
		}
		/* FIXME: retransmit SABM command */

		/* increment re-transmission counter */
		dl->retrans_ctr++;
		/* restart T200 (PH-READY-TO-SEND) */
		bsc_schedule_timer(&dl->t200, T200);
		break;
	case LAPDm_STATE_MF_EST:
		/* 5.5.7 */
		dl->retrans_ctr = 0;
		dl->state = LAPDm_STATE_TIMER_RECOV;
	case LAPDm_STATE_TIMER_RECOV:
		dl->retrans_ctr++;
		if (dl->retrans_ctr < N200) {
			/* FIXME: retransmit I frame (V_s-1) with P=1 */
			/* FIXME: send appropriate supervision frame with P=1 */
			/* restart T200 (PH-READY-TO-SEND) */
			bsc_schedule_timer(&dl->t200, T200);
		} else {
			/* FIXME: send ERROR INDICATION to L3 */
		}
		break;
	default:
		printf("T200 expired in dl->state %u\n", dl->state);
	}
}

static int lapdm_send_rr(struct lapdm_msg_ctx *mctx, uint8_t f_bit)
{
	uint8_t sapi = mctx->link_id & 7;
	struct msgb *msg = msgb_alloc(24, "LAPDm RR");
	uint8_t *data = msgb_put(msg, 3);

	data[0] = LAPDm_ADDR(LAPDm_LPD_NORMAL, sapi, CR_MS2BS_RESP);
	data[1] = LAPDm_CTRL_S(mctx->dl->V_recv, LAPDm_S_RR, f_bit);
	data[2] = LAPDm_LEN(0);

	return tx_ph_data_req(mctx->dl->entity->ms, msg, mctx->chan_nr, mctx->link_id);
}

/* L1 -> L2 */

/* Receive a LAPDm S (Unnumbered) message from L1 */
static int lapdm_rx_u(struct msgb *msg, struct lapdm_msg_ctx *mctx)
{
	struct lapdm_datalink *dl = mctx->dl;
	uint8_t length;
	int rc;

	switch (LAPDm_CTRL_U_BITS(mctx->ctrl)) {
	case LAPDm_U_SABM:
		printf("SABM ");
		/* Must be Format B */
		rc = check_length_ind(msg->l2h[2]);
		if (rc < 0)
			return rc;
		length = msg->l2h[2] >> 2;
		/* FIXME: G.4.5 check */
		if (dl->state == LAPDm_STATE_MF_EST) {
			if (length == 0) {
				/* FIXME: re-establishment procedure 5.6 */
			} else {
				/* FIXME: check for contention resoultion */
				printf("SABM command, multiple frame established state\n");
				return 0;
			}
		}
		if (length == 0) {
			/* 5.4.1.2 Normal establishment procedures */
			rc = send_rslms_rll_simple(RSL_MT_EST_IND, mctx);
		} else {
			/* 5.4.1.4 Contention resolution establishment */
			msg->l3h = msg->l2h + 3;
			msgb_pull_l2h(msg);
			rc = send_rslms_rll_l3(RSL_MT_EST_IND, mctx, msg);
		}
		if (rc == 0)
			dl->state = LAPDm_STATE_SABM_SENT;
		break;
	case LAPDm_U_DM:
		printf("DM ");
		if (!LAPDm_CTRL_PF_BIT(mctx->ctrl)) {
			/* 5.4.1.2 DM responses with the F bit set to "0" shall be ignored. */
			return 0;
		}
		switch (dl->state) {
		case LAPDm_STATE_IDLE:
			/* 5.4.5 all other frame types shall be discarded */
			printf("state=IDLE (discarding) ");
			return 0;
		case LAPDm_STATE_MF_EST:
			if (LAPDm_CTRL_PF_BIT(mctx->ctrl) == 1)
				printf("unsolicited DM resposne ");
			else
				printf("unsolicited DM resposne, multiple frame established state ");
			return 0;
		case LAPDm_STATE_TIMER_RECOV:
			/* DM is normal in case PF = 1 */
			if (LAPDm_CTRL_PF_BIT(mctx->ctrl) == 0) {
				printf("unsolicited DM resposne, multiple frame established state ");
				return 0;
			}
			break;
		}
		/* reset T200 */
		bsc_del_timer(&dl->t200);
		rc = send_rslms_rll_simple(RSL_MT_REL_IND, mctx);
		break;
	case LAPDm_U_UI:
		printf("UI ");
		if (mctx->lapdm_fmt == LAPDm_FMT_B4) {
			length = N201_B4;
			msg->l3h = msg->l2h + 2;
		} else {
			rc = check_length_ind(msg->l2h[2]);
			if (rc < 0)
				return rc;
			length = msg->l2h[2] >> 2;
			msg->l3h = msg->l2h + 3;
		}
		/* do some length checks */
		if (length == 0) {
			/* 5.3.3 UI frames received with the length indicator set to "0" shall be ignored */
			printf("length=0 (discarding) ");
			return 0;
		}
		/* FIXME: G.4.5 check */
		switch (LAPDm_ADDR_SAPI(mctx->ctrl)) {
		case LAPDm_SAPI_NORMAL:
		case LAPDm_SAPI_SMS:
			break;
		default:
			/* 5.3.3 UI frames with invalid SAPI values shall be discarded */
			printf("sapi=%u (discarding) ", LAPDm_ADDR_SAPI(mctx->ctrl));
			return 0;
		}
		msgb_pull_l2h(msg);
		rc = send_rslms_rll_l3(RSL_MT_UNIT_DATA_IND, mctx, msg);
		break;
	case LAPDm_U_DISC:
		printf("DISC ");
		length = msg->l2h[2] >> 2;
		if (length > 0 || msg->l2h[2] & 0x02) {
			/* G.4.4 If a DISC or DM frame is received with L>0 or
			 * with the M bit set to "1", an MDL-ERROR-INDICATION
			 * primitive with cause "U frame with incorrect
			 * parameters" is sent to the mobile management entity. */
			printf("U frame iwth incorrect parameters ");
			return -EIO;
		}
		switch (dl->state) {
		case LAPDm_STATE_IDLE:
			/* FIXME: send DM with F=P */
			break;
		default:
			/* FIXME */
			break;
		}
		break;
	case LAPDm_U_UA:
		printf("UA ");
		/* FIXME: G.4.5 check */
		if (!LAPDm_CTRL_PF_BIT(mctx->ctrl)) {
			/* 5.4.1.2 A UA response with the F bit set to "0" shall be ignored. */
			printf("F=0 (discarding) ");
			return 0;
		}
		switch (dl->state) {
		case LAPDm_STATE_SABM_SENT:
			break;
		case LAPDm_STATE_IDLE:
			/* 5.4.5 all other frame types shall be discarded */
		default:
			printf("unsolicited UA response! (discarding) ");
			return 0;
		}
		/* reset Timer T200 */
		bsc_del_timer(&dl->t200);
		/* set Vs, Vr and Va to 0 */
		dl->V_send = dl->V_recv = dl->V_ack = 0;
		/* enter multiple-frame-established state */
		dl->state = LAPDm_STATE_MF_EST;
		/* send notification to L3 */
		rc = send_rslms_rll_simple(RSL_MT_EST_CONF, mctx);
		break;
	}
	return rc;
}

/* Receive a LAPDm S (Supervisory) message from L1 */
static int lapdm_rx_s(struct msgb *msg, struct lapdm_msg_ctx *mctx)
{
	struct lapdm_datalink *dl = mctx->dl;
	uint8_t length;

	length = msg->l2h[2] >> 2;
	if (length > 0 || msg->l2h[2] & 0x02) {
		/* G.4.3 If a supervisory frame is received with L>0 or
		 * with the M bit set to "1", an MDL-ERROR-INDICATION
		 * primitive with cause "S frame with incorrect
		 * parameters" is sent to the mobile management entity. */
		return -EIO;
	}
	switch (dl->state) {
	case LAPDm_STATE_IDLE:
		/* FIXME: if P=1, respond DM with F=1 (5.2.2) */
		/* 5.4.5 all other frame types shall be discarded */
		break;
	}
	switch (LAPDm_CTRL_S_BITS(mctx->ctrl)) {
	case LAPDm_S_RR:
		/* FIXME */
		break;
	case LAPDm_S_RNR:
		/* FIXME */
		break;
	case LAPDm_S_REJ:
		/* FIXME */
		break;
	}
	return 0;
}

/* Receive a LAPDm I (Information) message from L1 */
static int lapdm_rx_i(struct msgb *msg, struct lapdm_msg_ctx *mctx)
{
	struct lapdm_datalink *dl = mctx->dl;
	uint8_t nr = LAPDm_CTRL_I_Nr(mctx->ctrl);
	uint8_t ns = LAPDm_CTRL_I_Ns(mctx->ctrl);
	uint8_t length;
	int rc;

	length = msg->l2h[2] >> 2;
	/* FIXME: check for length > N201 */
	if (length == 0) {
		/* G.4.2 If the length indicator of an I frame is set
		 * to a numerical value L>N201 or L=0, an MDL-ERROR-INDICATION
		 * primitive with cause "I frame with incorrect length"
		 * is sent to the mobile management entity. */
		return -EIO;
	}
	/* FIXME: G.4.2 If the numerical value of L is L<N201 and the M
	 * bit is set to "1", then an MDL-ERROR-INDICATION primitive with
	 * cause "I frame with incorrect use of M bit" is sent to the
	 * mobile management entity. */
	switch (dl->state) {
	case LAPDm_STATE_IDLE:
		/* FIXME: if P=1, respond DM with F=1 (5.2.2) */
		/* 5.4.5 all other frame types shall be discarded */
		break;
	}

	/* processing of Nr, Ns and P fields */
	if (ns == dl->V_recv) {
		/* FIXME: check for M bit! */
		dl->V_recv = inc_mod8(dl->V_recv);

		/* send a DATA INDICATION to L3 */
		msg->l3h = msg->l2h + 2;
		msgb_pull_l2h(msg);
		rc = send_rslms_rll_l3(RSL_MT_DATA_IND, mctx, msg);
	} else {
		printf("N(s) sequence error: Ns=%u, V_recv=%u ", ns, dl->V_recv);
		/* FIXME: 5.7.1: N(s) sequence error */
		/* discard data */
		return -EIO;
	}

	/* Check for P bit */
	if (LAPDm_CTRL_PF_BIT(mctx->ctrl)) {
		/* 5.5.2.1 */
		/* FIXME: check ifwe are in own receiver busy */
		/* FIXME: Send RR with F=1 */
		rc = lapdm_send_rr(mctx, 1);
	} else {
		/* 5.5.2.2 */
		/* FIXME: check ifwe are in own receiver busy */
		//if (we_have_I_frame_pending) {
		if (0) {
			/* FIXME: send that I frame with Nr=Vr */
		} else {
			/* Send RR with F=0 */
			rc = lapdm_send_rr(mctx, 0);
		}
	}

	if (dl->state != LAPDm_STATE_TIMER_RECOV) {
		/* When not in the timer recovery condition, the data
		 * link layer entity shall reset the timer T200 on
		 * receipt of a valid I frame with N(R) higher than V(A) */
		if (nr > dl->V_ack) {
			/* FIXME: 5.5.3.1 Note 1 + 2 */
			bsc_del_timer(&dl->t200);
			/* FIXME: if there are outstanding I frames
			 * still unacknowledged, the data link layer
			 * entity shall set timer T200 */
		}

		/* FIXME: 5.7.4: N(R) sequence error */
		/* N(R) is called valid, if and only if (N(R)-V(A)) mod 8 <= (V(S)-V(A)) mod 8. */
	}

	/* V(A) shall be set to the value of N(R) */
	dl->V_ack = LAPDm_CTRL_I_Nr(mctx->ctrl);

	return rc;
}

/* Receive a LAPDm message from L1 */
static int lapdm_ph_data_ind(struct msgb *msg, struct lapdm_msg_ctx *mctx)
{
	int rc;

	if (LAPDm_CTRL_is_U(mctx->ctrl))
		rc = lapdm_rx_u(msg, mctx);
	else if (LAPDm_CTRL_is_S(mctx->ctrl))
		rc = lapdm_rx_s(msg, mctx);
	else if (LAPDm_CTRL_is_I(mctx->ctrl))
		rc = lapdm_rx_i(msg, mctx);
	else {
		printf("unknown LAPDm format ");
		rc = -EINVAL;
	}
	return rc;
}

/* input into layer2 (from layer 1) */
int l2_ph_data_ind(struct msgb *msg, struct lapdm_entity *le, struct l1ctl_info_dl *l1i)
{
	uint8_t cbits = l1i->chan_nr >> 3;
	uint8_t sapi = l1i->link_id & 7;
	struct lapdm_msg_ctx mctx;
	int rc;

	printf("l2_ph_data_ind() ");
	/* when we reach here, we have a msgb with l2h pointing to the raw
	 * 23byte mac block. The l1h has already been purged. */

	mctx.dl = datalink_for_sapi(le, sapi);
	mctx.chan_nr = l1i->chan_nr;
	mctx.link_id = l1i->link_id;
	mctx.addr = mctx.ctrl = 0;

	/* check for L1 chan_nr/link_id and determine LAPDm hdr format */
	if (cbits == 0x10 || cbits == 0x12) {
		/* Format Bbis is used on BCCH and CCCH(PCH, NCH and AGCH) */
		mctx.lapdm_fmt = LAPDm_FMT_Bbis;
		printf("fmt=Bbis ");
	} else {
		if (mctx.link_id & 0x40) {
			/* It was received from network on SACCH, thus
			 * lapdm_fmt must be B4 */
			mctx.lapdm_fmt = LAPDm_FMT_B4;
			printf("fmt=B4 ");
			/* SACCH frames have a two-byte L1 header that OsmocomBB L1 doesn't
			 * strip */
			msg->l2h += 2;
		} else {
			mctx.lapdm_fmt = LAPDm_FMT_B;
			printf("fmt=B ");
		}
	}

	switch (mctx.lapdm_fmt) {
	case LAPDm_FMT_A:
	case LAPDm_FMT_B:
	case LAPDm_FMT_B4:
		mctx.addr = msg->l2h[0];
		if (!(mctx.addr & 0x01)) {
			printf("we don't support multibyte addresses (discarding)\n");
			return -EINVAL;
		}
		mctx.ctrl = msg->l2h[1];
		/* obtain SAPI from address field */
		mctx.link_id |= LAPDm_ADDR_SAPI(mctx.addr);
		rc = lapdm_ph_data_ind(msg, &mctx);
		break;
	case LAPDm_FMT_Bter:
		/* FIXME */
		break;
	case LAPDm_FMT_Bbis:
		/* directly pass up to layer3 */
		printf("UI ");
		msg->l3h = msg->l2h;
		msgb_pull_l2h(msg);
		rc = send_rslms_rll_l3(RSL_MT_UNIT_DATA_IND, &mctx, msg);
		break;
	}
	printf("\n");

	return rc;
}

/* L3 -> L2 / RSLMS -> LAPDm */

/* L3 requests establishment of data link */
static int rslms_rx_rll_est_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	uint8_t chan_nr = rllh->chan_nr;
	uint8_t link_id = rllh->link_id;
	uint8_t sapi = rllh->link_id & 7;
	struct tlv_parsed tv;
	uint8_t len;
	uint8_t *lapdh;

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));
	if (TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		/* contention resolution establishment procedure */
		if (dl->state != LAPDm_STATE_IDLE) {
			/* 5.4.1.4: The data link layer shall, however, ignore any such
			 * service request if it is not in the idle state when the
			 * request is received. */
			msgb_free(msg);
			return 0;
		}
		if (sapi != 0) {
			/* According to clause 6, the contention resolution
			 * procedure is only permitted with SAPI value 0 */
			msgb_free(msg);
			return -EINVAL;
		}
		/* transmit a SABM command with the P bit set to "1". The SABM
		 * command shall contain the layer 3 message unit */
		len = LAPDm_LEN(TLVP_LEN(&tv, RSL_IE_L3_INFO));

		/* FIXME: store information field in dl entity */
	} else {
		/* normal establishment procedure */
		len = LAPDm_LEN(0);
	}

	/* Remove RLL header from msgb */
	msgb_pull_l2h(msg);

	/* Push LAPDm header on msgb */
	lapdh = msgb_push(msg, 3);
	lapdh[0] = LAPDm_ADDR(LAPDm_LPD_NORMAL, sapi, CR_MS2BS_CMD);
	lapdh[1] = LAPDm_CTRL_U(LAPDm_U_SABM, 1);
	lapdh[2] = len;

	/* Tramsmit and start T200 */
	bsc_schedule_timer(&dl->t200, T200);
	return tx_ph_data_req(dl->entity->ms, msg, chan_nr, link_id);
}

/* L3 requests transfer of unnumbered information */
static int rslms_rx_rll_udata_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	uint8_t chan_nr = rllh->chan_nr;
	uint8_t link_id = rllh->link_id;
	uint8_t sapi = link_id & 7;
	struct tlv_parsed tv;
	uint8_t *lapdh;

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));

	/* Remove RLL header from msgb */
	msgb_pull_l2h(msg);

	/* Push LAPDm header on msgb */
	lapdh = msgb_push(msg, 3);
	lapdh[0] = LAPDm_ADDR(LAPDm_LPD_NORMAL, sapi, CR_MS2BS_CMD);
	lapdh[1] = LAPDm_CTRL_U(LAPDm_U_SABM, 1);
	lapdh[2] = LAPDm_LEN(TLVP_LEN(&tv, RSL_IE_L3_INFO));

	/* Tramsmit and start T200 */
	bsc_schedule_timer(&dl->t200, T200);
	return tx_ph_data_req(dl->entity->ms, msg, chan_nr, link_id);
}

/* L3 requests transfer of acknowledged information */
static int rslms_rx_rll_data_req(struct msgb *msg, struct lapdm_datalink *dl)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	uint8_t chan_nr = rllh->chan_nr;
	uint8_t link_id = rllh->link_id;
	uint8_t sapi = rllh->link_id & 7;
	struct tlv_parsed tv;
	uint8_t *lapdh;

	switch (dl->state) {
	case LAPDm_STATE_MF_EST:
		break;
	default:
		printf("refusing RLL DATA REQ during DL state %u\n", dl->state);
		return -EIO;
		break;
	}

	/* FIXME: check if the layer3 message length exceeds N201 */

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));

	/* Remove the RSL/RLL header */
	msgb_pull_l2h(msg);

	/* Push the LAPDm header */
	lapdh = msgb_put(msg, 3);
	lapdh[0] = LAPDm_ADDR(LAPDm_LPD_NORMAL, sapi, CR_MS2BS_CMD);
	lapdh[1] = LAPDm_CTRL_I(dl->V_recv, dl->V_send, 0);
	lapdh[2] = LAPDm_LEN(TLVP_LEN(&tv, RSL_IE_L3_INFO));

	/* The value of the send state variable V(S) shall be incremented by 1
	 * at the end of the transmission of the I frame */
	dl->V_send = inc_mod8(dl->V_send);

	/* If timer T200 is not running at the time right before transmitting a
	 * frame, when the PH-READY-TO-SEND primitive is received from the
	 * physical layer., it shall be set. */
	if (!bsc_timer_pending(&dl->t200))
		bsc_schedule_timer(&dl->t200, T200);

	/* FIXME: If the send state variable V(S) is equal to V(A) plus k
	 * (where k is the maximum number of outstanding I frames - see
	 * subclause 5.8.4), the data link layer entity shall not transmit any
	 * new I frames, but shall retransmit an I frame as a result
	 * of the error recovery procedures as described in subclauses 5.5.4 and
	 * 5.5.7. */

	return tx_ph_data_req(dl->entity->ms, msg, chan_nr, link_id);
}

/* incoming RSLms RLL message from L3 */
static int rslms_rx_rll(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int rc = 0;
	uint8_t sapi = rllh->link_id & 7;
	struct lapdm_entity *le;
	struct lapdm_datalink *dl;

	if (rllh->link_id & 0x40)
		le = &ms->lapdm_acch;
	else
		le = &ms->lapdm_dcch;
	dl = datalink_for_sapi(le, sapi);

	switch (rllh->c.msg_type) {
	case RSL_MT_UNIT_DATA_REQ:
		/* create and send UI command */
		rc = rslms_rx_rll_udata_req(msg, dl);
		break;
	case RSL_MT_EST_REQ:
		/* create and send SABM command */
		rc = rslms_rx_rll_est_req(msg, dl);
		break;
	case RSL_MT_DATA_REQ:
		/* create and send I command */
		rc = rslms_rx_rll_data_req(msg, dl);
		break;
	case RSL_MT_REL_REQ:
		/* FIXME: create and send DISC command */
	default:
		printf("unknown RLL message type 0x%02x\n",
			rllh->c.msg_type);
		break;
	}

	return rc;
}

/* input into layer2 (from layer 3) */
int rslms_recvmsg(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = rslms_rx_rll(msg, ms);
		break;
	default:
		printf("unknown RSLms message discriminator 0x%02x",
			rslh->msg_discr);
		msgb_free(msg);
		return -EINVAL;
	}

	return rc;
}

