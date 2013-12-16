/* Synchronous part of GSM Layer 1: API to Layer2+ */

/* (C) 2010 by Holger Hans Peter Freyther <zecke@selfish.org>
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

#define DEBUG

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <debug.h>
#include <byteorder.h>

#include <asm/system.h>

#include <osmocom/core/msgb.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <comm/sercomm.h>

#include <layer1/sync.h>
#include <layer1/async.h>
#include <layer1/mframe_sched.h>
#include <layer1/prim.h>
#include <layer1/tpu_window.h>
#include <layer1/sched_gsmtime.h>

#include <abb/twl3025.h>
#include <rf/trf6151.h>
#include <calypso/sim.h>
#include <calypso/dsp.h>

#include <l1ctl_proto.h>

/* the size we will allocate struct msgb* for HDLC */
#define L3_MSG_HEAD 4
#define L3_MSG_DATA 200
#define L3_MSG_SIZE (L3_MSG_HEAD + sizeof(struct l1ctl_hdr) + L3_MSG_DATA)

void (*l1a_l23_tx_cb)(struct msgb *msg) = NULL;

void l1_queue_for_l2(struct msgb *msg)
{
	if (l1a_l23_tx_cb) {
		l1a_l23_tx_cb(msg);
		return;
	}
	/* forward via serial for now */
	sercomm_sendmsg(SC_DLCI_L1A_L23, msg);
}

enum mf_type {
	MFNONE,
	MF51,
	MF26ODD,
	MF26EVEN
};
static uint32_t chan_nr2mf_task_mask(uint8_t chan_nr, uint8_t neigh_mode)
{
	uint8_t cbits = chan_nr >> 3;
	uint8_t tn = chan_nr & 0x7;
	uint8_t lch_idx;
	enum mframe_task master_task = 0;
	uint32_t neigh_task = 0;
	enum mf_type multiframe = 0;

	if (cbits == 0x01) {
		lch_idx = 0;
		master_task = (tn & 1) ? MF_TASK_TCH_F_ODD : MF_TASK_TCH_F_EVEN;
		multiframe = (tn & 1) ? MF26ODD : MF26EVEN;
	} else if ((cbits & 0x1e) == 0x02) {
		lch_idx = cbits & 0x1;
		master_task = MF_TASK_TCH_H_0 + lch_idx;
		multiframe = (lch_idx & 1) ? MF26ODD : MF26EVEN;
	} else if ((cbits & 0x1c) == 0x04) {
		lch_idx = cbits & 0x3;
		master_task = MF_TASK_SDCCH4_0 + lch_idx;
		multiframe = MF51;
	} else if ((cbits & 0x18) == 0x08) {
		lch_idx = cbits & 0x7;
		master_task = MF_TASK_SDCCH8_0 + lch_idx;
		multiframe = MF51;
#if 0
	} else if (cbits == 0x10) {
		/* FIXME: when to do extended BCCH? */
		master_task = MF_TASK_BCCH_NORM;
	} else if (cbits == 0x11 || cbits == 0x12) {
		/* FIXME: how to decide CCCH norm/extd? */
		master_task = MF_TASK_BCCH_CCCH;
#endif
	}
	switch (neigh_mode) {
	case NEIGH_MODE_PM:
		switch (multiframe) {
		case MF51:
			neigh_task = (1 << MF_TASK_NEIGH_PM51);
			break;
		case MF26EVEN:
			neigh_task = (1 << MF_TASK_NEIGH_PM26E);
			break;
		case MF26ODD:
			neigh_task = (1 << MF_TASK_NEIGH_PM26O);
			break;
		}
		break;
	}
	return (1 << master_task) | neigh_task;
}

static int  chan_nr2dchan_type(uint8_t chan_nr)
{
	uint8_t cbits = chan_nr >> 3;

	if (cbits == 0x01) {
		return GSM_DCHAN_TCH_F;
	} else if ((cbits & 0x1e) == 0x02) {
		return GSM_DCHAN_TCH_H;
	} else if ((cbits & 0x1c) == 0x04) {
		return GSM_DCHAN_SDCCH_4;
	} else if ((cbits & 0x18) == 0x08) {
		return GSM_DCHAN_SDCCH_8;
	}
	return GSM_DCHAN_UNKNOWN;
}

