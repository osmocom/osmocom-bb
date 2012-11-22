#ifndef _GSM48_RR_H
#define _GSM48_RR_H

#include <osmocom/gsm/protocol/gsm_04_08.h>

#define GSM_TA_CM			55385

#define	T200_DCCH			1	/* SDCCH/FACCH */
#define	T200_DCCH_SHARED		2	/* SDCCH shares SAPI 0 and 3 */
#define	T200_ACCH			2	/* SACCH SAPI 3 */


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
#define RR_REL_CAUSE_LINK_FAILURE	8

#define RR_SYNC_CAUSE_CIPHERING		1

#define L3_ALLOC_SIZE			256
#define L3_ALLOC_HEADROOM		64

#define RSL_ALLOC_SIZE			256
#define RSL_ALLOC_HEADROOM		64

#define RR_ALLOC_SIZE			256
#define RR_ALLOC_HEADROOM		64

/* GSM 04.08 RR-SAP header */
struct gsm48_rr_hdr {
	uint32_t		msg_type; /* RR-* primitive */
	uint8_t			sapi;
	uint8_t			cause;
};

/* GSM 04.07 9.1.1 */
#define GSM48_RR_ST_IDLE		0
#define GSM48_RR_ST_CONN_PEND		1
#define GSM48_RR_ST_DEDICATED		2
#define GSM48_RR_ST_REL_PEND		3

/* special states for SAPI 3 link */
#define GSM48_RR_SAPI3ST_IDLE		0
#define GSM48_RR_SAPI3ST_WAIT_EST	1
#define GSM48_RR_SAPI3ST_ESTAB		2
#define GSM48_RR_SAPI3ST_WAIT_REL	3

/* modify state */
#define GSM48_RR_MOD_NONE		0
#define GSM48_RR_MOD_IMM_ASS		1
#define GSM48_RR_MOD_ASSIGN		2
#define GSM48_RR_MOD_HANDO		3
#define GSM48_RR_MOD_ASSIGN_RESUME	4
#define GSM48_RR_MOD_HANDO_RESUME	5

/* channel description */
struct gsm48_rr_cd {
	uint8_t			tsc;
	uint8_t			h; /* using hopping */
	uint16_t		arfcn; /* dedicated mode */
	uint8_t			maio;
	uint8_t			hsn;
	uint8_t			chan_nr; /* type, slot, sub slot */
	uint8_t			ind_tx_power; /* last indicated power */
	uint8_t			ind_ta; /* last indicated ta */
	uint8_t			mob_alloc_lv[9]; /* len + up to 64 bits */
	uint8_t			freq_list_lv[131]; /* len + 130 octets */
	uint8_t			freq_seq_lv[10]; /* len + 9 octets */
	uint8_t			cell_desc_lv[17]; /* len + 16 octets */
	uint8_t			start; /* start time available */
	struct gsm_time		start_tm; /* start time */
	uint8_t			mode; /* mode of channel */
	uint8_t			cipher; /* ciphering of channel */
};

struct gsm48_cr_hist {
	uint8_t			valid;
	struct gsm48_req_ref	ref;
};

/* neighbor cell measurements */
struct gsm48_rr_meas {
	/* note: must be sorted by arfcn 1..1023,0 according to SI5* */
	uint8_t nc_num; /* number of measured cells (32 max) */
	int8_t nc_rxlev_dbm[32]; /* -128 = no value */
	uint8_t nc_bsic[32];
	uint16_t nc_arfcn[32];
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
	struct osmo_timer_list	t_starting; /* starting time for chan. access */
	struct osmo_timer_list	t_rel_wait; /* wait for L2 to transmit UA */
	struct osmo_timer_list	t3110;
	struct osmo_timer_list	t3122;
	struct osmo_timer_list	t3124;
	struct osmo_timer_list	t3126;
	int			t3126_value;
#ifndef TODO
	struct osmo_timer_list	temp_rach_ti; /* temporary timer */
#endif

	/* states if RR-EST-REQ was used */
	uint8_t			rr_est_req;
	struct msgb		*rr_est_msg;
	uint8_t			est_cause; /* cause used for establishment */
	uint8_t			paging_mi_type; /* how did we got paged? */

	/* channel request states */
	uint8_t			wait_assign; /* waiting for assignment state */
	uint8_t			n_chan_req; /* number left, incl. current */
	uint8_t			chan_req_val; /* current request value */ 
	uint8_t			chan_req_mask; /* mask of random bits */

	/* state of dedicated mdoe */
	uint8_t			dm_est;

	/* cr_hist */
	uint8_t			cr_ra; /* stores requested ra until confirmed */
	struct gsm48_cr_hist	cr_hist[3];

	/* V(SD) sequence numbers */
	uint16_t		v_sd; /* 16 PD 1-bit sequence numbers packed */

	/* current channel descriptions */
	struct gsm48_rr_cd	cd_now;

	/* current cipering */
	uint8_t			cipher_on;
	uint8_t			cipher_type; /* 10.5.2.9 */

	/* special states when assigning channel */
	uint8_t			modify_state;
	uint8_t			hando_sync_ind, hando_rot, hando_nci, hando_act;
	struct gsm48_rr_cd	cd_last; /* store last cd in case of failure */
	struct gsm48_rr_cd	cd_before; /* before start time */
	struct gsm48_rr_cd	cd_after; /* after start time */

	/* BA range */
	uint8_t			ba_ranges;
	uint32_t		ba_range[16];

	/* measurements */
	struct osmo_timer_list	t_meas;
	struct gsm48_rr_meas	meas;
	uint8_t			monitor;

	/* audio flow */
	uint8_t                 audio_mode;

	/* sapi 3 */
	uint8_t			sapi3_state;
	uint8_t			sapi3_link_id;
};

const char *get_rr_name(int value);
extern int gsm48_rr_init(struct osmocom_ms *ms);
extern int gsm48_rr_exit(struct osmocom_ms *ms);
int gsm48_rsl_dequeue(struct osmocom_ms *ms);
int gsm48_rr_downmsg(struct osmocom_ms *ms, struct msgb *msg);
struct msgb *gsm48_l3_msgb_alloc(void);
struct msgb *gsm48_rr_msgb_alloc(int msg_type);
int gsm48_rr_enc_cm2(struct osmocom_ms *ms, struct gsm48_classmark2 *cm,
	uint16_t arfcn);
int gsm48_rr_tx_rand_acc(struct osmocom_ms *ms, struct msgb *msg);
int gsm48_rr_los(struct osmocom_ms *ms);
int gsm48_rr_rach_conf(struct osmocom_ms *ms, uint32_t fn);
extern const char *gsm48_rr_state_names[];
int gsm48_rr_start_monitor(struct osmocom_ms *ms);
int gsm48_rr_stop_monitor(struct osmocom_ms *ms);
int gsm48_rr_alter_delay(struct osmocom_ms *ms);
int gsm48_rr_tx_voice(struct osmocom_ms *ms, struct msgb *msg);
int gsm48_rr_audio_mode(struct osmocom_ms *ms, uint8_t mode);

#endif /* _GSM48_RR_H */
