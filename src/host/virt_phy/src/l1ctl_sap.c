/* L1CTL SAP implementation.  */

#include <osmocom/core/linuxlist.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/utils.h>
#include <osmocom/core/gsmtap.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/gsm/protocol/gsm_04_08.h>
#include <osmocom/gsm/rsl.h>
#include <stdio.h>
#include <l1ctl_proto.h>
#include <netinet/in.h>
#include <string.h>
#include <virtphy/virtual_um.h>
#include <virtphy/l1ctl_sock.h>
#include <virtphy/virt_l1_model.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/gsmtapl1_if.h>
#include <virtphy/logging.h>
#include <virtphy/virt_l1_sched.h>

static struct l1_model_ms *l1_model_ms = NULL;

static void l1_model_tch_mode_set(uint8_t tch_mode)
{
	if (tch_mode == GSM48_CMODE_SPEECH_V1
	                || tch_mode == GSM48_CMODE_SPEECH_EFR) {
		l1_model_ms->state->tch_mode = tch_mode;

	} else {
		// set default value if no proper mode was assigned by l23
		l1_model_ms->state->tch_mode = GSM48_CMODE_SIGN;
	}
}

/**
 * @brief Init the SAP.
 */
void l1ctl_sap_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
	prim_rach_init(model);
	prim_fbsb_init(model);
	prim_data_init(model);
	prim_traffic_init(model);
}

/**
 * @brief L1CTL handler called for received messages from L23.
 *
 * Enqueues the message into the rx queue.
 */
void l1ctl_sap_rx_from_l23_inst_cb(struct l1ctl_sock_inst *lsi,
                                   struct msgb *msg)
{
	// check if the received msg is not empty
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
	len = (uint16_t *)msgb_push(msg, sizeof(*len));
	*len = htons(msg->len - sizeof(*len));
	l1ctl_sock_write_msg(lsi, msg);
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
	dl->snr = htons(snr);
	dl->band_arfcn = htons(arfcn);

	return msg;
}

/**
 * @brief General handler for incoming L1CTL messages from layer 2/3.
 *
 * This handler will call the specific routine dependent on the L1CTL message type.
 *
 */
void l1ctl_sap_handler(struct msgb *msg)
{
	struct l1ctl_hdr *l1h;

	if (!msg) {
		return;
	}

	l1h = (struct l1ctl_hdr *)msg->data;

	if (sizeof(*l1h) > msg->len) {
		LOGP(DL1C, LOGL_NOTICE, "Malformed message: too short. %u\n",
		                msg->len);
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
		goto exit_nofree;
	case L1CTL_DATA_REQ:
		l1ctl_rx_data_req(msg);
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
		goto exit_nofree;
	case L1CTL_SIM_REQ:
		l1ctl_rx_sim_req(msg);
		break;
	}

	exit_msgbfree: msgb_free(msg);
	exit_nofree: return; /* msg is scheduled for uplink and mustn't be freed here */
}

/***************************************************************
 * L1CTL RX ROUTINES *******************************************
 * For more routines check the respective handler classes ******
 * like virt_prim_rach.c ***************************************
 ***************************************************************/

/**
 * @brief Handler for received L1CTL_DM_EST_REQ from L23.
 *
 * -- dedicated mode established request --
 *
 * @param [in] msg the received message.
 *
 * Handle state change from idle to dedicated mode.
 *
 */
void l1ctl_rx_dm_est_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_dm_est_req *est_req =
	                (struct l1ctl_dm_est_req *)ul->payload;
	uint8_t rsl_chantype, subslot, timeslot;

	rsl_dec_chan_nr(ul->chan_nr, &rsl_chantype, &subslot, &timeslot);

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_DM_EST_REQ (chan_nr=0x%02x, tn=%u, ss=%u)\n",
	                ul->chan_nr, timeslot, subslot);

	l1_model_ms->state->dedicated.chan_type = rsl_chantype;
	l1_model_ms->state->dedicated.tn = timeslot;
	l1_model_ms->state->dedicated.subslot = subslot;
	l1_model_ms->state->state = MS_STATE_DEDICATED;

	/* TCH config */
	if (rsl_chantype == RSL_CHAN_Bm_ACCHs
	                || rsl_chantype == RSL_CHAN_Lm_ACCHs) {
		l1_model_ms->state->tch_mode = est_req->tch_mode;
		l1_model_tch_mode_set(est_req->tch_mode);
		l1_model_ms->state->audio_mode = est_req->audio_mode;
		// TODO: configure audio hardware for encoding / decoding / recording / playing voice
	}
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
 * Note: Not needed for virtual physical layer as freqency hopping is generally disabled.
 */
