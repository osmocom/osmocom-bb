#ifndef _OSMOCOM_LAPDM_H
#define _OSMOCOM_LAPDM_H

#include <stdint.h>

#include <osmocore/timer.h>
#include <osmocore/msgb.h>

#include <l1a_l23_interface.h>

enum lapdm_state {
	LAPDm_STATE_NULL,
	LAPDm_STATE_IDLE,
	LAPDm_STATE_SABM_SENT,
	LAPDm_STATE_MF_EST,
	LAPDm_STATE_TIMER_RECOV,
	LAPDm_STATE_OWN_RCVR_BUSY,
};

struct lapdm_entity;
struct osmocom_ms;

/* TS 04.06 / Section 3.5.2 */
struct lapdm_datalink {
	uint8_t V_send;	/* seq nr of next I frame to be transmitted */
	uint8_t V_ack;	/* last frame ACKed by peer */
	uint8_t N_send;	/* ? set to V_send at Tx time*/
	uint8_t V_recv;	/* seq nr of next I frame expected to be received */
	uint8_t N_recv;	/* expected send seq nr of the next received I frame */
	enum lapdm_state state;
	struct timer_list t200;
	uint8_t retrans_ctr;

	struct lapdm_entity *entity;
};

enum lapdm_dl_sapi {
	DL_SAPI0	= 0,
	DL_SAPI3	= 1,
	_NR_DL_SAPI
};

struct lapdm_entity {
	struct lapdm_datalink datalink[_NR_DL_SAPI];
	struct osmocom_ms *ms;
};

/* initialize a LAPDm entity */
void lapdm_init(struct lapdm_entity *le, struct osmocom_ms *ms);

/* input into layer2 (from layer 1) */
int l2_ph_data_ind(struct msgb *msg, struct lapdm_entity *le, struct l1_info_dl *l1i);

/* input into layer2 (from layer 3) */
int rslms_recvmsg(struct msgb *msg, struct osmocom_ms *ms);

/* sending messages up from L2 to L3 */
int rslms_sendmsg(struct msgb *msg, struct osmocom_ms *ms);

#endif /* _OSMOCOM_LAPDM_H */
