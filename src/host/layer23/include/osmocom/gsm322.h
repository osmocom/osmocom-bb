#ifndef _GSM322_H
#define _GSM322_H

/* 4.3.1.1 List of states for PLMN slection process (automatic mode) */
#define GSM322_A0_NULL			0
#define	GSM322_A1_TRYING_RPLMN		1
#define	GSM322_A2_ON_PLMN		2
#define	GSM322_A3_TRYING_PLMN		3
#define	GSM322_A4_WAIT_FOR_PLMN		4
#define	GSM322_A5_HPLMN_SEARCH		5
#define	GSM322_A6_NO_SIM		6

/* 4.3.1.2 List of states for PLMN slection process (manual mode) */
#define GSM322_M0_NULL			0
#define	GSM322_M1_TRYING_RPLMN		1
#define	GSM322_M2_ON_PLMN		2
#define	GSM322_M3_NOT_ON_PLMN		3
#define	GSM322_M4_TRYING_PLMN		4
#define	GSM322_M5_NO_SIM		5

/* 4.3.2 List of states for cell selection process */
#define GSM322_C0_NULL			0
#define	GSM322_C1_NORMAL_CELL_SEL	1
#define	GSM322_C2_STORED_CELL_SEL	2
#define	GSM322_C3_CAMPED_NORMALLY	3
#define	GSM322_C4_NORMAL_CELL_RESEL	4
#define	GSM322_C5_CHOOSE_CELL		5
#define	GSM322_C6_ANY_CELL_SEL		6
#define	GSM322_C7_CAMPED_ANY_CELL	7
#define	GSM322_C8_ANY_CELL_RESEL	8
#define	GSM322_C9_CHOOSE_ANY_CELL	9
#define GSM322_PLMN_SEARCH		10
#define GSM322_HPLMN_SEARCH		11

/* GSM 03.22 events */
#define	GSM322_EVENT_SWITCH_ON		1
#define	GSM322_EVENT_SWITCH_OFF		2	
#define	GSM322_EVENT_SIM_INSERT		3
#define	GSM322_EVENT_SIM_REMOVE		4
#define	GSM322_EVENT_REG_SUCCESS	5
#define	GSM322_EVENT_REG_FAILED		6
#define	GSM322_EVENT_ROAMING_NA		7
#define	GSM322_EVENT_INVALID_SIM	8
#define	GSM322_EVENT_NEW_PLMN		9
#define	GSM322_EVENT_ON_PLMN		10
#define	GSM322_EVENT_PLMN_SEARCH_START	11
#define	GSM322_EVENT_PLMN_SEARCH_END	12
#define	GSM322_EVENT_USER_RESEL		13
#define	GSM322_EVENT_PLMN_AVAIL		14
#define	GSM322_EVENT_CHOSE_PLMN		15
#define	GSM322_EVENT_SEL_MANUAL		16
#define	GSM322_EVENT_SEL_AUTO		17
#define	GSM322_EVENT_CELL_FOUND		18
#define	GSM322_EVENT_NO_CELL_FOUND	19
#define	GSM322_EVENT_LEAVE_IDLE		20
#define	GSM322_EVENT_RET_IDLE		21
#define	GSM322_EVENT_CELL_RESEL		22
#define	GSM322_EVENT_SYSINFO		23
#define	GSM322_EVENT_HPLMN_SEARCH	24

enum {
	PLMN_MODE_MANUAL,
	PLMN_MODE_AUTO
};

/* node for each PLMN */
struct gsm322_plmn_list {
	struct llist_head	entry;
	uint16_t		mcc, mnc;
	int8_t			rxlev_db; /* rx level in real dB */
	uint8_t			cause; /* cause value, if PLMN is not allowed */
};

/* node for each forbidden LA */
struct gsm322_la_list {
	struct llist_head	entry;
	uint16_t		mcc, mnc, lac;
	uint8_t			cause;
};

/* node for each BA-List */
struct gsm322_ba_list {
	struct llist_head	entry;
	uint16_t		mcc, mnc;
	/* Band allocation for 1024 frequencies.
	 * First bit of first index is frequency 0.
	 */
	uint8_t			freq[128];
};

#define GSM322_CS_FLAG_SUPPORT	0x01 /* frequency is supported by radio */
#define GSM322_CS_FLAG_BA	0x02 /* frequency is part of the current ba */
#define GSM322_CS_FLAG_POWER	0x04 /* frequency was power scanned */
#define GSM322_CS_FLAG_SIGNAL	0x08 /* valid signal detected */
#define GSM322_CS_FLAG_SYSINFO	0x10 /* complete sysinfo received */
#define GSM322_CS_FLAG_BARRED	0x20 /* cell is barred */
#define GSM322_CS_FLAG_FORBIDD	0x40 /* cell in list of forbidden LAs */
#define GSM322_CS_FLAG_TEMP_AA	0x80 /* if temporary available and allowable */

