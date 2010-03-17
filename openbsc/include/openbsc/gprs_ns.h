#ifndef _GPRS_NS_H
#define _GPRS_NS_H

struct gprs_ns_hdr {
	u_int8_t pdu_type;
	u_int8_t data[0];
} __attribute__((packed));

/* TS 08.16, Section 10.3.7, Table 14 */
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
};

/* TS 08.16, Section 10.3, Table 12 */
enum ns_ctrl_ie {
	NS_IE_CAUSE		= 0x00,
	NS_IE_VCI		= 0x01,
	NS_IE_PDU		= 0x02,
	NS_IE_BVCI		= 0x03,
	NS_IE_NSEI		= 0x04,
};

/* TS 08.16, Section 10.3.2, Table 13 */
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
};

/* a layer 1 entity transporting NS frames */
struct gprs_ns_link {
	union {
		struct {
			int fd;
		} ip;
	};
};


int gprs_ns_rcvmsg(struct msgb *msg, struct sockaddr_in *saddr);

int gprs_ns_sendmsg(struct gprs_ns_link *link, u_int16_t bvci,
		    struct msgb *msg);
#endif
