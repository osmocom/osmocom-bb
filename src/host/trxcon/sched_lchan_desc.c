/*
 * OsmocomBB <-> SDR connection bridge
 * TDMA scheduler: logical channels, RX / TX handlers
 *
 * (C) 2013 by Andreas Eversberg <jolly@eversberg.eu>
 * (C) 2015 by Alexander Chemeris <Alexander.Chemeris@fairwaves.co>
 * (C) 2015 by Harald Welte <laforge@gnumonks.org>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "sched_trx.h"

#define LID_DEDIC	0x00
#define LID_SACCH	0x40

/* TODO: implement */
#define tx_pdtch_fn	NULL
#define tx_tchf_fn	NULL
#define tx_tchh_fn	NULL

#define rx_pdtch_fn	NULL
#define rx_tchf_fn	NULL
#define rx_tchh_fn	NULL

/* Forward declaration of handlers */
int rx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan, uint8_t bid,
	sbit_t *bits, uint16_t nbits, int8_t rssi, float toa);

int tx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan,
	uint8_t bid, uint16_t *nbits);

int rx_sch_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan, uint8_t bid,
	sbit_t *bits, uint16_t nbits, int8_t rssi, float toa);

int tx_rach_fn(struct trx_instance *trx, struct trx_ts *ts,
	uint32_t fn, enum trx_lchan_type chan,
	uint8_t bid, uint16_t *nbits);

const struct trx_lchan_desc trx_lchan_desc[_TRX_CHAN_MAX] = {
	{
		TRXC_IDLE,		"IDLE",
		0x00,			LID_DEDIC,
		0x00,			0x00,

		/**
		 * MS: do nothing, save power...
		 * BTS: send dummy burst on C0
		 */
		NULL,			NULL,
	},
	{
		TRXC_FCCH,		"FCCH",
		0x00,			LID_DEDIC,
		0x00,			0x00,

		/* FCCH is handled by transceiver */
		NULL,			NULL,
	},
	{
		TRXC_SCH, 		"SCH",
		0x00,			LID_DEDIC,
		0x00,			TRX_CH_FLAG_AUTO,

		/**
		 * We already have clock indications from TRX,
		 * but we also need BSIC (BCC / NCC) value.
		 */
		rx_sch_fn,		NULL,
	},
	{
		TRXC_BCCH,		"BCCH",
		0x80,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	TRX_CH_FLAG_AUTO,
		rx_data_fn,		NULL,
	},
	{
		TRXC_RACH,		"RACH",
		0x88,			LID_DEDIC,
		0x00,			TRX_CH_FLAG_AUTO,
		NULL,			tx_rach_fn,
	},
	{
		TRXC_CCCH,		"CCCH",
		0x90,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	TRX_CH_FLAG_AUTO,
		rx_data_fn,		NULL,
	},
	{
		TRXC_TCHF,		"TCH/F",
		0x08,			LID_DEDIC,
		8 * GSM_BURST_PL_LEN,	0x00,
		rx_tchf_fn,		tx_tchf_fn,
	},
	{
		TRXC_TCHH_0,		"TCH/H(0)",
		0x10,			LID_DEDIC,
		6 * GSM_BURST_PL_LEN,	0x00,
		rx_tchh_fn,		tx_tchh_fn,
	},
	{
		TRXC_TCHH_1,		"TCH/H(1)",
		0x18,			LID_DEDIC,
		6 * GSM_BURST_PL_LEN,	0x00,
		rx_tchh_fn,		tx_tchh_fn,
	},
	{
		TRXC_SDCCH4_0,		"SDCCH/4(0)",
		0x20,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH4_1,		"SDCCH/4(1)",
		0x28,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH4_2,		"SDCCH/4(2)",
		0x30,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH4_3,		"SDCCH/4(3)",
		0x38,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_0,		"SDCCH/8(0)",
		0x40,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_1,		"SDCCH/8(1)",
		0x48,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_2,		"SDCCH/8(2)",
		0x50,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_3,		"SDCCH/8(3)",
		0x58,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_4,		"SDCCH/8(4)",
		0x60,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_5,		"SDCCH/8(5)",
		0x68,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_6,		"SDCCH/8(6)",
		0x70,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SDCCH8_7,		"SDCCH/8(7)",
		0x78,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCHTF,		"SACCH/TF",
		0x08,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCHTH_0,		"SACCH/TH(0)",
		0x10,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCHTH_1,		"SACCH/TH(1)",
		0x18,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH4_0,		"SACCH/4(0)",
		0x20,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH4_1,		"SACCH/4(1)",
		0x28,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH4_2,		"SACCH/4(2)",
		0x30,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH4_3,		"SACCH/4(3)",
		0x38,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_0,		"SACCH/8(0)",
		0x40,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_1,		"SACCH/8(1)",
		0x48,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_2,		"SACCH/8(2)",
		0x50,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_3,		"SACCH/8(3)",
		0x58,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_4,		"SACCH/8(4)",
		0x60,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_5,		"SACCH/8(5)",
		0x68,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_6,		"SACCH/8(6)",
		0x70,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_SACCH8_7,		"SACCH/8(7)",
		0x78,			LID_SACCH,
		4 * GSM_BURST_PL_LEN,	0x00,
		rx_data_fn,		tx_data_fn,
	},
	{
		TRXC_PDTCH,		"PDTCH",
		0x08,			LID_DEDIC,
		12 * GSM_BURST_PL_LEN,	TRX_CH_FLAG_PDCH,
		rx_pdtch_fn,		tx_pdtch_fn,
	},
	{
		TRXC_PTCCH,		"PTCCH",
		0x08,			LID_DEDIC,
		4 * GSM_BURST_PL_LEN,	TRX_CH_FLAG_PDCH,
		rx_data_fn,		tx_data_fn,
	},
};