static int chan_nr_is_tch(uint8_t chan_nr)
{
	return ((chan_nr >> 3) == 0x01 ||		/* TCH/F */
		((chan_nr >> 3) & 0x1e) == 0x02);	/* TCH/H */
}

static void audio_set_enabled(uint8_t tch_mode, uint8_t audio_mode)
{
	if (tch_mode == GSM48_CMODE_SIGN) {
		twl3025_unit_enable(TWL3025_UNIT_VUL, 0);
		twl3025_unit_enable(TWL3025_UNIT_VDL, 0);
	} else {
		twl3025_unit_enable(TWL3025_UNIT_VUL,
		                    !!(audio_mode & AUDIO_TX_MICROPHONE));
		twl3025_unit_enable(TWL3025_UNIT_VDL,
		                    !!(audio_mode & AUDIO_RX_SPEAKER));
	}
}

struct msgb *l1ctl_msgb_alloc(uint8_t msg_type)
{
	struct msgb *msg;
	struct l1ctl_hdr *l1h;

	msg = msgb_alloc_headroom(L3_MSG_SIZE, L3_MSG_HEAD, "l1ctl");
	if (!msg) {
		while (1) {
			puts("OOPS. Out of buffers...\n");
		}

		return NULL;
	}
	l1h = (struct l1ctl_hdr *) msgb_put(msg, sizeof(*l1h));
	l1h->msg_type = msg_type;
	l1h->flags = 0;

	msg->l1h = (uint8_t *)l1h;

	return msg;
}

struct msgb *l1_create_l2_msg(int msg_type, uint32_t fn, uint16_t snr,
			      uint16_t arfcn)
{
	struct l1ctl_info_dl *dl;
	struct msgb *msg = l1ctl_msgb_alloc(msg_type);

	dl = (struct l1ctl_info_dl *) msgb_put(msg, sizeof(*dl));
	dl->frame_nr = htonl(fn);
	dl->snr = snr;
	dl->band_arfcn = htons(arfcn);

	return msg;
}

/* receive a L1CTL_FBSB_REQ from L23 */
static void l1ctl_rx_fbsb_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_fbsb_req *sync_req = (struct l1ctl_fbsb_req *) l1h->data;

	if (sizeof(*sync_req) > msg->len) {
		printf("Short sync msg. %u\n", msg->len);
		return;
	}

	printd("L1CTL_FBSB_REQ (arfcn=%u, flags=0x%x)\n",
		ntohs(sync_req->band_arfcn), sync_req->flags);

	/* reset scheduler and hardware */
	l1s_reset();

	/* pre-set the CCCH mode */
	l1s.serving_cell.ccch_mode = sync_req->ccch_mode;

	printd("Starting FCCH Recognition\n");
	l1s_fbsb_req(1, sync_req);
}

/* receive a L1CTL_DM_EST_REQ from L23 */
static void l1ctl_rx_dm_est_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_dm_est_req *est_req = (struct l1ctl_dm_est_req *) ul->payload;

	printd("L1CTL_DM_EST_REQ (arfcn=%u, chan_nr=0x%02x, tsc=%u)\n",
		ntohs(est_req->h0.band_arfcn), ul->chan_nr, est_req->tsc);

	/* disable neighbour cell measurement of C0 TS 0 */
	mframe_disable(MF_TASK_NEIGH_PM51_C0T0);

	/* configure dedicated channel state */
	l1s.dedicated.type = chan_nr2dchan_type(ul->chan_nr);
	l1s.dedicated.tsc  = est_req->tsc;
	l1s.dedicated.tn   = ul->chan_nr & 0x7;
	l1s.dedicated.h    = est_req->h;

	if (est_req->h) {
		int i;
		l1s.dedicated.h1.hsn  = est_req->h1.hsn;
		l1s.dedicated.h1.maio = est_req->h1.maio;
		l1s.dedicated.h1.n    = est_req->h1.n;
		for (i=0; i<est_req->h1.n; i++)
			l1s.dedicated.h1.ma[i] = ntohs(est_req->h1.ma[i]);
	} else {
		l1s.dedicated.h0.arfcn = ntohs(est_req->h0.band_arfcn);
	}

	/* TCH config */
	if (chan_nr_is_tch(ul->chan_nr)) {
		/* Mode */
		l1a_tch_mode_set(est_req->tch_mode);
		l1a_audio_mode_set(est_req->audio_mode);

		/* Sync */
		l1s.tch_sync = 1;	/* can be set without locking */

		/* Audio path */
		audio_set_enabled(est_req->tch_mode, est_req->audio_mode);
	}

	/* figure out which MF tasks to enable */
	l1a_mftask_set(chan_nr2mf_task_mask(ul->chan_nr, NEIGH_MODE_PM));
}

