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

#ifndef __L1CTL_PROTO_H__
#define __L1CTL_PROTO_H__

enum {
	_L1CTL_NONE		= 0,
	L1CTL_FBSB_REQ,
	L1CTL_FBSB_CONF,
	L1CTL_DATA_IND,
	L1CTL_RACH_REQ,
	L1CTL_DM_EST_REQ,
	L1CTL_DATA_REQ,
	L1CTL_RESET_IND,
	L1CTL_PM_REQ,		/* power measurement */
	L1CTL_PM_CONF,		/* power measurement */
	L1CTL_ECHO_REQ,
	L1CTL_ECHO_CONF,
	L1CTL_RACH_CONF,
	L1CTL_RESET_REQ,
	L1CTL_RESET_CONF,
	L1CTL_DATA_CONF,
	L1CTL_CCCH_MODE_REQ,
	L1CTL_CCCH_MODE_CONF,
	L1CTL_DM_REL_REQ,
	L1CTL_PARAM_REQ,
	L1CTL_DM_FREQ_REQ,
	L1CTL_CRYPTO_REQ,
	L1CTL_SIM_REQ,
	L1CTL_SIM_CONF,
	L1CTL_TCH_MODE_REQ,
	L1CTL_TCH_MODE_CONF,
	L1CTL_NEIGH_PM_REQ,
	L1CTL_NEIGH_PM_IND,
	L1CTL_TRAFFIC_REQ,
	L1CTL_TRAFFIC_CONF,
	L1CTL_TRAFFIC_IND,
};

enum ccch_mode {
	CCCH_MODE_NONE = 0,
	CCCH_MODE_NON_COMBINED,
	CCCH_MODE_COMBINED,
};

enum neigh_mode {
	NEIGH_MODE_NONE = 0,
	NEIGH_MODE_PM,
	NEIGH_MODE_SB,
};

#define TRAFFIC_DATA_LEN	40

/*
 * NOTE: struct size. We do add manual padding out of the believe
 * that it will avoid some unaligned access.
 */

/* there are no more messages in a sequence */
#define L1CTL_F_DONE	0x01

struct l1ctl_hdr {
	uint8_t msg_type;
	uint8_t flags;
	uint8_t padding[2];
	uint8_t data[0];
} __attribute__((packed));

/*
 * downlink info ... down from the BTS..
 */
struct l1ctl_info_dl {
	/* GSM 08.58 channel number (9.3.1) */
	uint8_t chan_nr;
	/* GSM 08.58 link identifier (9.3.2) */
	uint8_t link_id;
	/* the ARFCN and the band. FIXME: what about MAIO? */
	uint16_t band_arfcn;

	uint32_t frame_nr;

	uint8_t rx_level;	/* 0 .. 63 in typical GSM notation (dBm+110) */
	uint8_t snr;		/* Signal/Noise Ration (dB) */
	uint8_t num_biterr;
	uint8_t fire_crc;

	uint8_t payload[0];
} __attribute__((packed));

/* new CCCH was found. This is following the header */
struct l1ctl_fbsb_conf {
	int16_t initial_freq_err;
	uint8_t result;
	uint8_t bsic;
	/* FIXME: contents of cell_info ? */
} __attribute__((packed));

/* CCCH mode was changed */
struct l1ctl_ccch_mode_conf {
	uint8_t ccch_mode;	/* enum ccch_mode */
	uint8_t padding[3];
} __attribute__((packed));

/* TCH mode was changed */
struct l1ctl_tch_mode_conf {
	uint8_t tch_mode;	/* enum tch_mode */
	uint8_t audio_mode;
	uint8_t padding[2];
} __attribute__((packed));

/* data on the CCCH was found. This is following the header */
struct l1ctl_data_ind {
	uint8_t data[23];
} __attribute__((packed));

/* traffic from the network */
struct l1ctl_traffic_ind {
	uint8_t data[TRAFFIC_DATA_LEN];
} __attribute__((packed));

/*
 * uplink info
 */
struct l1ctl_info_ul {
	/* GSM 08.58 channel number (9.3.1) */
	uint8_t chan_nr;
	/* GSM 08.58 link identifier (9.3.2) */
	uint8_t link_id;
	uint8_t padding[2];

	uint8_t payload[0];
} __attribute__((packed));

/*
 * msg for FBSB_REQ
 * the l1_info_ul header is in front
 */
