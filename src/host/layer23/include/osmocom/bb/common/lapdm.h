#ifndef _OSMOCOM_LAPDM_H
#define _OSMOCOM_LAPDM_H

#include <stdint.h>

#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>
#include <osmocom/gsm/prim.h>

/* primitive related sutff */

enum osmo_ph_prim {
	PRIM_PH_DATA,		/* PH-DATA */
	PRIM_PH_RACH,		/* PH-RANDOM_ACCESS */
	PRIM_PH_CONN,		/* PH-CONNECT */
	PRIM_PH_EMPTY_FRAME,	/* PH-EMPTY_FRAME */
	PRIM_PH_RTS,		/* PH-RTS */
};

/* for PH-RANDOM_ACCESS.req */
struct ph_rach_req_param {
	uint8_t ra;
	uint8_t ta;
	uint8_t tx_power;
	uint8_t is_combined_ccch;
	uint16_t offset;
};

/* for PH-RANDOM_ACCESS.ind */
struct ph_rach_ind_param {
	uint8_t ra;
	uint8_t acc_delay;
	uint32_t fn;
};

/* for PH-[UNIT]DATA.{req,ind} */
struct ph_data_param {
	uint8_t link_id;
	uint8_t chan_nr;
};

struct ph_conn_ind_param {
	uint32_t fn;
};

struct osmo_phsap_prim {
	struct osmo_prim_hdr oph;
	union {
		struct ph_data_param data;
		struct ph_rach_req_param rach_req;
		struct ph_rach_ind_param rach_ind;
		struct ph_conn_ind_param conn_ind;
	} u;
};

enum lapdm_mode {
	LAPDM_MODE_MS,
	LAPDM_MODE_BTS,
};

enum lapdm_state {
	LAPDm_STATE_NULL = 0,
	LAPDm_STATE_IDLE,
	LAPDm_STATE_SABM_SENT,
	LAPDm_STATE_MF_EST,
	LAPDm_STATE_TIMER_RECOV,
	LAPDm_STATE_DISC_SENT,
};

struct lapdm_entity;

struct lapdm_msg_ctx {
	struct lapdm_datalink *dl;
	int lapdm_fmt;
	uint8_t n201;
	uint8_t chan_nr;
	uint8_t link_id;
	uint8_t addr;
	uint8_t ctrl;
	uint8_t ta_ind;
	uint8_t tx_power_ind;
};

/* TS 04.06 / Section 3.5.2 */
struct lapdm_datalink {
	uint8_t V_send;	/* seq nr of next I frame to be transmitted */
	uint8_t V_ack;	/* last frame ACKed by peer */
	uint8_t N_send;	/* ? set to V_send at Tx time*/
	uint8_t V_recv;	/* seq nr of next I frame expected to be received */
	uint8_t N_recv;	/* expected send seq nr of the next received I frame */
	uint32_t state;
	int seq_err_cond; /* condition of sequence error */
	uint8_t own_busy, peer_busy;
	struct osmo_timer_list t200;
	uint8_t retrans_ctr;
	struct llist_head send_queue; /* frames from L3 */
	struct msgb *send_buffer; /* current frame transmitting */
	int send_out; /* how much was sent from send_buffer */
	uint8_t tx_hist[8][200]; /* tx history buffer */
	int tx_length[8]; /* length in history buffer */
	struct llist_head tx_queue; /* frames to L1 */
	struct lapdm_msg_ctx mctx; /* context of established connection */
	struct msgb *rcv_buffer; /* buffer to assemble the received message */

	struct lapdm_entity *entity;
};

enum lapdm_dl_sapi {
	DL_SAPI0	= 0,
	DL_SAPI3	= 1,
	_NR_DL_SAPI
};

typedef int (*lapdm_cb_t)(struct msgb *msg, struct lapdm_entity *le, void *ctx);

struct lapdm_cr_ent {
	uint8_t cmd;
	uint8_t resp;
};

/* register message handler for messages that are sent from L2->L3 */
struct lapdm_entity {
	struct lapdm_datalink datalink[_NR_DL_SAPI];
	int last_tx_dequeue; /* last entity that was dequeued */
	int tx_pending; /* currently a pending frame not confirmed by L1 */
	enum lapdm_mode mode; /* are we in BTS mode or MS mode */

	struct {
		/* filled-in once we set the lapdm_mode above */
		struct lapdm_cr_ent loc2rem;
		struct lapdm_cr_ent rem2loc;
	} cr;

	void *l1_ctx;	/* context for layer1 instance */
	void *l3_ctx;	/* context for layer3 instance */

	osmo_prim_cb l1_prim_cb;
	lapdm_cb_t l3_cb;	/* callback for sending stuff to L3 */

	struct lapdm_channel *lapdm_ch;
};

/* the two lapdm_entities that form a GSM logical channel (ACCH + DCCH) */
struct lapdm_channel {
	struct llist_head list;
	char *name;
	struct lapdm_entity lapdm_acch;
	struct lapdm_entity lapdm_dcch;
};

const char *get_rsl_name(int value);
extern const char *lapdm_state_names[];

/* initialize a LAPDm entity */
void lapdm_entity_init(struct lapdm_entity *le, enum lapdm_mode mode);
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

#endif /* _OSMOCOM_LAPDM_H */