/* Cell selection list */
struct gsm322_cs_list {
	uint8_t			flags; /* see GSM322_CS_FLAG_* */
	int8_t			rxlev_db; /* rx level in real dB */
	struct gsm48_sysinfo	*sysinfo;
#if 0
	int8_t			min_db; /* minimum level to enter cell */
	int8_t			max_pwr; /* maximum power to access cell */
	uint16_t		class_barr; /* barred classes */
	uint16_t		mcc, mnc, lac; /* received mcc, mnc, lac */
#endif
};

/* PLMN search process */
struct gsm322_plmn {
	struct osmocom_ms	*ms;
	int			state; /* GSM322_Ax_* or GSM322_Mx_* */

	struct llist_head	event_queue; /* event messages */
	struct llist_head	sorted_plmn; /* list of sorted PLMN */
	struct llist_head	forbidden_la; /* forbidden LAs */

	struct timer_list	timer;

	int			plmn_curr; /* current index in sorted_plmn */
	uint16_t		mcc, mnc; /* current network selected */
};

/* state of CCCH activation */
#define GSM322_CCCH_ST_IDLE	0	/* no connection */
#define GSM322_CCCH_ST_INIT	1	/* initalized */
#define GSM322_CCCH_ST_SYNC	2	/* got sync */
#define GSM322_CCCH_ST_DATA	3	/* receiveing data */

struct gsm48_sysinfo;
/* Cell selection process */
struct gsm322_cellsel {
	struct osmocom_ms	*ms;
	int			state; /* GSM322_Cx_* */

	struct llist_head	event_queue; /* event messages */
	struct llist_head	ba_list; /* BCCH Allocation per PLMN */

	struct timer_list	timer;

	uint16_t		mcc, mnc; /* current network to search for */
	struct gsm322_cs_list	list[1024]; /* cell selection list per freq. */

	uint8_t			powerscan; /* currently scanning for power */
	uint32_t		scan_state; /* special state of current scan */
	uint8_t			ccch_state; /* special state of current ccch */
	uint16_t		arfcn; /* current tuned idle mode arfcn */
	struct gsm48_sysinfo	*si; /* current sysinfo */

	uint8_t			selected; /* if a cell is selected */
	uint16_t		sel_arfcn;
	struct gsm48_sysinfo	sel_si; /* copy of selected cell, will update */
	uint16_t		sel_mcc, sel_mnc, sel_lac, sel_id;
};

/* GSM 03.22 message */
struct gsm322_msg {
	int			msg_type;
	uint16_t		mcc, mnc;
	uint8_t			sysinfo; /* system information type */
	uint8_t			reject; /* location update reject cause */
};

#define	GSM322_ALLOC_SIZE	sizeof(struct gsm322_msg)
#define GSM322_ALLOC_HEADROOM	0

int gsm322_init(struct osmocom_ms *ms);
int gsm322_exit(struct osmocom_ms *ms);
struct msgb *gsm322_msgb_alloc(int msg_type);
int gsm322_plmn_sendmsg(struct osmocom_ms *ms, struct msgb *msg);
int gsm322_cs_sendmsg(struct osmocom_ms *ms, struct msgb *msg);
int gsm322_c_event(struct osmocom_ms *ms, struct msgb *msg);
int gsm322_plmn_dequeue(struct osmocom_ms *ms);
int gsm322_cs_dequeue(struct osmocom_ms *ms);
int gsm322_add_forbidden_la(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc, uint16_t lac, uint8_t cause);
int gsm322_del_forbidden_la(struct osmocom_ms *ms, uint16_t mcc,
	uint16_t mnc, uint16_t lac);
int gsm322_is_forbidden_la(struct osmocom_ms *ms, uint16_t mcc, uint16_t mnc,
	uint16_t lac);
int gsm322_dump_sorted_plmn(struct osmocom_ms *ms);
int gsm322_dump_cs_list(struct gsm322_cellsel *cs, uint8_t flags,
			void (*print)(void *, const char *, ...), void *priv);
int gsm322_dump_forbidden_la(struct osmocom_ms *ms,
			void (*print)(void *, const char *, ...), void *priv);
int gsm322_dump_ba_list(struct gsm322_cellsel *cs, uint16_t mcc, uint16_t mnc,
			void (*print)(void *, const char *, ...), void *priv);
void start_cs_timer(struct gsm322_cellsel *cs, int sec, int micro);
void start_loss_timer(struct gsm322_cellsel *cs, int sec, int micro);
extern const char *plmn_a_state_names[];
extern const char *plmn_m_state_names[];
extern const char *cs_state_names[];

#endif /* _GSM322_H */
