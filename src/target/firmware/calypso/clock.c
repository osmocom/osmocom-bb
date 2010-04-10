/* Driver for Calypso clock management */

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
#include <calypso/clock.h>

#define REG_DPLL		0xffff9800
#define DPLL_LOCK		(1 << 0)
#define DPLL_BREAKLN		(1 << 1)
#define DPLL_BYPASS_DIV_SHIFT	2		/* 2 bits */
#define DPLL_PLL_ENABLE		(1 << 4)
#define DPLL_PLL_DIV_SHIFT	5		/* 2 bits */
#define DPLL_PLL_MULT_SHIFT	7		/* 5 bits */
#define DPLL_TEST		(1 << 12)
#define DPLL_IOB		(1 << 13)	/* Initialize on break */
#define DPLL_IAI		(1 << 14)	/* Initialize after Idle */

#define BASE_ADDR_CLKM	0xfffffd00
#define CLKM_REG(m)	(BASE_ADDR_CLKM+(m))

enum clkm_reg {
	CNTL_ARM_CLK	= 0,
	CNTL_CLK	= 2,
	CNTL_RST	= 4,
	CNTL_ARM_DIV	= 8,
};

/* CNTL_ARM_CLK */
#define ARM_CLK_BIG_SLEEP	(1 << 0)	/* MCU Master Clock enabled? */
#define ARM_CLK_CLKIN_SEL0	(1 << 1)	/* MCU source clock (0 = DPLL output, 1 = VTCXO or CLKIN */
#define ARM_CLK_CLKIN_SEL	(1 << 2)	/* 0 = VTCXO or 1 = CLKIN */
#define ARM_CLK_MCLK_DIV5	(1 << 3)	/* enable 1.5 or 2.5 division factor */
#define ARM_CLK_MCLK_DIV_SHIFT	4		/* 3 bits */
#define ARM_CLK_DEEP_POWER_SHIFT	8
#define ARM_CLK_DEEP_SLEEP	12

/* CNTL_CLK */
#define CLK_IRQ_CLK_DIS		(1 << 0)	/* IRQ clock control (0 always, 1 according ARM_MCLK_EN) */
#define CLK_BRIDGE_CLK_DIS	(1 << 1)
#define CLK_TIMER_CLK_DIS	(1 << 2)
#define CLK_DPLL_DIS		(1 << 3)	/* 0: DPLL is not stopped during SLEEP */
#define CLK_CLKOUT_EN		(1 << 4)	/* Enable CLKOUT output pins */
#define CLK_EN_IDLE3_FLG	(1 << 5)	/* DSP idle flag control (1 =
						 * SAM/HOM register forced to HOM when DSP IDLE3) */
#define CLK_VCLKOUT_DIV2	(1 << 6)	/* 1: VCLKOUT-FR is divided by 2 */
#define CLK_VTCXO_DIV2		(1 << 7)	/* 1: VTCXO is dividied by 2 */

#define BASE_ADDR_MEMIF		0xfffffb00
#define MEMIF_REG(x)		(BASE_ADDR_MEMIF+(x))

enum memif_reg {
	API_RHEA_CTL	= 0x0e,
	EXTRA_CONF	= 0x10,
};

static void dump_reg16(uint32_t addr, char *name)
{
	printf("%s=0x%04x\n", name, readw(addr));
}

void calypso_clk_dump(void)
{
	dump_reg16(REG_DPLL, "REG_DPLL");
	dump_reg16(CLKM_REG(CNTL_ARM_CLK), "CNTL_ARM_CLK");
	dump_reg16(CLKM_REG(CNTL_CLK), "CNTL_CLK");
	dump_reg16(CLKM_REG(CNTL_RST), "CNTL_RST");
	dump_reg16(CLKM_REG(CNTL_ARM_DIV), "CNTL_ARM_DIV");
}

