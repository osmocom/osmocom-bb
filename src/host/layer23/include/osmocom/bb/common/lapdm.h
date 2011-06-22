#ifndef _OSMOCOM_LAPDM_H
#define _OSMOCOM_LAPDM_H

#include <stdint.h>

#include <osmocom/core/timer.h>
#include <osmocom/core/msgb.h>

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

/* register message handler for messages that are sent from L2->L3 */
struct lapdm_entity {
	struct lapdm_datalink datalink[_NR_DL_SAPI];
	int last_tx_dequeue; /* last entity that was dequeued */
	int tx_pending; /* currently a pending frame not confirmed by L1 */

	void *l1_ctx;	/* context for layer1 instance */
	void *l3_ctx;	/* context for layer3 instance */

	lapdm_cb_t l1_cb;	/* callback for sending stuff to L1 */
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
void lapdm_entity_init(struct lapdm_entity *le);
void lapdm_channel_init(struct lapdm_channel *lc);

/* deinitialize a LAPDm entity */
void lapdm_entity_exit(struct lapdm_entity *le);
void lapdm_channel_exit(struct lapdm_channel *lc);

/* input into layer2 (from layer 1) */
int l2_ph_data_ind(struct msgb *msg, struct lapdm_entity *le, uint8_t chan_nr, uint8_t link_id);
int l2_ph_data_conf(struct msgb *msg, struct lapdm_entity *le);

/* L1 confirms channel request */
int l2_ph_chan_conf(struct msgb *msg, struct osmocom_ms *ms, uint32_t frame_nr);

/* input into layer2 (from layer 3) */
int lapdm_rslms_recvmsg(struct msgb *msg, struct lapdm_channel *lc);

int osmol2_register_handler(struct osmocom_ms *ms, lapdm_cb_t cb);

#endif /* _OSMOCOM_LAPDM_H */
