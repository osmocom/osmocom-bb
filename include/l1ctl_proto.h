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
 */

#ifndef __L1CTL_PROTO_H__
#define __L1CTL_PROTO_H__

enum {
	_L1CTL_NONE			= 0x00,
	L1CTL_FBSB_REQ			= 0x01,
	L1CTL_FBSB_CONF			= 0x02,
	L1CTL_DATA_IND			= 0x03,
	L1CTL_RACH_REQ			= 0x04,
	L1CTL_DM_EST_REQ		= 0x05,
	L1CTL_DATA_REQ			= 0x06,
	L1CTL_RESET_IND			= 0x07,
	L1CTL_PM_REQ			= 0x08, /* power measurement */
	L1CTL_PM_CONF			= 0x09, /* power measurement */
	L1CTL_ECHO_REQ			= 0x0a,
	L1CTL_ECHO_CONF			= 0x0b,
	L1CTL_RACH_CONF			= 0x0c,
	L1CTL_RESET_REQ			= 0x0d,
	L1CTL_RESET_CONF		= 0x0e,
	L1CTL_DATA_CONF			= 0x0f,
	L1CTL_CCCH_MODE_REQ		= 0x10,
	L1CTL_CCCH_MODE_CONF		= 0x11,
	L1CTL_DM_REL_REQ		= 0x12,
	L1CTL_PARAM_REQ			= 0x13,
	L1CTL_DM_FREQ_REQ		= 0x14,
	L1CTL_CRYPTO_REQ		= 0x15,
	L1CTL_SIM_REQ			= 0x16,
	L1CTL_SIM_CONF			= 0x17,
	L1CTL_TCH_MODE_REQ		= 0x18,
	L1CTL_TCH_MODE_CONF		= 0x19,
	L1CTL_NEIGH_PM_REQ		= 0x1a,
	L1CTL_NEIGH_PM_IND		= 0x1b,
	L1CTL_TRAFFIC_REQ		= 0x1c,
	L1CTL_TRAFFIC_CONF		= 0x1d,
	L1CTL_TRAFFIC_IND		= 0x1e,
	L1CTL_BURST_IND			= 0x1f,
	/* configure TBF for uplink/downlink */
	L1CTL_TBF_CFG_REQ		= 0x20,
	L1CTL_TBF_CFG_CONF		= 0x21,
	L1CTL_DATA_TBF_REQ		= 0x22,
	L1CTL_DATA_TBF_CONF		= 0x23,
	/* Extended (11-bit) RACH (see 3GPP TS 05.02, section 5.2.7) */
	L1CTL_EXT_RACH_REQ		= 0x24,
};

enum ccch_mode {
	CCCH_MODE_NONE = 0,
	CCCH_MODE_NON_COMBINED,
	CCCH_MODE_COMBINED,
	CCCH_MODE_COMBINED_CBCH,
};

enum neigh_mode {
	NEIGH_MODE_NONE = 0,
	NEIGH_MODE_PM,
	NEIGH_MODE_SB,
};

enum l1ctl_coding_scheme {
	L1CTL_CS_NONE,
	L1CTL_CS1,
	L1CTL_CS2,
	L1CTL_CS3,
	L1CTL_CS4,
	L1CTL_MCS1,
	L1CTL_MCS2,
	L1CTL_MCS3,
	L1CTL_MCS4,
	L1CTL_MCS5,
	L1CTL_MCS6,
	L1CTL_MCS7,
	L1CTL_MCS8,
	L1CTL_MCS9,
};

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

/* 3GPP TS 44.014, section 5.1 (Calypso specific numbers) */
enum l1ctl_tch_loop_mode {
	L1CTL_TCH_LOOP_OPEN	= 0x00,
	L1CTL_TCH_LOOP_A	= 0x01,
	L1CTL_TCH_LOOP_B	= 0x02,
	L1CTL_TCH_LOOP_C	= 0x03,
	L1CTL_TCH_LOOP_D	= 0x04,
	L1CTL_TCH_LOOP_E	= 0x05,
	L1CTL_TCH_LOOP_F	= 0x06,
	L1CTL_TCH_LOOP_I	= 0x07,
};

/* TCH mode was changed */
struct l1ctl_tch_mode_conf {
	uint8_t tch_mode;	/* enum tch_mode */
	uint8_t audio_mode;
	uint8_t tch_loop_mode;	/* enum l1ctl_tch_loop_mode */
	struct { /* 3GPP TS 08.58 9.3.52, 3GPP TS 44.018 10.5.2.21aa */
		uint8_t start_codec;
		uint8_t codecs_bitmask;
	} amr;
} __attribute__((packed));

/* data on the CCCH was found. This is following the header */
struct l1ctl_data_ind {
	uint8_t data[23];
} __attribute__((packed));

/* traffic from the network */
struct l1ctl_traffic_ind {
	uint8_t data[0];
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

struct l1ctl_info_ul_tbf {
	/* references l1ctl_tbf_cfg_req.tbf_nr */
	uint8_t tbf_nr;
	uint8_t coding_scheme;
	uint8_t padding[2];
	/* RLC/MAC block, size determines CS */
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
	uint8_t tch_loop_mode;	/* enum l1ctl_tch_loop_mode */
	struct { /* 3GPP TS 08.58 9.3.52, 3GPP TS 44.018 10.5.2.21aa */
		uint8_t start_codec;
		uint8_t codecs_bitmask;
	} amr;
} __attribute__((packed));

/* the l1_info_ul header is in front */
struct l1ctl_rach_req {
	uint8_t ra;
	uint8_t combined;
	uint16_t offset;
} __attribute__((packed));


/* the l1_info_ul header is in front */
struct l1ctl_ext_rach_req {
	uint16_t ra11;
	uint8_t synch_seq;
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
	uint8_t key_len;
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

#define BI_FLG_DUMMY (1 << 4)
#define BI_FLG_SACCH (1 << 5)

struct l1ctl_burst_ind {
	uint32_t frame_nr;
	uint16_t band_arfcn;    /* ARFCN + band + ul indicator               */
	uint8_t chan_nr;        /* GSM 08.58 channel number (9.3.1)          */
	uint8_t flags;          /* BI_FLG_xxx + burst_id = 2LSBs             */
	uint8_t rx_level;       /* 0 .. 63 in typical GSM notation (dBm+110) */
	uint8_t snr;            /* Reported SNR >> 8 (0-255)                 */
	uint8_t bits[15];       /* 114 bits + 2 steal bits. Filled MSB first */
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
	uint8_t data[0];
} __attribute__((packed));

struct l1ctl_tbf_cfg_req {
	/* future support for multiple concurrent TBFs. 0 for now */
	uint8_t tbf_nr;
	/* is this about an UL TBF (1) or DL (0) */
	uint8_t is_uplink;
	uint8_t padding[2];

	/* one USF for each TN, or 255 for invalid/unused */
	uint8_t usf[8];
} __attribute__((packed));

#endif /* __L1CTL_PROTO_H__ */
