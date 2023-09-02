/* Initialization for the iWOW TR-800 modem */

/*
 * This code was written by Mychaela Falconia <falcon@freecalypso.org>
 * who refuses to claim copyright on it and has released it as public domain
 * instead. NO rights reserved, all rights relinquished.
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
	/* DSR_MODEM/LPG pin is unconnected - make it LPG dummy output */
	reg |= (1 << 6);
	/* TWL3025: Set SPI+RIF RX clock to rising edge */
	reg |= (1 << 13) | (1 << 14);
	writew(reg, ASIC_CONF_REG);

	/*
	 * Calypso signals GPIO0, TSPDI/GPIO4, BCLKX/GPIO6, MCUEN1/GPIO8 and
	 * MCUEN2/GPIO13 are unused and unconnected inside the TR-800 module.
	 * Configure them as dummy outputs in order to prevent floating inputs.
	 */
	writew(0x0215, IO_CONF_REG);
	writew(0xDC0E, IO_CNTL_REG);
	writew(0x0000, ARMIO_LATCH_OUT);

	/* configure ADD(22), needed for second half of flash */
	reg = readw(ARM_CONF_REG);
	reg |= (1 << 3);
	writew(reg, ARM_CONF_REG);
}

/*
 * A total of 8 Calypso GPIO/multifunction pins (3 pure GPIO, 5 multifunction)
 * are brought out on the TR-800 module, with module users (application board
 * designers) explicitly allowed to wire them in whichever way is needed for
 * the custom application at hand.  6 of these pins power up as inputs.
 * Should the firmware leave them as inputs, or switch them to dummy outputs
 * to prevent floating inputs?  The answer in FreeCalypso (for TR-800 modules
 * rebranded as FC Tango) is a special file written into FFS: /etc/tango-pinmux.
 * This board wiring config file tells the firmware what it should do with each
 * of the 8 GPIO/multifunction pins in question; the format is defined here:
 *
 * https://www.freecalypso.org/hg/freecalypso-docs/file/tip/Tango-pinmux
 *
 * The following function reads /etc/tango-pinmux from FFS and applies the pin
 * multiplexing configuration encoded therein.  If this file is missing, all
 * pins in question are left in their default power-up state.
 */
static void board_pinmux_init(void)
{
	uint8_t pinmux[4];
	int rc;
	uint16_t conf_reg, cntl_reg, out_reg;

	rc = tiffs_read_file_fixedlen("/etc/tango-pinmux", pinmux, 4);
	if (rc < 0)
		return;		/* error msg already printed */
	if (rc == 0) {
		puts("Warning: /etc/tango-pinmux not found, pins left in default power-up state\n");
		return;
	}
	/* read-modify-write registers */
	conf_reg = readw(IO_CONF_REG);
	cntl_reg = readw(IO_CNTL_REG);
	out_reg = readw(ARMIO_LATCH_OUT);
	/* GPIO1 */
	if (pinmux[0] & 0x80) {
		cntl_reg &= ~(1 << 1);
		if (pinmux[0] & 0x01)
			out_reg |= (1 << 1);
		else
			out_reg &= ~(1 << 1);
	}
	/* GPIO2 */
	if (pinmux[1] & 0x08) {
		/* pinmux says it's DCD output - set it high */
		cntl_reg &= ~(1 << 2);
		out_reg |= (1 << 2);
	} else if (pinmux[1] & 0x02) {
		/* generic output */
		cntl_reg &= ~(1 << 2);
		if (pinmux[1] & 0x01)
			out_reg |= (1 << 2);
		else
			out_reg &= ~(1 << 2);
	}
	/* GPIO3 */
	if (pinmux[1] & 0x20) {
		/* generic output */
		cntl_reg &= ~(1 << 3);
		if (pinmux[1] & 0x10)
			out_reg |= (1 << 3);
		else
			out_reg &= ~(1 << 3);
	}
	/* MCSI or GPIO? */
	if (pinmux[2] & 0x80) {
		/* MCSI pins switch to GPIO */
		conf_reg |= 0x1E0;
		writew(conf_reg, IO_CONF_REG);
		/* GPIO9 */
		if (pinmux[3] & 0x10) {
			cntl_reg &= ~(1 << 9);
			if (pinmux[3] & 0x01)
				out_reg |= (1 << 9);
			else
				out_reg &= ~(1 << 9);
		} else
			cntl_reg |= (1 << 9);
		/* GPIO10 */
		if (pinmux[3] & 0x20) {
			cntl_reg &= ~(1 << 10);
			if (pinmux[3] & 0x02)
				out_reg |= (1 << 10);
			else
				out_reg &= ~(1 << 10);
		}
		/* GPIO11 */
		if (pinmux[3] & 0x40) {
			cntl_reg &= ~(1 << 11);
			if (pinmux[3] & 0x04)
				out_reg |= (1 << 11);
			else
				out_reg &= ~(1 << 11);
		}
		/* GPIO12 */
		if (pinmux[3] & 0x80) {
			cntl_reg &= ~(1 << 12);
			if (pinmux[3] & 0x08)
				out_reg |= (1 << 12);
			else
				out_reg &= ~(1 << 12);
		}
	}
	writew(out_reg, ARMIO_LATCH_OUT);
	writew(cntl_reg, IO_CNTL_REG);
}

void board_init(int with_irq)
{
	/*
	 * Configure the memory interface.
	 * nCS0 and nCS1 are internal flash and RAM - please refer to
	 * this technical article for an explanation of timing parameters:
https://www.freecalypso.org/hg/freecalypso-docs/file/tip/MEMIF-wait-states
	 */
	calypso_mem_cfg(CALYPSO_nCS0, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS1, 4, CALYPSO_MEM_16bit, 1);
	/* nCS2 and nCS3 are brought out for user-added custom hw */
	calypso_mem_cfg(CALYPSO_nCS2, 5, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS3, 5, CALYPSO_MEM_16bit, 1);
	/* Calypso nCS4 is not brought out on TR-800, hence a dummy */
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

	/*
	 * The choice of which UART should be used for what is arbitrary -
	 * change to taste!
	 */
	sercomm_bind_uart(UART_MODEM);
	cons_bind_uart(UART_IRDA);

	/* initialize MODEM UART to be used for sercomm */
	uart_init(UART_MODEM, with_irq);
	uart_baudrate(UART_MODEM, UART_115200);

	/* Initialize IRDA UART to be used for old-school console code. */
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

	/* Initialize keypad driver */
	keypad_init(keymap, with_irq);

	/* Initialize ABB driver (uses SPI) */
	twl3025_init();

	/* Initialize TIFFS reader (15 sectors of 64 KiB each) */
	tiffs_init(0x700000, 0x10000, 15);

	/* Initialize configurable pin multiplexing */
	board_pinmux_init();
}
