/* Calypso TPU debugger, displays and decodes TPU instruction RAM */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
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
#include <stdlib.h>
#include <stdio.h>

#include <osmocom/core/msgb.h>

/* TPU disassembler begin */

static const char *tpu_instr_name[] = {
	[0]	= "SLEEP",
	[1]	= "AT",
	[2]	= "OFFSET",
	[3]	= "SYNCHRO",
	[4]	= "MOVE",
	[5]	= "WAIT",
	[6]	= "UNDEFINED6",
	[7]	= "UNDEFINED7",
};

static const char *tpu_addr_name[0x1f] = {
	[0]	= "TSP_CTLR1",
	[1]	= "TSP_CTRL2",
	[4]	= "TSP_TX_1",
	[3]	= "TSP_TX_2",
	[2]	= "TSP_TX_3",
	[5]	= "TSP_TX_4",
	[6]	= "TSPACT_L",
	[7]	= "TSPACT_H",
	[9]	= "TSP_SET1",
	[0xa]	= "TSP_SET2",
	[0xb]	= "TSP_SET3",
	[0x10]	= "DSP_INT_PG",
	[0x11]	= "GAUGING_EN",
};

static uint8_t tpu_reg_cache[0x1f];
static uint16_t tpu_qbit;

static void tpu_show_instr(uint16_t tpu)
{
	uint16_t instr = tpu >> 13;
	uint16_t param = tpu & 0x1fff;
	uint16_t addr, data, bitlen;
	uint32_t tsp_data;

	tpu_qbit++;

	printf("\t %04u %04x %s ", tpu_qbit, tpu, tpu_instr_name[instr]);
	switch (instr) {
	case 0:
		tpu_qbit = 0;
	default:
		break;
	case 1:
		tpu_qbit = param;
		printf("%u ", param);
		break;
	case 5:
		tpu_qbit += param;
		printf("%u ", param);
		break;
	case 2:
	case 3:
		printf("%u ", param);
		break;
	case 4:
		addr = param & 0x1f;
		data = param >> 5;
		tpu_reg_cache[addr] = data;
		printf("%10s=0x%04x ", tpu_addr_name[addr], data);
		switch (addr) {
		case 0:
			bitlen = (data & 0x1f) + 1;
			printf("DEV_IDX=%u, BITLEN=%u ", data >> 5, bitlen);
			if (bitlen <= 8) {
				tsp_data = tpu_reg_cache[4];
				printf(" TSP_DATA=0x%02x ", tsp_data);
			} else if (bitlen <= 16) {
				tsp_data = tpu_reg_cache[3];
				tsp_data |= tpu_reg_cache[4] << 8;
				printf(" TSP_DATA=0x%04x ", tsp_data);
			} else if (bitlen <= 24) {
				tsp_data = tpu_reg_cache[2];
				tsp_data |= tpu_reg_cache[3] << 8;
				tsp_data |= tpu_reg_cache[4] << 16;
				printf(" TSP_DATA=0x%06x ", tsp_data);
			} else {
				tsp_data = tpu_reg_cache[5];
				tsp_data |= tpu_reg_cache[2] << 8;
				tsp_data |= tpu_reg_cache[3] << 16;
				tsp_data |= tpu_reg_cache[4] << 24;
				printf(" TSP_DATA=0x%08x ", tsp_data);
			}
			break;
		case 1:
			if (data & 0x01)
				printf("READ ");
			if (data & 0x02)
				printf("WRITE ");
			break;
		}
	}
	printf("\n");
}

void hdlc_tpudbg_cb(uint8_t dlci, struct msgb *msg)
{
	uint32_t *fn = (uint32_t *) msg->data;
	uint16_t *tpu;

	printf("TPU FN %u\n", *fn);
	for (tpu = (uint16_t *) (msg->data + 4); tpu < (uint16_t *) msg->tail; tpu++)
		tpu_show_instr(*tpu);

	msgb_free(msg);
}
