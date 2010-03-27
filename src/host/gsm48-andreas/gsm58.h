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

/* GSM 05.08 events */
#define	GSM58_EVENT_START_NORMAL	1
#define	GSM58_EVENT_START_STORED	2
#define	GSM58_EVENT_START_ANY		3
#define	GSM58_EVENT_TIMEOUT		4
#define	GSM58_EVENT_SYNC		5
#define	GSM58_EVENT_SYSINFO		6
#define	GSM58_EVENT_	7
#define	GSM58_EVENT_	8
#define	GSM58_EVENT_	9
#define	GSM58_EVENT_	10
#define	GSM58_EVENT_	11
#define	GSM58_EVENT_	12

case GSM58_EVENT_START_NORMAL_SEL:
gsm58_start_normal_sel(ms, msg);
break;
case GSM58_EVENT_START_STORED_SEL:
gsm58_start_stored_sel(ms, msg);
break;
case GSM58_EVENT_START_ANY_SEL:
gsm58_start_any_sel(ms, msg);
break;
case GSM58_EVENT_TIMEOUT:
gsm58_sel_timeout(ms, msg);
break;
case GSM58_EVENT_SYNC:
gsm58_sel_sync(ms, msg);
break;
case GSM58_EVENT_SYSINFO:
gsm58_sel_sysinfo(ms, msg);
break;

enum {
	GSM58_MODE_IDLE,
	GSM58_MODE_SYNC,
	GSM58_MODE_READ,
	GSM58_MODE_CAMP
};

/* GSM 05.08 message */
struct gsm58_msg {
	int			msg_type;
	uint16_t		mcc;
	uint16_t		mnc;
	uint8_t			ba[128];
	uint16_t		arfcn;
};

#define	GSM58_ALLOC_SIZE	sizeof(struct gsm58_mgs)
#define GSM58_ALLOC_HEADROOM	0

/* GSM 05.08 selection process */
struct gsm58_selproc {
	int			mode;
	uint16_t		mcc;
	uint16_t		mnc;
	uint8_t			ba[128];
	uint16_t		cur_freq; /* index */
	uint16_t		arfcn; /* channel number */
};


