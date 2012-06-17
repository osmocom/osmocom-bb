#ifndef _GPRS_NS_H
#define _GPRS_NS_H

#include <stdint.h>

/* Our Implementation */
#include <netinet/in.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/msgb.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/select.h>
#include <osmocom/gprs/gprs_msgb.h>

#include <osmocom/gprs/protocol/gsm_08_16.h>

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

/*! \brief Osmocom NS link layer types */
enum gprs_ns_ll {
	GPRS_NS_LL_UDP,		/*!< NS/UDP/IP */
	GPRS_NS_LL_E1,		/*!< NS/E1 */
	GPRS_NS_LL_FR_GRE,	/*!< NS/FR/GRE/IP */
};

/*! \brief Osmoco NS events */
enum gprs_ns_evt {
	GPRS_NS_EVT_UNIT_DATA,
};

struct gprs_nsvc;
/*! \brief Osmocom GPRS callback function type */
typedef int gprs_ns_cb_t(enum gprs_ns_evt event, struct gprs_nsvc *nsvc,
			 struct msgb *msg, uint16_t bvci);

/*! \brief An instance of the NS protocol stack */
struct gprs_ns_inst {
	/*! \brief callback to the user for incoming UNIT DATA IND */
	gprs_ns_cb_t *cb;

	/*! \brief linked lists of all NSVC in this instance */
	struct llist_head gprs_nsvcs;

	/*! \brief a NSVC object that's needed to deal with packets for
	 * 	   unknown NSVC */
	struct gprs_nsvc *unknown_nsvc;

	uint16_t timeout[NS_TIMERS_COUNT];

	/*! \brief NS-over-IP specific bits */
	struct {
		struct osmo_fd fd;
		uint32_t local_ip;
		uint16_t local_port;
	} nsip;
	/*! \brief NS-over-FR-over-GRE-over-IP specific bits */
	struct {
		struct osmo_fd fd;
		uint32_t local_ip;
		unsigned int enabled:1;
	} frgre;
};

enum nsvc_timer_mode {
	/* standard timers */
	NSVC_TIMER_TNS_TEST,
	NSVC_TIMER_TNS_ALIVE,
	NSVC_TIMER_TNS_RESET,
	_NSVC_TIMER_NR,
};

/*! \brief Structure representing a single NS-VC */
struct gprs_nsvc {
	/*! \brief list of NS-VCs within NS Instance */
	struct llist_head list;
	/*! \brief pointer to NS Instance */
	struct gprs_ns_inst *nsi;

	uint16_t nsei;	/*! \brief end-to-end significance */
	uint16_t nsvci;	/*! \brief uniquely identifies NS-VC at SGSN */

	uint32_t state;
	uint32_t remote_state;

	struct osmo_timer_list timer;
	enum nsvc_timer_mode timer_mode;
	int alive_retries;

	unsigned int remote_end_is_sgsn:1;
	unsigned int persistent:1;

	struct rate_ctr_group *ctrg;

	/*! \brief which link-layer are we based on? */
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
struct gprs_ns_inst *gprs_ns_instantiate(gprs_ns_cb_t *cb, void *ctx);

/* Destroy a NS protocol instance */
void gprs_ns_destroy(struct gprs_ns_inst *nsi);

/* Listen for incoming GPRS packets via NS/UDP */
int gprs_ns_nsip_listen(struct gprs_ns_inst *nsi);

/* Establish a connection (from the BSS) to the SGSN */
struct gprs_nsvc *gprs_ns_nsip_connect(struct gprs_ns_inst *nsi,
					struct sockaddr_in *dest,
					uint16_t nsei, uint16_t nsvci);


struct sockaddr_in;

/* main function for higher layers (BSSGP) to send NS messages */
int gprs_ns_sendmsg(struct gprs_ns_inst *nsi, struct msgb *msg);

int gprs_ns_tx_reset(struct gprs_nsvc *nsvc, uint8_t cause);
int gprs_ns_tx_block(struct gprs_nsvc *nsvc, uint8_t cause);
int gprs_ns_tx_unblock(struct gprs_nsvc *nsvc);

/* Listen for incoming GPRS packets via NS/FR/GRE */
int gprs_ns_frgre_listen(struct gprs_ns_inst *nsi);

struct gprs_nsvc *gprs_nsvc_create(struct gprs_ns_inst *nsi, uint16_t nsvci);
void gprs_nsvc_delete(struct gprs_nsvc *nsvc);
struct gprs_nsvc *gprs_nsvc_by_nsei(struct gprs_ns_inst *nsi, uint16_t nsei);
struct gprs_nsvc *gprs_nsvc_by_nsvci(struct gprs_ns_inst *nsi, uint16_t nsvci);

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

enum signal_ns {
	S_NS_RESET,
	S_NS_BLOCK,
	S_NS_UNBLOCK,
	S_NS_ALIVE_EXP,	/* Tns-alive expired more than N times */
};

struct ns_signal_data {
	struct gprs_nsvc *nsvc;
	uint8_t cause;
};

void gprs_ns_set_log_ss(int ss);

/*! }@ */

#endif
