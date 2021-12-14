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
#define ARM_CONF_REG	0xfffef006
#define ASIC_CONF_REG	0xfffef008
#define IO_CONF_REG	0xfffef00a
#define LPG_LCR_REG	0xfffe7800
#define LPG_PM_REG	0xfffe7801

int gtm900_hw_is_mg01gsmt;

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
	 * Configure Calypso GPIO and multifunction pins the same way
	 * how Huawei's official firmware configures them.
	 */
	writew(0x03F5, IO_CONF_REG);
	writew(0xDC58, IO_CNTL_REG);
	writew(0x0007, ARMIO_LATCH_OUT);

	/* Set LPG output permanently on (power LED) */
	writew(1, LPG_PM_REG);
	writew((1 << 7), LPG_LCR_REG);

	/* configure ADD(22), needed for second half of flash on MG01GSMT */
	reg = readw(ARM_CONF_REG);
	reg |= (1 << 3);
	writew(reg, ARM_CONF_REG);
}

/*
 * There exist two firmware-incompatible versions of GTM900-B hardware:
 * MG01GSMT and MGCxGSMT.  They have different flash chip types (8 MiB
 * vs. 4 MiB) with correspondingly different TIFFS configurations
 * (and we need TIFFS in order to read factory RF calibration values),
 * and they have different (incompatible) RFFE control signals.
 *
 * We are going to check the flash chip type and use it to decide which
 * hw variant we are running on.
 */
static void board_flash_init(void)
{
	uint16_t manufacturer_id, device_id[3];

	/* Use an address above the Calypso boot ROM
	 * so we don't need to unmap it to access the flash. */
	flash_get_id((void *)0x40000, &manufacturer_id, device_id);

	switch (manufacturer_id) {
	case CFI_MANUF_SPANSION:
		/* is it S71PL064J? */
		if (device_id[0] == 0x227E && device_id[1] == 0x2202 &&
		    device_id[2] == 0x2201) {
			gtm900_hw_is_mg01gsmt = 1;
			break;
		}
		/* is it S71PL032J? */
		if (device_id[0] == 0x227E && device_id[1] == 0x220A &&
		    device_id[2] == 0x2201) {
			gtm900_hw_is_mg01gsmt = 0;
			break;
		}
		goto bad;
	case CFI_MANUF_SAMSUNG:
		/* is it K5A3281CTM? */
		if (device_id[0] == 0x22A0) {
			gtm900_hw_is_mg01gsmt = 0;
			break;
		}
		/* is it K5L3316CAM? */
		if (device_id[0] == 0x257E && device_id[1] == 0x2503 &&
		    device_id[2] == 0x2501) {
			gtm900_hw_is_mg01gsmt = 0;
			break;
		}
		/* FALL THRU */
	default:
	bad:
		printf("Unknown module detected, "
		       "flash ID 0x%04x 0x%04x 0x%04x 0x%04x\n"
		       "Please contact mailing list!\n\n", manufacturer_id,
		       device_id[0], device_id[1], device_id[2]);
		return;
	}

	/* Initialize TIFFS reader */
	if (gtm900_hw_is_mg01gsmt)
		tiffs_init(0x700000, 0x10000, 15);
	else
		tiffs_init(0x380000, 0x10000, 7);
}

void board_init(int with_irq)
{
	/*
	 * Configure the memory interface.
	 * Huawei's official fw sets WS=4 for RAM, but not for flash -
	 * but let's be consistent and use WS=4 for both.  Please refer
	 * to this technical article for the underlying theory:
https://www.freecalypso.org/hg/freecalypso-docs/file/tip/MEMIF-wait-states
	 */
	calypso_mem_cfg(CALYPSO_nCS0, 4, CALYPSO_MEM_16bit, 1);
	calypso_mem_cfg(CALYPSO_nCS1, 4, CALYPSO_MEM_16bit, 1);
	/*
	 * The remaining 3 chip selects are unused on this hw,
	 * thus their settings are dummies.
	 */
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

	/* Initialize board flash */
	board_flash_init();
}
