/* Driver for I2C Master Controller inside TI Calypso */

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

#include <debug.h>
#include <memory.h>
#include <i2c.h>

#define BASE_ADDR_I2C	0xfffe2800
#define I2C_REG(x)	(BASE_ADDR_I2C+(x))

enum i2c_reg {
	DEVICE_REG	= 0,
	ADDRESS_REG,
	DATA_WR_REG,	
	DATA_RD_REG,
	CMD_REG,
	CONF_FIFO_REG,
	CONF_CLK_REG,
	CONF_CLK_FUNC_REF,
	STATUS_FIFO_REG,
	STATUS_ACTIVITY_REG,
};

#define I2C_CMD_SOFT_RESET	(1 << 0)
#define I2C_CMD_EN_CLK		(1 << 1)
#define I2C_CMD_START		(1 << 2)
#define I2C_CMD_RW_READ		(1 << 3)
#define I2C_CMD_COMP_READ	(1 << 4)
#define I2C_CMD_IRQ_ENABLE	(1 << 5)

#define I2C_STATUS_ERROR_DATA	(1 << 0)
#define I2C_STATUS_ERROR_DEV	(1 << 1)
#define I2C_STATUS_IDLE		(1 << 2) // 1: not idle, 0: idle
#define I2C_STATUS_INTERRUPT	(1 << 3)

int i2c_write(uint8_t chip, uint32_t addr, int alen, const uint8_t *buffer, int len)
{
	uint8_t cmd;

	/* Calypso I2C controller doesn't support fancy addressing */
	if (alen > 1)
		return -1;

	/* FIXME: implement writes longer than fifo size */
	if (len > 16)
		return -1;

	printd("i2c_write(chip=0x%02u, addr=0x%02u): ", chip, addr);

	writeb(chip & 0x7f, I2C_REG(DEVICE_REG));
	writeb(addr & 0xff, I2C_REG(ADDRESS_REG));
	
	/* we have to tell the controller how many bits we'll put into the fifo ?!? */
	writeb(len-1, I2C_REG(CONF_FIFO_REG));

	/* fill the FIFO */
	while (len--) {
		uint8_t byte = *buffer++;
		writeb(byte, I2C_REG(DATA_WR_REG));
		printd("%02X ", byte);
	}
	dputchar('\n');

	/* start the transfer */
	cmd = readb(I2C_REG(CMD_REG));
	cmd |= I2C_CMD_START;
	writeb(cmd, I2C_REG(CMD_REG));

	/* wait until transfer completes */
	while (1) {
		uint8_t reg = readb(I2C_REG(STATUS_ACTIVITY_REG));
		printd("I2C Status: 0x%02x\n", reg & 0xf);
		if (!(reg & I2C_STATUS_IDLE)) // 0: idle 1: not idle
			break;
	}
	dputs("I2C transfer completed\n");

	return 0;
}

void i2c_init(int speed, int slaveadd)
{
	/* scl_out = clk_func_ref / 3,
	   clk_func_ref = master_clock_freq / (divisor_2 + 1)
	   master_clock_freq = ext_clock_freq / divisor_1 */
	/* clk_func_ref = scl_out * 3,
	   divisor_2 = (master_clock_freq / clk_func_ref) - 1
	   divisor_1 = ext_clock_freq / master_clock_freq */
	/* for a target freq of 200kHz:
		ext_clock_freq = 13MHz
		clk_func_ref = 3 * 300kHZ = 600kHz
		divisor_1 = 1 => master_clock_freq = ext_clock_freq = 13MHz
		divisor_2 = 21 => clk_func_ref = 13MHz / (21+2) = 590.91 kHz
		scl_out = clk_func_ref / 3 = 509.91 kHz / 3 = 196.97kHz */
	writeb(I2C_CMD_SOFT_RESET, I2C_REG(CMD_REG));

	writeb(0x00, I2C_REG(CONF_CLK_REG));
	writeb(21, I2C_REG(CONF_CLK_FUNC_REF));

	writeb(I2C_CMD_EN_CLK, I2C_REG(CMD_REG));
}
