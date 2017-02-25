#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <osmocom/gsm/gsm_utils.h>
#include <osmocom/gsm/protocol/gsm_08_58.h>
#include <osmocom/core/msgb.h>
#include <virtphy/l1ctl_sap.h>
#include <virtphy/virt_l1_sched.h>
#include <osmocom/core/gsmtap.h>
#include <virtphy/logging.h>
#include <l1ctl_proto.h>

static struct l1_model_ms *l1_model_ms = NULL;

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
 * Note: ms will start receiving msgs on virtual um only after this req was received.
 * Note: virt bts does not broadcast freq and sync bursts.
 *
 */
void l1ctl_rx_fbsb_req(struct msgb *msg)
{
	struct l1ctl_hdr *l1h = (struct l1ctl_hdr *)msg->data;
	struct l1ctl_fbsb_req *sync_req = (struct l1ctl_fbsb_req *)l1h->data;

	DEBUGP(DL1C,
	       "Received and handled from l23 - L1CTL_FBSB_REQ (arfcn=%u, flags=0x%x)\n",
	       ntohs(sync_req->band_arfcn), sync_req->flags);

	l1_model_ms->state->state = MS_STATE_IDLE_SYNCING;
	l1_model_ms->state->fbsb.arfcn = ntohs(sync_req->band_arfcn);
}

/**
 * @brief A msg was received on l1 that can be used for synchronization.
 *
 * Note: for virtual layer 1 this can be a random downlink message, as we can parse the fn from the gsmtap header.
 */
void prim_fbsb_sync(struct msgb *msg)
{
	struct gsmtap_hdr *gh = msgb_l1(msg);
	uint32_t fn = ntohl(gh->frame_number); // frame number of the rcv msg
	uint16_t arfcn = ntohs(gh->arfcn); // arfcn of the received msg

	// ignore messages from other arfcns as the one requested to sync to by l23
	if (l1_model_ms->state->fbsb.arfcn != (arfcn & GSMTAP_ARFCN_MASK)) {
		talloc_free(msg);
		return;
	}
	l1_model_ms->state->serving_cell.arfcn = (arfcn & GSMTAP_ARFCN_MASK);
	l1_model_ms->state->state = MS_STATE_IDLE_CAMPING;
	/* Not needed in virtual phy */
	l1_model_ms->state->serving_cell.fn_offset = 0;
	l1_model_ms->state->serving_cell.time_alignment = 0;
	l1_model_ms->state->serving_cell.bsic = 0;
	/* Update current gsm time each time we receive a message on the virt um */
	gsm_fn2gsmtime(&l1_model_ms->state->downlink_time, fn);
	/* Restart scheduler */
	virt_l1_sched_restart(l1_model_ms->state->downlink_time);
	talloc_free(msg);
	l1ctl_tx_fbsb_conf(0, (arfcn & GSMTAP_ARFCN_MASK));
}

/**
 * @brief Transmit L1CTL_FBSB_CONF to l23.
 *
 * -- frequency burst synchronisation burst confirm --
 *
 * @param [in] res 0 -> success, 255 -> error.
 * @param [in] arfcn the arfcn we are synced to.
 *
 * No calculation needed for virtual pyh -> uses dummy values for a good link quality.
 */
void l1ctl_tx_fbsb_conf(uint8_t res, uint16_t arfcn)
{
	struct msgb *msg;
	struct l1ctl_fbsb_conf *resp;
	uint32_t fn = 0; // 0 should be okay here
	uint16_t snr = 40; // signal noise ratio > 40db is best signal (unused in virt)
	int16_t initial_freq_err = 0; // 0 means no error (unused in virt)
	uint8_t bsic = 0; // bsci can be read from sync burst (unused in virt)

	msg = l1ctl_create_l2_msg(L1CTL_FBSB_CONF, fn, snr, arfcn);

	resp = (struct l1ctl_fbsb_conf *)msgb_put(msg, sizeof(*resp));
	resp->initial_freq_err = htons(initial_freq_err);
	resp->result = res;
	resp->bsic = bsic;

	DEBUGP(DL1C, "Sending to l23 - %s (res: %u)\n",
	       getL1ctlPrimName(L1CTL_FBSB_CONF), res);

	l1ctl_sap_tx_to_l23(msg);
}
/**
 * @brief Initialize virtual prim rach.
 *
 * @param [in] model the l1 model instance
 */
void prim_fbsb_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
}
