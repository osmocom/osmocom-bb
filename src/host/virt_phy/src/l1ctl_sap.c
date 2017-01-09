/* L1CTL SAP implementation.  */

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <stdio.h>
#include <l1ctl_proto.h>
#include <netinet/in.h>

#include "virtual_um.h"
#include "l1ctl_sock.h"
#include "virt_l1_model.h"
#include "l1ctl_sap.h"
#include "logging.h"

static struct l1_model_ms *l1_model_ms = NULL;

/**
 * @brief Init the SAP.
 */
void l1ctl_sap_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
}

/**
 * @brief L1CTL handler called for received messages from L23.
 *
 * Enqueues the message into the rx queue.
 */
void l1ctl_sap_rx_from_l23_inst_cb(struct l1ctl_sock_inst *lsi, struct msgb *msg)
{
	if (msg) {
		DEBUGP(DL1C, "Message incoming from layer 2: %s\n",
		                osmo_hexdump(msg->data, msg->len));
		l1ctl_sap_handler(msg);
	}
}
/**
 * @see l1ctl_sap_rx_from_l23_cb(struct l1ctl_sock_inst *lsi, struct msgb *msg).
 */
void l1ctl_sap_rx_from_l23(struct msgb *msg)
{
	l1ctl_sap_rx_from_l23_inst_cb(l1_model_ms->lsi, msg);
}

/**
 * @brief Send a l1ctl message to layer 23.
 *
 * This will forward the message as it is to the upper layer.
 */
void l1ctl_sap_tx_to_l23_inst(struct l1ctl_sock_inst *lsi, struct msgb *msg)
{
	uint16_t *len;
	/* prepend 16bit length before sending */
	len = (uint16_t *) msgb_push(msg, sizeof(*len));
	*len = htons(msg->len - sizeof(*len));

	if(l1ctl_sock_write_msg(lsi, msg) == -1 ) {
		//DEBUGP(DL1C, "Error writing to layer2 socket");
	}
}

/**
 * @see void l1ctl_sap_tx_to_l23(struct l1ctl_sock_inst *lsi, struct msgb *msg).
 */
void l1ctl_sap_tx_to_l23(struct msgb *msg)
{
	l1ctl_sap_tx_to_l23_inst(l1_model_ms->lsi, msg);
}

/**
 * @brief Allocates a msgb with set l1ctl header and room for a l3 header.
 *
 * @param [in] msg_type L1CTL primitive message type set to l1ctl_hdr.
 * @return the allocated message.
 *
 * The message looks as follows:
 * # headers
 * [l1ctl_hdr]		: initialized. msgb->l1h points here
 * [spare-bytes]	: L3_MSG_HEAD bytes reserved for l3 header
 * # data
 * [spare-bytes]	: L3_MSG_DATA bytes reserved for data. msgb->tail points here. msgb->data points here.
 */
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
	l1h = (struct l1ctl_hdr *)msgb_put(msg, sizeof(*l1h));
	l1h->msg_type = msg_type;
	l1h->flags = 0;

	msg->l1h = (uint8_t *)l1h;

	return msg;
}

/**
 * @brief Allocates a msgb with set l1ctl header and room for a l3 header and puts l1ctl_info_dl to the msgb data.
 *
 * @param [in] msg_type L1CTL primitive message type set to l1ctl_hdr.
 * @param [in] fn framenumber put into l1ctl_info_dl.
 * @param [in] snr time slot number put into l1ctl_info_dl.
 * @param [in] arfcn arfcn put into l1ctl_info_dl.
 * @return the allocated message.
 *
 * The message looks as follows:
 * # headers
 * [l1ctl_hdr]		: initialized. msgb->l1h points here
 * [spare-bytes]	: L3_MSG_HEAD bytes reserved for l3 header
 * # data
 * [l1ctl_info_dl]	: initialized with params. msgb->data points here.
 * [spare-bytes]	: L3_MSG_DATA bytes reserved for data. msgb->tail points here.
 */
