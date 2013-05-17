#ifndef _GSM48_MM_H
#define _GSM48_MM_H

/* GSM 04.07 9.2.2 */
#define GSM48_MMXX_MASK			0xf00
#define GSM48_MMCC_CLASS		0x100
#define GSM48_MMSS_CLASS		0x200
#define GSM48_MMSMS_CLASS		0x300
#define GSM48_MMCC_EST_REQ		0x110
#define GSM48_MMCC_EST_IND		0x112
#define GSM48_MMCC_EST_CNF		0x111
#define GSM48_MMCC_REL_REQ		0x120
#define GSM48_MMCC_REL_IND		0x122
#define GSM48_MMCC_DATA_REQ		0x130
#define GSM48_MMCC_DATA_IND		0x132
#define GSM48_MMCC_UNIT_DATA_REQ	0x140
#define GSM48_MMCC_UNIT_DATA_IND	0x142
#define GSM48_MMCC_SYNC_IND		0x152
#define GSM48_MMCC_REEST_REQ		0x160
#define GSM48_MMCC_REEST_CNF		0x161
#define GSM48_MMCC_ERR_IND		0x172
#define GSM48_MMCC_PROMPT_IND		0x182
#define GSM48_MMCC_PROMPT_REJ		0x184
#define GSM48_MMSS_EST_REQ		0x210
#define GSM48_MMSS_EST_IND		0x212
#define GSM48_MMSS_EST_CNF		0x211
#define GSM48_MMSS_REL_REQ		0x220
#define GSM48_MMSS_REL_IND		0x222
#define GSM48_MMSS_DATA_REQ		0x230
#define GSM48_MMSS_DATA_IND		0x232
#define GSM48_MMSS_UNIT_DATA_REQ	0x240
#define GSM48_MMSS_UNIT_DATA_IND	0x242
#define GSM48_MMSS_REEST_REQ		0x260
#define GSM48_MMSS_REEST_CNF		0x261
#define GSM48_MMSS_ERR_IND		0x272
#define GSM48_MMSS_PROMPT_IND		0x282
#define GSM48_MMSS_PROMPT_REJ		0x284
#define GSM48_MMSMS_EST_REQ		0x310
#define GSM48_MMSMS_EST_IND		0x312
#define GSM48_MMSMS_EST_CNF		0x311
#define GSM48_MMSMS_REL_REQ		0x320
#define GSM48_MMSMS_REL_IND		0x322
#define GSM48_MMSMS_DATA_REQ		0x330
#define GSM48_MMSMS_DATA_IND		0x332
#define GSM48_MMSMS_UNIT_DATA_REQ	0x340
#define GSM48_MMSMS_UNIT_DATA_IND	0x342
#define GSM48_MMSMS_REEST_REQ		0x360
#define GSM48_MMSMS_REEST_CNF		0x361
#define GSM48_MMSMS_ERR_IND		0x372
#define GSM48_MMSMS_PROMPT_IND		0x382
#define GSM48_MMSMS_PROMPT_REJ		0x384

#define MMXX_ALLOC_SIZE			256
#define MMXX_ALLOC_HEADROOM		64

/* MMxx-SAP header */
struct gsm48_mmxx_hdr {
	int		msg_type; /* MMxx_* primitive */
	uint32_t	ref; /* reference to transaction */
	uint32_t	transaction_id; /* transaction identifier */
	uint8_t		sapi; /* sapi */
	uint8_t		emergency; /* emergency type of call */
	uint8_t		cause; /* cause used for release */
};

/* GSM 6.1.2 */
#define	GSM48_MMR_REG_REQ		0x01
#define	GSM48_MMR_REG_CNF		0x02
#define	GSM48_MMR_NREG_REQ		0x03
#define	GSM48_MMR_NREG_IND		0x04

/* MMR-SAP header */
struct gsm48_mmr {
	int		msg_type;

	uint8_t		cause;
};

/* GSM 04.07 9.2.1 */
#define GSM48_MMXX_ST_IDLE		0
#define GSM48_MMXX_ST_CONN_PEND		1
#define GSM48_MMXX_ST_DEDICATED		2
#define GSM48_MMXX_ST_CONN_SUSP		3
#define GSM48_MMXX_ST_REESTPEND		4

/* GSM 04.08 4.1.2.1 */
#define	GSM48_MM_ST_NULL		0
#define GSM48_MM_ST_LOC_UPD_INIT	3
#define GSM48_MM_ST_WAIT_OUT_MM_CONN	5
#define GSM48_MM_ST_MM_CONN_ACTIVE	6
#define GSM48_MM_ST_IMSI_DETACH_INIT	7
#define GSM48_MM_ST_PROCESS_CM_SERV_P	8
#define GSM48_MM_ST_WAIT_NETWORK_CMD	9
#define GSM48_MM_ST_LOC_UPD_REJ		10
#define GSM48_MM_ST_WAIT_RR_CONN_LUPD	13
#define GSM48_MM_ST_WAIT_RR_CONN_MM_CON	14
#define GSM48_MM_ST_WAIT_RR_CONN_IMSI_D	15
#define GSM48_MM_ST_WAIT_REEST		17
#define GSM48_MM_ST_WAIT_RR_ACTIVE	18
#define GSM48_MM_ST_MM_IDLE		19
#define GSM48_MM_ST_WAIT_ADD_OUT_MM_CON	20
#define GSM48_MM_ST_MM_CONN_ACTIVE_VGCS	21
#define GSM48_MM_ST_WAIT_RR_CONN_VGCS	22
#define GSM48_MM_ST_LOC_UPD_PEND	23
#define GSM48_MM_ST_IMSI_DETACH_PEND	24
#define GSM48_MM_ST_RR_CONN_RELEASE_NA	25

