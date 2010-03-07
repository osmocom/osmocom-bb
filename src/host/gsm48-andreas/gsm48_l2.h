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

/* link layer primitives */

#define	DL_UNIT_DATA_REQ	0x0110
#define	DL_UNIT_DATA_IND	0x0112
#define	DL_DATA_REQ		0x0120
#define	DL_DATA_IND		0x0122
#define	DL_ESTABLISH_REQ	0x0130
#define	DL_ESTABLISH_IND	0x0132
#define	DL_ESTABLISH_CNF	0x0131
#define	DL_SUSPEND_REQ		0x0140
#define	DL_SUSPEND_CNF		0x0141
#define	DL_RESUME_REQ		0x0150
#define	DL_RESUME_CNF		0x0151
#define	DL_CONNECT_REQ		0x0160
#define	DL_CONNECT_CNF		0x0161
#define	DL_RELEASE_REQ		0x0170
#define	DL_RELEASE_IND		0x0172
#define	DL_RELEASE_CNF		0x0171
#define	DL_RECONNECT_REQ	0x0180
#define	DL_RANDOM_ACCESS_REQ	0x0190
#define	DL_RANDOM_ACCESS_IND	0x0192
#define	DL_RANDOM_ACCESS_CNF	0x0191
#define DL_RANDOM_ACCESS_FLU	0x0195 /* special flush message */
#define	MDL_ERROR_IND		0x0212
#define	MDL_RELEASE_REQ		0x0220

/* report error cause to layer 3 (GSM 04.06 4.1.3.5) */

#define L2L3_CAUSE_T200_N200		1
#define L2L3_CAUSE_REEST_REQ		2
#define L2L3_CAUSE_UNSOL_UA_RSP		3
#define L2L3_CAUSE_UNSOL_DM_RSP		4
#define L2L3_CAUSE_UNSOL_DM_RSP_MF	5
#define L2L3_CAUSE_UNSOL_SUPER_RSP	6
#define L2L3_CAUSE_SEQ_ERROR		7
#define L2L3_CAUSE_U_FRAME_INCORR	8
#define L2L3_CAUSE_SHORT_L1HEAD1_NOTSUP	9
#define L2L3_CAUSE_SHORT_L1HEAD1_NOTAPP	10
#define L2L3_CAUSE_S_FRAME_INCORR	11
#define L2L3_CAUSE_I_FRAME_INCORR_M	12
#define L2L3_CAUSE_I_FRAME_INCORR_LEN	13
#define L2L3_CAUSE_FRAME_NOT_IMPL	14
#define L2L3_CAUSE_SABM_MULTI		15
#define L2L3_CAUSE_SABM_INFO		16

/* DL_* messages */
struct gsm_dl {
	int		msg_type;
	struct msgb	msg;
	u_int8_t	cause;
	int		delay;
	u_int8_t	channel_request[3];
	u_int8_t	mobile_alloc_lv[9];
};





