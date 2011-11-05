#ifndef _GPRS_NS_H
#define _GPRS_NS_H

#include <stdint.h>

/* GPRS Networks Service (NS) messages on the Gb interface
 * 3GPP TS 08.16 version 8.0.1 Release 1999 / ETSI TS 101 299 V8.0.1 (2002-05)
 * 3GPP TS 48.016 version 6.5.0 Release 6 / ETSI TS 148 016 V6.5.0 (2005-11) */

struct gprs_ns_hdr {
	uint8_t pdu_type;
	uint8_t data[0];
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

/* TS 08.16, Section 10.3, Table 12 */
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
	/* TS 48.016 Section 10.3.2, Table 10.3.2.1 */
	NS_CAUSE_INVAL_NR_IPv4_EP	= 0x0e,
	NS_CAUSE_INVAL_NR_IPv6_EP	= 0x0f,
	NS_CAUSE_INVAL_NR_NS_VC		= 0x10,
	NS_CAUSE_INVAL_WEIGH		= 0x11,
	NS_CAUSE_UNKN_IP_EP		= 0x12,
	NS_CAUSE_UNKN_IP_ADDR		= 0x13,
	NS_CAUSE_UNKN_IP_TEST_FAILED	= 0x14,
};

/* Our Implementation */
#include <netinet/in.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/select.h>

#define NS_TIMERS_COUNT 7
#define NS_TIMERS "(tns-block|tns-block-retries|tns-reset|tns-reset-retries|tns-test|tns-alive|tns-alive-retries)"
#define NS_TIMERS_HELP	\
	"(un)blocking Timer (Tns-block) timeout\n"		\
	"(un)blocking Timer (Tns-block) number of retries\n"	\
	"Reset Timer (Tns-reset) timeout\n"			\
	"Reset Timer (Tns-reset) number of retries\n"		\
	"Test Timer (Tns-test) timeout\n"			\
	"Alive Timer (Tns-alive) timeout\n"			\
	"Alive Timer (Tns-alive) number of retries\n"

enum ns_timeout {
	NS_TOUT_TNS_BLOCK,
	NS_TOUT_TNS_BLOCK_RETRIES,
	NS_TOUT_TNS_RESET,
	NS_TOUT_TNS_RESET_RETRIES,
	NS_TOUT_TNS_TEST,
	NS_TOUT_TNS_ALIVE,
	NS_TOUT_TNS_ALIVE_RETRIES,
};

#define NSE_S_BLOCKED	0x0001
#define NSE_S_ALIVE	0x0002

enum gprs_ns_ll {
	GPRS_NS_LL_UDP,
	GPRS_NS_LL_E1,
	GPRS_NS_LL_FR_GRE,
};

enum gprs_ns_evt {
	GPRS_NS_EVT_UNIT_DATA,
};

struct gprs_nsvc;
typedef int gprs_ns_cb_t(enum gprs_ns_evt event, struct gprs_nsvc *nsvc,
			 struct msgb *msg, uint16_t bvci);

/* An instance of the NS protocol stack */
struct gprs_ns_inst {
	/* callback to the user for incoming UNIT DATA IND */
	gprs_ns_cb_t *cb;

	/* linked lists of all NSVC in this instance */
	struct llist_head gprs_nsvcs;

	/* a NSVC object that's needed to deal with packets for unknown NSVC */
	struct gprs_nsvc *unknown_nsvc;

	uint16_t timeout[NS_TIMERS_COUNT];

	/* NS-over-IP specific bits */
	struct {
		struct osmo_fd fd;
		uint32_t local_ip;
		uint16_t local_port;
	} nsip;
	/* NS-over-FR-over-GRE-over-IP specific bits */
	struct {
		struct osmo_fd fd;
		uint32_t local_ip;
		int enabled:1;
	} frgre;
};

enum nsvc_timer_mode {
	/* standard timers */
	NSVC_TIMER_TNS_TEST,
	NSVC_TIMER_TNS_ALIVE,
	NSVC_TIMER_TNS_RESET,
	_NSVC_TIMER_NR,
};

struct gprs_nsvc {
	struct llist_head list;
	struct gprs_ns_inst *nsi;

	uint16_t nsei;		/* end-to-end significance */
	uint16_t nsvci;	/* uniquely identifies NS-VC at SGSN */

	uint32_t state;
	uint32_t remote_state;

	struct osmo_timer_list timer;
	enum nsvc_timer_mode timer_mode;
	int alive_retries;

	unsigned int remote_end_is_sgsn:1;
	unsigned int persistent:1;

	struct rate_ctr_group *ctrg;

	/* which link-layer are we based on? */
	enum gprs_ns_ll ll;

	union {
		struct {
			struct sockaddr_in bts_addr;
		} ip;
		struct {
			struct sockaddr_in bts_addr;
		} frgre;
	};
};

/* Create a new NS protocol instance */
struct gprs_ns_inst *gprs_ns_instantiate(gprs_ns_cb_t *cb);

/* Destroy a NS protocol instance */
void gprs_ns_destroy(struct gprs_ns_inst *nsi);

/* Listen for incoming GPRS packets via NS/UDP */
int gprs_ns_nsip_listen(struct gprs_ns_inst *nsi);

struct sockaddr_in;

/* main function for higher layers (BSSGP) to send NS messages */
int gprs_ns_sendmsg(struct gprs_ns_inst *nsi, struct msgb *msg);

int gprs_ns_tx_reset(struct gprs_nsvc *nsvc, uint8_t cause);
int gprs_ns_tx_block(struct gprs_nsvc *nsvc, uint8_t cause);
int gprs_ns_tx_unblock(struct gprs_nsvc *nsvc);

/* Listen for incoming GPRS packets via NS/FR/GRE */
int gprs_ns_frgre_listen(struct gprs_ns_inst *nsi);

/* Establish a connection (from the BSS) to the SGSN */
struct gprs_nsvc *nsip_connect(struct gprs_ns_inst *nsi,
				struct sockaddr_in *dest, uint16_t nsei,
				uint16_t nsvci);

struct gprs_nsvc *nsvc_create(struct gprs_ns_inst *nsi, uint16_t nsvci);
void nsvc_delete(struct gprs_nsvc *nsvc);
struct gprs_nsvc *nsvc_by_nsei(struct gprs_ns_inst *nsi, uint16_t nsei);
struct gprs_nsvc *nsvc_by_nsvci(struct gprs_ns_inst *nsi, uint16_t nsvci);

/* Initiate a RESET procedure (including timer start, ...)*/
void gprs_nsvc_reset(struct gprs_nsvc *nsvc, uint8_t cause);

/* Add NS-specific VTY stuff */
int gprs_ns_vty_init(struct gprs_ns_inst *nsi);

#define NS_ALLOC_SIZE	2048
#define NS_ALLOC_HEADROOM 20
static inline struct msgb *gprs_ns_msgb_alloc(void)
{
	return msgb_alloc_headroom(NS_ALLOC_SIZE, NS_ALLOC_HEADROOM, "GPRS/NS");
}

#endif
