#pragma once

#include <osmocom/gsm/l1sap.h>
#include <osmocom/gsm/lapd_core.h>

/*! \defgroup lapdm LAPDm implementation according to GSM TS 04.06
 *  @{
 */

/*! \file lapdm.h */

/*! \brief LAPDm mode/role */
enum lapdm_mode {
	LAPDM_MODE_MS,		/*!< \brief behave like a MS (mobile phone) */
	LAPDM_MODE_BTS,		/*!< \brief behave like a BTS (network) */
};

struct lapdm_entity;

/*! \brief LAPDm message context */
struct lapdm_msg_ctx {
	struct lapdm_datalink *dl;
	int lapdm_fmt;
	uint8_t chan_nr;
	uint8_t link_id;
	uint8_t ta_ind;		/* TA indicated by network */
	uint8_t tx_power_ind;	/* MS power indicated by network */
};

/*! \brief LAPDm datalink like TS 04.06 / Section 3.5.2 */
struct lapdm_datalink {
	struct lapd_datalink dl; /* \brief common LAPD */
	struct lapdm_msg_ctx mctx; /*!< \brief context of established connection */

	struct lapdm_entity *entity; /*!< \brief LAPDm entity we are part of */
};

/*! \brief LAPDm datalink SAPIs */
enum lapdm_dl_sapi {
	DL_SAPI0	= 0,	/*!< \brief SAPI 0 */
	DL_SAPI3	= 1,	/*!< \brief SAPI 1 */
	_NR_DL_SAPI
};

typedef int (*lapdm_cb_t)(struct msgb *msg, struct lapdm_entity *le, void *ctx);

#define LAPDM_ENT_F_EMPTY_FRAME		0x0001
#define LAPDM_ENT_F_POLLING_ONLY	0x0002

/*! \brief a LAPDm Entity */
struct lapdm_entity {
	/*! \brief the SAPIs of the LAPDm entity */
	struct lapdm_datalink datalink[_NR_DL_SAPI];
	int last_tx_dequeue; /*!< \brief last entity that was dequeued */
	int tx_pending; /*!< \brief currently a pending frame not confirmed by L1 */
	enum lapdm_mode mode; /*!< \brief are we in BTS mode or MS mode */
	unsigned int flags;

	void *l1_ctx;	/*!< \brief context for layer1 instance */
	void *l3_ctx;	/*!< \brief context for layer3 instance */

	osmo_prim_cb l1_prim_cb;/*!< \brief callback for sending prims to L1 */
	lapdm_cb_t l3_cb;	/*!< \brief callback for sending stuff to L3 */

	/*! \brief pointer to \ref lapdm_channel of which we're part */
	struct lapdm_channel *lapdm_ch;

	uint8_t ta;		/* TA used and indicated to network */
	uint8_t tx_power;	/* MS power used and indicated to network */
};

/*! \brief the two lapdm_entities that form a GSM logical channel (ACCH + DCCH) */
struct lapdm_channel {
	struct llist_head list;		/*!< \brief internal linked list */
	char *name;			/*!< \brief human-readable name */
	struct lapdm_entity lapdm_acch;	/*!< \brief Associated Control Channel */
	struct lapdm_entity lapdm_dcch;	/*!< \brief Dedicated Control Channel */
};

const char *get_rsl_name(int value);
extern const char *lapdm_state_names[];

struct lapdm_datalink *lapdm_datalink_for_sapi(struct lapdm_entity *le, uint8_t sapi);

/* initialize a LAPDm entity */
void lapdm_entity_init(struct lapdm_entity *le, enum lapdm_mode mode, int t200);
void lapdm_channel_init(struct lapdm_channel *lc, enum lapdm_mode mode);

/* deinitialize a LAPDm entity */
void lapdm_entity_exit(struct lapdm_entity *le);
void lapdm_channel_exit(struct lapdm_channel *lc);

/* input into layer2 (from layer 1) */
int lapdm_phsap_up(struct osmo_prim_hdr *oph, struct lapdm_entity *le);

/* input into layer2 (from layer 3) */
int lapdm_rslms_recvmsg(struct msgb *msg, struct lapdm_channel *lc);

void lapdm_channel_set_l3(struct lapdm_channel *lc, lapdm_cb_t cb, void *ctx);
void lapdm_channel_set_l1(struct lapdm_channel *lc, osmo_prim_cb cb, void *ctx);

int lapdm_entity_set_mode(struct lapdm_entity *le, enum lapdm_mode mode);
int lapdm_channel_set_mode(struct lapdm_channel *lc, enum lapdm_mode mode);

void lapdm_entity_reset(struct lapdm_entity *le);
void lapdm_channel_reset(struct lapdm_channel *lc);

void lapdm_entity_set_flags(struct lapdm_entity *le, unsigned int flags);
void lapdm_channel_set_flags(struct lapdm_channel *lc, unsigned int flags);

int lapdm_phsap_dequeue_prim(struct lapdm_entity *le, struct osmo_phsap_prim *pp);

/*! @} */
