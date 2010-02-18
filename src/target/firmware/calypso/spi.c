/* Driver for SPI Master Controller inside TI Calypso */

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
#include <stdio.h>

//#define DEBUG
#include <debug.h>

#include <memory.h>
#include <spi.h>
#include <delay.h>

#define BASE_ADDR_SPI	0xfffe3000
#define SPI_REG(n)	(BASE_ADDR_SPI+(n))

enum spi_regs {
	REG_SET1	= 0x00,
	REG_SET2	= 0x02,
	REG_CTRL	= 0x04,
	REG_STATUS	= 0x06,
	REG_TX_LSB	= 0x08,
	REG_TX_MSB	= 0x0a,
	REG_RX_LSB	= 0x0c,
	REG_RX_MSB	= 0x0e,
};

#define SPI_SET1_EN_CLK		(1 << 0)
#define SPI_SET1_WR_IRQ_DIS	(1 << 4)
#define SPI_SET1_RDWR_IRQ_DIS	(1 << 5)

#define SPI_CTRL_RDWR		(1 << 0)
#define SPI_CTRL_WR		(1 << 1)
#define SPI_CTRL_NB_SHIFT	2
#define SPI_CTRL_AD_SHIFT	7

#define SPI_STATUS_RE		(1 << 0)	/* Read End */
#define SPI_STATUS_WE		(1 << 1)	/* Write End */

void spi_init(void)
{
	writew(SPI_SET1_EN_CLK | SPI_SET1_WR_IRQ_DIS | SPI_SET1_RDWR_IRQ_DIS,
	       SPI_REG(REG_SET1));

	writew(0x0001, SPI_REG(REG_SET2));
}

int spi_xfer(uint8_t dev_idx, uint8_t bitlen, const void *dout, void *din)
{
	uint8_t bytes_per_xfer;
	uint8_t reg_status, reg_ctrl = 0;
	uint32_t tmp;

	if (bitlen == 0)
		return 0;

	if (bitlen > 32)
		return -1;

	if (dev_idx > 4)
		return -1;

	bytes_per_xfer = bitlen / 8;
	if (bitlen % 8)
		bytes_per_xfer ++;

	reg_ctrl |= (bitlen - 1) << SPI_CTRL_NB_SHIFT;
	reg_ctrl |= (dev_idx & 0x7) << SPI_CTRL_AD_SHIFT;

	if (bitlen <= 8) {
		tmp = *(uint8_t *)dout;
		tmp <<= 24 + (8-bitlen);	/* align to MSB */
	} else if (bitlen <= 16) {
		tmp = *(uint16_t *)dout;
		tmp <<= 16 + (16-bitlen);	/* align to MSB */
	} else {
		tmp = *(uint32_t *)dout;
		tmp <<= (32-bitlen);		/* align to MSB */
	}
	printd("spi_xfer(dev_idx=%u, bitlen=%u, data_out=0x%08x): ",
		dev_idx, bitlen, tmp);

	/* fill transmit registers */
	writew(tmp >> 16, SPI_REG(REG_TX_MSB));
	writew(tmp & 0xffff, SPI_REG(REG_TX_LSB));

	/* initiate transfer */
	if (din)
		reg_ctrl |= SPI_CTRL_RDWR;
	else
		reg_ctrl |= SPI_CTRL_WR;
	writew(reg_ctrl, SPI_REG(REG_CTRL));
	printd("reg_ctrl=0x%04x ", reg_ctrl);

	/* wait until the transfer is complete */
	while (1) {
		reg_status = readw(SPI_REG(REG_STATUS));
		printd("status=0x%04x ", reg_status);
		if (din && (reg_status & SPI_STATUS_RE))
			break;
		else if (reg_status & SPI_STATUS_WE)
			break;
	}
	/* FIXME: calibrate how much delay we really need (seven 13MHz cycles) */
	delay_ms(1);

	if (din) {
		tmp = readw(SPI_REG(REG_RX_MSB)) << 16;
		tmp |= readw(SPI_REG(REG_RX_LSB));
		printd("data_in=0x%08x ", tmp);

		if (bitlen <= 8)
			*(uint8_t *)din = tmp & 0xff;
		else if (bitlen <= 16)
			*(uint16_t *)din = tmp & 0xffff;
		else
			*(uint32_t *)din = tmp;
	}
	dputchar('\n');

	return 0;
}