struct msgb *l1ctl_create_l2_msg(int msg_type, uint32_t fn, uint16_t snr,
                                 uint16_t arfcn)
{
	struct l1ctl_info_dl *dl;
	struct msgb *msg = l1ctl_msgb_alloc(msg_type);

	dl = (struct l1ctl_info_dl *)msgb_put(msg, sizeof(*dl));
	dl->frame_nr = htonl(fn);
	dl->snr = snr;
	dl->band_arfcn = htons(arfcn);

	return msg;
}

/**
 * @brief General handler for incoming L1CTL messages from layer 2/3.
 *
 * This handler will dequeue the rx queue (if !empty) and call the specific routine for the dequeued l1ctl message.
 *
 */
void l1ctl_sap_handler(struct msgb *msg)
{
//	struct msgb *msg;
	struct l1ctl_hdr *l1h;
	unsigned long flags;

	if (!msg)
		return;

	l1h = (struct l1ctl_hdr *)msg->data;

	if (sizeof(*l1h) > msg->len) {
		LOGP(DL1C, LOGL_NOTICE, "Short message. %u\n", msg->len);
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
		l1ctl_rx_sim_req(msg);
		break;
	}

	exit_msgbfree: msgb_free(msg);
	exit_nofree: return;
}

/***************************************************************
 * L1CTL RX ROUTINES *******************************************
 ***************************************************************/

/**
 * @brief Handler for received L1CTL_FBSB_REQ from L23.
 *
 * -- frequency burst synchronisation burst request --
 *
 * @param [in] msg the received message.
 *
 * Transmit frequency control and synchronisation bursts on FCCH and SCH to calibrate transceiver and search for base stations.
 * Sync to a given arfcn.
 *
 * Note: Not needed for virtual physical layer.
 * TODO: Could be used to bind/connect to different virtual_bts sockets with a arfcn-socket mapping.
 */
void l1ctl_rx_fbsb_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_fbsb_req *sync_req = (struct l1ctl_fbsb_req *)l1h->data;

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_FBSB_REQ (arfcn=%u, flags=0x%x)\n",
	                ntohs(sync_req->band_arfcn), sync_req->flags);

	l1ctl_tx_fbsb_conf(0, ntohs(sync_req->band_arfcn));
}

/**
 * @brief Handler for received L1CTL_DM_EST_REQ from L23.
 *
 * -- dedicated mode established request --
 *
 * @param [in] msg the received message.
 *
 * Handle state change from idle to dedicated mode.
 *
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_dm_est_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_dm_est_req *est_req =
	                (struct l1ctl_dm_est_req *)ul->payload;

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_DM_EST_REQ (arfcn=%u, chan_nr=0x%02x, tsc=%u)\n",
	                ntohs(est_req->h0.band_arfcn), ul->chan_nr,
	                est_req->tsc);

//	/* disable neighbour cell measurement of C0 TS 0 */
//	mframe_disable(MF_TASK_NEIGH_PM51_C0T0);
//
//	/* configure dedicated channel state */
//	l1s.dedicated.type = chan_nr2dchan_type(ul->chan_nr);
//	l1s.dedicated.tsc  = est_req->tsc;
//	l1s.dedicated.tn   = ul->chan_nr & 0x7;
//	l1s.dedicated.h    = est_req->h;
//
//	if (est_req->h) {
//		int i;
//		l1s.dedicated.h1.hsn  = est_req->h1.hsn;
//		l1s.dedicated.h1.maio = est_req->h1.maio;
//		l1s.dedicated.h1.n    = est_req->h1.n;
//		for (i=0; i<est_req->h1.n; i++)
//			l1s.dedicated.h1.ma[i] = ntohs(est_req->h1.ma[i]);
//	} else {
//		l1s.dedicated.h0.arfcn = ntohs(est_req->h0.band_arfcn);
//	}
//
//	/* TCH config */
//	if (chan_nr_is_tch(ul->chan_nr)) {
//		/* Mode */
//		l1a_tch_mode_set(est_req->tch_mode);
//		l1a_audio_mode_set(est_req->audio_mode);
//
//		/* Sync */
//		l1s.tch_sync = 1;	/* can be set without locking */
//
//		/* Audio path */
//		audio_set_enabled(est_req->tch_mode, est_req->audio_mode);
//	}
//
//	/* figure out which MF tasks to enable */
//	l1a_mftask_set(chan_nr2mf_task_mask(ul->chan_nr, NEIGH_MODE_PM));
}