void l1ctl_rx_dm_freq_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_info_ul *ul = (struct l1ctl_info_ul *)l1h->data;
	struct l1ctl_dm_freq_req *freq_req =
	                (struct l1ctl_dm_freq_req *)ul->payload;

	DEBUGP(DL1C,
	                "Received and ignored from l23 - L1CTL_DM_FREQ_REQ (arfcn0=%u, hsn=%u, maio=%u)\n",
	                ntohs(freq_req->h0.band_arfcn), freq_req->h1.hsn,
	                freq_req->h1.maio);
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

	if (cr->algo && key_len != A5_KEY_LEN) {
		DEBUGP(DL1C, "L1CTL_CRYPTO_REQ -> Invalid key\n");
		return;
	}

	l1_model_ms->crypto_inf->algo = cr->algo;
	memcpy(l1_model_ms->crypto_inf->key, cr->key,
	                sizeof(uint8_t) * A5_KEY_LEN);
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
 */
void l1ctl_rx_dm_rel_req(struct msgb *msg)
{
	DEBUGP(DL1C, "Received and handled from l23 - L1CTL_DM_REL_REQ\n");

	l1_model_ms->state->dedicated.chan_type = 0;
	l1_model_ms->state->dedicated.tn = 0;
	l1_model_ms->state->dedicated.subslot = 0;
	l1_model_ms->state->tch_mode = GSM48_CMODE_SIGN;
	l1_model_ms->state->state = MS_STATE_IDLE_CAMPING;

	// TODO: disable ciphering
	// TODO: disable audio recording / playing
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
 * @brief Handler for received L1CTL_RESET_REQ from L23.
 *
 * -- reset request --
 *
 * @param [in] msg the received message.
 *
 * Reset layer 1 (state machine, scheduler, transceiver) depending on the reset type.
 *
 * Note: Currently we do not perform anything else than response with a reset confirm
 * to just tell l2 that we are rdy.
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
		l1_model_ms->state->state = MS_STATE_IDLE_SEARCHING;
		virt_l1_sched_stop();
		l1ctl_tx_reset(L1CTL_RESET_CONF, reset_req->type);
		break;
	case L1CTL_RES_T_SCHED:
		virt_l1_sched_restart(l1_model_ms->state->downlink_time);
		DEBUGP(DL1C,
		                "Received and handled from l23 - L1CTL_RESET_REQ (type=SCHED)\n");
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
 * @see l1ctl_proto.h -- enum ccch_mode
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

	l1_model_tch_mode_set(tch_mode_req->tch_mode);
	l1_model_ms->state->audio_mode = tch_mode_req->audio_mode;

	DEBUGP(DL1C,
	                "Received and handled from l23 - L1CTL_TCH_MODE_REQ (tch_mode=0x%02x audio_mode=0x%02x)\n",
	                tch_mode_req->tch_mode, tch_mode_req->audio_mode);

	// TODO: configure audio hardware for encoding / decoding / recording / playing voice

	l1ctl_tx_tch_mode_conf(l1_model_ms->state->tch_mode,
	                l1_model_ms->state->audio_mode);
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
 * Note: Not needed for virtual physical layer as we dont maintain neigbors.
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
 * @brief Handler for received L1CTL_SIM_REQ from L23.
 *
 * -- sim request --
 *
 * @param [in] msg the received message.
 *
 * Forward and a sim request to the SIM APDU.
 *
 * Note: Not needed for virtual layer. Please configure layer23 application to use test-sim implementation.
 * In this case layer1 wont need to handle sim logic.
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
 * For more routines check the respective handler classes ******
 * like virt_prim_rach.c ***************************************
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
	                "Sending to l23 - L1CTL_TCH_MODE_CONF (tch_mode: %u, audio_mode: %u)\n",
	                tch_mode, audio_mode);
	l1ctl_sap_tx_to_l23(msg);
}

