/* Layer 1 - BTS functions */

/* (C) 2012 by Sylvain Munaut <tnt@246tnt.com>
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <byteorder.h>

#include <calypso/dsp.h>

#include <layer1/l23_api.h>
#include <layer1/sync.h>
#include <layer1/tdma_sched.h>
#include <layer1/tpu_window.h>
#include <layer1/trx.h>


/* ------------------------------------------------------------------------ */
/* DSP extensions API                                                       */
/* ------------------------------------------------------------------------ */

#define BASE_API		0xFFD00000

#define API_DSP2ARM(x)		(BASE_API + ((x) - 0x0800) * sizeof(uint16_t))

#define BASE_API_EXT_DB0	API_DSP2ARM(0x2000)
#define BASE_API_EXT_DB1	API_DSP2ARM(0x2200)
#define BASE_API_EXT_NDB	API_DSP2ARM(0x2400)

struct dsp_ext_ndb {
	uint16_t active;
	uint16_t tsc;
} __attribute__((packed));

struct dsp_ext_db {
	/* TX command and data ptr */
	struct {
#define DSP_EXT_TX_CMD_NONE	0
#define DSP_EXT_TX_CMD_FB	1
#define DSP_EXT_TX_CMD_SB	2
#define DSP_EXT_TX_CMD_DUMMY	3
#define DSP_EXT_TX_CMD_AB	4
#define DSP_EXT_TX_CMD_NB	5
		uint16_t cmd;
		uint16_t data;
	} tx[8];

	/* RX command and data ptr */
	struct {
#define DSP_EXT_RX_CMD_NONE	0
#define DSP_EXT_RX_CMD_AB	1
#define DSP_EXT_RX_CMD_NB	2
		uint16_t cmd;
		uint16_t data;
	} rx[8];

	/* SCH data */
	uint16_t sch[2];

	/* Generic data array */
	uint16_t data[0];
} __attribute__((packed));

struct dsp_ext_api {
	struct dsp_ext_ndb *ndb;
	struct dsp_ext_db *db[2];
};

struct dsp_ext_api dsp_ext_api = {
	.ndb = (struct dsp_ext_ndb *) BASE_API_EXT_NDB,
	.db  = {
		(struct dsp_ext_db *) BASE_API_EXT_DB0,
		(struct dsp_ext_db *) BASE_API_EXT_DB1,
	},
};

static inline struct dsp_ext_db *
dsp_ext_get_db(int r_wn /* 0=W, 1=R */)
{
	int idx = r_wn ? dsp_api.r_page : dsp_api.w_page;
	return dsp_ext_api.db[idx];
}





static uint32_t
sb_build(uint8_t bsic, uint16_t t1, uint8_t t2, uint8_t t3p)
{
	return  ((t1  & 0x001) << 23) |
		((t1  & 0x1fe) <<  7) |
		((t1  & 0x600) >>  9) |
		((t2  &  0x1f) << 18) |
		((t3p &   0x1) << 24) |
		((t3p &   0x6) << 15) |
		((bsic & 0x3f) <<  2);
}

static uint8_t tchh_subslot[26] =
	{ 0,1,0,1,0,1,0,1,0,1,0,1,0,0,1,0,1,0,1,0,1,0,1,0,1,1 };
static uint8_t sdcch4_subslot[102] =
	{ 3,3,3,3,0,0,2,2,2,2,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,2,2,2,2,
	  3,3,3,3,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,
	  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,2,2,2,2 };
static uint8_t sdcch8_subslot[102] =
	{ 5,5,5,5,6,6,6,6,7,7,7,7,0,0,0,0,0,0,0,1,1,1,1,2,2,2,
	  2,3,3,3,3,4,4,4,4,5,5,5,5,6,6,6,6,7,7,7,7,0,0,0,0,
	  1,1,1,1,2,2,2,2,3,3,3,3,0,0,0,0,0,0,0,1,1,1,1,2,2,2,
	  2,3,3,3,3,4,4,4,4,5,5,5,5,6,6,6,6,7,7,7,7,4,4,4,4 };



