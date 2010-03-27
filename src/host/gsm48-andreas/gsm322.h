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

/* 4.3.1.1 List of states for PLMN slection process (automatic mode) */
#define GSM_A0_NULL		0
#define	GSM_A1_TRYING_RPLMN	1
#define	GSM_A2_ON_PLMN		2
#define	GSM_A3_TRYING_PLMN	3
#define	GSM_A4_WAIT_FOR_PLMN	4
#define	GSM_A5_HPLMN		5
#define	GSM_A6_NO_SIM		6

/* 4.3.1.2 List of states for PLMN slection process (manual mode) */
#define GSM_M1_NULL		0
#define	GSM_M1_TRYING_RPLMN	1
#define	GSM_M2_ON_PLMN		2
#define	GSM_M3_NOT_ON_PLMN	3
#define	GSM_M4_TRYING_PLMN	4
#define	GSM_M5_NO_SIM		5

/* 4.3.2 List of states for cell selection process */
#define GSM_C0_NULL		0
#define	GSM_C1_NORMAL_CELL_SEL	1
#define	GSM_C2_STORED_CELL_SEL	2
#define	GSM_C3_CAMPED_NORMALLY	3
#define	GSM_C4_NORMAL_CELL_RSEL	4
#define	GSM_C5_CHOOSE_CELL	5
#define	GSM_C6_ANY_CELL_SEL	6
#define	GSM_C7_CAMPED_ANY_CELL	7
#define	GSM_C8_ANY_CELL_RSEL	8
#define	GSM_C9_CHOOSE_ANY_CELL	9
#define	GSM_Cx_CONNECTED_MODE_1	10
#define	GSM_Cx_CONNECTED_MODE_2	11

/* 4.3.4 List of states for location registration process */
#define	GSM_L0_NULL		0
#define	GSM_L1_UPDATED		1
#define	GSM_L2_IDLE_NO_SIM	2
#define	GSM_L3_ROAMING_NOT_ALL	3
#define	GSM_L4_NOT_UPDATED	4
#define	GSM_Lx_LR_PENDING	5

/* GSM 03.22 events */
#define	GSM322_EVENT_SWITCH_ON	1
#define	GSM322_EVENT_SWITCH_OFF	2	
#define	GSM322_EVENT_SIM_INSERT	3
#define	GSM322_EVENT_SIM_REMOVE	4
#define	GSM322_EVENT_REG_FAILUE	5
#define	GSM322_EVENT_REG_SUCC	6
#define	GSM322_EVENT_NEW_PLMN	7
#define	GSM322_EVENT_ON_PLMN	8
#define	GSM322_EVENT_LU_REJECT	9
#define	GSM322_EVENT_HPLMN_SEAR	10
#define	GSM322_EVENT_HPLMN_FOUN	11
#define	GSM322_EVENT_HPLMN_NOTF	12
#define	GSM322_EVENT_USER_RESEL	13
#define	GSM322_EVENT_LOSS_RADIO	14
#define	GSM322_EVENT_PLMN_AVAIL	15
#define	GSM322_EVENT_INVAL_SIM	16
#define	GSM322_EVENT_CHOSE_PLMN	17
#define	GSM322_EVENT_SEL_MANUAL	18
#define	GSM322_EVENT_SEL_AUTO	19
#define	GSM322_EVENT_CELL_FOUND	20
#define	GSM322_EVENT_NO_CELL_F	21
#define	GSM322_EVENT_LEAVE_IDLE	22
#define	GSM322_EVENT_RET_IDLE	23
#define	GSM322_EVENT_CELL_RESEL	24
#define	GSM322_EVENT_	25
#define	GSM322_EVENT_	26
#define	GSM322_EVENT_	27
#define	GSM322_EVENT_	28

enum {
	PLMN_MODE_MANUAL,
	PLMN_MODE_AUTO
};

/* node for each PLMN */
struct gsm322_plmn_list {
	struct llist_head	entry;
	uint16_t		mcc;
	uint16_t		mnc;
};

/* node for each forbidden LA */
struct gsm322_la_list {
	struct llist_head	entry;
	uint16_t		mcc;
	uint16_t		mnc;
	uint16_t		lac;
};

/* node for each BA-List */
struct gsm322_ba_list {
	struct llist_head	entry;
	uint16_t		mcc;
	uint16_t		mnc;
	/* Band allocation for 1024 frequencies.
	 * First bit of first index is frequency 0.
	 */
	uint8_t			freq[128];
};

/* PLMN search process */
struct gsm322_plmn {
	int			state;
	struct llist_head	nplmn_list; /* new list of PLMN */
	struct llist_head	splmn_list; /* sorted list of PLMN */
	int			plmn_curr; /* current selected PLMN */
	uint16_t		mcc; /* current mcc */
	uint16_t		mnc; /* current mnc */
	struct llist_head	la_list; /* forbidden LAs */
	struct llist_head	ba_list; /* BCCH Allocation per PLMN */
};

/* GSM 03.22 message */
struct gsm322_msg {
	int			msg_type;
	uint16_t		mcc;
	uint16_t		mnc;
	uint8_t			reject; /* location update reject */
};

#define	GSM322_ALLOC_SIZE	sizeof(struct gsm322_msg)
#define GSM322_ALLOC_HEADROOM	0