/**
 * @brief Handler for received L1CTL_DM_FREQ_REQ from L23.
 *
 * -- dedicated mode frequency request --
 *
 * @param [in] msg the received message.
 *
 * Handle frequency change in dedicated mode. E.g. used for frequency hopping.
 *
 * Note: Not needed for virtual physical layer.
 */
void l1ctl_rx_dm_freq_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_dm_freq_req *freq_req =
	                (struct l1ctl_dm_freq_req *)ul->payload;

	DEBUGP(DL1C,
	                "Received and ignored from l23 - L1CTL_DM_FREQ_REQ (arfcn=%u, tsc=%u)\n",
	                ntohs(freq_req->h0.band_arfcn), freq_req->tsc);
}

/**
 * @brief Handler for received L1CTL_CRYPTO_REQ from L23.
 *
 * -- cryptographic request --
 *
 * @param [in] msg the received message.
 *
 * Configure the key and algorithm used for cryptographic operations in the DSP (Digital Signal Processor).
 *
 * Note: in the virtual physical layer the cryptographic operations are not handled in the DSP.
 *
 * TODO: Implement cryptographic operations for virtual um!
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_crypto_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_crypto_req *cr = (struct l1ctl_crypto_req *)ul->payload;
	uint8_t key_len = msg->len - sizeof(*l1h) - sizeof(*ul) - sizeof(*cr);

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_CRYPTO_REQ (algo=A5/%u, len=%u)\n",
	                cr->algo, key_len);

//	if (cr->algo && key_len != 8) {
//		DEBUGP(DL1C, "L1CTL_CRYPTO_REQ -> Invalid key\n");
//		return;
//	}
//
//	dsp_load_ciph_param(cr->algo, cr->key);
}

/**
 * @brief Handler for received L1CTL_DM_REL_REQ from L23.
 *
 * -- dedicated mode release request --
 *
 * @param [in] msg the received message.
 *
 * Handle state change from dedicated to idle mode. Flush message buffers of dedicated channel.
 *
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_dm_rel_req(struct msgb *msg)
{
//	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;

	DEBUGP(DL1C, "Received and ignored from l23 - L1CTL_DM_REL_REQ\n");
//	l1a_mftask_set(0);
//	l1s.dedicated.type = GSM_DCHAN_NONE;
//	l1a_txq_msgb_flush(&l1s.tx_queue[L1S_CHAN_MAIN]);
//	l1a_txq_msgb_flush(&l1s.tx_queue[L1S_CHAN_SACCH]);
//	l1a_txq_msgb_flush(&l1s.tx_queue[L1S_CHAN_TRAFFIC]);
//	l1a_meas_msgb_set(NULL);
//	dsp_load_ciph_param(0, NULL);
//	l1a_tch_mode_set(GSM48_CMODE_SIGN);
//	audio_set_enabled(GSM48_CMODE_SIGN, 0);
//	l1s.neigh_pm.n = 0;
}

/**
 * @brief Handler for received L1CTL_PARAM_REQ from L23.
 *
 * -- parameter request --
 *
 * @param [in] msg the received message.
 *
 * Configure transceiver parameters timing advance value and sending power.
 *
 * Note: Not needed for virtual physical layer.
 */
void l1ctl_rx_param_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_par_req *par_req = (struct l1ctl_par_req *)ul->payload;

	DEBUGP(DL1C,
	                "Received and ignored from l23 - L1CTL_PARAM_REQ (ta=%d, tx_power=%d)\n",
	                par_req->ta, par_req->tx_power);
}

