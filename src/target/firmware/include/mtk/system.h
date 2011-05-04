/*
 * (C) 2010 by Tieto <www.tieto.com>
 *	Marcin Mielczarczyk <marcin.mielczarczyk@tieto.com>
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

#ifndef __MTK_SYSTEM_H_
#define __MTK_SYSTEM_H_

/*
 * Configuration block section (Clock, Power Down, Version and Reset
 */

/* Register definitions */
#define MTK_CONFG_HW_VERSION	(MTK_CONFG_BASE + 0x000)
#define MTK_CONFG_FW_VERSION	(MTK_CONFG_BASE + 0x004)
#define MTK_CONFG_HW_CODE	(MTK_CONFG_BASE + 0x008)
#define MTK_CONFG_SLEEP_CON	(MTK_CONFG_BASE + 0x114)
#define MTK_CONFG_MCUCLK_CON	(MTK_CONFG_BASE + 0x118)
#define MTK_CONFG_DSPCLK_CON	(MTK_CONFG_BASE + 0x11C)
#define MTK_CONFG_IDN_SEL	(MTK_CONFG_BASE + 0x200)
#define MTK_CONFG_PDN_CON0	(MTK_CONFG_BASE + 0x300)
#define MTK_CONFG_PDN_CON1	(MTK_CONFG_BASE + 0x304)
#define MTK_CONFG_PDN_CON2	(MTK_CONFG_BASE + 0x308)
#define MTK_CONFG_PDN_CON3	(MTK_CONFG_BASE + 0x30C)
#define MTK_CONFG_PDN_SET0	(MTK_CONFG_BASE + 0x310)
#define MTK_CONFG_PDN_SET1	(MTK_CONFG_BASE + 0x314)
#define MTK_CONFG_PDN_SET2	(MTK_CONFG_BASE + 0x318)
#define MTK_CONFG_PDN_SET3	(MTK_CONFG_BASE + 0x31C)
#define MTK_CONFG_PDN_CLR0	(MTK_CONFG_BASE + 0x320)
#define MTK_CONFG_PDN_CLR1	(MTK_CONFG_BASE + 0x324)
#define MTK_CONFG_PDN_CLR2	(MTK_CONFG_BASE + 0x328)
#define MTK_CONFG_PDN_CLR3	(MTK_CONFG_BASE + 0x32C)

/* CONFG_MCUCLK_CON bit fields definitions */
#define MCUCLK_CON_AHBX8CLK_SHIFT	(0)
#define MCUCLK_CON_AHBX4CLK_SHIFT	(4)
#define MCUCLK_CON_ARMCLK_SHIFT		(8)
#define MCUCLK_CON_EMICLK_SHIFT		(12)

/* PDN_CON0 bit fields definitions */
#define PDN_CON0_CON0_DMA	(1 << 0)
#define PDN_CON0_USB		(1 << 1)
#define PDN_CON0_GCU		(1 << 2)
#define PDN_CON0_WAVE		(1 << 3)
#define PDN_CON0_SEJ		(1 << 4)
#define PDN_CON0_IR		(1 << 6)
#define PDN_CON0_PWM3		(1 << 7)
#define PDN_CON0_PWM		(1 << 8)
#define PDN_CON0_SIM2		(1 << 10)
#define PDN_CON0_IRDBG1		(1 << 12)
#define PDN_CON0_IRDBG2		(1 << 13)

/* PDN_CON1 bit fields definitions */
#define PDN_CON1_GPT		(1 << 0)
#define PDN_CON1_KP		(1 << 1)
#define PDN_CON1_GPIO		(1 << 2)
#define PDN_CON1_UART1		(1 << 3)
#define PDN_CON1_SIM		(1 << 4)
#define PDN_CON1_PWM1		(1 << 5)
#define PDN_CON1_LCD		(1 << 7)
#define PDN_CON1_UART2		(1 << 8)
#define PDN_CON1_MSDC		(1 << 9)
#define PDN_CON1_TP		(1 << 10)
#define PDN_CON1_PWM2		(1 << 11)
#define PDN_CON1_NFI		(1 << 12)
#define PDN_CON1_UART3		(1 << 14)
#define PDN_CON1_IRDA		(1 << 15)

/* PDN_CON2 bit fields definitions */
#define PDN_CON2_TDMA		(1 << 0)
#define PDN_CON2_RTC		(1 << 1)
#define PDN_CON2_BSI		(1 << 2)
#define PDN_CON2_BPI		(1 << 3)
#define PDN_CON2_AFC		(1 << 4)
#define PDN_CON2_APC		(1 << 5)

/*
 * Reset Generation Unit block section
 */
#define MTK_RGU_WDT_MODE	(MTK_RGU_BASE + 0x00)
#define MTK_RGU_WDT_LENGTH	(MTK_RGU_BASE + 0x04)
#define MTK_RGU_WDT_RESTART	(MTK_RGU_BASE + 0x08)
#define MTK_RGU_WDT_STA		(MTK_RGU_BASE + 0x0C)
#define MTK_RGU_SW_PERIPH_RSTN	(MTK_RGU_BASE + 0x10)
#define MTK_RGU_SW_DSP_RSTN	(MTK_RGU_BASE + 0x14)
#define MTK_RGU_WDT_RSTINTERVAL	(MTK_RGU_BASE + 0x18)
#define MTK_RGU_WDT_SWRST	(MTK_RGU_BASE + 0x1C)

