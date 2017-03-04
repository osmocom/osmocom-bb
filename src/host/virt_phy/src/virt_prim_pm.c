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
// FIXME: ugly to configure that in code. Either make a config file or change power selection to automatically check which arfcns can be received.
static uint16_t available_arfcns[] = {666};

/**
 * @brief Handler for received L1CTL_PM_REQ from L23.
 *
 * -- power measurement request --
 *
 * @param [in] msg the received message.
 *
 * Process power measurement for a given range of arfcns to calculate signal power and connection quality.
 *
 * Note: We do not need to calculate that for the virtual physical layer,
 * but l23 apps can expect a response. So this response is mocked here.
 * For available arfcns we always return a perfect link quality, for all other the worst.
 *
 * TODO: Change PM so that we check the downlink first for for some time to get the arfcns we receive. Then return a good link for that and a bad for all others.
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

	DEBUGP(DL1C,
	                "Received from l23 - L1CTL_PM_REQ TYPE=%u, FROM=%d, TO=%d\n",
	                pm_req->type, pm_req->range.band_arfcn_from,
	                pm_req->range.band_arfcn_to);

	for (arfcn_next = pm_req->range.band_arfcn_from;
	                arfcn_next <= pm_req->range.band_arfcn_to;
	                ++arfcn_next) {
		struct l1ctl_pm_conf *pm_conf =
		                (struct l1ctl_pm_conf *)msgb_put(resp_msg,
		                                sizeof(*pm_conf));
		int cnt, available = 0;
		pm_conf->band_arfcn = htons(arfcn_next);
		// check if arfcn is available
		for(cnt = 0; cnt < sizeof(available_arfcns) / sizeof(uint16_t); cnt++) {
			if(arfcn_next == available_arfcns[cnt]) {
				available = 1;
				break;
			}
		}
		// rxlev 63 is great, 0 is bad the two values are min and max
		pm_conf->pm[0] = available ? 63 : 0;
		pm_conf->pm[1] = available ? 63 : 0;
		if (arfcn_next == pm_req->range.band_arfcn_to) {
			struct l1ctl_hdr *resp_l1h = msgb_l1(resp_msg);
			resp_l1h->flags |= L1CTL_F_DONE;
		}
		// no more space to hold mor pm info in msgb, flush to l23
		if (msgb_tailroom(resp_msg) < sizeof(*pm_conf)) {
			l1ctl_sap_tx_to_l23(resp_msg);
			resp_msg = l1ctl_msgb_alloc(L1CTL_PM_CONF);
		}
	}
	// transmit the remaining part of pm response to l23
	if (resp_msg) {
		l1ctl_sap_tx_to_l23(resp_msg);
	}
}

/**
 * @brief Initialize virtual prim pm.
 *
 * @param [in] model the l1 model instance
 */
void prim_pm_init(struct l1_model_ms *model)
{
	l1_model_ms = model;
}
