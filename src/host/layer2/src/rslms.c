
#include <stdint.h>
#include <errno.h>
#include <stdio.h>

#include <osmocore/msgb.h>
#include <osmocore/rsl.h>
#include <osmocore/tlv.h>
#include <osmocore/protocol/gsm_04_08.h>

#include <osmocom/lapdm.h>
#include <osmocom/rslms.h>
#include <osmocom/layer3.h>
#include <osmocom/osmocom_data.h>
#include <osmocom/layer2.h>

int rsl_dec_chan_nr(uint8_t chan_nr, uint8_t *type, uint8_t *subch, uint8_t *timeslot)
{
	*timeslot = chan_nr & 0x7;

	if ((chan_nr & 0xf8) == RSL_CHAN_Bm_ACCHs) {
		*type = RSL_CHAN_Bm_ACCHs;
		*subch = 0;
	} else if ((chan_nr & 0xf0) == RSL_CHAN_Lm_ACCHs) {
		*type = RSL_CHAN_Lm_ACCHs;
		*subch = (chan_nr >> 3) & 0x1;
	} else if ((chan_nr & 0xe0) == RSL_CHAN_SDCCH4_ACCH) {
		*type = RSL_CHAN_SDCCH4_ACCH;
		*subch = (chan_nr >> 3) & 0x3;
	} else if ((chan_nr & 0xc0) == RSL_CHAN_SDCCH8_ACCH) {
		*type = RSL_CHAN_SDCCH8_ACCH;
		*subch = (chan_nr >> 3) & 0x7;
	} else {
		printf("unable to decode chan_nr\n");
		return -EINVAL;
	}

	return 0;
}

/* Send a 'simple' RLL request to L2 */
int rslms_tx_rll_req(struct osmocom_ms *ms, uint8_t msg_type,
		     uint8_t chan_nr, uint8_t link_id)
{
	struct msgb *msg;

	msg = rsl_rll_simple(msg_type, chan_nr, link_id);

	return rslms_recvmsg(msg, ms);
}

/* Send a RLL request (including L3 info) to L2 */
int rslms_tx_rll_req_l3(struct osmocom_ms *ms, uint8_t msg_type,
			uint8_t chan_nr, uint8_t link_id, struct msgb *msg)
{
	rsl_rll_push_l3(msg, msg_type, chan_nr, link_id);

	return rslms_recvmsg(msg, ms);
}

static int rach_count = 0;

static int rslms_rx_udata_ind(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	struct tlv_parsed tv;
	int rc = 0;
	
	printf("RSLms UNIT DATA IND chan_nr=0x%02x link_id=0x%02x\n",
		rllh->chan_nr, rllh->link_id);

	rsl_tlv_parse(&tv, rllh->data, msgb_l2len(msg)-sizeof(*rllh));
	if (!TLVP_PRESENT(&tv, RSL_IE_L3_INFO)) {
		printf("UNIT_DATA_IND without L3 INFO ?!?\n");
		return -EIO;
	}
	msg->l3h = (uint8_t *) TLVP_VAL(&tv, RSL_IE_L3_INFO);

	if (rllh->chan_nr == RSL_CHAN_PCH_AGCH)
		rc = gsm48_rx_ccch(msg, ms);
	else if (rllh->chan_nr == RSL_CHAN_BCCH) {
		rc = gsm48_rx_bcch(msg);
		if (rach_count < 2) {
			tx_ph_rach_req(ms);
			rach_count++;
		}
	}

	return rc;
}

static int rslms_rx_rll(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_rll_hdr *rllh = msgb_l2(msg);
	int rc = 0;

	switch (rllh->c.msg_type) {
	case RSL_MT_DATA_IND:
		printf("RSLms DATA IND\n");
		break;
	case RSL_MT_UNIT_DATA_IND:
		rc = rslms_rx_udata_ind(msg, ms);
		break;
	case RSL_MT_EST_IND:
		printf("RSLms EST IND\n");
		break;
	case RSL_MT_EST_CONF:
		printf("RSLms EST CONF\n");
		break;
	case RSL_MT_REL_CONF:
		printf("RSLms REL CONF\n");
		break;
	case RSL_MT_ERROR_IND:
		printf("RSLms ERR IND\n");
		break;
	default:
		printf("unknown RSLms message type 0x%02x\n", rllh->c.msg_type);
		rc = -EINVAL;
		break;
	}
	return rc;
}

/* input function that L2 calls when sending messages up to L3 */
int rslms_sendmsg(struct msgb *msg, struct osmocom_ms *ms)
{
	struct abis_rsl_common_hdr *rslh = msgb_l2(msg);
	int rc = 0;

	switch (rslh->msg_discr & 0xfe) {
	case ABIS_RSL_MDISC_RLL:
		rc = rslms_rx_rll(msg, ms);
		break;
	default:
		printf("unknown RSLms msg_discr 0x%02x\n", rslh->msg_discr);
		rc = -EINVAL;
		break;
	}

	return rc;
}
