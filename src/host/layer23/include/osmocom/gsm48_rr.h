#ifndef _GSM48_RR_H
#define _GSM48_RR_H

#include "osmocore/protocol/gsm_04_08.h"

#define GSM_TA_CM			55385

/* GSM 04.07 9.1.2 */
#define	GSM48_RR_EST_REQ		0x10
#define	GSM48_RR_EST_IND		0x12
#define	GSM48_RR_EST_CNF		0x11
#define	GSM48_RR_REL_IND		0x22
#define	GSM48_RR_SYNC_IND		0x32
#define	GSM48_RR_DATA_REQ		0x40
#define	GSM48_RR_DATA_IND		0x42
#define	GSM48_RR_UNIT_DATA_IND		0x52
#define	GSM48_RR_ABORT_REQ		0x60
#define	GSM48_RR_ABORT_IND		0x62
#define	GSM48_RR_ACT_REQ		0x70

#define RR_EST_CAUSE_EMERGENCY		1
#define RR_EST_CAUSE_REESTAB_TCH_F	2
#define RR_EST_CAUSE_REESTAB_TCH_H	3
#define RR_EST_CAUSE_REESTAB_2_TCH_H	4
#define RR_EST_CAUSE_ANS_PAG_ANY	5
#define RR_EST_CAUSE_ANS_PAG_SDCCH	6
#define RR_EST_CAUSE_ANS_PAG_TCH_F	7
#define RR_EST_CAUSE_ANS_PAG_TCH_ANY	8
#define RR_EST_CAUSE_ORIG_TCHF		9
#define RR_EST_CAUSE_LOC_UPD		12
#define RR_EST_CAUSE_OTHER_SDCCH	13

#define RR_REL_CAUSE_UNDEFINED		0
#define RR_REL_CAUSE_NORMAL		1
#define RR_REL_CAUSE_NOT_AUTHORIZED	2
#define RR_REL_CAUSE_RA_FAILURE		3
#define RR_REL_CAUSE_T3122		4
#define RR_REL_CAUSE_TRY_LATER		5
#define RR_REL_CAUSE_EMERGENCY_ONLY	6
#define RR_REL_CAUSE_LOST_SIGNAL	7

#define L3_ALLOC_SIZE			256
#define L3_ALLOC_HEADROOM		64

#define RR_ALLOC_SIZE			256
#define RR_ALLOC_HEADROOM		64

/* GSM 04.08 RR-SAP header */
struct gsm48_rr_hdr {
	uint32_t		msg_type; /* RR-* primitive */
	uint8_t			cause;
};

/* GSM 04.07 9.1.1 */
#define GSM48_RR_ST_IDLE		0
#define GSM48_RR_ST_CONN_PEND		1
#define GSM48_RR_ST_DEDICATED		2
#define GSM48_RR_ST_REL_PEND		3

/* channel description */
struct gsm48_rr_cd {
	uint8_t			tsc;
	uint8_t			h; /* using hopping */
	uint16_t		arfcn; /* dedicated mode */
	uint8_t			maio;
	uint8_t			hsn;
	uint8_t			chan_nr; /* type, slot, sub slot */
	uint8_t			link_id;
	uint8_t			ta; /* timing advance */
	uint8_t			mob_alloc_lv[9]; /* len + up to 64 bits */
	uint8_t			start_t1, start_t2, start_t3; /* start. time */
};

/* measurements */
struct gsm48_rr_meas {
	uint8_t			rxlev_full;
	uint8_t			rxlev_sub;
	uint8_t			rxqual_full;
	uint8_t			rxqual_sub;
	uint8_t			dtx;
	uint8_t			ba;
	uint8_t			meas_valid;
	uint8_t			ncell_na;
	uint8_t			count;
	uint8_t			rxlev_nc[6];
	uint8_t			bsic_nc[6];
	uint8_t			bcch_f_nc[6];
};

struct gsm48_cr_hist {
	uint32_t	fn;
	uint8_t		chan_req;
	uint8_t		valid;
};

/* RR sublayer instance */
struct gsm48_rrlayer {
	struct osmocom_ms	*ms;
	int			state;

	/* queue for RSL-SAP message upwards */
	struct llist_head	rsl_upqueue;

	/* queue for messages while RR connection is built up */
	struct llist_head       downqueue;

	/* timers */
	struct timer_list	t_rel_wait; /* wait for L2 to transmit UA */
	struct timer_list	t3110;
	struct timer_list	t3122;
	struct timer_list	t3124;
	struct timer_list	t3126;
	int			t3126_value;
#ifndef TODO
	struct timer_list	temp_rach_ti; /* temporary timer */
#endif

	/* states if RR-EST-REQ was used */
	uint8_t			rr_est_req;
	struct msgb		*rr_est_msg;

	/* channel request states */
	uint8_t			wait_assign; /* waiting for assignment state */
	uint8_t			n_chan_req; /* number left, incl. current */
	uint8_t			chan_req_val; /* current request value */ 
	uint8_t			chan_req_mask; /* mask of random bits */ 

	/* cr_hist must be signed and greater 8 bit, -1 = no value */
	struct gsm48_cr_hist	cr_hist[3];

	/* current channel descriptions */
	struct gsm48_rr_cd	cd_now;

	/* current cipering */
	uint8_t			cipher_on;
	uint8_t			cipher_type; /* 10.5.2.9 */

	/* special states when changing channel */
	uint8_t			hando_susp_state;
	uint8_t			assign_susp_state;
	uint8_t			resume_last_state;
	struct gsm48_rr_cd	cd_last;

	/* measurements */
	struct gsm48_rr_meas	meas;

	/* BA range */
	uint8_t			ba_ranges;
	uint32_t		ba_range[16];
};

const char *get_rr_name(int value);
extern int gsm48_rr_init(struct osmocom_ms *ms);
extern int gsm48_rr_exit(struct osmocom_ms *ms);
int gsm48_rsl_dequeue(struct osmocom_ms *ms);
int gsm48_rr_downmsg(struct osmocom_ms *ms, struct msgb *msg);
struct msgb *gsm48_l3_msgb_alloc(void);
struct msgb *gsm48_rr_msgb_alloc(int msg_type);
int gsm48_decode_lai(struct gsm48_loc_area_id *lai, uint16_t *mcc,
	uint16_t *mnc, uint16_t *lac);
int gsm48_rr_enc_cm2(struct osmocom_ms *ms, struct gsm48_classmark2 *cm);
int gsm48_rr_tx_rand_acc(struct osmocom_ms *ms, struct msgb *msg);
int gsm48_rr_los(struct osmocom_ms *ms);
int gsm48_rr_rach_conf(struct osmocom_ms *ms, uint32_t fn);
extern const char *gsm48_rr_state_names[];

#endif /* _GSM48_RR_H */
