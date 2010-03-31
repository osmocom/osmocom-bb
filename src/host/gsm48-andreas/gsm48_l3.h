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
#define	RR_EST_REQ		0x8110
#define	RR_EST_IND		0x8112
#define	RR_EST_CNF		0x8111
#define	RR_REL_IND		0x8122
#define	RR_SYNC_IND		0x8132
#define	RR_DATA_REQ		0x8140
#define	RR_DATA_IND		0x8142
#define	RR_UNIT_DATA_IND	0x8152
#define	RR_ABORT_REQ		0x8160
#define	RR_ABORT_IND		0x8162
#define	RR_ACT_REQ		0x8170

struct gsm48_rr {
	u_int32_t       msg_type; /* RR_* primitive */
	struct msgb	*msg; /* gsm48 msg */
	u_int8_t	cause;
};

/* GSM 04.07 9.2.2 */
#define GSM48_MMCC_EST_REQ		0x9110 todo: renumber
#define GSM48_MMCC_EST_IND		0x9112
#define GSM48_MMCC_EST_CNF		0x9111
#define GSM48_MMCC_REL_REQ		0x9120
#define GSM48_MMCC_REL_IND		0x9122
#define GSM48_MMCC_DATA_REQ		0x9130
#define GSM48_MMCC_DATA_IND		0x9132
#define GSM48_MMCC_UNIT_DATA_REQ	0x9140
#define GSM48_MMCC_UNIT_DATA_IND	0x9142
#define GSM48_MMCC_SYNC_IND		0x9152
#define GSM48_MMCC_REEST_REQ		0x9160
#define GSM48_MMCC_REEST_CNF		0x9161
#define GSM48_MMCC_ERR_IND		0x9172
#define GSM48_MMCC_PROMPT_IND		0x9182
#define GSM48_MMCC_PROMPT_REJ		0x9184
#define GSM48_MMSS_EST_REQ		0x9210
#define GSM48_MMSS_EST_IND		0x9212
#define GSM48_MMSS_EST_CNF		0x9211
#define GSM48_MMSS_REL_REQ		0x9220
#define GSM48_MMSS_REL_IND		0x9222
#define GSM48_MMSS_DATA_REQ		0x9230
#define GSM48_MMSS_DATA_IND		0x9232
#define GSM48_MMSS_UNIT_DATA_REQ	0x9240
#define GSM48_MMSS_UNIT_DATA_IND	0x9242
#define GSM48_MMSS_REEST_REQ		0x9260
#define GSM48_MMSS_REEST_CNF		0x9261
#define GSM48_MMSS_ERR_IND		0x9272
#define GSM48_MMSS_PROMPT_IND		0x9282
#define GSM48_MMSS_PROMPT_REJ		0x9284
#define GSM48_MMSMS_EST_REQ		0x9310
#define GSM48_MMSMS_EST_IND		0x9312
#define GSM48_MMSMS_EST_CNF		0x9311
#define GSM48_MMSMS_REL_REQ		0x9320
#define GSM48_MMSMS_REL_IND		0x9322
#define GSM48_MMSMS_DATA_REQ		0x9330
#define GSM48_MMSMS_DATA_IND		0x9332
#define GSM48_MMSMS_UNIT_DATA_REQ	0x9340
#define GSM48_MMSMS_UNIT_DATA_IND	0x9342
#define GSM48_MMSMS_REEST_REQ		0x9360
#define GSM48_MMSMS_REEST_CNF		0x9361
#define GSM48_MMSMS_ERR_IND		0x9372
#define GSM48_MMSMS_PROMPT_IND		0x9382
#define GSM48_MMSMS_PROMPT_REJ		0x9384

/* GSM 04.07 9.1.1 */
#define GSM_RRSTATE_IDLE		0
#define GSM_RRSTATE_CONN_PEND		1
#define GSM_RRSTATE_DEDICATED		2

/* GSM 04.07 6.1.1 */
#define GSM_MMRSTATE_NOTUPDATED		0
#define GSM_MMRSTATE_WAIT		1
#define GSM_MMRSTATE_UPDATED		2

/* GSM 04.07 9.2.1 */
#define GSM_MMCCSTATE_IDLE		0
#define GSM_MMCCSTATE_CONN_PEND		1
#define GSM_MMCCSTATE_DEDICATED		2
#define GSM_MMCCSTATE_CONN_SUSP		3
#define GSM_MMCCSTATE_REESTPEND		4
#define GSM_MMSSSTATE_IDLE		0
#define GSM_MMSSSTATE_CONN_PEND		1
#define GSM_MMSSSTATE_DEDICATED		2
#define GSM_MMSSSTATE_CONN_SUSP		3
#define GSM_MMSSSTATE_REESTPEND		4
#define GSM_MMSMSSTATE_IDLE		0
#define GSM_MMSMSSTATE_CONN_PEND	1
#define GSM_MMSMSSTATE_DEDICATED	2
#define GSM_MMSMSSTATE_CONN_SUSP	3
#define GSM_MMSMSSTATE_REESTPEND	4

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
#define	GSM_CCSTATE_NULL		0
#define	GSM_CCSTATE_INITIATED		1
#define	GSM_CCSTATE_MO_CALL_PROC	3
#define	GSM_CCSTATE_CALL_DELIVERED	4
#define	GSM_CCSTATE_CALL_PRESENT	6
#define	GSM_CCSTATE_CALL_RECEIVED	7
#define	GSM_CCSTATE_CONNECT_REQUEST	8
#define	GSM_CCSTATE_MO_TERM_CALL_CONF	9
#define	GSM_CCSTATE_ACTIVE		10
#define	GSM_CCSTATE_DISCONNECT_REQ	12
#define	GSM_CCSTATE_DISCONNECT_IND	12
#define	GSM_CCSTATE_RELEASE_REQ		19
#define	GSM_CCSTATE_MO_ORIG_MODIFY	26
#define	GSM_CCSTATE_MO_TERM_MODIFY	27
#define	GSM_CCSTATE_CONNECT_IND		28

/* MM events */
#define GSM48_MM_EVENT_NEW_LAI		0xa001
#define GSM48_MM_EVENT_TIMEOUT_T3211	0xa002
#define GSM48_MM_EVENT_TIMEOUT_T3212	0xa003
#define GSM48_MM_EVENT_TIMEOUT_T3213	0xa004
#define GSM48_MM_EVENT_IMSI_DETACH	0xa005
#define GSM48_MM_EVENT_IMSI_ATTACH	0xa006
#define GSM48_MM_EVENT_POWER_OFF	0xa007
#define GSM48_MM_EVENT_PAGING		0xa009
#define GSM48_MM_EVENT_AUTH_RESPONSE	0xa00a

struct gsm48_mmevent {
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
	struct timer_list	t3211;
	struct timer_list	t3212;
	struct timer_list	t3213;
	int			t3212_value;
	struct llist_head	mm_conn;
	char			name_short[32];
	char			name_long[32];
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



