/* Driver for uWire Master Controller inside TI Calypso */

/* (C) 2010 by Sylvain Munaut <tnt@246tNt.com>
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

//#define DEBUG
#include <debug.h>

#include <memory.h>
#include <uwire.h>
#include <delay.h>

#define BASE_ADDR_UWIRE	0xfffe4000
#define UWIRE_REG(n)	(BASE_ADDR_UWIRE+(n))

enum uwire_regs {
	REG_DATA	= 0x00,
	REG_CSR		= 0x02,
	REG_SR1		= 0x04,
	REG_SR2		= 0x06,
	REG_SR3		= 0x08,
};

#define UWIRE_CSR_BITS_RD(n)	(((n) & 0x1f) << 0)
#define UWIRE_CSR_BITS_WR(n)	(((n) & 0x1f) << 5)
#define UWIRE_CSR_IDX(n)	(((n) & 3) << 10)
#define UWIRE_CSR_CS_CMD	(1 << 12)
#define UWIRE_CSR_START		(1 << 13)
#define UWIRE_CSR_CSRB		(1 << 14)
#define UWIRE_CSR_RDRB		(1 << 15)

#define UWIRE_CSn_EDGE_RD	(1 << 0)	/* 1=falling 0=rising */
#define UWIRE_CSn_EDGE_WR	(1 << 1)	/* 1=falling 0=rising */
#define UWIRE_CSn_CS_LVL	(1 << 2)
#define UWIRE_CSn_FRQ_DIV2	(0 << 3)
#define UWIRE_CSn_FRQ_DIV4	(1 << 3)
#define UWIRE_CSn_FRQ_DIV8	(2 << 3)
#define UWIRE_CSn_CKH

#define UWIRE_CSn_SHIFT(n)	(((n) & 1) ? 6 : 0)
#define UWIRE_CSn_REG(n)	(((n) & 2) ? REG_SR2 : REG_SR1)

#define UWIRE_SR3_CLK_EN	(1 << 0)
#define UWIRE_SR3_CLK_DIV2	(0 << 1)
#define UWIRE_SR3_CLK_DIV4	(1 << 1)
#define UWIRE_SR3_CLK_DIV7	(2 << 1)
#define UWIRE_SR3_CLK_DIV10	(3 << 1)

static inline void _uwire_wait(int mask, int val)
{
	while ((readw(UWIRE_REG(REG_CSR)) & mask) != val);
}

void uwire_init(void)
{
	writew(UWIRE_SR3_CLK_EN | UWIRE_SR3_CLK_DIV2, UWIRE_REG(REG_SR3));
	/* FIXME only init CS0 for now */
	writew(((UWIRE_CSn_CS_LVL | UWIRE_CSn_FRQ_DIV2) << UWIRE_CSn_SHIFT(0)),
		UWIRE_REG(UWIRE_CSn_REG(0)));
	writew(UWIRE_CSR_IDX(0) | UWIRE_CSR_CS_CMD, UWIRE_REG(REG_CSR));
	_uwire_wait(UWIRE_CSR_CSRB, 0);
}

int uwire_xfer(int cs, int bitlen, const void *dout, void *din)
{
	uint16_t tmp = 0;

	if (bitlen <= 0 || bitlen > 16)
		return -1;
	if (cs < 0 || cs > 4)
		return -1;

	/* FIXME uwire_init always selects CS0 for now */

	printd("uwire_xfer(dev_idx=%u, bitlen=%u\n", cs, bitlen);

	/* select the chip */
	writew(UWIRE_CSR_IDX(0) | UWIRE_CSR_CS_CMD, UWIRE_REG(REG_CSR));
	_uwire_wait(UWIRE_CSR_CSRB, 0);

	if (dout) {
		if (bitlen <= 8)
			tmp = *(uint8_t *)dout;
		else if (bitlen <= 16)
			tmp = *(uint16_t *)dout;
		tmp <<= 16 - bitlen; /* align to MSB */
		writew(tmp, UWIRE_REG(REG_DATA));
		printd(", data_out=0x%04hx", tmp);
	}

	tmp =	(dout ? UWIRE_CSR_BITS_WR(bitlen) : 0) |
		(din  ? UWIRE_CSR_BITS_RD(bitlen) : 0) |
		UWIRE_CSR_START;
	writew(tmp, UWIRE_REG(REG_CSR));

	_uwire_wait(UWIRE_CSR_CSRB, 0);

	if (din) {
		_uwire_wait(UWIRE_CSR_RDRB, UWIRE_CSR_RDRB);

		tmp = readw(UWIRE_REG(REG_DATA));
		printd(", data_in=0x%08x", tmp);

		if (bitlen <= 8)
			*(uint8_t *)din = tmp & 0xff;
		else if (bitlen <= 16)
			*(uint16_t *)din = tmp & 0xffff;
	}
	/* unselect the chip */
	writew(UWIRE_CSR_IDX(0) | 0, UWIRE_REG(REG_CSR));
	_uwire_wait(UWIRE_CSR_CSRB, 0);

	printd(")\n");

	return 0;
}
