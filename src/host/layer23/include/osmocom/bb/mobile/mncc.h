/* GSM Mobile Radio Interface Layer 3 messages on the A-bis interface 
 * 3GPP TS 04.08 version 7.21.0 Release 1998 / ETSI TS 100 940 V7.21.0 */

/* (C) 2008-2009 by Harald Welte <laforge@gnumonks.org>
 * (C) 2008, 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2009 by Andreas Eversberg <jolly@eversberg.eu>
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

#ifndef _MNCC_H
#define _MNCC_H

#include <osmocom/core/linuxlist.h>
#include <osmocom/gsm/mncc.h>

struct gsm_call {
	struct llist_head	entry;

	struct osmocom_ms	*ms;

	uint32_t		callref;

	uint8_t			init; /* call initiated, no response yet */
	uint8_t			hold; /* call on hold */
	uint8_t			ring; /* call ringing/knocking */

	struct osmo_timer_list	dtmf_timer;
	uint8_t			dtmf_state;
	uint8_t			dtmf_index;
	char			dtmf[32]; /* dtmf sequence */
};

#define DTMF_ST_IDLE		0	/* no DTMF active */
#define DTMF_ST_START		1	/* DTMF started, waiting for resp. */
#define DTMF_ST_MARK		2	/* wait tone duration */
#define DTMF_ST_STOP		3	/* DTMF stopped, waiting for resp. */
#define DTMF_ST_SPACE		4	/* wait space between tones */

#define MNCC_SETUP_REQ		0x0101
#define MNCC_SETUP_IND		0x0102
#define MNCC_SETUP_RSP		0x0103
#define MNCC_SETUP_CNF		0x0104
#define MNCC_SETUP_COMPL_REQ	0x0105
#define MNCC_SETUP_COMPL_IND	0x0106
/* MNCC_REJ_* is perfomed via MNCC_REL_* */
#define MNCC_CALL_CONF_IND	0x0107
#define MNCC_CALL_PROC_REQ	0x0108
#define MNCC_PROGRESS_REQ	0x0109
#define MNCC_ALERT_REQ		0x010a
#define MNCC_ALERT_IND		0x010b
#define MNCC_NOTIFY_REQ		0x010c
#define MNCC_NOTIFY_IND		0x010d
#define MNCC_DISC_REQ		0x010e
#define MNCC_DISC_IND		0x010f
#define MNCC_REL_REQ		0x0110
#define MNCC_REL_IND		0x0111
#define MNCC_REL_CNF		0x0112
#define MNCC_FACILITY_REQ	0x0113
#define MNCC_FACILITY_IND	0x0114
#define MNCC_START_DTMF_IND	0x0115
#define MNCC_START_DTMF_RSP	0x0116
#define MNCC_START_DTMF_REJ	0x0117
#define MNCC_STOP_DTMF_IND	0x0118
#define MNCC_STOP_DTMF_RSP	0x0119
#define MNCC_MODIFY_REQ		0x011a
#define MNCC_MODIFY_IND		0x011b
#define MNCC_MODIFY_RSP		0x011c
#define MNCC_MODIFY_CNF		0x011d
#define MNCC_MODIFY_REJ		0x011e
#define MNCC_HOLD_IND		0x011f
#define MNCC_HOLD_CNF		0x0120
#define MNCC_HOLD_REJ		0x0121
#define MNCC_RETRIEVE_IND	0x0122
#define MNCC_RETRIEVE_CNF	0x0123
#define MNCC_RETRIEVE_REJ	0x0124
#define MNCC_USERINFO_REQ	0x0125
#define MNCC_USERINFO_IND	0x0126
#define MNCC_REJ_REQ		0x0127
#define MNCC_REJ_IND		0x0128
#define MNCC_PROGRESS_IND	0x0129
#define MNCC_CALL_PROC_IND	0x012a
#define MNCC_CALL_CONF_REQ	0x012b
#define MNCC_START_DTMF_REQ	0x012c
#define MNCC_STOP_DTMF_REQ	0x012d
#define MNCC_HOLD_REQ		0x012e
#define MNCC_RETRIEVE_REQ	0x012f

#define MNCC_BRIDGE		0x0200
#define MNCC_FRAME_RECV		0x0201
#define MNCC_FRAME_DROP		0x0202
#define MNCC_LCHAN_MODIFY	0x0203

#define GSM_TCHF_FRAME		0x0300
#define GSM_TCHF_FRAME_EFR	0x0301

#define GSM_MAX_FACILITY	128
#define GSM_MAX_SSVERSION	128
#define GSM_MAX_USERUSER	128

#define	MNCC_F_BEARER_CAP	0x0001
#define MNCC_F_CALLED		0x0002
#define MNCC_F_CALLING		0x0004
#define MNCC_F_REDIRECTING	0x0008
#define MNCC_F_CONNECTED	0x0010
#define MNCC_F_CAUSE		0x0020
#define MNCC_F_USERUSER		0x0040
#define MNCC_F_PROGRESS		0x0080
#define MNCC_F_EMERGENCY	0x0100
#define MNCC_F_FACILITY		0x0200
#define MNCC_F_SSVERSION	0x0400
#define MNCC_F_CCCAP		0x0800
#define MNCC_F_KEYPAD		0x1000
#define MNCC_F_SIGNAL		0x2000

struct gsm_mncc {
	/* context based information */
	uint32_t	msg_type;
	uint32_t	callref;

	/* which fields are present */
	uint32_t	fields;

	/* data derived informations (MNCC_F_ based) */
	struct gsm_mncc_bearer_cap	bearer_cap;
	struct gsm_mncc_number		called;
	struct gsm_mncc_number		calling;
	struct gsm_mncc_number		redirecting;
	struct gsm_mncc_number		connected;
	struct gsm_mncc_cause		cause;
	struct gsm_mncc_progress	progress;
	struct gsm_mncc_useruser	useruser;
	struct gsm_mncc_facility	facility;
	struct gsm_mncc_cccap		cccap;
	struct gsm_mncc_ssversion	ssversion;
	struct	{
		int		sup;
		int		inv;
	} clir;
	int		signal;

	/* data derived information, not MNCC_F based */
	int		keypad;
	int		more;
	int		notify; /* 0..127 */
	int		emergency;
	char		imsi[16];

	unsigned char	lchan_type;
	unsigned char	lchan_mode;
};

struct gsm_data_frame {
	uint32_t	msg_type;
	uint32_t	callref;
	unsigned char	data[0];
};

const char *get_mncc_name(int value);
int mncc_recv(struct osmocom_ms *ms, int msg_type, void *arg);
void mncc_set_cause(struct gsm_mncc *data, int loc, int val);

#endif