static int
l1s_bts_resp(uint8_t p1, uint8_t p2, uint16_t p3)
{
	struct dsp_ext_db *db = dsp_ext_get_db(1);
	struct gsm_time rx_time;

	/* Time of the command */
	gsm_fn2gsmtime(&rx_time, l1s.current_time.fn - 2);
		/* We're shifted in time since we RX in the 'next' frame */


	/* RX side */
	/* ------- */

	/* Access Burst ? */
	if (db->rx[0].cmd == DSP_EXT_RX_CMD_AB)
	{
		static int burst_count = 0;
		static struct l1ctl_bts_burst_ab_ind _bi[10];
		static int energy[10];
		struct l1ctl_bts_burst_ab_ind *bi = &_bi[burst_count];
		int i, j;
		uint16_t *iq = &db->data[32];

		energy[burst_count] = 0;

		/* Frame number */
		bi->fn = htonl(rx_time.fn);

		/* Timeslot */
		bi->tn = l1s.bts.rx_start;

		/* Data (cut to 8 bits */
		bi->toa = db->rx[1].cmd;
		if (bi->toa > 68)
			goto exit;
		for (i=0,j=(db->rx[1].cmd)<<1; i<2*88; i++,j++)
			bi->iq[i] = iq[j] >> 8;

		/* energy */
		energy[burst_count] = db->rx[0].data;

		if (++burst_count == 10) {
			struct msgb *msg;
			int energy_max = 0, energy_avg = 0;

			burst_count = 0;

			/* find strongest burst out of 10 */
			j = 0;
			for (i = 0; i < 10; i++) {
				energy_avg += energy[i];
				if (energy[i] > energy_max) {
					energy_max = energy[i];
					j = i;
				}
				energy[i] = 0;
			}

//			printf("### RACH ### (%04x %04x)\n", energy_max, energy_avg);

			/* Create message */
			msg = l1ctl_msgb_alloc(L1CTL_BTS_BURST_AB_IND);
			if (!msg)
				goto exit;

			memcpy(msgb_put(msg, sizeof(*bi)), &_bi[j], sizeof(*bi));

			/* Send it ! */
			l1_queue_for_l2(msg);
		}
	}

	/* Normal Burst ? */
	else if (db->rx[0].cmd == DSP_EXT_RX_CMD_NB)
	{
		uint16_t *d = &db->data[32];
		int rssi = agc_inp_dbm8_by_pm(d[1] >> 3) / 8;
		int16_t toa = (int16_t)d[0] - 3;

		if (d[3] > 0x1000) {
			struct msgb *msg;
			struct l1ctl_bts_burst_nb_ind *bi;
			int i;

//			printf("### NB ### (%04x %04x)\n", d[1], d[3]);

			/* Create message */
			msg = l1ctl_msgb_alloc(L1CTL_BTS_BURST_NB_IND);
			if (!msg)
				goto exit;

			bi = (struct l1ctl_bts_burst_nb_ind *) msgb_put(msg, sizeof(*bi));

			/* Frame number */
			bi->fn = htonl(rx_time.fn);

			/* Timeslot */
			bi->tn = l1s.bts.rx_start;

			/* TOA */
			if (toa > -32 && toa < 32)
				bi->toa = toa;

			/* RSSI */
			if (rssi < -110)
				bi->rssi = -110;
			else if (rssi < 0)
				bi->rssi = rssi;

			/* Pack bits */
			memset(bi->data, 0x00, sizeof(bi->data));

			for (i=0; i<116; i++)
			{
				int sbit = 0x0008 << ((3 - (i & 3)) << 2);
				int sword = i >> 2;
				int dbit = 1 << (7 - (i & 7));
				int dbyte = i >> 3;

				if (d[5+sword] & sbit)
					bi->data[dbyte] |= dbit;
			}

			/* Send it ! */
			l1_queue_for_l2(msg);
		}
	}

exit:
	/* We're done with this */
	dsp_api.r_page_used = 1;

	return 0;
}