/* receive a L1CTL_DM_FREQ_REQ from L23 */
static void l1ctl_rx_dm_freq_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_dm_freq_req *freq_req =
			(struct l1ctl_dm_freq_req *) ul->payload;

	printd("L1CTL_DM_FREQ_REQ (arfcn=%u, tsc=%u)\n",
		ntohs(freq_req->h0.band_arfcn), freq_req->tsc);

	/* configure dedicated channel state */
	l1s.dedicated.st_tsc  = freq_req->tsc;
	l1s.dedicated.st_h    = freq_req->h;

	if (freq_req->h) {
		int i;
		l1s.dedicated.st_h1.hsn  = freq_req->h1.hsn;
		l1s.dedicated.st_h1.maio = freq_req->h1.maio;
		l1s.dedicated.st_h1.n    = freq_req->h1.n;
		for (i=0; i<freq_req->h1.n; i++)
			l1s.dedicated.st_h1.ma[i] = ntohs(freq_req->h1.ma[i]);
	} else {
		l1s.dedicated.st_h0.arfcn = ntohs(freq_req->h0.band_arfcn);
	}

	l1a_freq_req(ntohs(freq_req->fn));
}

/* receive a L1CTL_CRYPTO_REQ from L23 */
static void l1ctl_rx_crypto_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_crypto_req *cr = (struct l1ctl_crypto_req *) ul->payload;
	uint8_t key_len = msg->len - sizeof(*l1h) - sizeof(*ul) - sizeof(*cr);

	printd("L1CTL_CRYPTO_REQ (algo=A5/%u, len=%u)\n", cr->algo, key_len);

	if (cr->algo && key_len != 8) {
		printd("L1CTL_CRYPTO_REQ -> Invalid key\n");
		return;
	}

	dsp_load_ciph_param(cr->algo, cr->key);
}

/* receive a L1CTL_DM_REL_REQ from L23 */
static void l1ctl_rx_dm_rel_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;

	printd("L1CTL_DM_REL_REQ\n");
	l1a_mftask_set(0);
	l1s.dedicated.type = GSM_DCHAN_NONE;
	l1a_txq_msgb_flush(&l1s.tx_queue[L1S_CHAN_MAIN]);
	l1a_txq_msgb_flush(&l1s.tx_queue[L1S_CHAN_SACCH]);
	l1a_txq_msgb_flush(&l1s.tx_queue[L1S_CHAN_TRAFFIC]);
	l1a_meas_msgb_set(NULL);
	dsp_load_ciph_param(0, NULL);
	l1a_tch_mode_set(GSM48_CMODE_SIGN);
	audio_set_enabled(GSM48_CMODE_SIGN, 0);
	l1s.neigh_pm.n = 0;
}

/* receive a L1CTL_PARAM_REQ from L23 */
static void l1ctl_rx_param_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_par_req *par_req = (struct l1ctl_par_req *) ul->payload;

	printd("L1CTL_PARAM_REQ (ta=%d, tx_power=%d)\n", par_req->ta,
		par_req->tx_power);

	l1s.ta = par_req->ta;
	l1s.tx_power = par_req->tx_power;
}