/**
 * @brief Handler for received L1CTL_RACH_REQ from L23.
 *
 * -- random access channel request --
 *
 * @param [in] msg the received message.
 *
 * Transmit RACH request on RACH.
 *
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_rach_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_rach_req *rach_req = (struct l1ctl_rach_req *)ul->payload;

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_RACH_REQ (ra=0x%02x, offset=%d combined=%d)\n",
	                rach_req->ra, ntohs(rach_req->offset),
	                rach_req->combined);

//	l1a_rach_req(ntohs(rach_req->offset), rach_req->combined,
//		rach_req->ra);
}

/**
 * @brief Handler for received L1CTL_DATA_REQ from L23.
 *
 * -- data request --
 *
 * @param [in] msg the received message.
 *
 * Transmit message on a signalling channel. FACCH/SDCCH or SACCH depending on the headers set link id (TS 8.58 - 9.3.2).
 *
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_data_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_data_ind *data_ind = (struct l1ctl_data_ind *)ul->payload;
	struct llist_head *tx_queue;

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_DATA_REQ (link_id=0x%02x)\n",
	                ul->link_id);

//	msg->l3h = data_ind->data;
//	if (ul->link_id & 0x40) {
//		struct gsm48_hdr *gh = (struct gsm48_hdr *)(data_ind->data + 5);
//		if (gh->proto_discr == GSM48_PDISC_RR
//		 && gh->msg_type == GSM48_MT_RR_MEAS_REP) {
//			DEBUGP(DL1C, "updating measurement report\n");
//			l1a_meas_msgb_set(msg);
//			return;
//		}
//		tx_queue = &l1s.tx_queue[L1S_CHAN_SACCH];
//	} else
//		tx_queue = &l1s.tx_queue[L1S_CHAN_MAIN];
//
//	DEBUGP(DL1C, "ul=%p, ul->payload=%p, data_ind=%p, data_ind->data=%p l3h=%p\n",
//		ul, ul->payload, data_ind, data_ind->data, msg->l3h);
//
//	l1a_txq_msgb_enq(tx_queue, msg);
}

/**
 * @brief Handler for received L1CTL_PM_REQ from L23.
 *
 * -- power measurement request --
 *
 * @param [in] msg the received message.
 *
 * Process power measurement for a given range of arfcns to calculate signal power and connection quality.
 *
 * Note: We do not need to calculate that for the virtual physical layer, but l23 apps can expect a response. So this response is mocked here.
 */
void l1ctl_rx_pm_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_pm_req *pm_req = (struct l1ctl_pm_req *)l1h->data;
	struct msgb *resp_msg = l1ctl_msgb_alloc(L1CTL_PM_CONF);
	uint16_t arfcn_next;
	// convert to host order
	pm_req->range.band_arfcn_from = ntohs(pm_req->range.band_arfcn_from);
	pm_req->range.band_arfcn_to = ntohs(pm_req->range.band_arfcn_to);

	DEBUGP(DL1C, "Received from l23 - L1CTL_PM_REQ TYPE=%u, FROM=%d, TO=%d\n",
	                pm_req->type, pm_req->range.band_arfcn_from, pm_req->range.band_arfcn_to);

	for(arfcn_next = pm_req->range.band_arfcn_from; arfcn_next <= pm_req->range.band_arfcn_to; ++arfcn_next) {
		struct l1ctl_pm_conf *pm_conf = (struct l1ctl_pm_conf *)msgb_put(resp_msg, sizeof(*pm_conf));
		pm_conf->band_arfcn = htons(arfcn_next);
		// rxlev 63 is great, 0 is bad the two values are probably min and max
		pm_conf->pm[0] = 63;
		pm_conf->pm[1] = 63;
		if(arfcn_next == pm_req->range.band_arfcn_to) {
			struct l1ctl_hdr *resp_l1h = resp_msg->l1h;
			resp_l1h->flags |= L1CTL_F_DONE;
		}
		// no more space in msgb, flush to l2
		if(msgb_tailroom(resp_msg) < sizeof(*pm_conf)) {
			l1ctl_sap_tx_to_l23(resp_msg);
			resp_msg = l1ctl_msgb_alloc(L1CTL_PM_CONF);
		}
	}
	if(resp_msg) {
		l1ctl_sap_tx_to_l23(resp_msg);
	}
}

