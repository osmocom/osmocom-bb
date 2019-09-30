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

#include <osmocom/gsm/protocol/gsm_08_58.h>
#include "sched_trx.h"

/* Forward declaration of handlers */
int rx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid,
	sbit_t *bits, int8_t rssi, int16_t toa256);

int tx_data_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid);

int rx_sch_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid,
	sbit_t *bits, int8_t rssi, int16_t toa256);

int tx_rach_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid);

int rx_tchf_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid,
	sbit_t *bits, int8_t rssi, int16_t toa256);

int tx_tchf_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid);

int rx_tchh_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid,
	sbit_t *bits, int8_t rssi, int16_t toa256);

int tx_tchh_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid);

int rx_pdtch_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid,
	sbit_t *bits, int8_t rssi, int16_t toa256);

int tx_pdtch_fn(struct trx_instance *trx, struct trx_ts *ts,
	struct trx_lchan_state *lchan, uint32_t fn, uint8_t bid);

const struct trx_lchan_desc trx_lchan_desc[_TRX_CHAN_MAX] = {
	[TRXC_IDLE] = {
		.name = "IDLE",
		.desc = "Idle channel",
		/* The MS needs to perform neighbour measurements during
		 * IDLE slots, however this is not implemented (yet). */
	},
	[TRXC_FCCH] = {
		.name = "FCCH", /* 3GPP TS 05.02, section 3.3.2.1 */
		.desc = "Frequency correction channel",
		/* Handled by transceiver, nothing to do. */
	},
	[TRXC_SCH] = {
		.name = "SCH", /* 3GPP TS 05.02, section 3.3.2.2 */
		.desc = "Synchronization channel",

		/* 3GPP TS 05.03, section 4.7. Handled by transceiver,
		 * however we still need to parse BSIC (BCC / NCC). */
		.flags = TRX_CH_FLAG_AUTO,
		.rx_fn = rx_sch_fn,
	},
	[TRXC_BCCH] = {
		.name = "BCCH", /* 3GPP TS 05.02, section 3.3.2.3 */
		.desc = "Broadcast control channel",
		.chan_nr = RSL_CHAN_BCCH,

		/* Rx only, xCCH convolutional coding (3GPP TS 05.03, section 4.4),
		 * regular interleaving (3GPP TS 05.02, clause 7, table 3):
		 * a L2 frame is interleaved over 4 consecutive bursts. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_AUTO,
		.rx_fn = rx_data_fn,
	},
	[TRXC_RACH] = {
		.name = "RACH", /* 3GPP TS 05.02, section 3.3.3.1 */
		.desc = "Random access channel",
		.chan_nr = RSL_CHAN_RACH,

		/* Tx only, RACH convolutional coding (3GPP TS 05.03, section 4.6). */
		.flags = TRX_CH_FLAG_AUTO,
		.tx_fn = tx_rach_fn,
	},
	[TRXC_CCCH] = {
		.name = "CCCH", /* 3GPP TS 05.02, section 3.3.3.1 */
		.desc = "Common control channel",
		.chan_nr = RSL_CHAN_PCH_AGCH,

		/* Rx only, xCCH convolutional coding (3GPP TS 05.03, section 4.4),
		 * regular interleaving (3GPP TS 05.02, clause 7, table 3):
		 * a L2 frame is interleaved over 4 consecutive bursts. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_AUTO,
		.rx_fn = rx_data_fn,
	},
	[TRXC_TCHF] = {
		.name = "TCH/F", /* 3GPP TS 05.02, section 3.2 */
		.desc = "Full Rate traffic channel",
		.chan_nr = RSL_CHAN_Bm_ACCHs,
		.link_id = TRX_CH_LID_DEDIC,

		/* Rx and Tx, multiple convolutional coding types (3GPP TS 05.03,
		 * chapter 3), block diagonal interleaving (3GPP TS 05.02, clause 7):
		 *
		 *   - a traffic frame is interleaved over 8 consecutive bursts
		 *     using the even numbered bits of the first 4 bursts
		 *     and odd numbered bits of the last 4 bursts;
		 *   - a FACCH/F frame 'steals' (replaces) one traffic frame,
		 *     interleaving is done in the same way.
		 *
		 * The MS shall continuously transmit bursts, even if there is nothing
		 * to send, unless DTX (Discontinuous Transmission) is used. */
		.burst_buf_size = 8 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_tchf_fn,
		.tx_fn = tx_tchf_fn,
	},
	[TRXC_TCHH_0] = {
		.name = "TCH/H(0)", /* 3GPP TS 05.02, section 3.2 */
		.desc = "Half Rate traffic channel (sub-channel 0)",
		.chan_nr = RSL_CHAN_Lm_ACCHs + (0 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Rx and Tx, multiple convolutional coding types (3GPP TS 05.03,
		 * chapter 3), block diagonal interleaving (3GPP TS 05.02, clause 7):
		 *
		 *   - a traffic frame is interleaved over 6 consecutive bursts
		 *     using the even numbered bits of the first 2 bursts,
		 *     all bits of the middle two 2 bursts,
		 *     and odd numbered bits of the last 2 bursts;
		 *   - a FACCH/H frame 'steals' (replaces) two traffic frames,
		 *     interleaving is done over 4 consecutive bursts,
		 *     the same as given for a TCH/FS.
		 *
		 * The MS shall continuously transmit bursts, even if there is nothing
		 * to send, unless DTX (Discontinuous Transmission) is used. */
		.burst_buf_size = 6 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_tchh_fn,
		.tx_fn = tx_tchh_fn,
	},
	[TRXC_TCHH_1] = {
		.name = "TCH/H(1)", /* 3GPP TS 05.02, section 3.2 */
		.desc = "Half Rate traffic channel (sub-channel 1)",
		.chan_nr = RSL_CHAN_Lm_ACCHs + (1 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_TCHH_0, see above. */
		.burst_buf_size = 6 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_tchh_fn,
		.tx_fn = tx_tchh_fn,
	},
	[TRXC_SDCCH4_0] = {
		.name = "SDCCH/4(0)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 0)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (0 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH4_1] = {
		.name = "SDCCH/4(1)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 1)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (1 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH4_2] = {
		.name = "SDCCH/4(2)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 2)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (2 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH4_3] = {
		.name = "SDCCH/4(3)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 3)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (3 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_0] = {
		.name = "SDCCH/8(0)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 0)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (0 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_1] = {
		.name = "SDCCH/8(1)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 1)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (1 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_2] = {
		.name = "SDCCH/8(2)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 2)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (2 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_3] = {
		.name = "SDCCH/8(3)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 3)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (3 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_4] = {
		.name = "SDCCH/8(4)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 4)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (4 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_5] = {
		.name = "SDCCH/8(5)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 5)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (5 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_6] = {
		.name = "SDCCH/8(6)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 6)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (6 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH8_7] = {
		.name = "SDCCH/8(7)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Stand-alone dedicated control channel (sub-channel 7)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (7 << 3),
		.link_id = TRX_CH_LID_DEDIC,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCHTF] = {
		.name = "SACCH/TF", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow TCH/F associated control channel",
		.chan_nr = RSL_CHAN_Bm_ACCHs,
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCHTH_0] = {
		.name = "SACCH/TH(0)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow TCH/H associated control channel (sub-channel 0)",
		.chan_nr = RSL_CHAN_Lm_ACCHs + (0 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCHTH_1] = {
		.name = "SACCH/TH(1)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow TCH/H associated control channel (sub-channel 1)",
		.chan_nr = RSL_CHAN_Lm_ACCHs + (1 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH4_0] = {
		.name = "SACCH/4(0)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/4 associated control channel (sub-channel 0)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (0 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH4_1] = {
		.name = "SACCH/4(1)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/4 associated control channel (sub-channel 1)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (1 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH4_2] = {
		.name = "SACCH/4(2)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/4 associated control channel (sub-channel 2)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (2 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH4_3] = {
		.name = "SACCH/4(3)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/4 associated control channel (sub-channel 3)",
		.chan_nr = RSL_CHAN_SDCCH4_ACCH + (3 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH4_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_0] = {
		.name = "SACCH/8(0)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 0)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (0 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_1] = {
		.name = "SACCH/8(1)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 1)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (1 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_2] = {
		.name = "SACCH/8(2)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 2)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (2 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_3] = {
		.name = "SACCH/8(3)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 3)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (3 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_4] = {
		.name = "SACCH/8(4)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 4)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (4 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_5] = {
		.name = "SACCH/8(5)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 5)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (5 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_6] = {
		.name = "SACCH/8(6)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 6)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (6 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SACCH8_7] = {
		.name = "SACCH/8(7)", /* 3GPP TS 05.02, section 3.3.4.1 */
		.desc = "Slow SDCCH/8 associated control channel (sub-channel 7)",
		.chan_nr = RSL_CHAN_SDCCH8_ACCH + (7 << 3),
		.link_id = TRX_CH_LID_SACCH,

		/* Same as for TRXC_BCCH and TRXC_SDCCH8_* (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_CBTX,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_PDTCH] = {
		.name = "PDTCH", /* 3GPP TS 05.02, sections 3.2.4, 3.3.2.4 */
		.desc = "Packet data traffic & control channel",
		.chan_nr = RSL_CHAN_OSMO_PDCH,

		/* Rx and Tx, multiple coding schemes: CS-1..4 and MCS-1..9 (3GPP TS
		 * 05.03, chapter 5), regular interleaving as specified for xCCH.
		 * NOTE: the burst buffer is three times bigger because the
		 * payload of EDGE bursts is three times longer. */
		.burst_buf_size = 3 * 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_PDCH,
		.rx_fn = rx_pdtch_fn,
		.tx_fn = tx_pdtch_fn,
	},
	[TRXC_PTCCH] = {
		.name = "PTCCH", /* 3GPP TS 05.02, section 3.3.4.2 */
		.desc = "Packet Timing advance control channel",
		.chan_nr = RSL_CHAN_OSMO_PDCH,

		/* Same as for TRXC_BCCH (xCCH), see above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_PDCH,
		.rx_fn = rx_data_fn,
		.tx_fn = tx_data_fn,
	},
	[TRXC_SDCCH4_CBCH] = {
		.name = "SDCCH/4(CBCH)", /* 3GPP TS 05.02, section 3.3.5 */
		.desc = "Cell Broadcast channel on SDCCH/4",
		.chan_nr = RSL_CHAN_OSMO_CBCH4,

		/* Same as for TRXC_BCCH (xCCH), but Rx only. See above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.flags = TRX_CH_FLAG_AUTO,
		.rx_fn = rx_data_fn,
	},
	[TRXC_SDCCH8_CBCH] = {
		.name = "SDCCH/8(CBCH)", /* 3GPP TS 05.02, section 3.3.5 */
		.desc = "Cell Broadcast channel on SDCCH/8",
		.chan_nr = RSL_CHAN_OSMO_CBCH8,

		/* Same as for TRXC_BCCH (xCCH), but Rx only. See above. */
		.burst_buf_size = 4 * GSM_BURST_PL_LEN,
		.rx_fn = rx_data_fn,
	},
};
