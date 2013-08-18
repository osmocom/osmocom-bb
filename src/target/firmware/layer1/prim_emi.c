/* Layer 1 - EMI functions */

/* (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
 * based on code by Sylvain Munaut <tnt@246tnt.com>
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
#include <calypso/buzzer.h>

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

struct dsp_ext_api dsp_ext_api_emi = {
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
	return dsp_ext_api_emi.db[idx];
}

static int
l1s_emi_resp(uint8_t p1, uint8_t p2, uint16_t p3)
{
	/* We're done with this */
	dsp_api.r_page_used = 1;

	return 0;
}

static int
l1s_emi_cmd(uint8_t p1, uint8_t p2, uint16_t p3)
{
	struct dsp_ext_db *db = dsp_ext_get_db(0);

	int i;

	/* Enable extensions */
	dsp_ext_api_emi.ndb->active = 1;


	/* skip if not burst to be sent */
	l1s.emi.burst_curr++;
	if (l1s.emi.burst_curr == l1s.emi.burst_num)
		l1s.emi.burst_curr = 0;
	if (!(l1s.emi.burst_map[l1s.emi.burst_curr >> 5] & (1 << (31 - (l1s.emi.burst_curr & 0x1f)))))
		return 0;
	buzzer_volume(l1s.emi.tone);
	buzzer_note(NOTE(NOTE_C, OCTAVE_5));

	/* RX side */
	/* ------- */

	db->rx[0].cmd = 0;
	db->rx[0].data = 0x000;


	/* TX side */
	/* ------- */

	#define SLOT 3

	/* Reset all commands to dummy burst */
	for (i=0; i<8; i++)
		db->tx[i].cmd = DSP_EXT_TX_CMD_DUMMY;

	/* Enable the task */
	dsp_api.db_w->d_task_ra = RACH_DSP_TASK;

	/* Open TX window */
	l1s_tx_apc_helper(l1s.emi.arfcn);
	l1s_tx_multi_win_ctrl(l1s.emi.arfcn, 0, 2, l1s.emi.slots);

	/* delay some time */
	delay_us(300);
	buzzer_volume(0);

	return 0;
}

const struct tdma_sched_item emi_sched_set[] = {
	SCHED_ITEM_DT(l1s_emi_cmd, 2, 0, 0),	SCHED_END_FRAME(),
	SCHED_ITEM(l1s_emi_resp, -5, 0, 0),	SCHED_END_FRAME(),
	SCHED_END_SET()
};
