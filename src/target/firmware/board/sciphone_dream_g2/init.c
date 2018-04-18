/* Initialization for the MT62xx Basebands */

/* (C) 2010 by Steve Markgraf <steve@steve-m.de>
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
#include <uart.h>
#include <console.h>
#include <delay.h>

#include <flash/cfi_flash.h>

#include <comm/sercomm.h>
#include <comm/timer.h>

#include <mtk/emi.h>
#include <mtk/mt6235.h>
#include <mtk/system.h>

void pll_init(void)
{
	/* Power on PLL */
	writew(0, MTK_PLL_PDN_CON);
	writew(PLL_CLKSQ_DIV2_DSP | PLL_CLKSQ_DIV2_MCU, MTK_PLL_CLK_CON);

	writew(PLL_RST, MTK_PLL_PLL);
	writew(0, MTK_PLL_PLL);
	delay_ms(1);

	/* Turn on PLL for MCU, DSP and USB */
	writew(PLL_MPLLSEL_PLL | PLL_DPLLSEL | PLL_UPLLSEL, MTK_PLL_PLL);

	/*
	 * Setup MCU clock register:
	 * ARMCLK = 208MHz, AHBx4CLK = 52MHz, AHBx8CLK = 104MHz
	 * we have to write to the read-only part (EMICLK) as well, otherwise
	 * the EMI won't work! (datasheet lies)
	 */
	writew(7 << MCUCLK_CON_AHBX8CLK_SHIFT |
	       3 << MCUCLK_CON_AHBX4CLK_SHIFT |
	       15 << MCUCLK_CON_ARMCLK_SHIFT |
	       7 << MCUCLK_CON_EMICLK_SHIFT,
	       MTK_CONFG_MCUCLK_CON);
}

void memory_init(void)
{
	int i;

	/* Initialization for Hynix RAM */

	/* Configure DRAM controller */
	writel(0x0001000e, MTK_EMI_GEND);
	writel(0x00088a0a, MTK_EMI_GENA);
	writel(0x00000280, MTK_EMI_GENB);
	writel(0x52945294, MTK_EMI_GENC);
	writel(0x1c016605, MTK_EMI_CONL);
	writel(0x00002828, MTK_EMI_CONM);
	writel(0x02334000, MTK_EMI_CONI);
	writel(0x16c12212, MTK_EMI_CONJ);
	writel(0x032d0000, MTK_EMI_CONK);

	for (i = 0; i < 5; ++i) {
		/* Setup five single bits, one by one for DRAM init */
		writel((1 << (24 + i)) | (0x400013), MTK_EMI_CONN);
		delay_ms(1);
		writel(0x400013, MTK_EMI_CONN);
		delay_ms(1);
	}

#if 0
	/* Initialization for Toshiba RAM */

	/* Configure DRAM controller */
	writel(0x0001000E, MTK_EMI_GEND);
	writel(0x00088E3A, MTK_EMI_GENA);
	writel(0x000000C0, MTK_EMI_GENB);
	writel(0x18C618C6, MTK_EMI_GENC);
	writel(0x18007505, MTK_EMI_CONL);
	writel(0x00002828, MTK_EMI_CONM);
	writel(0x00332000, MTK_EMI_CONI);
	writel(0x3CD24431, MTK_EMI_CONJ);
	writel(0x02000000, MTK_EMI_CONK);

	for (i = 0; i < 5; ++i) {
		/* Setup five single bits, one by one for DRAM init */
		writel((1 << (24 + i)) | (0x500013), MTK_EMI_CONN);
		delay_ms(1);
		writel(0x500013, MTK_EMI_CONN);
		delay_ms(1);
	}

#endif
}

void board_init(int with_irq)
{
	/* powerup the baseband */
	writew(POWERKEY1_MAGIC, MTK_RTC_POWERKEY1);
	writew(POWERKEY2_MAGIC, MTK_RTC_POWERKEY2);
	writew(BBPU_MAGIC | RTC_BBPU_WRITE_EN |
	       RTC_BBPU_BBPU | RTC_BBPU_AUTO,
	       MTK_RTC_BBPU);
	writew(1, MTK_RTC_WRTGR);

	/* disable watchdog timer */
	writew(WDT_MODE_KEY, MTK_RGU_WDT_MODE);

	pll_init();
	memory_init();

	/* Initialize UART */
	sercomm_bind_uart(UART_MODEM);
	cons_bind_uart(UART_IRDA);
	uart_init(UART_MODEM, with_irq);
	uart_baudrate(UART_MODEM, UART_115200);
}