struct l1ctl_fbsb_req {
	uint16_t band_arfcn;
	uint16_t timeout;	/* in TDMA frames */

	uint16_t freq_err_thresh1;
	uint16_t freq_err_thresh2;

	uint8_t num_freqerr_avg;
	uint8_t flags;		/* L1CTL_FBSB_F_* */
	uint8_t sync_info_idx;
	uint8_t ccch_mode;	/* enum ccch_mode */
	uint8_t rxlev_exp;	/* expected signal level */
} __attribute__((packed));

#define L1CTL_FBSB_F_FB0	(1 << 0)
#define L1CTL_FBSB_F_FB1	(1 << 1)
#define L1CTL_FBSB_F_SB		(1 << 2)
#define L1CTL_FBSB_F_FB01SB	(L1CTL_FBSB_F_FB0|L1CTL_FBSB_F_FB1|L1CTL_FBSB_F_SB)

/*
 * msg for CCCH_MODE_REQ
 * the l1_info_ul header is in front
 */
struct l1ctl_ccch_mode_req {
	uint8_t ccch_mode;	/* enum ccch_mode */
	uint8_t padding[3];
} __attribute__((packed));

/*
 * msg for TCH_MODE_REQ
 * the l1_info_ul header is in front
 */
struct l1ctl_tch_mode_req {
	uint8_t tch_mode;	/* enum gsm48_chan_mode */
#define AUDIO_TX_MICROPHONE	(1<<0)
#define AUDIO_TX_TRAFFIC_REQ	(1<<1)
#define AUDIO_RX_SPEAKER	(1<<2)
#define AUDIO_RX_TRAFFIC_IND	(1<<3)
	uint8_t audio_mode;
	uint8_t padding[2];
} __attribute__((packed));

/* the l1_info_ul header is in front */
struct l1ctl_rach_req {
	uint8_t ra;
	uint8_t combined;
	uint16_t offset;
} __attribute__((packed));

/* the l1_info_ul header is in front */
struct l1ctl_par_req {
	int8_t ta;
	uint8_t tx_power;
	uint8_t padding[2];
} __attribute__((packed));

struct l1ctl_h0 {
	uint16_t band_arfcn;
} __attribute__((packed));

struct l1ctl_h1 {
	uint8_t hsn;
	uint8_t maio;
	uint8_t n;
	uint8_t _padding[1];
	uint16_t ma[64];
} __attribute__((packed));

struct l1ctl_dm_est_req {
	uint8_t tsc;
	uint8_t h;
	union {
		struct l1ctl_h0 h0;
		struct l1ctl_h1 h1;
	};
	uint8_t tch_mode;
	uint8_t audio_mode;
} __attribute__((packed));

struct l1ctl_dm_freq_req {
	uint16_t fn;
	uint8_t tsc;
	uint8_t h;
	union {
		struct l1ctl_h0 h0;
		struct l1ctl_h1 h1;
	};
} __attribute__((packed));

struct l1ctl_crypto_req {
	uint8_t algo;
	uint8_t key[0];
} __attribute__((packed));

struct l1ctl_pm_req {
	uint8_t type;
	uint8_t padding[3];

	union {
		struct {
			uint16_t band_arfcn_from;
			uint16_t band_arfcn_to;
		} range;
	};
} __attribute__((packed));

/* a single L1CTL_PM response */
struct l1ctl_pm_conf {
	uint16_t band_arfcn;
	uint8_t pm[2];
} __attribute__((packed));

enum l1ctl_reset_type {
	L1CTL_RES_T_BOOT,	/* only _IND */
	L1CTL_RES_T_FULL,
	L1CTL_RES_T_SCHED,
};

/* argument to L1CTL_RESET_REQ and L1CTL_RESET_IND */
struct l1ctl_reset {
	uint8_t type;
	uint8_t pad[3];
} __attribute__((packed));

struct l1ctl_neigh_pm_req {
	uint8_t n;
	uint8_t padding[1];
	uint16_t band_arfcn[64];
	uint8_t tn[64];
} __attribute__((packed));

/* neighbour cell measurement results */
struct l1ctl_neigh_pm_ind {
	uint16_t band_arfcn;
	uint8_t pm[2];
	uint8_t tn;
	uint8_t padding;
} __attribute__((packed));

/* traffic data to network */
struct l1ctl_traffic_req {
	uint8_t data[TRAFFIC_DATA_LEN];
} __attribute__((packed));

#endif /* __L1CTL_PROTO_H__ */