/**
 * @brief Handler for received L1CTL_RESET_REQ from L23.
 *
 * -- reset request --
 *
 * @param [in] msg the received message.
 *
 * Reset layer 1 (state machine, scheduler, transceiver) depending on the reset type.
 *
 */
void l1ctl_rx_reset_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_reset *reset_req = (struct l1ctl_reset *)l1h->data;

	switch (reset_req->type) {
	case L1CTL_RES_T_FULL:
		DEBUGP(DL1C,
		                "Received and handled from l23 - L1CTL_RESET_REQ (type=FULL)\n");
//		l1s_reset();
//		l1s_reset_hw();
//		audio_set_enabled(GSM48_CMODE_SIGN, 0);
		l1ctl_tx_reset(L1CTL_RESET_CONF, reset_req->type);
		break;
	case L1CTL_RES_T_SCHED:
		DEBUGP(DL1C,
		                "Received and handled from l23 - L1CTL_RESET_REQ (type=SCHED)\n");
//		sched_gsmtime_reset();
		l1ctl_tx_reset(L1CTL_RESET_CONF, reset_req->type);
		break;
	default:
		LOGP(DL1C, LOGL_ERROR,
		                "Received and ignored from l23 - L1CTL_RESET_REQ (type=unknown)\n");
		break;
	}
}

/**
 * @brief Handler for received L1CTL_CCCH_MODE_REQ from L23.
 *
 * -- common control channel mode request --
 *
 * @param [in] msg the received message.
 *
 * Configure CCCH combined / non-combined mode.
 *
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_ccch_mode_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_ccch_mode_req *ccch_mode_req =
	                (struct l1ctl_ccch_mode_req *)l1h->data;
	uint8_t ccch_mode = ccch_mode_req->ccch_mode;

	DEBUGP(DL1C, "Received and handled from l23 - L1CTL_CCCH_MODE_REQ\n");

	l1_model_ms->state->serving_cell.ccch_mode = ccch_mode;

	// check if more has to be done here

	l1ctl_tx_ccch_mode_conf(ccch_mode);

//	/* pre-set the CCCH mode */
//	l1s.serving_cell.ccch_mode = ccch_mode;
//
//	/* Update task */
//	mframe_disable(MF_TASK_CCCH_COMB);
//	mframe_disable(MF_TASK_CCCH);
//
//	if (ccch_mode == CCCH_MODE_COMBINED)
//		mframe_enable(MF_TASK_CCCH_COMB);
//	else if (ccch_mode == CCCH_MODE_NON_COMBINED)
//		mframe_enable(MF_TASK_CCCH);
//
//	l1ctl_tx_ccch_mode_conf(ccch_mode);
}

/**
 * @brief Handler for received L1CTL_TCH_MODE_REQ from L23.
 *
 * -- traffic channel mode request --
 *
 * @param [in] msg the received message.
 *
 * Configure TCH mode and audio mode.
 *
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_tch_mode_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_tch_mode_req *tch_mode_req =
	                (struct l1ctl_tch_mode_req *)l1h->data;
	uint8_t tch_mode = tch_mode_req->tch_mode;
	uint8_t audio_mode = tch_mode_req->audio_mode;

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_TCH_MODE_REQ (tch_mode=0x%02x audio_mode=0x%02x)\n",
	                tch_mode, audio_mode);
//	tch_mode = l1a_tch_mode_set(tch_mode);
//	audio_mode = l1a_audio_mode_set(audio_mode);
//
//	audio_set_enabled(tch_mode, audio_mode);
//
//	l1s.tch_sync = 1; /* Needed for audio to work */
//
//	l1ctl_tx_tch_mode_conf(tch_mode, audio_mode);
}