/* receive a L1CTL_RACH_REQ from L23 */
static void l1ctl_rx_rach_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_rach_req *rach_req = (struct l1ctl_rach_req *) ul->payload;

	printd("L1CTL_RACH_REQ (ra=0x%02x, offset=%d combined=%d)\n",
		rach_req->ra, ntohs(rach_req->offset), rach_req->combined);

	l1a_rach_req(ntohs(rach_req->offset), rach_req->combined,
		rach_req->ra);
}

/* receive a L1CTL_DATA_REQ from L23 */
static void l1ctl_rx_data_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_data_ind *data_ind = (struct l1ctl_data_ind *) ul->payload;
	struct llist_head *tx_queue;

	printd("L1CTL_DATA_REQ (link_id=0x%02x)\n", ul->link_id);

	msg->l3h = data_ind->data;
	if (ul->link_id & 0x40) {
		struct gsm48_hdr *gh = (struct gsm48_hdr *)(data_ind->data + 5);
		if (gh->proto_discr == GSM48_PDISC_RR
		 && gh->msg_type == GSM48_MT_RR_MEAS_REP) {
			printd("updating measurement report\n");
			l1a_meas_msgb_set(msg);
			return;
		}
		tx_queue = &l1s.tx_queue[L1S_CHAN_SACCH];
	} else
		tx_queue = &l1s.tx_queue[L1S_CHAN_MAIN];

	printd("ul=%p, ul->payload=%p, data_ind=%p, data_ind->data=%p l3h=%p\n",
		ul, ul->payload, data_ind, data_ind->data, msg->l3h);

	l1a_txq_msgb_enq(tx_queue, msg);
}

/* receive a L1CTL_PM_REQ from L23 */
static void l1ctl_rx_pm_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_pm_req *pm_req = (struct l1ctl_pm_req *) l1h->data;

	switch (pm_req->type) {
	case 1:
		l1s.pm.mode = 1;
		l1s.pm.range.arfcn_start =
				ntohs(pm_req->range.band_arfcn_from);
		l1s.pm.range.arfcn_next =
				ntohs(pm_req->range.band_arfcn_from);
		l1s.pm.range.arfcn_end =
				ntohs(pm_req->range.band_arfcn_to);
		printf("L1CTL_PM_REQ start=%u end=%u\n",
			l1s.pm.range.arfcn_start, l1s.pm.range.arfcn_end);
		break;
	}
	l1s_reset_hw(); /* must reset, otherwise measurement results are delayed */
	l1s_pm_test(1, l1s.pm.range.arfcn_next);
}

/* Transmit a L1CTL_RESET_IND or L1CTL_RESET_CONF */
void l1ctl_tx_reset(uint8_t msg_type, uint8_t reset_type)
{
	struct msgb *msg = l1ctl_msgb_alloc(msg_type);
	struct l1ctl_reset *reset_resp;
	reset_resp = (struct l1ctl_reset *)
				msgb_put(msg, sizeof(*reset_resp));
	reset_resp->type = reset_type;

	l1_queue_for_l2(msg);
}

/* receive a L1CTL_RESET_REQ from L23 */
static void l1ctl_rx_reset_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_reset *reset_req =
				(struct l1ctl_reset *) l1h->data;

	switch (reset_req->type) {
	case L1CTL_RES_T_FULL:
		printf("L1CTL_RESET_REQ: FULL!\n");
		l1s_reset();
		l1s_reset_hw();
		audio_set_enabled(GSM48_CMODE_SIGN, 0);
		l1ctl_tx_reset(L1CTL_RESET_CONF, reset_req->type);
		break;
	case L1CTL_RES_T_SCHED:
		printf("L1CTL_RESET_REQ: SCHED!\n");
		l1ctl_tx_reset(L1CTL_RESET_CONF, reset_req->type);
		sched_gsmtime_reset();
		break;
	default:
		printf("unknown L1CTL_RESET_REQ type\n");
		break;
	}
}

/* Transmit a L1CTL_CCCH_MODE_CONF */
static void l1ctl_tx_ccch_mode_conf(uint8_t ccch_mode)
{
	struct msgb *msg = l1ctl_msgb_alloc(L1CTL_CCCH_MODE_CONF);
	struct l1ctl_ccch_mode_conf *mode_conf;
	mode_conf = (struct l1ctl_ccch_mode_conf *)
				msgb_put(msg, sizeof(*mode_conf));
	mode_conf->ccch_mode = ccch_mode;

	l1_queue_for_l2(msg);
}

