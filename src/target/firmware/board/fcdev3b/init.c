/* Initialization for the FreeCalypso FCDEV3B modem */

/* Based on board/gta0x/init.c with the following obnoxious legalese:
 *
 * (C) 2010 by Harald Welte <laforge@gnumonks.org>
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

#include <comm/sercomm.h>
#include <comm/timer.h>

#include <abb/twl3025.h>
#include <rf/trf6151.h>

#define ARMIO_LATCH_OUT 0xfffe4802
#define IO_CNTL_REG	0xfffe4804
#define ARM_CONF_REG	0xfffef006
#define ASIC_CONF_REG	0xfffef008
#define IO_CONF_REG	0xfffef00a

static void board_io_init(void)
{
	uint16_t reg;

	reg = readw(ASIC_CONF_REG);
	/* TWL3025: Set SPI+RIF RX clock to rising edge */
	reg |= (1 << 13) | (1 << 14);
	writew(reg, ASIC_CONF_REG);

	/*
	 * Most Calypso peripheral interface signals are unconnected
	 * on this modem.  We configure them to be GPIOs in IO_CONF_REG,
	 * then configure them to be outputs in IO_CNTL_REG, then set
	 * the outputs to 0 in ARMIO_LATCH_OUT.
	 *
	 * Differences from Openmoko GTA0x:
	 *
	 * GPIO1 output needs to be 1 to enable the loudspeaker amplifier
	 * GPIO3 needs to be configured as an input
	 * I/O 9-12 are MCSI rather than GPIOs
	 */
	writew(0x0215, IO_CONF_REG);
	writew(0xDC08, IO_CNTL_REG);
	writew(0x0002, ARMIO_LATCH_OUT);

	/* configure ADD(22), needed for second half of flash */
	reg = readw(ARM_CONF_REG);
	reg |= (1 << 3);
	writew(reg, ARM_CONF_REG);
}

void board_init(int with_irq)
{
	/* Configure the memory interface */
	/* Using the same settings as the official FreeCalypso fw */
	calypso_mem_cfg(CALYPSO_nCS0, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS1, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS2, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS3, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_CS4, 4, CALYPSO_MEM_16bit, 1);
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

	/* Initialize IRDA UART to be used for old-school console code.
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

	/* Initialize ABB driver (uses SPI) */
	twl3025_init();

	/* Initialize TIFFS reader (8 sectors of 256 KiB each) */
	tiffs_init(0x01800000, 0x40000, 8);
}
