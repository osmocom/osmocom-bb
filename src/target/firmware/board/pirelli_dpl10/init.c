/* Initialization for the Pirelli DP-L10 */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2011-12 by Steve Markgraf <steve@steve-m.de>
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
#include <ctors.h>
#include <memory.h>
#include <board.h>
#include <keypad.h>
#include <console.h>
#include <flash/cfi_flash.h>

#include <calypso/irq.h>
#include <calypso/clock.h>
#include <calypso/dma.h>
#include <calypso/rtc.h>
#include <calypso/timer.h>
#include <uart.h>
#include <calypso/backlight.h>

#include <comm/sercomm.h>
#include <comm/timer.h>

#include <abb/twl3025.h>
#include <rf/trf6151.h>

#include <fb/framebuffer.h>
#include "keymap.h"

#define ARMIO_LATCH_OUT 0xfffe4802
#define IO_CNTL_REG	0xfffe4804
#define ARM_CONF_REG	0xfffef006
#define ASIC_CONF_REG	0xfffef008
#define IO_CONF_REG	0xfffef00a

static void board_io_init(void)
{
	uint16_t reg;

	reg = readw(ASIC_CONF_REG);
	/* Set function pins to I2C Mode */
	reg |= ((1 << 12) | (1 << 7));		/* SCL / SDA */
	/* TWL3025: Set SPI+RIF RX clock to rising edge */
	reg |= (1 << 13) | (1 << 14);
	reg &= ~(1 << 1);
	writew(reg, ASIC_CONF_REG);

	/* enable IO functionality */
	reg = readw(IO_CONF_REG);
	reg |= (1 << 9) | (1 << 4) | (1 << 3) | (1 << 2) | (1 << 1) | (1 << 0);
	writew(reg, IO_CONF_REG);

	/* set IO directions */
	reg = readw(IO_CNTL_REG);
	reg &= ~((1 << 7) | (1 << 4) | (1 << 1));
	writew(reg, IO_CNTL_REG);

	/* reset display controller, disable bypass mode, set nCS4 to display */
	reg = readw(ARMIO_LATCH_OUT);
	reg &= ~(1 << 4);
	writew(reg, ARMIO_LATCH_OUT);
	reg &= ~(1 << 7);
	reg |= (1 << 4) | (1 << 1);
	writew(reg, ARMIO_LATCH_OUT);

	/* configure ADD(22), needed for second half of flash */
	reg = readw(ARM_CONF_REG);
	reg |= (1 << 3);
	writew(reg, ARM_CONF_REG);
}

void board_init(int with_irq)
{
	/* Configure the memory interface */
	calypso_mem_cfg(CALYPSO_nCS0, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS1, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS2, 5, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS3, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_CS4, 7, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS6, 0, CALYPSO_MEM_32bit, 1);
	calypso_mem_cfg(CALYPSO_nCS7, 0, CALYPSO_MEM_32bit, 0);

	/* Set VTCXO_DIV2 = 1, configure PLL for 104 MHz and give ARM half of that */
	calypso_clock_set(2, CALYPSO_PLL13_104_MHZ, ARM_MCLK_DIV_2);

	/* Configure the RHEA bridge with some sane default values */
	calypso_rhea_cfg(0, 0, 0xff, 0, 1, 0, 0);

	/* Initialize board-specific GPIO */
	board_io_init();

	/* Enable bootrom mapping to route exception vectors to RAM */
	calypso_bootrom(with_irq);
	calypso_exceptions_install();

	/* Initialize interrupt controller */
	if (with_irq)
		irq_init();

	sercomm_bind_uart(UART_IRDA);
	cons_bind_uart(UART_MODEM);

	/* initialize IRDA UART to be used for sercomm */
	uart_init(UART_IRDA, with_irq);
	uart_baudrate(UART_IRDA, UART_115200);

	/* Initialize MODEM UART to be used for old-school console code. */
	uart_init(UART_MODEM, with_irq);
	uart_baudrate(UART_MODEM, UART_115200);

	/* Initialize hardware timers */
	hwtimer_init();

	/* Initialize DMA controller */
	dma_init();

	/* Initialize real time clock */
	rtc_init();

	/* Initialize system timers (uses hwtimer 2) */
	timer_init();

	/* Initialize LCD driver and backlight (0 is max, 255 min brightness) */
	bl_mode_pwl(1);
	bl_level(50);

	fb_init();

	/* Initialize keypad driver */
	keypad_init(keymap, with_irq);

	/* Initialize ABB driver (uses SPI) */
	twl3025_init();

	/* enable LEDB driver of Iota for keypad backlight */
	twl3025_reg_write(AUXLED, 0x02);
}