/* receive a L1CTL_CCCH_MODE_REQ from L23 */
static void l1ctl_rx_ccch_mode_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_ccch_mode_req *ccch_mode_req =
		(struct l1ctl_ccch_mode_req *) l1h->data;
	uint8_t ccch_mode = ccch_mode_req->ccch_mode;

	/* pre-set the CCCH mode */
	l1s.serving_cell.ccch_mode = ccch_mode;

	/* Update task */
	mframe_disable(MF_TASK_CCCH_COMB);
	mframe_disable(MF_TASK_CCCH);

	if (ccch_mode == CCCH_MODE_COMBINED)
		mframe_enable(MF_TASK_CCCH_COMB);
	else if (ccch_mode == CCCH_MODE_NON_COMBINED)
		mframe_enable(MF_TASK_CCCH);

	l1ctl_tx_ccch_mode_conf(ccch_mode);
}

/* Transmit a L1CTL_TCH_MODE_CONF */
static void l1ctl_tx_tch_mode_conf(uint8_t tch_mode, uint8_t audio_mode)
{
	struct msgb *msg = l1ctl_msgb_alloc(L1CTL_TCH_MODE_CONF);
	struct l1ctl_tch_mode_conf *mode_conf;
	mode_conf = (struct l1ctl_tch_mode_conf *)
				msgb_put(msg, sizeof(*mode_conf));
	mode_conf->tch_mode = tch_mode;
	mode_conf->audio_mode = audio_mode;

	l1_queue_for_l2(msg);
}

/* receive a L1CTL_TCH_MODE_REQ from L23 */
static void l1ctl_rx_tch_mode_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_tch_mode_req *tch_mode_req =
		(struct l1ctl_tch_mode_req *) l1h->data;
	uint8_t tch_mode = tch_mode_req->tch_mode;
	uint8_t audio_mode = tch_mode_req->audio_mode;

	printd("L1CTL_TCH_MODE_REQ (tch_mode=0x%02x audio_mode=0x%02x)\n",
		tch_mode, audio_mode);
	tch_mode = l1a_tch_mode_set(tch_mode);
	audio_mode = l1a_audio_mode_set(audio_mode);

	audio_set_enabled(tch_mode, audio_mode);

	l1s.tch_sync = 1; /* Needed for audio to work */

	l1ctl_tx_tch_mode_conf(tch_mode, audio_mode);
}

/* receive a L1CTL_NEIGH_PM_REQ from L23 */
static void l1ctl_rx_neigh_pm_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_neigh_pm_req *pm_req =
		(struct l1ctl_neigh_pm_req *) l1h->data;
	int i;

	/* reset list in order to prevent race condition */
	l1s.neigh_pm.n = 0; /* atomic */
	l1s.neigh_pm.second = 0;
	/* now reset pointer and fill list */
	l1s.neigh_pm.pos = 0;
	l1s.neigh_pm.running = 0;
	for (i = 0; i < pm_req->n; i++) {
		l1s.neigh_pm.band_arfcn[i] = ntohs(pm_req->band_arfcn[i]);
		l1s.neigh_pm.tn[i] = pm_req->tn[i];
	}
	printf("L1CTL_NEIGH_PM_REQ new list with %u entries\n", pm_req->n);
	l1s.neigh_pm.n = pm_req->n; /* atomic */

	/* on C0 enable PM on frame 51 */
	if (l1s.dedicated.type == GSM_DCHAN_NONE)
		mframe_enable(MF_TASK_NEIGH_PM51_C0T0);
}