static int
l1s_bts_cmd(uint8_t p1, uint8_t p2, uint16_t p3)
{
	struct dsp_ext_db *db = dsp_ext_get_db(0);

	uint32_t sb;
	uint8_t data[15];
	int type, i, t3, fn_mod_26, fn_mod_102;

	/* Enable extensions */
	dsp_ext_api.ndb->active = 1;

	/* Force the TSC in all cases */
	dsp_ext_api.ndb->tsc = l1s.bts.bsic & 7;


	/* RX side */
	/* ------- */

	db->rx[0].cmd = 0;
	db->rx[0].data = 0x000;

	t3 = l1s.next_time.t3;

	if (t3 != 2)
	{
		/* We're really a frame in advance since we RX in the next frame ! */
		t3 = t3 - 1;
		fn_mod_26 = (l1s.next_time.fn + 2715648 - 1) % 26;
		fn_mod_102 = (l1s.next_time.fn + 2715648 - 1) % 102;

		/* Select which type of burst */
		switch (l1s.bts.type[l1s.bts.rx_start]) {
		case 1: /* TCH/F */
			if (l1s.bts.handover[l1s.bts.rx_start] & (1 << 0))
				db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			else
				db->rx[0].cmd = DSP_EXT_RX_CMD_NB;
			break;
		case 2: /* TCH/H */
		case 3:
			if ((l1s.bts.handover[l1s.bts.rx_start]
					& (1 << tchh_subslot[fn_mod_26])))
				db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			else
				db->rx[0].cmd = DSP_EXT_RX_CMD_NB;
			break;
		case 4:
		case 6:
			db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			break;
		case 5: /* BCCH+SDCCH/4 */
			if ((t3 >= 14) && (t3 <= 36))
				db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			else if ((t3 == 4) || (t3 == 5))
				db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			else if ((t3 == 45) || (t3 == 46))
				db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			else if ((l1s.bts.handover[l1s.bts.rx_start]
					& (1 << sdcch4_subslot[fn_mod_102])))
				db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			else
				db->rx[0].cmd = DSP_EXT_RX_CMD_NB;
			break;
		case 7: /* SDCCH/8 */
			if ((l1s.bts.handover[l1s.bts.rx_start]
					& (1 << sdcch8_subslot[fn_mod_102])))
				db->rx[0].cmd = DSP_EXT_RX_CMD_AB;
			else
				db->rx[0].cmd = DSP_EXT_RX_CMD_NB;
			break;
		default:
			db->rx[0].cmd = DSP_EXT_RX_CMD_NB;
		}

		/* Enable dummy bursts detection */
		dsp_api.db_w->d_ctrl_system |= (1 << B_BCCH_FREQ_IND);

		/* Enable task */
		dsp_api.db_w->d_task_d = 23;

		/* store current gain */
		uint8_t last_gain = rffe_get_gain();

		rffe_compute_gain(-47 - l1s.bts.gain, CAL_DSP_TGT_BB_LVL);

		/* Open RX window */
		l1s_rx_win_ctrl(l1s.bts.arfcn | ARFCN_UPLINK, L1_RXWIN_NB, l1s.bts.rx_start);

		/* restore last gain */
		rffe_set_gain(last_gain);
	}


	/* TX side */
	/* ------- */

	// FIXME put that in a loop over all TX bursts
	static uint8_t SLOT, tn;
	if (l1s.bts.tx_start == 5) {
		SLOT = 3;
		tn = 0;
	} else {
		SLOT = 0;
		tn = 1;
	}

	/* Reset all commands to dummy burst */
	for (i=0; i<8; i++)
		db->tx[i].cmd = DSP_EXT_TX_CMD_DUMMY;

	/* Get the next burst */
	type = trx_get_burst(l1s.next_time.fn, tn, data);

	/* Program the TX commands */
	if (t3 == 2 && (l1s.bts.type[tn] >> 1) != 2) {
		// FIXME use dummy until TSC will be set individually
		db->tx[SLOT].cmd = DSP_EXT_TX_CMD_DUMMY;
	} else
	switch (type) {
	case BURST_FB:
		db->tx[SLOT].cmd = DSP_EXT_TX_CMD_FB;
		break;

	case BURST_SB:
		db->tx[SLOT].cmd = DSP_EXT_TX_CMD_SB;

		sb = sb_build(
			l1s.bts.bsic,                   /* BSIC */
			l1s.next_time.t1,               /* T1   */
			l1s.next_time.t2,               /* T2   */
			(l1s.next_time.t3 - 1) / 10     /* T3'  */
		);

		db->sch[0] = sb;
		db->sch[1] = sb >> 16;

		break;

	case BURST_DUMMY:
		db->tx[SLOT].cmd = DSP_EXT_TX_CMD_DUMMY;
		break;

	case BURST_NB:
		db->tx[SLOT].cmd = DSP_EXT_TX_CMD_NB;
		db->tx[SLOT].data = sizeof(struct dsp_ext_db) / sizeof(uint16_t);

		for (i=0; i<7; i++)
			db->data[i] = (data[i<<1] << 8) | data[(i<<1)+1];
		db->data[7] = data[14] << 8;
		break;

	default:
		db->tx[SLOT].cmd = DSP_EXT_TX_CMD_DUMMY;
		break;
	}

	/* Enable the task */
	dsp_api.db_w->d_task_ra = RACH_DSP_TASK;

	/* Open TX window */
	l1s_tx_apc_helper(l1s.bts.arfcn);
	if (l1s.bts.tx_num > 1)
		l1s_tx_multi_win_ctrl(l1s.bts.arfcn, 0, (l1s.bts.tx_start + 5) & 7, l1s.bts.tx_num);
	else
		l1s_tx_win_ctrl(l1s.bts.arfcn, L1_TXWIN_NB, 0, (l1s.bts.tx_start + 5) & 7);

	return 0;
}

const struct tdma_sched_item bts_sched_set[] = {
	SCHED_ITEM_DT(l1s_bts_cmd, 2, 0, 0),	SCHED_END_FRAME(),
						SCHED_END_FRAME(),
	SCHED_ITEM(l1s_bts_resp, -5, 0, 0),	SCHED_END_FRAME(),
	SCHED_END_SET()
};
