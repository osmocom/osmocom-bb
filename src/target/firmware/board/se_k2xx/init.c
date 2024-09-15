/* Initialization for the Sony Ericsson K200i/K220i */

/* (C) 2010 by Harald Welte <laforge@gnumonks.org>
 * (C) 2010-22 by Steve Markgraf <steve@steve-m.de>
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

#include <fb/framebuffer.h>
#include "keymap.h"

#define ASIC_CONF_REG	0xfffef008
#define ARMIO_LATCH_OUT	0xfffe4802
#define IO_CNTL_REG	0xfffe4804
#define IO_CONF_REG	0xfffef00a

static void board_io_init(void)
{
	uint16_t reg;

	reg = readw(ASIC_CONF_REG);
	/* Set LPG and PWL pin mux */
	reg |= (1 << 6) | (1 << 4);
	/* TWL3025: Set SPI+RIF RX clock to rising edge */
	reg |= (1 << 13) | (1 << 14);
	writew(reg, ASIC_CONF_REG);

	writew(0xc060, IO_CNTL_REG);
	writew(0x03fd, IO_CONF_REG);

	/* set default IO state */
	writew(0x1f83, ARMIO_LATCH_OUT);
}

void board_init(int with_irq)
{
	/* Configure the memory interface */
	calypso_mem_cfg(CALYPSO_nCS0, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS1, 5, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS2, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS3, 5, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_CS4, 5, CALYPSO_MEM_8bit, 1); /* TODO: add one dummy cycle */
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

	sercomm_bind_uart(UART_MODEM);
	cons_bind_uart(UART_IRDA);

	/* initialize MODEM UART to be used for sercomm */
	uart_init(UART_MODEM, with_irq);
	uart_baudrate(UART_MODEM, UART_115200);

	/* initialize IRDA UART to be used for old-school console code.
	 * note: IRDA uart only accessible on C115 and C117 PCB */
	uart_init(UART_IRDA, with_irq);
	uart_baudrate(UART_IRDA, UART_115200);

	/* Initialize hardware timers */
	hwtimer_init();

	/* Initialize DMA controller */
	dma_init();

	/* Initialize real time clock */
	rtc_init();

	/* Initialize system timers (uses hwtimer 2) */
	timer_init();

	/* Initialize LCD driver and backlight */
	fb_init();
	bl_mode_pwl(1);
	bl_level(50);

	/* Initialize keypad driver */
	keypad_init(keymap, with_irq);

	/* Initialize ABB driver (uses SPI) */
	twl3025_init();

	/* K200i uses 13 sectors of 256 KiB each */
	if (tiffs_init(0x01800000, 0x40000, 13) < 0) {
		/* K220i uses 52 sectors of 64 KiB each */
		tiffs_init(0x01800000, 0x10000, 52);
	}
}
