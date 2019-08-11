/* Initialization for the Huawei GTM900-B modem */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010-19 by Steve Markgraf <steve@steve-m.de>
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
#include <tiffs.h>

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
#include "keymap.h"

#define ARMIO_LATCH_OUT 0xfffe4802
#define IO_CNTL_REG	0xfffe4804
#define ASIC_CONF_REG	0xfffef008
#define IO_CONF_REG	0xfffef00a
#define LPG_LCR_REG	0xfffe7800
#define LPG_PM_REG	0xfffe7801

static void board_io_init(void)
{
	uint16_t reg;

	reg = readw(ASIC_CONF_REG);
	/* Set LPG pin mux for power LED */
	reg |= (1 << 6);
	/* TWL3025: Set SPI+RIF RX clock to rising edge */
	reg |= (1 << 13) | (1 << 14);
	writew(reg, ASIC_CONF_REG);

	/*
	 * Most Calypso peripheral interface signals are unconnected
	 * on this modem.  We configure them to be GPIOs in IO_CONF_REG,
	 * then configure them to be outputs in IO_CNTL_REG, then set
	 * the outputs to 0 in ARMIO_LATCH_OUT.
	 */
	writew(0x03F5, IO_CONF_REG);
	writew(0xC000, IO_CNTL_REG);
	writew(0x0000, ARMIO_LATCH_OUT);

	/* Set LPG output permanently on (power LED) */
	writew(1, LPG_PM_REG);
	writew((1 << 7), LPG_LCR_REG);
}

void board_init(int with_irq)
{
	/* Configure the memory interface */
	calypso_mem_cfg(CALYPSO_nCS0, 3, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS1, 3, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS2, 5, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS3, 5, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_CS4, 0, CALYPSO_MEM_8bit, 1);
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

	/* Initialize keypad driver */
	keypad_init(keymap, with_irq);

	/* Initialize ABB driver (uses SPI) */
	twl3025_init();
}
