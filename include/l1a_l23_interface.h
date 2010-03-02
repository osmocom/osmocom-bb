/* Messages to be sent between the different layers */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010 by Holger Hans Peter Freyther
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

#ifndef l1a_l23_interface_h
#define l1a_l23_interface_h

#define L1CTL_NEW_CCCH_REQ	1
#define L1CTL_NEW_CCCH_RESP	2
#define L1CTL_DATA_IND		3
#define L1CTL_RACH_REQ		4
#define L1CTL_DM_EST_REQ	5
#define L1CTL_DATA_REQ		7
#define L1CTL_RESET		8

/*
 * NOTE: struct size. We do add manual padding out of the believe
 * that it will avoid some unaligned access.
 */

struct gsm_time {
	uint32_t	fn;	/* FN count */
	uint16_t	t1;	/* FN div (26*51) */
	uint8_t		t2;	/* FN modulo 26 */
	uint8_t		t3;	/* FN modulo 51 */
	uint8_t		tc;
};

/*
 * downlink info ... down from the BTS..
 */
struct l1ctl_info_dl {
	/* common header, should be its own struct */
	uint8_t msg_type;
	uint8_t padding;

	/* GSM 08.58 channel number (9.3.1) */
	uint8_t chan_nr;
	/* GSM 08.58 link identifier (9.3.2) */
	uint8_t link_id;

	/* the ARFCN and the band. FIXME: what about MAIO? */
	uint16_t band_arfcn;

	struct gsm_time time;
	uint8_t rx_level;
	uint16_t snr[4];
} __attribute__((packed));

/* new CCCH was found. This is following the header */
struct l1ctl_sync_new_ccch_resp {
	uint8_t bsic;
	uint8_t padding[3];
} __attribute__((packed));

/* data on the CCCH was found. This is following the header */
struct l1ctl_data_ind {
	uint8_t data[23];
} __attribute__((packed));

/*
 * uplink info
 */
struct l1ctl_info_ul {
	/* common header, should be its own struct */
	uint8_t msg_type;
	uint8_t padding;

	/* GSM 08.58 channel number (9.3.1) */
	uint8_t chan_nr;
	/* GSM 08.58 link identifier (9.3.2) */
	uint8_t link_id;

	uint8_t tx_power;
	uint8_t padding2;

	uint8_t payload[0];
} __attribute__((packed));

/*
 * msg for SYNC_NEW_CCCH_REQ
 * the l1_info_ul header is in front
 */
struct l1ctl_sync_new_ccch_req {
	uint16_t band_arfcn;
} __attribute__((packed));


/* the l1_info_ul header is in front */
struct l1ctl_rach_req {
	uint8_t ra;
	uint8_t padding[3];
} __attribute__((packed));

struct l1ctl_dm_est_req {
	uint16_t band_arfcn;
	union {
		struct {
			uint8_t maio_high:4,
				 h:1,
				 tsc:3;
			uint8_t hsn:6,
				 maio_low:2;
		} h1;
		struct {
			uint8_t arfcn_high:2,
				 spare:2,
				 h:1,
				 tsc:3;
			uint8_t arfcn_low;
		} h0;
	};
} __attribute__((packed));

#endif
