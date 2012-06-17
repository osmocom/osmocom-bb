#ifndef _OSMO_08_16_H
#define _OSMO_08_16_H

/* GPRS Networks Service (NS) messages on the Gb interface
 * 3GPP TS 08.16 version 8.0.1 Release 1999 / ETSI TS 101 299 V8.0.1 (2002-05)
 * 3GPP TS 48.016 version 6.5.0 Release 6 / ETSI TS 148 016 V6.5.0 (2005-11) */

#include <stdint.h>

/*! \addtogroup libgb
 *  @{
 */

/*! \file gprs_ns.h */

/*! \brief Common header of GPRS NS */
struct gprs_ns_hdr {
	uint8_t pdu_type;	/*!< NS PDU type */
	uint8_t data[0];	/*!< variable-length payload */
} __attribute__((packed));

/*! \brief NS PDU Type (TS 08.16, Section 10.3.7, Table 14) */
enum ns_pdu_type {
	NS_PDUT_UNITDATA	= 0x00,
	NS_PDUT_RESET		= 0x02,
	NS_PDUT_RESET_ACK	= 0x03,
	NS_PDUT_BLOCK		= 0x04,
	NS_PDUT_BLOCK_ACK	= 0x05,
	NS_PDUT_UNBLOCK		= 0x06,
	NS_PDUT_UNBLOCK_ACK	= 0x07,
	NS_PDUT_STATUS		= 0x08,
	NS_PDUT_ALIVE		= 0x0a,
	NS_PDUT_ALIVE_ACK	= 0x0b,
	/* TS 48.016 Section 10.3.7, Table 10.3.7.1 */
	SNS_PDUT_ACK		= 0x0c,
	SNS_PDUT_ADD		= 0x0d,
	SNS_PDUT_CHANGE_WEIGHT	= 0x0e,
	SNS_PDUT_CONFIG		= 0x0f,
	SNS_PDUT_CONFIG_ACK	= 0x10,
	SNS_PDUT_DELETE		= 0x11,
	SNS_PDUT_SIZE		= 0x12,
	SNS_PDUT_SIZE_ACK	= 0x13,
};

/*! \brief NS Control IE (TS 08.16, Section 10.3, Table 12) */
enum ns_ctrl_ie {
	NS_IE_CAUSE		= 0x00,
	NS_IE_VCI		= 0x01,
	NS_IE_PDU		= 0x02,
	NS_IE_BVCI		= 0x03,
	NS_IE_NSEI		= 0x04,
	/* TS 48.016 Section 10.3, Table 10.3.1 */
	NS_IE_IPv4_LIST		= 0x05,
	NS_IE_IPv6_LIST		= 0x06,
	NS_IE_MAX_NR_NSVC	= 0x07,
	NS_IE_IPv4_EP_NR	= 0x08,
	NS_IE_IPv6_EP_NR	= 0x09,
	NS_IE_RESET_FLAG	= 0x0a,
	NS_IE_IP_ADDR		= 0x0b,
};

/*! \brief NS Cause (TS 08.16, Section 10.3.2, Table 13) */
enum ns_cause {
	NS_CAUSE_TRANSIT_FAIL		= 0x00,
	NS_CAUSE_OM_INTERVENTION	= 0x01,
	NS_CAUSE_EQUIP_FAIL		= 0x02,
	NS_CAUSE_NSVC_BLOCKED		= 0x03,
	NS_CAUSE_NSVC_UNKNOWN		= 0x04,
	NS_CAUSE_BVCI_UNKNOWN		= 0x05,
	NS_CAUSE_SEM_INCORR_PDU		= 0x08,
	NS_CAUSE_PDU_INCOMP_PSTATE	= 0x0a,
	NS_CAUSE_PROTO_ERR_UNSPEC	= 0x0b,
	NS_CAUSE_INVAL_ESSENT_IE	= 0x0c,
	NS_CAUSE_MISSING_ESSENT_IE	= 0x0d,
	/* TS 48.016 Section 10.3.2, Table 10.3.2.1 */
	NS_CAUSE_INVAL_NR_IPv4_EP	= 0x0e,
	NS_CAUSE_INVAL_NR_IPv6_EP	= 0x0f,
	NS_CAUSE_INVAL_NR_NS_VC		= 0x10,
	NS_CAUSE_INVAL_WEIGH		= 0x11,
	NS_CAUSE_UNKN_IP_EP		= 0x12,
	NS_CAUSE_UNKN_IP_ADDR		= 0x13,
	NS_CAUSE_UNKN_IP_TEST_FAILED	= 0x14,
};

#endif