/* receive a L1CTL_TRAFFIC_REQ from L23 */
static void l1ctl_rx_traffic_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *) msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *) l1h->data;
	struct l1ctl_traffic_req *tr = (struct l1ctl_traffic_req *) ul->payload;
	int num = 0;

	/* printd("L1CTL_TRAFFIC_REQ\n"); */ /* Very verbose, can overwelm serial */

	msg->l2h = tr->data;

	num = l1a_txq_msgb_count(&l1s.tx_queue[L1S_CHAN_TRAFFIC]);
	if (num >= 4) {
		printd("dropping traffic frame\n");
		msgb_free(msg);
		return;
	}

	l1a_txq_msgb_enq(&l1s.tx_queue[L1S_CHAN_TRAFFIC], msg);
}

static void l1ctl_sim_req(struct msgb *msg)
{
	uint16_t len = msg->len - sizeof(struct l1ctl_hdr);
	uint8_t *data = msg->data + sizeof(struct l1ctl_hdr);

#if 1 /* for debugging only */
	{
		int i;
		printf("SIM Request (%u): ", len);
		for (i = 0; i < len; i++)
			printf("%02x ", data[i]);
		puts("\n");
	}
#endif

   sim_apdu(len, data);
}

static struct llist_head l23_rx_queue = LLIST_HEAD_INIT(l23_rx_queue);

/* callback from SERCOMM when L2 sends a message to L1 */
void l1a_l23_rx(uint8_t dlci, struct msgb *msg)
{
	unsigned long flags;

	local_firq_save(flags);
	msgb_enqueue(&l23_rx_queue, msg);
	local_irq_restore(flags);
}

void l1a_l23_handler(void)
{
	struct msgb *msg;
	struct l1ctl_hdr *l1h;
	unsigned long flags;

	local_firq_save(flags);
	msg = msgb_dequeue(&l23_rx_queue);
	local_irq_restore(flags);
	if (!msg)
		return;

	l1h = (struct l1ctl_hdr *) msg->data;

#if 0
	{
		int i;
		printf("l1a_l23_rx_cb (%u): ", msg->len);
		for (i = 0; i < msg->len; i++)
			printf("%02x ", msg->data[i]);
		puts("\n");
	}
#endif

	msg->l1h = msg->data;

	if (sizeof(*l1h) > msg->len) {
		printf("l1a_l23_cb: Short message. %u\n", msg->len);
		goto exit_msgbfree;
	}

	switch (l1h->msg_type) {
	case L1CTL_FBSB_REQ:
		l1ctl_rx_fbsb_req(msg);
		break;
	case L1CTL_DM_EST_REQ:
		l1ctl_rx_dm_est_req(msg);
		break;
	case L1CTL_DM_REL_REQ:
		l1ctl_rx_dm_rel_req(msg);
		break;
	case L1CTL_PARAM_REQ:
		l1ctl_rx_param_req(msg);
		break;
	case L1CTL_DM_FREQ_REQ:
		l1ctl_rx_dm_freq_req(msg);
		break;
	case L1CTL_CRYPTO_REQ:
		l1ctl_rx_crypto_req(msg);
		break;
	case L1CTL_RACH_REQ:
		l1ctl_rx_rach_req(msg);
		break;
	case L1CTL_DATA_REQ:
		l1ctl_rx_data_req(msg);
		/* we have to keep the msgb, not free it! */
		goto exit_nofree;
	case L1CTL_PM_REQ:
		l1ctl_rx_pm_req(msg);
		break;
	case L1CTL_RESET_REQ:
		l1ctl_rx_reset_req(msg);
		break;
	case L1CTL_CCCH_MODE_REQ:
		l1ctl_rx_ccch_mode_req(msg);
		break;
	case L1CTL_TCH_MODE_REQ:
		l1ctl_rx_tch_mode_req(msg);
		break;
	case L1CTL_NEIGH_PM_REQ:
		l1ctl_rx_neigh_pm_req(msg);
		break;
	case L1CTL_TRAFFIC_REQ:
		l1ctl_rx_traffic_req(msg);
		/* we have to keep the msgb, not free it! */
		goto exit_nofree;
	case L1CTL_SIM_REQ:
		l1ctl_sim_req(msg);
		break;
	}

exit_msgbfree:
	msgb_free(msg);
exit_nofree:
	return;
}

void l1a_l23api_init(void)
{
	sercomm_register_rx_cb(SC_DLCI_L1A_L23, l1a_l23_rx);
}

