/*
 * (C) 2010 by Andreas Eversberg <jolly@eversberg.eu>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/* application layer primitives */

/* GSM 6.1.2 */
#define	GSM_MMR_REG_REQ		0x1101
#define	GSM_MMR_REG_CNF		0x1102
#define	GSM_MMR_NREG_REQ	0x1103
#define	GSM_MMR_NREG_IND	0x1104

#define MMR_F_IMSI		0x0001
#define MMR_F_CAUSE		0x0002

struct gsm48_mmr {
	u_int32_t       msg_type;

	u_int32_t	fields;
	char		imsi[16];
	u_int8_t	cause;
};

/* GSM 6.3.2 */
#define GSM_MNSS_BEGIN_REQ	0x2110
#define GSM_MNSS_BEGIN_IND	0x2112
#define GSM_MNSS_FACILITY_REQ	0x2120
#define GSM_MNSS_FACILITY_IND	0x2122
#define GSM_MNSS_END_REQ	0x2130
#define GSM_MNSS_END_IND	0x2132

#define MNSS_F_REGISTER		0x0001
#define MNSS_F_FACILITY		0x0002
#define MNSS_F_RELCOMPL		0x0004

struct gsm48_mnss {
	u_int32_t       msg_type;
	u_int32_t       callref;

	u_int32_t	fields;
	struct gsm_mnss_register	register;
	struct gsm_mnss_facility	facility;
	struct gsm_mnss_release_compl	release_compl;
};

/* interlayer primitives */

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

/* GSM 04.08 RR-SAP header */
struct gsm48_rr_hdr {
	u_int32_t       msg_type; /* RR_* primitive */
	u_int8_t	cause;
};

/* GSM 04.07 9.2.2 */
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

/* GSM 04.08 MMxx-SAP header */
struct gsm48_mmxx_hdr {
	u_int32_t       msg_type; /* RR_* primitive */
	u_int8_t	reject_cause;
};

/* GSM 04.07 9.1.1 */
#define GSM48_RR_ST_IDLE		0
#define GSM48_RR_ST_CONN_PEND		1
#define GSM48_RR_ST_DEDICATED		2

/* GSM 04.07 6.1.1 */
#define GSM48_MMR_ST_NOTUPDATED		0
#define GSM48_MMR_ST_WAIT		1
#define GSM48_MMR_ST_UPDATED		2

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

/* GSM 04.08 5.1.2.2 */
#define	GSM48_CC_ST_NULL		0
#define	GSM48_CC_ST_INITIATED		1
#define	GSM48_CC_ST_MO_CALL_PROC	3
#define	GSM48_CC_ST_CALL_DELIVERED	4
#define	GSM48_CC_ST_CALL_PRESENT	6
#define	GSM48_CC_ST_CALL_RECEIVED	7
#define	GSM48_CC_ST_CONNECT_REQUEST	8
#define	GSM48_CC_ST_MO_TERM_CALL_CONF	9
#define	GSM48_CC_ST_ACTIVE		10
#define	GSM48_CC_ST_DISCONNECT_REQ	12
#define	GSM48_CC_ST_DISCONNECT_IND	12
#define	GSM48_CC_ST_RELEASE_REQ		19
#define	GSM48_CC_ST_MO_ORIG_MODIFY	26
#define	GSM48_CC_ST_MO_TERM_MODIFY	27
#define	GSM48_CC_ST_CONNECT_IND		28

/* MM events */
#define GSM48_MM_EVENT_NEW_LAI		1
#define GSM48_MM_EVENT_TIMEOUT_T3211	2
#define GSM48_MM_EVENT_TIMEOUT_T3212	3
#define GSM48_MM_EVENT_TIMEOUT_T3213	4
#define GSM48_MM_EVENT_IMSI_DETACH	5
#define GSM48_MM_EVENT_IMSI_ATTACH	6
#define GSM48_MM_EVENT_POWER_OFF	7
#define GSM48_MM_EVENT_PAGING		9
#define GSM48_MM_EVENT_AUTH_RESPONSE	10

/* message for MM events */
struct gsm48_mm_event {
	u_int32_t       msg_type;

	u_int8_t	sres[4];
};

/* GSM 04.08 MM timers */
#define GSM_T3210_MS	20, 0
#define GSM_T3211_MS	15, 0
// T3212 is given by SYSTEM INFORMATION
#define GSM_T3213_MS	4, 0
#define GSM_T3220_MS	5, 0
#define GSM_T3230_MS	15, 0
#define GSM_T3240_MS	10, 0
#define GSM_T3241_MS	300, 0


/* MM sublayer instance */
struct gsm48_mmlayer {
	struct osmocom_ms	*ms;
	int			state;
	int			substate;

	/* queue for MMxx-SAP message upwards */
	struct llist_head       mm_upqueue;

	/* timers */
	struct timer_list	t3210;
	struct timer_list	t3211;
	struct timer_list	t3212;
	struct timer_list	t3213;
	struct timer_list	t3240;
	int			t3212_value;

	/* list of MM connections */
	struct llist_head	mm_conn;

	/* network name */
	char			name_short[32];
	char			name_long[32];

	/* location update */
	uint8_t			lupd_type;	/* current coded type */
	int			lupd_attempt;	/* attempt counter */
	uint8_t			lupd_rej_cause;	/* cause of last reject */
};

/* MM connection types */
#define GSM48_MM_CONN_TYPE_CC	1
#define GSM48_MM_CONN_TYPE_SS	2
#define GSM48_MM_CONN_TYPE_SMS	3

/* MM connection entry */
struct gsm48_mm_conn {
	struct llist_head	list;

	unsigned int		ref;
	int			type;
};


/* GSM 04.08 RR timers */
#define GSM_T3126_MS	5, 0

struct gsm_rr_chan_desc {
	struct gsm48_chan_desc	chan_desc;
	uint8_t			power_command;
	uint8_t			frq_list_before[131];
	uint8_t			frq_list_after[131];
	uint8_t			cell_chan_desc[16];
	uint8_t			...
	uint8_t
	uint8_t
};

/* RR sublayer instance */
struct gsm_rrlayer {
	struct osmocom_ms	*ms;
	int			state;

	/* queue for RR-SAP message upwards */
	struct llist_head	rr_upqueue;

	/* queue for messages while RR connection is built up */
	struct llist_head       downqueue;

	/* timers */
	struct timer_list	t3122;
	struct timer_list	t3124;
	struct timer_list	t3126;
	int			t3126_value;

	/* states if RR-EST-REQ was used */
	int			rr_est_req;
	struct msgb		*rr_est_msg;

	/* channel request states */
	u_int8_t		chan_req;
	/* cr_hist must be signed and greater 8 bit */
	int			cr_hist[3];

	/* collection of all channel descriptions */
	struct gsm_rr_chan_desc	chan_desc;

	/* special states when changing channel */
	int			hando_susp_state;
	int			assign_susp_state;
	int			resume_last_state;
	struct gsm_rr_chan_desc	chan_last;

};

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
#define RR_REL_CAUSE_NOT_AUTHORIZED	1
#define RR_REL_CAUSE_RA_FAILURE		2