void calypso_pll_set(uint16_t inp)
{
	uint8_t mult = inp >> 8;
	uint8_t div = inp & 0xff;
	uint16_t reg = readw(REG_DPLL);

	reg &= ~0x0fe0;
	reg |= (div & 0x3) << DPLL_PLL_DIV_SHIFT;
	reg |= (mult & 0x1f) << DPLL_PLL_MULT_SHIFT;
	reg |= DPLL_PLL_ENABLE;

	writew(reg, REG_DPLL);
}

void calypso_reset_set(enum calypso_rst calypso_rst, int active)
{
	uint8_t reg = readb(CLKM_REG(CNTL_RST));

	if (active)
		reg |= calypso_rst;
	else
		reg &= ~calypso_rst;

	writeb(reg, CLKM_REG(CNTL_RST));
}

int calypso_reset_get(enum calypso_rst calypso_rst)
{
	uint8_t reg = readb(CLKM_REG(CNTL_RST));

	if (reg & calypso_rst)
		return 1;
	else
		return 0;
}

void calypso_clock_set(uint8_t vtcxo_div2, uint16_t inp, enum mclk_div mclk_div)
{
	uint16_t cntl_clock = readw(CLKM_REG(CNTL_CLK));
	uint16_t cntl_arm_clk = readw(CLKM_REG(CNTL_ARM_CLK));

	/* First set the vtcxo_div2 */
	cntl_clock &= ~CLK_VCLKOUT_DIV2;
	if (vtcxo_div2)
		cntl_clock |= CLK_VTCXO_DIV2;
	else
		cntl_clock &= ~CLK_VTCXO_DIV2;
	writew(cntl_clock, CLKM_REG(CNTL_CLK));

	/* Then configure the MCLK divider */
	cntl_arm_clk &= ~ARM_CLK_CLKIN_SEL0;
	if (mclk_div & 0x80) {
		mclk_div &= ~0x80;
		cntl_arm_clk |= ARM_CLK_MCLK_DIV5;
	} else
		cntl_arm_clk &= ~ARM_CLK_MCLK_DIV5;
	cntl_arm_clk &= ~(0x7 << ARM_CLK_MCLK_DIV_SHIFT);
	cntl_arm_clk |= (mclk_div << ARM_CLK_MCLK_DIV_SHIFT);
	writew(cntl_arm_clk, CLKM_REG(CNTL_ARM_CLK));

	/* Then finally set the PLL */
	calypso_pll_set(inp);
}

void calypso_mem_cfg(enum calypso_bank bank, uint8_t ws,
		     enum calypso_mem_width width, int we)
{
	writew((ws & 0x1f) | ((width & 3) << 5) | ((we & 1) << 7),
	       BASE_ADDR_MEMIF + bank);
}

void calypso_bootrom(int enable)
{
	uint16_t conf = readw(MEMIF_REG(EXTRA_CONF));

	conf |= (3 << 8);

	if (enable)
		conf &= ~(1 << 9);

	writew(conf, MEMIF_REG(EXTRA_CONF));
}

void calypso_debugunit(int enable)
{
	uint16_t conf = readw(MEMIF_REG(EXTRA_CONF));

	if (enable)
		conf &= ~(1 << 11);
	else
		conf |= (1 << 11);

	writew(conf, MEMIF_REG(EXTRA_CONF));
}

#define REG_RHEA_CNTL	0xfffff900
#define REG_API_CNTL	0xfffff902
#define REG_ARM_RHEA	0xfffff904

void calypso_rhea_cfg(uint8_t fac0, uint8_t fac1, uint8_t timeout,
		      uint8_t ws_h, uint8_t ws_l, uint8_t w_en0, uint8_t w_en1)
{
	writew(fac0 | (fac1 << 4) | (timeout << 8), REG_RHEA_CNTL);
	writew(ws_h | (ws_l << 5), REG_API_CNTL);
	writew(w_en0 | (w_en1 << 1), REG_ARM_RHEA);
}