/**
 * @brief Get the scheduled fn for a msg depending on its chan_nr and link_id.
 */
uint32_t sched_fn_ul(struct gsm_time cur_time, uint8_t chan_nr,
                                      uint8_t link_id)
{
	uint8_t chan_type, chan_ss, chan_ts;
	rsl_dec_chan_nr(chan_nr, &chan_type, &chan_ss, &chan_ts);

	uint32_t sched_fn = cur_time.fn;
	uint16_t mod_102 = cur_time.fn % 2 * 51;
	switch (chan_type) {
	case RSL_CHAN_Bm_ACCHs:
		switch (link_id) {
		case LID_DEDIC:
			// dl=[0...11,13...24] ul=[0...11,13...24]
			// skip idle frames and frames reserved for TCH_ACCH
			if(cur_time.t2 == 12 || cur_time.t2 == 25) {
				sched_fn++;
			}
			break;
		// dl=42, ul=42+15
		case LID_SACCH:
			if((chan_ts & 1)) {
				// Odd traffic channel timeslot -> dl=[25] ul=[25]
				// TCH_ACCH always at the end of tch multiframe (mod 26)
				sched_fn -= cur_time.t2;
				sched_fn += 25;
			}
			else {
				// Even traffic channel timeslot -> dl=[12] ul=[12]
				if(cur_time.t2 <= 12) {
					sched_fn -= cur_time.t2;
					sched_fn += 12;
				} else {
					sched_fn -= cur_time.t2;
					sched_fn += 26 + 12;
				}

			}
			break;
		}
		break;
	case RSL_CHAN_Lm_ACCHs:
		break; /* TCH/H not supported */
	case RSL_CHAN_SDCCH4_ACCH:
		switch (chan_ss) {
		case 0:
			switch (link_id) {
			// dl=22, ul=22+15
			case LID_DEDIC:
				if(cur_time.t3 <= 22 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 22 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 22 + 15;
				}
				break;
			// dl=42, ul=42+15
			case LID_SACCH:
				if(mod_102 <= 42 + 15) {
					sched_fn -= mod_102;
					sched_fn += 42 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 42 + 15;
				}
				break;
			}
			break;
		case 1:
			switch (link_id) {
			// dl=26, ul=26+15
			case LID_DEDIC:
				if(cur_time.t3 <= 26 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 26 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 26 + 15;
				}
				break;
			// dl=46, ul=46+15
			case LID_SACCH:
				if(mod_102 <= 46 + 15) {
					sched_fn -= mod_102;
					sched_fn += 46 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 46 + 15;
				}
				break;
			}
			break;
		case 2:
			switch (link_id) {
			// dl=32, ul=32+15
			case LID_DEDIC:
				if(cur_time.t3 <= 32 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 32 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 32 + 15;
				}
				break;
			// dl=51+42, ul=51+42+15
			case LID_SACCH:
				if(mod_102 <= 51 + 42 + 15) {
					sched_fn -= mod_102;
					sched_fn += 51 + 42 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 51 + 42 + 15;
				}
				break;
			}
			break;
		case 3:
			switch (link_id) {
			// dl=36, ul=36+15
			case LID_DEDIC:
				if(cur_time.t3 <= 36 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 36 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 36 + 15;
				}
				break;
			// dl=51+46, ul=51+46+15
			case LID_SACCH:
				if(mod_102 <= 51 + 46 + 15) {
					sched_fn -= mod_102;
					sched_fn += 51 + 46 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 51 + 46 + 15;
				}
				break;
			}
			break;
		}
		break;
	case RSL_CHAN_SDCCH8_ACCH:
		switch (chan_ss) {
		case 0:
			switch (link_id) {
			// dl=0, ul=0+15
			case LID_DEDIC:
				if(cur_time.t3 <= 0 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 0 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 0 + 15;
				}
				break;
			// dl=32, ul=32+15
			case LID_SACCH:
				if(mod_102 <= 32 + 15) {
					sched_fn -= mod_102;
					sched_fn += 32 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 32 + 15;
				}
				break;
			}
			break;
		case 1:
			switch (link_id) {
			// dl=4, ul=4+15
			case LID_DEDIC:
				if(cur_time.t3 <= 4 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 4 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 4 + 15;
				}
				break;
			// dl=36, ul=36+15
			case LID_SACCH:
				if(mod_102 <= 36 + 15) {
					sched_fn -= mod_102;
					sched_fn += 36 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 36 + 15;
				}
				break;
			}
			break;
		case 2:
			switch (link_id) {
			// dl=8, ul=8+15
			case LID_DEDIC:
				if(cur_time.t3 <= 8 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 8 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 8 + 15;
				}
				break;
			// dl=40, ul=40+15
			case LID_SACCH:
				if(mod_102 <= 40 + 15) {
					sched_fn -= mod_102;
					sched_fn += 40 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 40 + 15;
				}
				break;
			}
			break;
		case 3:
			switch (link_id) {
			// dl=12, ul=12+15
			case LID_DEDIC:
				if(cur_time.t3 <= 12 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 12 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 12 + 15;
				}
				break;
			// dl=44, ul=44+15
			case LID_SACCH:
				if(mod_102 <= 44 + 15) {
					sched_fn -= mod_102;
					sched_fn += 44 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 44 + 15;
				}
				break;
			}
			break;
		case 4:
			switch (link_id) {
			// dl=16, ul=16+15
			case LID_DEDIC:
				if(cur_time.t3 <= 16 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 16 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 16 + 15;
				}
				break;
			// dl=51+32, ul=51+32+15
			case LID_SACCH:
				if(mod_102 <= 51 + 32 + 15) {
					sched_fn -= mod_102;
					sched_fn += 51 + 32 + 15;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 51 + 32 + 15;
				}
				break;
			}
			break;
		case 5:
			switch (link_id) {
			// dl=20, ul=36+15
			case LID_DEDIC:
				if(cur_time.t3 <= 20 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 20 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 20 + 15;
				}
				break;
			// dl=51+36, ul=51+36+15 ==> 0
			case LID_SACCH:
				if(mod_102 <= 0) {
					sched_fn -= mod_102;
					sched_fn += 0;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 0;
				}
				break;
			}
			break;
		case 6:
			switch (link_id) {
			// dl=24, ul=24+15
			case LID_DEDIC:
				if(cur_time.t3 <= 24 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 24 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 24 + 15;
				}
				break;
			// dl=51+40, ul=51+40+15 ==> 4
			case LID_SACCH:
				if(mod_102 <= 4) {
					sched_fn -= mod_102;
					sched_fn += 4;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 4;
				}
				break;
			}
			break;
		case 7:
			switch (link_id) {
			// dl=28, ul=28+15
			case LID_DEDIC:
				if(cur_time.t3 <= 28 + 15) {
					sched_fn -= cur_time.t3;
					sched_fn += 28 + 15;
				} else {
					sched_fn -= cur_time.t3;
					sched_fn += 51 + 28 + 15;
				}
				break;
			// dl=51+44, ul=51+44+15 ==> 8
			case LID_SACCH:
				if(mod_102 <= 8) {
					sched_fn -= mod_102;
					sched_fn += 8;
				} else {
					sched_fn -= mod_102;
					sched_fn += 2 * 51 + 8;
				}
				break;
			}
			break;
		}
		break;
	case RSL_CHAN_RACH:
		break; /* Use virt_prim_rach.c for calculation of sched fn for rach */
	default:
		break; /* Use current fn as default */
	}
	return sched_fn;
}