/**
 * @brief Handler for received L1CTL_NEIGH_PM_REQ from L23.
 *
 * -- neighbor power measurement request --
 *
 * @param [in] msg the received message.
 *
 * Update the maintained list of neighbor cells used in neighbor cell power measurement.
 * The neighbor cell description is one of the info messages sent by the BTS on BCCH.
 * This method will also enable neighbor measurement in the multiframe scheduler.
 *
 * Note: Not needed for virtual physical layer.
 */
void l1ctl_rx_neigh_pm_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_neigh_pm_req *pm_req =
	                (struct l1ctl_neigh_pm_req *)l1h->data;

	DEBUGP(DL1C,
	                "Received and ignored from l23 - L1CTL_NEIGH_PM_REQ new list with %u entries\n",
	                pm_req->n);
}

/**
 * @brief Handler for received L1CTL_TRAFFIC_REQ from L23.
 *
 * -- traffic request --
 *
 * @param [in] msg the received message.
 *
 * Enqueue the message (traffic frame) to the L1 state machine's transmit queue.
 * Will drop the traffic frame at queue sizes >= 4.
 *
 * TODO: Implement this handler routine!
 */
void l1ctl_rx_traffic_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_traffic_req *tr = (struct l1ctl_traffic_req *)ul->payload;
	int num = 0;

	DEBUGP(DL1C, "Received and handled from l23 - L1CTL_TRAFFIC_REQ\n");

//	msg->l2h = tr->data;

//	num = l1a_txq_msgb_count(&l1s.tx_queue[L1S_CHAN_TRAFFIC]);
//	if (num >= 4) {
//		DEBUGP(DL1C, "dropping traffic frame\n");
//		msgb_free(msg);
//		return;
//	}
//
//	l1a_txq_msgb_enq(&l1s.tx_queue[L1S_CHAN_TRAFFIC], msg);
}

/**
 * @brief Handler for received L1CTL_SIM_REQ from L23.
 *
 * -- sim request --
 *
 * @param [in] msg the received message.
 *
 * Forward and a sim request to the SIM APDU.
 *
 * Note: Not needed for virtual layer. Please configure layer23 application to use test-sim implementation.
 * ms <x>
 * --------
 * sim test
 * test-sim
 *  imsi <xxxxxxxxxxxxxxx>
 *  ki comp128 <xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx xx>
 * --------
 */
void l1ctl_rx_sim_req(struct msgb *msg)
{
	uint16_t len = msg->len - sizeof(struct l1ctl_hdr);
	uint8_t *data = msg->data + sizeof(struct l1ctl_hdr);

	DEBUGP(DL1C,
	                "Received and ignored from l23 - SIM Request length: %u, data: %s: ",
	                len, osmo_hexdump(data, sizeof(data)));

}

/***************************************************************
 * L1CTL TX ROUTINES *******************************************
 ***************************************************************/

/**
 * @brief Transmit L1CTL_RESET_IND or L1CTL_RESET_CONF to layer 23.
 *
 * -- reset indication / confirm --
 *
 * @param [in] msg_type L1CTL primitive message type.
 * @param [in] reset_type reset type (full, boot or just scheduler reset).
 */
void l1ctl_tx_reset(uint8_t msg_type, uint8_t reset_type)
{
	struct msgb *msg = l1ctl_msgb_alloc(msg_type);
	struct l1ctl_reset *reset_resp;
	reset_resp = (struct l1ctl_reset *)msgb_put(msg, sizeof(*reset_resp));
	reset_resp->type = reset_type;

	DEBUGP(DL1C, "Sending to l23 - %s (reset_type: %u)\n",
	       	       getL1ctlPrimName(msg_type), reset_type);
	l1ctl_sap_tx_to_l23(msg);
}

/**
 * @brief Transmit L1CTL msg of a given type to layer 23.
 *
 * @param [in] msg_type L1CTL primitive message type.
 */