/* GSM 04.08 4.1.2.1 */
#define GSM48_MM_SST_NORMAL_SERVICE	1
#define GSM48_MM_SST_ATTEMPT_UPDATE	2
#define GSM48_MM_SST_LIMITED_SERVICE	3
#define GSM48_MM_SST_NO_IMSI		4
#define GSM48_MM_SST_NO_CELL_AVAIL	5
#define GSM48_MM_SST_LOC_UPD_NEEDED	6
#define GSM48_MM_SST_PLMN_SEARCH	7
#define GSM48_MM_SST_PLMN_SEARCH_NORMAL	8
#define GSM48_MM_SST_RX_VGCS_NORMAL	9
#define GSM48_MM_SST_RX_VGCS_LIMITED	10

/* MM events */
#define GSM48_MM_EVENT_CELL_SELECTED	1
#define GSM48_MM_EVENT_NO_CELL_FOUND	2
#define GSM48_MM_EVENT_TIMEOUT_T3210	3
#define GSM48_MM_EVENT_TIMEOUT_T3211	4
#define GSM48_MM_EVENT_TIMEOUT_T3212	5
#define GSM48_MM_EVENT_TIMEOUT_T3213	6
#define GSM48_MM_EVENT_TIMEOUT_T3220	7
#define GSM48_MM_EVENT_TIMEOUT_T3230	8
#define GSM48_MM_EVENT_TIMEOUT_T3240	9
#define GSM48_MM_EVENT_IMSI_DETACH	10
#define GSM48_MM_EVENT_POWER_OFF	11
#define GSM48_MM_EVENT_PAGING		12
#define GSM48_MM_EVENT_AUTH_RESPONSE	13
#define GSM48_MM_EVENT_SYSINFO		14
#define GSM48_MM_EVENT_USER_PLMN_SEL	15
#define GSM48_MM_EVENT_LOST_COVERAGE	16

/* message for MM events */
struct gsm48_mm_event {
	uint32_t	msg_type;

	uint8_t		sres[4];
};

/* GSM 04.08 MM timers */
#define GSM_T3210_MS    20, 0
#define GSM_T3211_MS    15, 0
/* T3212 is given by SYSTEM INFORMATION */
#define GSM_T3213_MS    4, 0
#define GSM_T3220_MS    5, 0
#define GSM_T3230_MS    15, 0
#define GSM_T3240_MS    10, 0
#define GSM_T3241_MS    300, 0

/* MM sublayer instance */
struct gsm48_mmlayer {
	struct osmocom_ms	*ms;
	int			state;
	int			substate;

	/* queue for RR-SAP, MMxx-SAP, MMR-SAP, events message upwards */
	struct llist_head       rr_upqueue;
	struct llist_head       mmxx_upqueue;
	struct llist_head       mmr_downqueue;
	struct llist_head       event_queue;

	/* timers */
	struct osmo_timer_list	t3210, t3211, t3212, t3213;
	struct osmo_timer_list	t3220, t3230, t3240;
	int			t3212_value;
	int			start_t3211; /* remember to start timer */

	/* list of MM connections */
	struct llist_head	mm_conn;

	/* network name */
	char			name_short[32];
	char			name_long[32];

	/* location update */
	uint8_t			lupd_pending;	/* current pending loc. upd. */
	uint8_t			lupd_type;	/* current coded type */
	uint8_t			lupd_attempt;	/* attempt counter */
	uint8_t			lupd_ra_failure;/* random access failed */
	uint8_t			lupd_rej_cause;	/* cause of last reject */
	uint8_t			lupd_periodic;	/* periodic update pending */
	uint8_t			lupd_retry;	/* pending T3211/T3213 to */
	uint16_t		lupd_mcc, lupd_mnc, lupd_lac;

	/* imsi detach */
	uint8_t			delay_detach;	/* do detach when possible */

	/* other */
	uint8_t			est_cause; /* cause of establishment msg */
	int			mr_substate;	/* rem most recent substate */
	uint8_t			power_off_idle; /* waits for IDLE before po */

	/* sapi 3 */
	int			sapi3_link;
};

/* MM connection entry */
struct gsm48_mm_conn {
	struct llist_head	list;
	struct gsm48_mmlayer	*mm;

	/* ref and type form a unique tupple */
	uint32_t		ref; /* reference to trans */
	uint8_t			protocol;
	uint8_t			transaction_id;
	uint8_t			sapi;

	int			state;
};

uint8_t gsm48_current_pwr_lev(struct gsm_settings *set, uint16_t arfcn);
int gsm48_mm_init(struct osmocom_ms *ms);
int gsm48_mm_exit(struct osmocom_ms *ms);
struct msgb *gsm48_mmr_msgb_alloc(int msg_type);
struct msgb *gsm48_mmevent_msgb_alloc(int msg_type);
int gsm48_mmevent_msg(struct osmocom_ms *ms, struct msgb *msg);
int gsm48_mmr_downmsg(struct osmocom_ms *ms, struct msgb *msg);
int gsm48_rr_dequeue(struct osmocom_ms *ms);
int gsm48_mmxx_dequeue(struct osmocom_ms *ms);
int gsm48_mmr_dequeue(struct osmocom_ms *ms);
int gsm48_mmevent_dequeue(struct osmocom_ms *ms);
int gsm48_mmxx_downmsg(struct osmocom_ms *ms, struct msgb *msg);
struct msgb *gsm48_mmxx_msgb_alloc(int msg_type, uint32_t ref,
	uint8_t transaction_id, uint8_t sapi);
const char *get_mmr_name(int value);
const char *get_mm_name(int value);
const char *get_mmxx_name(int value);
extern const char *gsm48_mm_state_names[];
extern const char *gsm48_mm_substate_names[];

#endif /* _GSM48_MM_H */