#define WDT_MODE_KEY		0x2200
#define WDT_LENGTH_KEY		0x0008
#define WDT_RESTART_KEY		0x1971
#define SW_PERIPH_RSTN_KEY	0x0037
#define WDT_SWRST_KEY		0x1209

/*
 * RTC block section
 */

/* RTC registers definition */
#define MTK_RTC_BBPU		(MTK_RTC_BASE + 0x00)
#define MTK_RTC_IRQ_STA		(MTK_RTC_BASE + 0x04)
#define MTK_RTC_IRQ_EN		(MTK_RTC_BASE + 0x08)
#define MTK_RTC_CII_EN		(MTK_RTC_BASE + 0x0C)
#define MTK_RTC_AL_MASK		(MTK_RTC_BASE + 0x10)
#define MTK_RTC_TC_SEC		(MTK_RTC_BASE + 0x14)
#define MTK_RTC_TC_MIN		(MTK_RTC_BASE + 0x18)
#define MTK_RTC_TC_HOU		(MTK_RTC_BASE + 0x1C)
#define MTK_RTC_TC_DOM		(MTK_RTC_BASE + 0x20)
#define MTK_RTC_TC_DOW		(MTK_RTC_BASE + 0x24)
#define MTK_RTC_TC_MTH		(MTK_RTC_BASE + 0x28)
#define MTK_RTC_TC_YEA		(MTK_RTC_BASE + 0x2C)
#define MTK_RTC_AL_SEC		(MTK_RTC_BASE + 0x30)
#define MTK_RTC_AL_MIN		(MTK_RTC_BASE + 0x34)
#define MTK_RTC_AL_HOU		(MTK_RTC_BASE + 0x38)
#define MTK_RTC_AL_DOM		(MTK_RTC_BASE + 0x3C)
#define MTK_RTC_AL_DOW		(MTK_RTC_BASE + 0x40)
#define MTK_RTC_AL_MTH		(MTK_RTC_BASE + 0x44)
#define MTK_RTC_AL_YEA		(MTK_RTC_BASE + 0x48)
#define MTK_RTC_XOSCCALI	(MTK_RTC_BASE + 0x4C)
#define MTK_RTC_POWERKEY1	(MTK_RTC_BASE + 0x50)
#define MTK_RTC_POWERKEY2	(MTK_RTC_BASE + 0x54)
#define MTK_RTC_PDN1		(MTK_RTC_BASE + 0x58)
#define MTK_RTC_PDN2		(MTK_RTC_BASE + 0x5C)
#define MTK_RTC_SPAR1		(MTK_RTC_BASE + 0x64)
#define MTK_RTC_DIFF		(MTK_RTC_BASE + 0x6C)
#define MTK_RTC_CALI		(MTK_RTC_BASE + 0x70)
#define MTK_RTC_WRTGR		(MTK_RTC_BASE + 0x74)

#define POWERKEY1_MAGIC		0xA357
#define POWERKEY2_MAGIC		0x67D2

/* RTC_BBPU bit fields definitions */
#define RTC_BBPU_PWREN		(1 << 0)
#define RTC_BBPU_WRITE_EN	(1 << 1)
#define RTC_BBPU_BBPU		(1 << 2)
#define RTC_BBPU_AUTO		(1 << 3)
#define RTC_BBPU_CLRPKY		(1 << 4)
#define RTC_BBPU_RELOAD		(1 << 5)
#define RTC_BBPU_CBUSY		(1 << 6)
#define RTC_BBPU_DBING		(1 << 7)
#define RTC_BBPU_KEY_BBPU	(1 << 8)

/* RTC_BBPU write is only acceptable when KEY_BBPU=0x43 */
#define BBPU_MAGIC		0x4300

/*
 * PLL block section
 */

/* PLL registers definition */
#define MTK_PLL_PLL		(MTK_PLL_BASE + 0x00)
#define MTK_PLL_PLL2		(MTK_PLL_BASE + 0x04)
#define MTK_PLL_CLK_CON		(MTK_PLL_BASE + 0x18)
#define MTK_PLL_PDN_CON		(MTK_PLL_BASE + 0x1C)

/* MTK_PLL_PLL bit fields definitions */
#define PLL_PLLVCOSEL		(0 << 0)
#define PLL_MPLLSEL_SYSCLK	(1 << 3)
#define PLL_MPLLSEL_PLL		(2 << 3)
#define PLL_DPLLSEL		(1 << 5)
#define PLL_UPLLSEL		(1 << 6)
#define PLL_RST			(1 << 7)
#define PLL_CALI		(1 << 8)

/* MTK_PLL_CLK_CON bit fields definitions */
#define PLL_CLKSQ_DIV2_DSP	(1 << 0)
#define PLL_CLKSQ_DIV2_MCU	(1 << 1)
#define PLL_CLKSQ_PLD		(1 << 2)
#define PLL_SRCCLK		(1 << 7)
#define PLL_CLKSQ_TEST		(1 << 15)

/* MTK_PLL_PDN_CON bit fields definitions */
#define PLL_PDN_CON_CLKSQ	(1 << 11)
#define PLL_PDN_CON_MCU_DIV2	(1 << 12)
#define PLL_PDN_CON_PLL		(1 << 13)
#define PLL_PDN_CON_DSP_DIV2	(1 << 15)

#endif