void l1ctl_tx_msg(uint8_t msg_type)
{
	struct msgb *msg = l1ctl_msgb_alloc(msg_type);
	DEBUGP(DL1C, "Sending to l23 - %s\n", getL1ctlPrimName(msg_type));
	l1ctl_sap_tx_to_l23(msg);
}

/**
 * @brief Transmit L1CTL_FBSB_CONF to l23.
 *
 * -- frequency burst synchronisation burst confirm --
 *
 * @param [in] res 0 -> success, 255 -> error.
 * @param [in] arfcn the arfcn we are synced to.
 *
 * No calculation needed for virtual pyh -> uses default values for a good link quality.
 */
void l1ctl_tx_fbsb_conf(uint8_t res, uint16_t arfcn)
{
	struct msgb *msg;
	struct l1ctl_fbsb_conf *resp;
	uint32_t fn = 0; // 0 should be okay here
	uint16_t snr = 40; // signal noise ratio > 40db is best signal.
	int16_t initial_freq_err = 0; // 0 means no error.
	uint8_t bsic = 0;

	msg = l1ctl_create_l2_msg(L1CTL_FBSB_CONF, fn,
			snr,
			arfcn);

	resp = (struct l1ctl_fbsb_conf *) msgb_put(msg, sizeof(*resp));
	resp->initial_freq_err = htons(initial_freq_err);
	resp->result = res;
	resp->bsic = bsic;

	DEBUGP(DL1C, "Sending to l23 - %s (res: %u)\n",
	                getL1ctlPrimName(L1CTL_FBSB_CONF), res);

	l1ctl_sap_tx_to_l23(msg);
}

/**
 * @brief Transmit L1CTL_CCCH_MODE_CONF to layer 23.
 *
 * -- common control channel mode confirm --
 *
 * @param [in] ccch_mode the new configured ccch mode. Combined or non-combined, see l1ctl_proto.
 *
 * Called by layer 1 to inform layer 2 that the ccch mode was successfully changed.
 */
void l1ctl_tx_ccch_mode_conf(uint8_t ccch_mode)
{
	struct msgb *msg = l1ctl_msgb_alloc(L1CTL_CCCH_MODE_CONF);
	struct l1ctl_ccch_mode_conf *mode_conf;
	mode_conf = (struct l1ctl_ccch_mode_conf *)msgb_put(msg,
	                sizeof(*mode_conf));
	mode_conf->ccch_mode = ccch_mode;

	DEBUGP(DL1C, "Sending to l23 - L1CTL_CCCH_MODE_CONF (mode: %u)\n",
	                ccch_mode);
	l1ctl_sap_tx_to_l23(msg);
}

/**
 * @brief Transmit L1CTL_TCH_MODE_CONF to layer 23.
 *
 * -- traffic channel mode confirm --
 *
 * @param [in] tch_mode the new configured traffic channel mode, see gsm48_chan_mode in gsm_04_08.h.
 * @param [in] audio_mode the new configured audio mode(s), see l1ctl_tch_mode_req in l1ctl_proto.h.
 *
 * Called by layer 1 to inform layer 23 that the traffic channel mode was successfully changed.
 */
void l1ctl_tx_tch_mode_conf(uint8_t tch_mode, uint8_t audio_mode)
{
	struct msgb *msg = l1ctl_msgb_alloc(L1CTL_TCH_MODE_CONF);
	struct l1ctl_tch_mode_conf *mode_conf;
	mode_conf = (struct l1ctl_tch_mode_conf *)msgb_put(msg,
	                sizeof(*mode_conf));
	mode_conf->tch_mode = tch_mode;
	mode_conf->audio_mode = audio_mode;

	DEBUGP(DL1C,
	                "Sending to l23 - L1CTL_TCH_MODE_CONF (tch_mode: %u, audio_mode: %u)\n", tch_mode,
	                audio_mode);
	l1ctl_sap_tx_to_l23(msg);
}


